--!strict
--- Conformance: Promise (ADR 0094), on evaera's semantics with the engine's
--- names -- PascalCase methods, `Enum.PromiseState`, `ExpectAsync`.

local testing = require("@luaug/testing")

local describe = testing.describe
local it = testing.it
local expect = testing.expect

local State = Enum.PromiseState

describe("Promise", function()
    it("runs the executor at once, and resolves with every value", function()
        local order: { string } = {}
        local promise = Promise.new(function(resolve)
            table.insert(order, "executor")
            resolve(1, "two", nil)
        end)
        table.insert(order, "after")
        expect(order[1]):ToBe("executor")
        expect(order[2]):ToBe("after")
        local ok, a, b = promise:Await()
        expect(ok):ToBe(true)
        expect(a):ToBe(1)
        expect(b):ToBe("two")
        expect(promise:GetStatus()):ToBe(State.Resolved)
    end)

    it("waits for a value that arrives later", function()
        local promise = Promise.new(function(resolve)
            task.delay(0.05, resolve, "late")
        end)
        expect(promise:GetStatus()):ToBe(State.Started)
        expect(promise:ExpectAsync()):ToBe("late")
    end)

    it("chains through AndThen, and adopts a promise a handler returns", function()
        local result = Promise.resolve(2)
            :AndThen(function(x: number)
                return x * 10
            end)
            :AndThen(function(x: number)
                return Promise.new(function(resolve)
                    task.delay(0.02, resolve, x + 1)
                end)
            end)
        expect(result:ExpectAsync()):ToBe(21)
    end)

    it("rejects when an executor or a handler errors, and Catch recovers", function()
        local ok, problem = Promise.new(function()
            error("executor failed")
        end):Await()
        expect(ok):ToBe(false)
        expect(string.find(tostring(problem), "executor failed", 1, true) ~= nil):ToBe(true)

        local recovered = Promise.resolve()
            :AndThen(function()
                error("handler failed")
            end)
            :Catch(function(message: any)
                return "caught: " .. tostring(message)
            end)
        local value = recovered:ExpectAsync()
        expect(string.find(value, "handler failed", 1, true) ~= nil):ToBe(true)
    end)

    it("ExpectAsync raises the rejection", function()
        expect(function()
            Promise.reject("nope"):ExpectAsync()
        end):ToThrow("nope")
    end)

    it("Finally sees the final state and passes the outcome on", function()
        local seen: EnumItem? = nil
        local passed = Promise.resolve("kept"):Finally(function(state: EnumItem)
            seen = state
        end)
        expect(passed:ExpectAsync()):ToBe("kept")
        expect(seen):ToBe(State.Resolved)

        local rejected: EnumItem? = nil
        local ok = Promise.reject("x")
            :Finally(function(state: EnumItem)
                rejected = state
            end)
            :Await()
        expect(ok):ToBe(false)
        expect(rejected):ToBe(State.Rejected)
    end)

    it("Tap, AndThenCall and AndThenReturn", function()
        local tapped: number? = nil
        local passedOn = Promise.resolve(5)
            :Tap(function(x: number)
                tapped = x
                return "ignored"
            end)
            :ExpectAsync()
        expect(passedOn):ToBe(5)
        expect(tapped):ToBe(5)
        expect(Promise.resolve("a")
            :AndThenCall(function(x: string)
                return x .. "!"
            end, "b")
            :ExpectAsync()):ToBe("b!")
        expect(Promise.resolve(1):AndThenReturn("instead"):ExpectAsync()):ToBe("instead")
    end)

    it("cancels: the hook runs, consumers are cancelled, and the parent once nobody waits", function()
        local hooked = false
        local parent = Promise.new(function(_, _, onCancel)
            onCancel(function()
                hooked = true
            end)
        end)
        local first = parent:AndThen(function() end)
        local second = parent:AndThen(function() end)

        first:Cancel()
        expect(parent:GetStatus()):ToBe(State.Started)
        second:Cancel()
        expect(parent:GetStatus()):ToBe(State.Cancelled)
        expect(hooked):ToBe(true)

        local root = Promise.new(function() end)
        local child = root:AndThen(function() end)
        root:Cancel()
        expect(child:GetStatus()):ToBe(State.Cancelled)
    end)

    it("delay is simulation time and resolves with the seconds asked for", function()
        local promise = Promise.delay(0.1)
        expect(promise:GetStatus()):ToBe(State.Started)
        expect(promise:ExpectAsync()):ToBe(0.1)
    end)

    it("all, allSettled, race, any and some", function()
        local values = Promise.all({ Promise.resolve(1), Promise.delay(0.02), Promise.resolve(3) }):ExpectAsync()
        expect(values[1]):ToBe(1)
        expect(values[2]):ToBe(0.02)
        expect(values[3]):ToBe(3)

        local ok = Promise.all({ Promise.resolve(1), Promise.reject("bad") }):Await()
        expect(ok):ToBe(false)

        local states = Promise.allSettled({ Promise.resolve(1), Promise.reject("x") }):ExpectAsync()
        expect(states[1]):ToBe(State.Resolved)
        expect(states[2]):ToBe(State.Rejected)

        local slow = Promise.delay(1)
        expect(Promise.race({ Promise.resolve("fast"), slow }):ExpectAsync()):ToBe("fast")
        expect(slow:GetStatus()):ToBe(State.Cancelled)

        expect(Promise.any({ Promise.reject("a"), Promise.resolve("b") }):ExpectAsync()):ToBe("b")

        local two = Promise.some({ Promise.resolve(1), Promise.reject("x"), Promise.resolve(3) }, 2):ExpectAsync()
        expect(#two):ToBe(2)
    end)

    it("fromEvent resolves on the fire the predicate accepts, and disconnects", function()
        local signal = Signal.new()
        local promise = Promise.fromEvent(signal, function(value: number)
            return value > 1
        end)
        signal:Fire(1)
        signal:Fire(2)
        expect(promise:ExpectAsync()):ToBe(2)
    end)

    it("retry tries again while it rejects", function()
        local attempts = 0
        local value = Promise.retry(function()
            attempts += 1
            if attempts < 3 then
                error("not yet")
            end
            return "done"
        end, 5):ExpectAsync()
        expect(value):ToBe("done")
        expect(attempts):ToBe(3)
    end)

    it("Timeout rejects a promise that does not settle in time, and cancels it", function()
        local never = Promise.new(function() end)
        local ok, reason = never:Timeout(0.05, "too slow"):Await()
        expect(ok):ToBe(false)
        expect(reason):ToBe("too slow")
        expect(never:GetStatus()):ToBe(State.Cancelled)
    end)

    it("Now passes a resolved promise on and rejects a pending one", function()
        expect(Promise.resolve("ready"):Now():ExpectAsync()):ToBe("ready")
        local ok = Promise.new(function() end):Now("not ready"):Await()
        expect(ok):ToBe(false)
    end)

    it("is, try and promisify", function()
        expect(Promise.is(Promise.resolve())):ToBe(true)
        expect(Promise.is({})):ToBe(false)
        expect(Promise.try(function(a: number, b: number)
            return a + b
        end, 1, 2):ExpectAsync()):ToBe(3)
        local promised = Promise.promisify(function(x: number)
            return x * 2
        end)
        expect(promised(4):ExpectAsync()):ToBe(8)
    end)

    it("warns about a rejection nothing handled, and not about one that was", function()
        local debugService = game:GetService("DebugService")
        local messages: { string } = {}
        local capture = debugService.MessageOut:Connect(function(message: string)
            table.insert(messages, message)
        end)

        Promise.reject("nobody took this")
        Promise.reject("somebody took this"):Catch(function() end)

        task.wait()
        task.wait()
        capture:Disconnect()

        local warned, wronglyWarned = false, false
        for _, message in messages do
            if string.find(message, "nobody took this", 1, true) then
                warned = true
            end
            if string.find(message, "somebody took this", 1, true) then
                wronglyWarned = true
            end
        end
        expect(warned):ToBe(true)
        expect(wronglyWarned):ToBe(false)
    end)
end)
