--!strict
--- Conformance: `task.cancel` (api-design.md §3.2).
---
--- "Cancels a pending resumption. Cancelling a thread that is not scheduled is
--- an error." Both halves are load-bearing: the cancel must actually stop the
--- resumption (asserted by letting the deadline pass and finding the side effect
--- absent), and the error must be raised rather than silently ignored, because
--- a silent no-op is how a cancelled-then-resumed thread hides.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local afterEach = testing.afterEach

local connections: { Connection } = {}

local function track(connection: Connection): Connection
    table.insert(connections, connection)
    return connection
end

local function timestep(): number
    return game:GetService("PhysicsService").FixedTimestep
end

describe("task.cancel", function()
    afterEach(function()
        for _, connection in connections do
            connection:Disconnect()
        end
        table.clear(connections)
    end)

    it("stops a thread parked in task.wait from ever resuming", function()
        local step = timestep()
        local resumed = false
        local parked: thread? = nil
        task.spawn(function()
            parked = coroutine.running()
            task.wait(step * 2)
            resumed = true
        end)
        task.cancel((assert(parked, "task.spawn resumes the thread before returning")))
        task.wait(step * 8)
        expect(resumed):ToBe(false)
    end)

    it("leaves the statements after the cancelled wait unrun", function()
        local step = timestep()
        local order: { string } = {}
        local parked: thread? = nil
        task.spawn(function()
            parked = coroutine.running()
            table.insert(order, "before")
            task.wait(step * 2)
            table.insert(order, "after")
        end)
        task.cancel((assert(parked, "task.spawn resumes the thread before returning")))
        task.wait(step * 8)
        expect(order):ToEqual({ "before" })
    end)

    it("stops a pending task.delay from running its callback", function()
        local step = timestep()
        local ran = false
        local pending = task.delay(step * 3, function()
            ran = true
        end)
        task.cancel(pending)
        task.wait(step * 10)
        expect(ran):ToBe(false)
    end)

    it("stops a queued task.defer from running its callback", function()
        local ran = false
        local pending = task.defer(function()
            ran = true
        end)
        task.cancel(pending)
        task.wait()
        expect(ran):ToBe(false)
    end)

    it("cancels only the thread it was given", function()
        local step = timestep()
        local order: { string } = {}
        local first = task.delay(step * 3, function()
            table.insert(order, "first")
        end)
        task.delay(step * 3, function()
            table.insert(order, "second")
        end)
        task.delay(step * 3, function()
            table.insert(order, "third")
        end)
        task.cancel(first)
        task.wait(step * 8)
        expect(order):ToEqual({ "second", "third" })
    end)

    it("cancels a pending resumption from inside a signal handler", function()
        local step = timestep()
        local ran = false
        local pending = task.delay(step * 4, function()
            ran = true
        end)
        local signal = Signal.new()
        track(signal:Connect(function()
            task.cancel(pending)
        end))
        signal:Fire()
        task.wait(step * 10)
        expect(ran):ToBe(false)
        signal:Destroy()
    end)

    it("raises when cancelling a thread that was never scheduled", function()
        local idle = coroutine.create(function() end)
        expect(function()
            task.cancel(idle)
        end):ToThrow()
    end)

    it("raises when cancelling a thread that has already finished", function()
        local finished: thread? = nil
        task.spawn(function()
            finished = coroutine.running()
        end)
        local completed = assert(finished, "task.spawn resumes the thread before returning")
        expect(function()
            task.cancel(completed)
        end):ToThrow()
    end)

    it("raises when cancelling a delayed thread after its callback has already run", function()
        local step = timestep()
        local pending = task.delay(step, function() end)
        task.wait(step * 6)
        expect(function()
            task.cancel(pending)
        end):ToThrow()
    end)

    it("raises when cancelling the same thread twice", function()
        local step = timestep()
        local pending = task.delay(step * 4, function() end)
        task.cancel(pending)
        expect(function()
            task.cancel(pending)
        end):ToThrow()
    end)

    it("raises when given something that is not a thread", function()
        expect(function()
            task.cancel(nil :: any)
        end):ToThrow()
        expect(function()
            task.cancel(42 :: any)
        end):ToThrow()
        expect(function()
            task.cancel((function() end) :: any)
        end):ToThrow()
    end)

    it("does not stop the caller when it raises", function()
        local idle = coroutine.create(function() end)
        local ok = pcall(function()
            task.cancel(idle)
        end)
        expect(ok):ToBe(false)
        expect(task.wait()):ToBeA("number")
    end)
end)
