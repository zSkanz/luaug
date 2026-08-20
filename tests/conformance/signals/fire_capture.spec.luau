--!strict
--- Conformance: what a fire captures, and connection order.
--- Written from api-design.md §3.1 ("What a fire captures", "Connection order
--- is guaranteed"): enqueuing a fire records its arguments and the identity of
--- the connection list at that moment.

local testing = require("@luaug/testing")

local describe = testing.describe
local it = testing.it
local expect = testing.expect

describe("what a fire captures", function()
    -- "A connection made after the fire does not run for it. It was not
    -- listening when the thing happened."
    it("does not invoke a connection made after the fire", function()
        local sig = Signal.new()
        local count = 0

        sig:Fire()
        sig:Connect(function()
            count += 1
        end)

        task.wait()
        expect(count):ToBe(0)
    end)

    it("invokes a connection made before the fire", function()
        local sig = Signal.new()
        local count = 0
        sig:Connect(function()
            count += 1
        end)

        sig:Fire()

        task.wait()
        expect(count):ToBe(1)
    end)

    it("does not invoke a connection disconnected before the drain", function()
        local sig = Signal.new()
        local count = 0
        local connection = sig:Connect(function()
            count += 1
        end)

        sig:Fire()
        connection:Disconnect()

        task.wait()
        expect(count):ToBe(0)
    end)

    -- ":Disconnect() is reliable, not advisory" -- including across handlers of
    -- the same fire.
    it("does not invoke a connection disconnected by an earlier handler of the same fire", function()
        local sig = Signal.new()
        local secondRan = false
        local connection: Connection

        sig:Connect(function()
            connection:Disconnect()
        end)
        connection = sig:Connect(function()
            secondRan = true
        end)

        sig:Fire()

        task.wait()
        expect(secondRan):ToBe(false)
    end)

    it("does not invoke a connection disconnected by a handler of an earlier fire in the same drain", function()
        local first = Signal.new()
        local second = Signal.new()
        local ran = false
        local connection = second:Connect(function()
            ran = true
        end)
        first:Connect(function()
            connection:Disconnect()
        end)

        first:Fire()
        second:Fire()

        task.wait()
        expect(ran):ToBe(false)
    end)

    it("still counts a handler that already ran when a later handler disconnects it", function()
        local sig = Signal.new()
        local count = 0
        local connection: Connection
        connection = sig:Connect(function()
            count += 1
        end)
        sig:Connect(function()
            connection:Disconnect()
        end)

        sig:Fire()

        task.wait()
        expect(count):ToBe(1)
        expect(connection.Connected):ToBe(false)
    end)

    it("does not invoke a connection made during the drain for a fire queued before it", function()
        local trigger = Signal.new()
        local sig = Signal.new()
        local count = 0

        trigger:Connect(function()
            sig:Connect(function()
                count += 1
            end)
        end)

        -- The `sig` fire is queued first, so it captured a connection list that
        -- the handler below has not joined yet.
        sig:Fire()
        trigger:Fire()

        task.wait()
        expect(count):ToBe(0)
    end)

    it("invokes a connection made during the drain for a fire raised after it", function()
        local trigger = Signal.new()
        local sig = Signal.new()
        local count = 0

        trigger:Connect(function()
            sig:Connect(function()
                count += 1
            end)
            sig:Fire()
        end)

        trigger:Fire()

        task.wait()
        expect(count):ToBe(1)
    end)

    it("keeps a connection alive across drains", function()
        local sig = Signal.new()
        local count = 0
        sig:Connect(function()
            count += 1
        end)

        sig:Fire()
        task.wait()
        expect(count):ToBe(1)

        sig:Fire()
        task.wait()
        expect(count):ToBe(2)
    end)
end)

describe("connection order", function()
    it("invokes handlers in the order they were connected", function()
        local sig = Signal.new()
        local log: { string } = {}
        sig:Connect(function()
            table.insert(log, "first")
        end)
        sig:Connect(function()
            table.insert(log, "second")
        end)
        sig:Connect(function()
            table.insert(log, "third")
        end)

        sig:Fire()

        task.wait()
        expect(log):ToEqual({ "first", "second", "third" })
    end)

    it("treats a function connected twice as two independent connections", function()
        local sig = Signal.new()
        local count = 0
        local function handler()
            count += 1
        end
        local first = sig:Connect(handler)
        sig:Connect(handler)

        sig:Fire()
        task.wait()
        expect(count):ToBe(2)

        first:Disconnect()
        sig:Fire()
        task.wait()
        expect(count):ToBe(3)
    end)

    it("keeps connection order stable after an earlier connection is removed", function()
        local sig = Signal.new()
        local log: { string } = {}
        local first = sig:Connect(function()
            table.insert(log, "first")
        end)
        sig:Connect(function()
            table.insert(log, "second")
        end)
        sig:Connect(function()
            table.insert(log, "third")
        end)
        first:Disconnect()

        sig:Fire()

        task.wait()
        expect(log):ToEqual({ "second", "third" })
    end)

    it("invokes a Once handler in connection order among Connect handlers", function()
        local sig = Signal.new()
        local log: { string } = {}
        sig:Connect(function()
            table.insert(log, "before")
        end)
        sig:Once(function()
            table.insert(log, "once")
        end)
        sig:Connect(function()
            table.insert(log, "after")
        end)

        sig:Fire()

        task.wait()
        expect(log):ToEqual({ "before", "once", "after" })
    end)
end)

describe("Signal:Once", function()
    -- "`:Once` disconnects on invocation, so a signal fired twice before a drain
    -- invokes a `:Once` handler exactly once."
    it("invokes the handler exactly once when the signal fired twice before the drain", function()
        local sig = Signal.new()
        local count = 0
        sig:Once(function()
            count += 1
        end)

        sig:Fire()
        sig:Fire()

        task.wait()
        expect(count):ToBe(1)
    end)

    it("receives the arguments of the first of the two fires", function()
        local sig = Signal.new()
        local seen: string? = nil
        sig:Once(function(value: string)
            seen = value
        end)

        sig:Fire("first")
        sig:Fire("second")

        task.wait()
        expect(seen):ToBe("first")
    end)

    it("does not invoke the handler for a fire at a later resumption point", function()
        local sig = Signal.new()
        local count = 0
        sig:Once(function()
            count += 1
        end)

        sig:Fire()
        task.wait()
        sig:Fire()
        task.wait()

        expect(count):ToBe(1)
    end)

    it("does not invoke a Once handler connected after the fire", function()
        local sig = Signal.new()
        local count = 0

        sig:Fire()
        sig:Once(function()
            count += 1
        end)

        task.wait()
        expect(count):ToBe(0)
    end)

    it("does not invoke a Once handler disconnected before the drain", function()
        local sig = Signal.new()
        local count = 0
        local connection = sig:Once(function()
            count += 1
        end)

        sig:Fire()
        connection:Disconnect()

        task.wait()
        expect(count):ToBe(0)
    end)

    it("does not invoke a handler that disconnected itself for a second queued fire", function()
        local sig = Signal.new()
        local count = 0
        local connection: Connection
        connection = sig:Connect(function()
            count += 1
            connection:Disconnect()
        end)

        sig:Fire()
        sig:Fire()

        task.wait()
        expect(count):ToBe(1)
    end)
end)
