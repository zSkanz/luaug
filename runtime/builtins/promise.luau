--!strict
--[[
	Promise -- the engine's own promises (ADR 0094).

	Modelled on the semantics of evaera's Promise (MIT), the de-facto reference;
	nothing of it is copied, and its methods are PascalCase here because they
	are reached off a value (ADR 0034) while its constructors stay camelCase.

	- The executor runs at once, on its own thread, so it may yield.
	- Resolving with a promise adopts it.
	- A handler that errors rejects the promise it returned.
	- Cancelling reaches every consumer, and reaches the parent once all of the
	  parent's consumers have cancelled.
	- A rejection nothing handled is warned about after the drain.
	- `Promise.delay` is on the simulation's clock (`task.delay`, R10) and
	  resolves with the seconds asked for, not with a wall-clock measurement.

	Everything that is iterated is an array, so what happens in what order is a
	function of the calls and not of a hash (R10).

	Compiled at build time into the script module and installed as the global
	`Promise`, read-only, before the sandbox closes.
]]

type Packed = { n: number, [number]: any }

type PromiseState = {
    _status: EnumPromiseState,
    _values: Packed,
    _onResolved: { (...any) -> () },
    _onRejected: { (...any) -> () },
    _onSettled: { (EnumPromiseState) -> () },
    _cancelHook: (() -> ())?,
    _thread: thread?,
    _parent: PromiseState?,
    _consumers: { PromiseState },
    -- Something took this promise's outcome on -- a consumer, an `Await`, an
    -- adoption -- so a rejection of it is not one nobody saw.
    _handled: boolean,
    _origin: string,
}

-- The engine's own enum (ADR 0094), registered before the builtins install.
local Status = Enum.PromiseState

local Promise = {}
Promise.__index = Promise
Promise.__tostring = function(self: PromiseState): string
    return `Promise({self._status.Name})`
end

local function pack(...: any): Packed
    return table.pack(...) :: any
end

local function traceback(message: any): any
    if type(message) == "string" then
        return debug.traceback(message, 2)
    end
    return message
end

local function isPromise(value: any): boolean
    return type(value) == "table" and getmetatable(value) == Promise
end

-- Where the promise was made, for the warning about a rejection nobody took.
-- The first frame outside this chunk, however deep inside it the promise was
-- made -- a combinator makes promises three calls down.
local function origin(): string
    for level = 3, 12 do
        local source, line = debug.info(level, "sl")
        if source == nil then
            break
        end
        if source ~= "Promise" and source ~= "[C]" then
            return `{source}:{line}`
        end
    end
    return "an unknown place"
end

local new: (executor: any, parent: PromiseState?) -> PromiseState
local resolveWith: (self: PromiseState, ...any) -> ()
local rejectWith: (self: PromiseState, ...any) -> ()

local function settle(self: PromiseState, status: EnumPromiseState, values: Packed)
    self._status = status
    self._values = values
    local callbacks: { (...any) -> () } = if status == Status.Resolved then self._onResolved else self._onRejected
    self._onResolved = {}
    self._onRejected = {}
    for _, callback in callbacks do
        callback(table.unpack(values, 1, values.n))
    end
    local settled = self._onSettled
    self._onSettled = {}
    for _, callback in settled do
        callback(status)
    end
    self._parent = nil
end

local function cancel(self: PromiseState)
    if self._status ~= Status.Started then
        return
    end
    self._status = Status.Cancelled
    if self._cancelHook ~= nil then
        self._cancelHook()
    end
    local thread = self._thread
    if thread ~= nil and coroutine.status(thread) == "suspended" then
        pcall(task.cancel, thread)
    end
    local parent = self._parent
    if parent ~= nil then
        local at = table.find(parent._consumers, self)
        if at ~= nil then
            table.remove(parent._consumers, at)
        end
        -- The parent stops only when nobody else is still waiting on it.
        if #parent._consumers == 0 then
            cancel(parent)
        end
    end
    local consumers = self._consumers
    self._consumers = {}
    for _, consumer in consumers do
        cancel(consumer)
    end
    self._onResolved = {}
    self._onRejected = {}
    local settled = self._onSettled
    self._onSettled = {}
    for _, callback in settled do
        callback(Status.Cancelled)
    end
end

resolveWith = function(self: PromiseState, ...: any)
    if self._status ~= Status.Started then
        local first = ...
        if isPromise(first) then
            cancel(first)
        end
        return
    end

    -- **Resolving with a promise adopts it**: this one settles as that one does.
    local first = ...
    if isPromise(first) then
        local adopted: PromiseState = first
        if select("#", ...) > 1 then
            warn("Promise: a resolve with a promise ignores the values after it")
        end
        adopted._handled = true
        local function onResolved(...: any)
            resolveWith(self, ...)
        end
        local function onRejected(...: any)
            rejectWith(self, ...)
        end
        if adopted._status == Status.Started then
            table.insert(adopted._onResolved, onResolved)
            table.insert(adopted._onRejected, onRejected)
            table.insert(adopted._consumers, self)
            table.insert(adopted._onSettled, function(status: EnumPromiseState)
                if status == Status.Cancelled then
                    cancel(self)
                end
            end)
            self._parent = adopted
        elseif adopted._status == Status.Resolved then
            resolveWith(self, table.unpack(adopted._values, 1, adopted._values.n))
        elseif adopted._status == Status.Rejected then
            rejectWith(self, table.unpack(adopted._values, 1, adopted._values.n))
        else
            cancel(self)
        end
        return
    end

    settle(self, Status.Resolved, pack(...))
end

rejectWith = function(self: PromiseState, ...: any)
    if self._status ~= Status.Started then
        return
    end
    settle(self, Status.Rejected, pack(...))
    if not self._handled then
        -- After the drain, so a handler attached on the next line still counts.
        local values = self._values
        task.defer(function()
            if not self._handled then
                local reason = if values.n > 0 then tostring(values[1]) else "(no value)"
                warn(`Unhandled Promise rejection, from a promise made at {self._origin}:\n{reason}`)
            end
        end)
    end
end

new = function(executor: any, parent: PromiseState?): PromiseState
    local self: PromiseState = {
        _status = Status.Started,
        _values = pack(),
        _onResolved = {},
        _onRejected = {},
        _onSettled = {},
        _cancelHook = nil,
        _thread = nil,
        _parent = parent,
        _consumers = {},
        _handled = false,
        _origin = origin(),
    }
    setmetatable(self :: any, Promise)
    if parent ~= nil then
        table.insert(parent._consumers, self)
    end

    local function resolve(...: any)
        resolveWith(self, ...)
    end
    local function reject(...: any)
        rejectWith(self, ...)
    end
    local function onCancel(hook: (() -> ())?): boolean
        if hook ~= nil then
            if self._status == Status.Cancelled then
                hook()
            else
                self._cancelHook = hook
            end
        end
        return self._status == Status.Cancelled
    end

    self._thread = task.spawn(function()
        local ok, problem = xpcall(executor, traceback, resolve, reject, onCancel)
        if not ok then
            rejectWith(self, problem)
        end
    end)
    return self
end

-- A handler wrapped so that its result settles the promise it made: what it
-- returns resolves it, what it raises rejects it.
local function advance(handler: (...any) -> ...any, resolve: (...any) -> (), reject: (...any) -> ())
    return function(...: any)
        local arguments = pack(...)
        task.spawn(function()
            local results = pack(xpcall(handler, traceback, table.unpack(arguments, 1, arguments.n)))
            if results[1] then
                resolve(table.unpack(results, 2, results.n))
            else
                reject(results[2])
            end
        end)
    end
end

local function andThen(self: PromiseState, onResolved: any, onRejected: any): PromiseState
    -- A consumer takes the rejection on: it is its to pass on or to handle.
    self._handled = true
    -- **A consumer of a cancelled promise is cancelled**, not rejected: a
    -- rejection is a failure somebody must hear about, and nothing failed --
    -- `race` cancelling its losers would otherwise warn once for each.
    if self._status == Status.Cancelled then
        local child = new(function() end, nil)
        cancel(child)
        return child
    end
    return new(function(resolve, reject)
        local success: (...any) -> () = if onResolved ~= nil then advance(onResolved, resolve, reject) else resolve
        local failure: (...any) -> () = if onRejected ~= nil then advance(onRejected, resolve, reject) else reject
        if self._status == Status.Started then
            table.insert(self._onResolved, success)
            table.insert(self._onRejected, failure)
        elseif self._status == Status.Resolved then
            success(table.unpack(self._values, 1, self._values.n))
        else
            failure(table.unpack(self._values, 1, self._values.n))
        end
    end, if self._status == Status.Started then self else nil)
end

--- `Enum.PromiseState.Started`, `Resolved`, `Rejected` or `Cancelled`.
function Promise.GetStatus(self: PromiseState): EnumPromiseState
    return self._status
end

--- A promise of what `onResolved` returns, or of what `onRejected` returns
--- when this one rejects. Either may be nil, which passes the outcome on.
function Promise.AndThen(self: PromiseState, onResolved: any, onRejected: any): PromiseState
    return andThen(self, onResolved, onRejected)
end

--- `AndThen(nil, onRejected)`: handles a rejection.
function Promise.Catch(self: PromiseState, onRejected: (...any) -> ...any): PromiseState
    return andThen(self, nil, onRejected)
end

--- Calls `fn` with the resolved values and passes those values on, whatever
--- it returns -- waiting first when it returns a promise.
function Promise.Tap(self: PromiseState, fn: (...any) -> ...any): PromiseState
    return andThen(self, function(...: any)
        local values = pack(...)
        local result = fn(...)
        if isPromise(result) then
            return andThen(result, function()
                return table.unpack(values, 1, values.n)
            end, nil)
        end
        return table.unpack(values, 1, values.n)
    end, nil)
end

--- On resolution, calls `fn` with these arguments rather than the values.
function Promise.AndThenCall(self: PromiseState, fn: (...any) -> ...any, ...: any): PromiseState
    local arguments = pack(...)
    return andThen(self, function()
        return fn(table.unpack(arguments, 1, arguments.n))
    end, nil)
end

--- On resolution, resolves with these values instead.
function Promise.AndThenReturn(self: PromiseState, ...: any): PromiseState
    local values = pack(...)
    return andThen(self, function()
        return table.unpack(values, 1, values.n)
    end, nil)
end

--- Calls `fn` with the final status however this settles -- resolved,
--- rejected or cancelled -- and settles as this one did.
function Promise.Finally(self: PromiseState, fn: (status: EnumPromiseState) -> ...any): PromiseState
    self._handled = true
    local child: PromiseState? = nil
    local cancelled = false
    child = new(function(resolve, reject)
        local function run(status: EnumPromiseState)
            local values = self._values
            task.spawn(function()
                local ok, problem = xpcall(fn, traceback, status)
                if not ok then
                    reject(problem)
                elseif status == Status.Resolved then
                    resolve(table.unpack(values, 1, values.n))
                elseif status == Status.Rejected then
                    reject(table.unpack(values, 1, values.n))
                elseif child ~= nil then
                    -- Cancelled settles as cancelled, as it came.
                    cancel(child)
                else
                    cancelled = true
                end
            end)
        end
        if self._status == Status.Started then
            table.insert(self._onSettled, run)
        else
            run(self._status)
        end
    end, if self._status == Status.Started then self else nil)
    -- `run` may have finished before `new` returned the child it had to cancel.
    if cancelled then
        cancel(child :: PromiseState)
    end
    return child :: PromiseState
end

--- Cancels: the executor's thread is stopped, its cancel hook runs, and so
--- does every consumer's. The parent stops once no consumer is left.
function Promise.Cancel(self: PromiseState)
    cancel(self)
end

--- Waits for this to settle and returns `true` and its values, or `false`
--- and the rejection. Cancelled is `false` and nothing.
function Promise.Await(self: PromiseState): (boolean, ...any)
    self._handled = true
    if self._status == Status.Started then
        local settled = Signal.new()
        table.insert(self._onSettled, function()
            settled:Fire()
        end)
        settled:Wait()
        settled:Destroy()
    end
    if self._status == Status.Resolved then
        return true, table.unpack(self._values, 1, self._values.n)
    elseif self._status == Status.Rejected then
        return false, table.unpack(self._values, 1, self._values.n)
    end
    return false
end

--- `Await`, with the status in place of the boolean.
function Promise.AwaitStatus(self: PromiseState): (EnumPromiseState, ...any)
    local _ = Promise.Await(self)
    return self._status, table.unpack(self._values, 1, self._values.n)
end

--- The resolved values, or an error raised with the rejection.
function Promise.ExpectAsync(self: PromiseState): ...any
    local results = pack(Promise.Await(self))
    if not results[1] then
        error(if results.n > 1 then results[2] else "Promise is cancelled", 2)
    end
    return table.unpack(results, 2, results.n)
end

--- Resolved already: passes it on. Otherwise rejects at once with
--- `rejection`.
function Promise.Now(self: PromiseState, rejection: any?): PromiseState
    if self._status == Status.Resolved then
        return andThen(self, nil, nil)
    end
    self._handled = true
    local reason = if rejection ~= nil then rejection else "This Promise was not resolved in time for :Now()"
    return new(function(_, reject)
        reject(reason)
    end)
end

local function all(promises: { any }, amount: number?, settledOnly: boolean?): PromiseState
    if type(promises) ~= "table" then
        error("Promise.all expects an array of promises", 3)
    end
    for index, value in promises do
        if not isPromise(value) then
            error(`Promise.all expects an array of promises; element {index} is a {typeof(value)}`, 3)
        end
    end
    local total = #promises
    local wanted = if amount ~= nil then amount else total
    if wanted == 0 then
        return new(function(resolve)
            resolve({})
        end)
    end

    return new(function(resolve, reject, onCancel)
        local results = table.create(total)
        local resolvedCount = 0
        local rejectedCount = 0
        local done = false
        local function stopOthers()
            for _, promise in promises do
                cancel(promise)
            end
        end
        onCancel(stopOthers)
        for index, promise in promises do
            andThen(promise, function(...: any)
                if done then
                    return
                end
                resolvedCount += 1
                if settledOnly then
                    results[index] = Status.Resolved
                elseif amount ~= nil then
                    table.insert(results, (...))
                else
                    results[index] = (...)
                end
                if resolvedCount + (if settledOnly then rejectedCount else 0) >= wanted then
                    done = true
                    if not settledOnly then
                        stopOthers()
                    end
                    resolve(results)
                end
            end, function(...: any)
                if done then
                    return
                end
                rejectedCount += 1
                if settledOnly then
                    results[index] = Status.Rejected
                    if resolvedCount + rejectedCount >= total then
                        done = true
                        resolve(results)
                    end
                elseif rejectedCount > total - wanted then
                    done = true
                    stopOthers()
                    reject(...)
                end
            end)
        end
    end)
end

local Static = {}

--- A promise run by `executor(resolve, reject, onCancel)`, at once and on its
--- own thread.
function Static.new(executor: (resolve: (...any) -> (), reject: (...any) -> (), onCancel: any) -> ())
    return new(executor, nil)
end

--- `new`, but the executor runs after the drain rather than at once.
function Static.defer(executor: (resolve: (...any) -> (), reject: (...any) -> (), onCancel: any) -> ())
    return new(function(resolve: (...any) -> (), reject: (...any) -> (), onCancel: (hook: (() -> ())?) -> boolean)
        task.defer(function()
            if onCancel(nil) then
                return
            end
            -- `any`: its declared type returns nothing, and `xpcall` then has
            -- no second value for the checker to find on failure.
            local ok, problem = xpcall(executor :: any, traceback, resolve, reject, onCancel)
            if not ok then
                reject(problem)
            end
        end)
    end, nil)
end

--- Resolved already, with these values.
function Static.resolve(...: any)
    local values = pack(...)
    return new(function(resolve)
        resolve(table.unpack(values, 1, values.n))
    end, nil)
end

--- Rejected already, with these values.
function Static.reject(...: any)
    local values = pack(...)
    return new(function(_, reject)
        reject(table.unpack(values, 1, values.n))
    end, nil)
end

--- A promise of `fn(...)`: what it returns resolves it, what it raises
--- rejects it.
function Static.try(fn: (...any) -> ...any, ...: any)
    local arguments = pack(...)
    return new(function(resolve)
        resolve(fn(table.unpack(arguments, 1, arguments.n)))
    end, nil)
end

--- Resolves after `seconds` of simulation time, with `seconds`.
function Static.delay(seconds: number)
    if type(seconds) ~= "number" or seconds ~= seconds or seconds < 0 then
        error("Promise.delay expects a number of seconds, zero or more", 2)
    end
    return new(function(resolve, _, onCancel)
        local timer = task.delay(seconds, function()
            resolve(seconds)
        end)
        onCancel(function()
            pcall(task.cancel, timer)
        end)
    end, nil)
end

--- Resolves with every value, in order, when all resolve; rejects with the
--- first rejection and cancels the rest.
function Static.all(promises: { any })
    return all(promises, nil, false)
end

--- Resolves when every promise has settled, with each one's final status.
function Static.allSettled(promises: { any })
    return all(promises, nil, true)
end

--- Settles as the first to settle does, and cancels the rest.
function Static.race(promises: { any })
    for index, value in promises do
        if not isPromise(value) then
            error(`Promise.race expects an array of promises; element {index} is a {typeof(value)}`, 2)
        end
    end
    return new(function(resolve, reject, onCancel)
        local function stopOthers()
            for _, promise in promises do
                cancel(promise)
            end
        end
        onCancel(stopOthers)
        for _, promise in promises do
            andThen(promise, function(...: any)
                stopOthers()
                resolve(...)
            end, function(...: any)
                stopOthers()
                reject(...)
            end)
        end
    end, nil)
end

--- Resolves with the first `count` values to resolve, in the order they did;
--- rejects once too many have rejected for that to happen.
function Static.some(promises: { any }, count: number)
    if type(count) ~= "number" or count < 0 then
        error("Promise.some expects a count of zero or more", 2)
    end
    if count > #promises then
        error("Promise.some was asked for more promises than it was given", 2)
    end
    return all(promises, count, false)
end

--- Resolves with the first value to resolve; rejects if all of them reject.
function Static.any(promises: { any })
    return andThen(all(promises, 1, false), function(values: { any })
        return values[1]
    end, nil)
end

--- Resolves with the arguments of the next fire of `event` that `predicate`
--- accepts (every fire, without one), and disconnects.
function Static.fromEvent(event: any, predicate: ((...any) -> boolean)?)
    return new(function(resolve, _, onCancel)
        local connection: any = nil
        connection = event:Connect(function(...: any)
            if predicate ~= nil and not predicate(...) then
                return
            end
            if connection ~= nil then
                connection:Disconnect()
            end
            resolve(...)
        end)
        onCancel(function()
            connection:Disconnect()
        end)
    end, nil)
end

--- `try(fn, ...)`, tried again up to `times` more times while it rejects.
function Static.retry(fn: (...any) -> ...any, times: number, ...: any)
    local arguments = pack(...)
    return andThen(Static.try(fn, ...), nil, function(...: any)
        if times > 0 then
            return Static.retry(fn, times - 1, table.unpack(arguments, 1, arguments.n))
        end
        return Static.reject(...)
    end)
end

--- `retry`, with `seconds` of simulation time before each new try.
function Static.retryWithDelay(fn: (...any) -> ...any, times: number, seconds: number, ...: any)
    local arguments = pack(...)
    return andThen(Static.try(fn, ...), nil, function(...: any)
        if times > 0 then
            return andThen(Static.delay(seconds), function()
                return Static.retryWithDelay(fn, times - 1, seconds, table.unpack(arguments, 1, arguments.n))
            end, nil)
        end
        return Static.reject(...)
    end)
end

--- Whether `value` is a Promise.
function Static.is(value: any): boolean
    return isPromise(value)
end

--- `fn`, made to return a promise of what it returns.
function Static.promisify(fn: (...any) -> ...any)
    return function(...: any)
        return Static.try(fn, ...)
    end
end

--- Rejects with `rejection` unless this settles within `seconds` of
--- simulation time, and is cancelled then.
function Promise.Timeout(self: PromiseState, seconds: number, rejection: any?): PromiseState
    local reason = if rejection ~= nil then rejection else "Timed out"
    return Static.race({
        andThen(Static.delay(seconds), function()
            return Static.reject(reason)
        end, nil),
        self,
    })
end

table.freeze(Promise)

return table.freeze(Static)
