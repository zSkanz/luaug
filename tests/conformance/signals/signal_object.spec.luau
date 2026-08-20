--!strict
--- Conformance: the user-creatable `Signal` and its `Connection`.
--- Written from api-design.md §2.3 (the `Signal<T...>` / `Connection` row),
--- §3.1, and divergences #4 and #5 of §2.5.

local testing = require("@luaug/testing")

local describe = testing.describe
local it = testing.it
local expect = testing.expect

describe("Signal.new", function()
    it("is reachable as a global, without a require", function()
        expect(Signal).Never:ToBeNil()
        expect((Signal :: any).new):ToBeA("function")
    end)

    it("returns a distinct signal from every call", function()
        local first = Signal.new()
        local second = Signal.new()
        expect(first).Never:ToBe(second)
    end)

    -- Divergence #4: the type is named `Signal`, and the legacy spelling must
    -- not survive anywhere in the observable surface.
    it("reports its type as Signal, never as RBXScriptSignal", function()
        local sig = Signal.new()
        expect(typeof(sig)):ToBe("Signal")
        expect(typeof(sig)).Never:ToBe("RBXScriptSignal")
    end)

    it("reports a connection's type as Connection, never as RBXScriptConnection", function()
        local sig = Signal.new()
        local connection = sig:Connect(function() end)
        expect(typeof(connection)):ToBe("Connection")
        expect(typeof(connection)).Never:ToBe("RBXScriptConnection")
        connection:Disconnect()
    end)

    it("exposes Fire, Connect, Once, Wait and Destroy", function()
        local sig = Signal.new() :: any
        expect(sig.Fire):ToBeA("function")
        expect(sig.Connect):ToBeA("function")
        expect(sig.Once):ToBeA("function")
        expect(sig.Wait):ToBeA("function")
        expect(sig.Destroy):ToBeA("function")
    end)

    it("accepts a fire with no connections", function()
        local sig = Signal.new()
        sig:Fire("nobody is listening")
        task.wait()
    end)
end)

describe("Connection", function()
    it("is connected as soon as Connect returns", function()
        local sig = Signal.new()
        local connection = sig:Connect(function() end)
        expect(connection.Connected):ToBe(true)
    end)

    it("reports itself disconnected after Disconnect", function()
        local sig = Signal.new()
        local connection = sig:Connect(function() end)
        connection:Disconnect()
        expect(connection.Connected):ToBe(false)
    end)

    it("stays disconnected when Disconnect is called a second time", function()
        local sig = Signal.new()
        local connection = sig:Connect(function() end)
        connection:Disconnect()
        connection:Disconnect()
        expect(connection.Connected):ToBe(false)
    end)

    it("leaves the other connections of the same signal connected", function()
        local sig = Signal.new()
        local first = sig:Connect(function() end)
        local second = sig:Connect(function() end)
        first:Disconnect()
        expect(first.Connected):ToBe(false)
        expect(second.Connected):ToBe(true)
        second:Disconnect()
    end)

    it("is connected before a Once handler has been invoked", function()
        local sig = Signal.new()
        local connection = sig:Once(function() end)
        expect(connection.Connected):ToBe(true)
        connection:Disconnect()
    end)

    it("is disconnected after a Once handler has been invoked", function()
        local sig = Signal.new()
        local connection = sig:Once(function() end)
        sig:Fire()
        task.wait()
        expect(connection.Connected):ToBe(false)
    end)
end)

describe("Signal:Destroy", function()
    it("invokes nothing for a fire queued before the signal was destroyed", function()
        local sig = Signal.new()
        local count = 0
        sig:Connect(function()
            count += 1
        end)

        sig:Fire()
        sig:Destroy()

        task.wait()
        expect(count):ToBe(0)
    end)

    it("reports the connections of a destroyed signal as disconnected", function()
        local sig = Signal.new()
        local connection = sig:Connect(function() end)
        sig:Destroy()
        task.wait()
        expect(connection.Connected):ToBe(false)
    end)
end)

describe("the instance classes Signal.new replaces", function()
    -- Divergence #5: `Signal.new()` replaces BindableEvent/BindableFunction, so
    -- neither class name may be creatable.
    it("does not provide a BindableEvent class", function()
        local instanceNew = Instance.new :: any
        expect(function()
            instanceNew("BindableEvent")
        end):ToThrow()
    end)

    it("does not provide a BindableFunction class", function()
        local instanceNew = Instance.new :: any
        expect(function()
            instanceNew("BindableFunction")
        end):ToThrow()
    end)
end)
