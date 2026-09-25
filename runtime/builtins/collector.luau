--!strict
--[[
	Collector -- the engine's own cleanup bag (ADR 0094).

	Modelled on the surface of Janitor (MIT), which is the de-facto reference for
	this job; nothing of it is copied. What something is decides how it is
	cleaned: a function is called, a thread is cancelled, a `Connection` is
	disconnected, an `Instance` or a `Signal` or a table with `Destroy` is
	destroyed, a `Promise` is cancelled. `method` names another cleanup method,
	and `true` calls the object itself.

	**Cleaned in reverse insertion order.** Janitor walks a hash table, which R10
	forbids, and last-in-first-out is the order destructors run in: a connection
	made after the instance it listens to is disconnected before that instance
	is destroyed.

	Compiled at build time into the script module and installed as the global
	`Collector`, read-only, before the sandbox closes.
]]

type Entry = {
    object: any,
    method: string | boolean | nil,
    key: any,
}

type CollectorState = {
    _entries: { Entry },
    _keys: { [any]: Entry },
    _destroyed: boolean,
}

local Collector = {}
Collector.__index = Collector

local function isPromise(value: any): boolean
    -- Looked up when asked rather than when this chunk ran: the two builtins
    -- are installed one after the other, and neither may depend on the order.
    local promise = Promise :: any
    return promise ~= nil and promise.is(value)
end

local function clean(entry: Entry)
    local object = entry.object
    local method = entry.method
    if method == true then
        object()
        return
    end
    if type(method) == "string" then
        object[method](object)
        return
    end

    local kind = typeof(object)
    if kind == "function" then
        object()
    elseif kind == "thread" then
        -- Already finished or already cancelled is not an error worth raising
        -- from a cleanup.
        pcall(task.cancel, object)
    elseif kind == "Connection" then
        object:Disconnect()
    elseif kind == "Instance" or kind == "Signal" then
        object:Destroy()
    elseif isPromise(object) then
        object:Cancel()
    elseif (kind == "table" or kind == "userdata") and type(object.Destroy) == "function" then
        -- Found through the object's metatable too, which is where a class
        -- keeps it: `setmetatable(self, Class)` with `Class.__index = Class`.
        -- A FUNCTION, not merely present -- a `Destroy` field holding
        -- anything else is not something to call.
        object:Destroy()
    else
        error(`Collector cannot clean a {kind}; give the method to call as :Add's second argument`, 3)
    end
end

local function check(self: CollectorState)
    if self._destroyed then
        error("this Collector was destroyed; make a new one", 3)
    end
end

--- A new, empty Collector.
function Collector.new()
    local self: CollectorState = {
        _entries = {},
        _keys = {},
        _destroyed = false,
    }
    return setmetatable(self, Collector)
end

--- Takes `object` to clean later, and returns it. `method` names the method to
--- call instead of the one its kind implies, or `true` to call the object
--- itself; `key` names it, so `Remove` and `Get` can find it -- and adding
--- under a key already held cleans what was there first.
function Collector.Add(self: CollectorState, object: any, method: (string | boolean)?, key: any?): any
    check(self)
    if object == nil then
        error("Collector:Add was given nil", 2)
    end
    if key ~= nil then
        local previous = self._keys[key]
        if previous ~= nil then
            Collector.Remove(self, key)
        end
    end
    local entry: Entry = { object = object, method = method, key = key }
    table.insert(self._entries, entry)
    if key ~= nil then
        self._keys[key] = entry
    end
    return object
end

--- Takes a promise to cancel on cleanup, and returns it. Once it settles on
--- its own there is nothing left to cancel, so it lets go of it then.
function Collector.AddPromise(self: CollectorState, promise: any): any
    check(self)
    if not isPromise(promise) then
        error("Collector:AddPromise was given something that is not a Promise", 2)
    end
    local entry: Entry = { object = promise, method = nil, key = nil }
    table.insert(self._entries, entry)
    promise:Finally(function()
        local at = table.find(self._entries, entry)
        if at ~= nil then
            table.remove(self._entries, at)
        end
    end)
    return promise
end

--- Cleans the object held under `key` now, and forgets it.
function Collector.Remove(self: CollectorState, key: any)
    local entry = self._keys[key]
    if entry == nil then
        return
    end
    self._keys[key] = nil
    local at = table.find(self._entries, entry)
    if at ~= nil then
        table.remove(self._entries, at)
    end
    clean(entry)
end

--- Forgets the object held under `key` without cleaning it.
function Collector.RemoveNoClean(self: CollectorState, key: any)
    local entry = self._keys[key]
    if entry == nil then
        return
    end
    self._keys[key] = nil
    local at = table.find(self._entries, entry)
    if at ~= nil then
        table.remove(self._entries, at)
    end
end

--- The object held under `key`, or nil.
function Collector.Get(self: CollectorState, key: any): any
    local entry = self._keys[key]
    return if entry ~= nil then entry.object else nil
end

--- Cleans everything, newest first, and stays usable. A cleanup that adds to
--- this Collector is cleaned in the same call.
function Collector.Cleanup(self: CollectorState)
    local entries = self._entries
    while #entries > 0 do
        local entry = table.remove(entries) :: Entry
        if entry.key ~= nil and self._keys[entry.key] == entry then
            self._keys[entry.key] = nil
        end
        clean(entry)
    end
end

--- Cleans everything, and the Collector with it: adding to it afterwards is an
--- error.
function Collector.Destroy(self: CollectorState)
    Collector.Cleanup(self)
    self._destroyed = true
end

--- Cleans this Collector when `instance` is destroyed, and returns that
--- connection. More than one link is allowed only when asked for; otherwise a
--- new link replaces the last.
function Collector.LinkToInstance(self: CollectorState, instance: Instance, allowMultiple: boolean?): any
    check(self)
    local connection = instance.Destroying:Connect(function()
        Collector.Cleanup(self)
    end)
    if allowMultiple then
        Collector.Add(self, connection)
    else
        Collector.Add(self, connection, nil, "__linkToInstance")
    end
    return connection
end

table.freeze(Collector)

return table.freeze({
    new = Collector.new,
})
