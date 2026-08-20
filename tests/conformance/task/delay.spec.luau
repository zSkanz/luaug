--!strict
--- Conformance: `task.delay` (api-design.md §3.2).
---
--- "Resumes `fn` at the first task-resume phase at or after `duration` seconds
--- of SimClock time." Durations are expressed as multiples of
--- `PhysicsService.FixedTimestep` (§2.1) rather than as literal seconds, so the
--- cases stay true on a host configured to a tick rate other than the default.

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

describe("task.delay", function()
    afterEach(function()
        for _, connection in connections do
            connection:Disconnect()
        end
        table.clear(connections)
    end)

    it("does not run the callback before the call returns", function()
        local ran = false
        task.delay(timestep() * 4, function()
            ran = true
        end)
        expect(ran):ToBe(false)
    end)

    it("does not run the callback at the next resumption point", function()
        local step = timestep()
        local ran = false
        task.delay(step * 10, function()
            ran = true
        end)
        task.wait(step * 2)
        expect(ran):ToBe(false)
    end)

    it("runs the callback once the requested SimClock duration has elapsed", function()
        local step = timestep()
        local ran = false
        task.delay(step * 10, function()
            ran = true
        end)
        task.wait(step * 12)
        expect(ran):ToBe(true)
    end)

    it("runs the callback exactly once", function()
        local step = timestep()
        local runs = 0
        task.delay(step * 2, function()
            runs += 1
        end)
        task.wait(step * 10)
        expect(runs):ToBe(1)
    end)

    it("returns the thread it will resume", function()
        local inner: thread? = nil
        local returned = task.delay(timestep(), function()
            inner = coroutine.running()
        end)
        expect(returned):ToBeA("thread")
        task.wait(timestep() * 4)
        expect(returned):ToBe(inner)
    end)

    it("passes its extra arguments to the callback", function()
        local step = timestep()
        local received: { any } = {}
        task.delay(step, function(first: number, second: string, third: boolean)
            received = { first, second, third }
        end, 1, "two", true)
        task.wait(step * 4)
        expect(received):ToEqual({ 1, "two", true } :: { any })
    end)

    -- The same claim as above, held across many ticks with other deferred work
    -- running in between. Written after `examples/01-instances` found that a
    -- one-tick delay forwarded its arguments and a forty-tick one did not: the
    -- values were parked in storage the drain recycled, so a long enough wait
    -- turned every argument into nil. A contract that holds for one tick and not
    -- for forty is not the contract §3.2 states.
    it("still passes its arguments after many ticks of other deferred work", function()
        local step = timestep()
        local received: { any } = {}
        task.delay(step * 40, function(first: string, second: number)
            received = { first, second }
        end, "kept", 99)

        -- Noise, on purpose: every one of these captures arguments of its own
        -- while the delayed callback is still waiting for its deadline.
        for index = 1, 12 do
            task.defer(function(_a: number, _b: number) end, index, index * 2)
            task.delay(step, function(_a: string) end, `filler{index}`)
        end

        task.wait(step * 45)
        expect(received):ToEqual({ "kept", 99 } :: { any })
    end)

    it("runs the callback on a coroutine of its own, not the scheduling thread's", function()
        local step = timestep()
        local caller = coroutine.running()
        local inner: thread? = nil
        task.delay(step, function()
            inner = coroutine.running()
        end)
        task.wait(step * 4)
        expect(inner):ToBeA("thread")
        expect(inner).Never:ToBe(caller)
    end)

    -- "at or after 0 seconds" makes the very next task-resume phase eligible;
    -- a zero delay is still a delay, so it never runs before the call returns.
    it("runs a zero-duration callback at the next task-resume phase", function()
        local ran = false
        task.delay(0, function()
            ran = true
        end)
        expect(ran):ToBe(false)
        task.wait()
        expect(ran):ToBe(true)
    end)

    it("resumes an earlier deadline before a later one", function()
        local step = timestep()
        local order: { string } = {}
        task.delay(step * 6, function()
            table.insert(order, "late")
        end)
        task.delay(step * 2, function()
            table.insert(order, "early")
        end)
        task.wait(step * 10)
        expect(order):ToEqual({ "early", "late" })
    end)

    it("resumes equal deadlines in the order they were scheduled", function()
        local step = timestep()
        local order: { string } = {}
        task.delay(step * 3, function()
            table.insert(order, "first")
        end)
        task.delay(step * 3, function()
            table.insert(order, "second")
        end)
        task.delay(step * 3, function()
            table.insert(order, "third")
        end)
        task.wait(step * 8)
        expect(order):ToEqual({ "first", "second", "third" })
    end)

    it("runs a callback scheduled from inside a signal handler", function()
        local step = timestep()
        local ran = false
        local signal = Signal.new()
        track(signal:Connect(function()
            task.delay(step * 4, function()
                ran = true
            end)
        end))
        signal:Fire()
        task.wait(step)
        expect(ran):ToBe(false)
        task.wait(step * 8)
        expect(ran):ToBe(true)
        signal:Destroy()
    end)

    it("runs a callback scheduled from inside a deferred callback", function()
        local step = timestep()
        local ran = false
        task.defer(function()
            task.delay(step * 4, function()
                ran = true
            end)
        end)
        task.wait(step * 10)
        expect(ran):ToBe(true)
    end)

    it("runs a callback scheduled from inside another delayed callback", function()
        local step = timestep()
        local order: { string } = {}
        task.delay(step * 2, function()
            table.insert(order, "outer")
            task.delay(step * 2, function()
                table.insert(order, "inner")
            end)
        end)
        task.wait(step * 10)
        expect(order):ToEqual({ "outer", "inner" })
    end)

    it("lets a delayed callback yield on the SimClock like any other thread", function()
        local step = timestep()
        local order: { string } = {}
        task.delay(step, function()
            table.insert(order, "start")
            task.wait(step * 2)
            table.insert(order, "resumed")
        end)
        task.wait(step * 2)
        expect(order):ToEqual({ "start" })
        task.wait(step * 6)
        expect(order):ToEqual({ "start", "resumed" })
    end)

    it("keeps running other callbacks after a delayed callback errors", function()
        local step = timestep()
        local ran = false
        task.delay(step * 2, function()
            error("delayed failure")
        end)
        task.delay(step * 2, function()
            ran = true
        end)
        task.wait(step * 8)
        expect(ran):ToBe(true)
    end)

    -- Timers run on the SimClock, never the wall clock (ADR 0025), so the number
    -- of ticks a delay takes is a property of the tick rate alone.
    it("takes the same number of ticks for the same duration on every run", function()
        local step = timestep()
        local function ticksUntilRun(duration: number): number
            local elapsed = 0
            local done = false
            task.delay(duration, function()
                done = true
            end)
            while not done do
                elapsed += task.wait()
            end
            return elapsed / step
        end
        local first = ticksUntilRun(step * 5)
        local second = ticksUntilRun(step * 5)
        expect(second):ToBeCloseTo(first, 1e-6)
    end)
end)
