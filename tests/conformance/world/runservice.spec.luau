--!strict
--- Conformance: RunService frame events (docs/api-design.md 2.1, 3.1;
--- architecture.md 3).
---
--- The rate split is the one deliberate divergence from a naive single-rate
--- model and both documents call it out loudly, so it is pinned here rather
--- than left to the scheduler: `PreAnimation`, `PreSimulation`,
--- `PostSimulation` and `Heartbeat` are per sim tick with a fixed dt, and
--- `PreRender` is per render frame with a variable dt and is therefore never
--- part of the in-tick cycle.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

--- The in-frame order 2.1 documents for the fixed-tick half of the frame.
local FIXED_TICK_ORDER = { "PreAnimation", "PreSimulation", "PostSimulation", "Heartbeat" }

local function firstIndexOf(log: { string }, value: string): number?
    for index, entry in log do
        if entry == value then
            return index
        end
    end
    return nil
end

describe("RunService", function()
    local runService: RunService
    local connections: { Connection }

    --- The order cases have to name their events as data, so the lookup goes
    --- through one cast instead of five copies of the same handler.
    local function signalNamed(eventName: string): Signal<number>
        return (runService :: any)[eventName]
    end

    beforeEach(function()
        runService = game:GetService("RunService")
        connections = {}
    end)

    -- A leaked connection would keep appending to a dead spec's log and a
    -- leaked pause would deadlock every later case that waits for a tick.
    afterEach(function()
        for _, connection in connections do
            connection:Disconnect()
        end
        if runService:IsPaused() then
            runService:Resume()
        end
    end)

    local function record(eventName: string, log: { string }, dts: { number }?): ()
        local connection = signalNamed(eventName):Connect(function(dt: number)
            table.insert(log, eventName)
            if dts then
                table.insert(dts, dt)
            end
        end)
        table.insert(connections, connection)
    end

    describe("acquisition", function()
        it("is a singleton child of game", function()
            expect(game:GetService("RunService")):ToBe(runService)
            expect(runService.Parent):ToBe(game)
            expect(runService.ClassName):ToBe("RunService")
        end)
    end)

    describe("frame events", function()
        it("exposes all five documented frame events as connectable signals", function()
            for _, eventName in { "PreRender", "PreAnimation", "PreSimulation", "PostSimulation", "Heartbeat" } do
                local connection = signalNamed(eventName):Connect(function() end)
                table.insert(connections, connection)
                expect(connection.Connected):ToBe(true)
                connection:Disconnect()
                expect(connection.Connected):ToBe(false)
            end
        end)

        it("fires PreAnimation, PreSimulation, PostSimulation and Heartbeat in that order", function()
            local log: { string } = {}
            for _, eventName in FIXED_TICK_ORDER do
                record(eventName, log)
            end

            task.wait()
            task.wait()

            local start = assert(firstIndexOf(log, "PreAnimation"), "PreAnimation never fired")
            expect(log[start + 1]):ToBe("PreSimulation")
            expect(log[start + 2]):ToBe("PostSimulation")
            expect(log[start + 3]):ToBe("Heartbeat")
        end)

        it("repeats the same four-event cycle on every sim tick", function()
            local log: { string } = {}
            for _, eventName in FIXED_TICK_ORDER do
                record(eventName, log)
            end

            task.wait()
            task.wait()
            task.wait()
            task.wait()

            local index = assert(firstIndexOf(log, "PreAnimation"), "PreAnimation never fired")
            local cycles = 0
            while index + 3 <= #log do
                for offset, eventName in FIXED_TICK_ORDER do
                    expect(log[index + offset - 1]):ToBe(eventName)
                end
                cycles += 1
                index += 4
            end
            expect(cycles >= 2):ToBe(true)
        end)

        it("fires each fixed-tick event once per tick", function()
            local log: { string } = {}
            for _, eventName in FIXED_TICK_ORDER do
                record(eventName, log)
            end

            task.wait()
            task.wait()
            task.wait()

            local counts: { [string]: number } = {}
            local start = assert(firstIndexOf(log, "PreAnimation"), "PreAnimation never fired")
            for index = start, #log do
                local eventName = log[index]
                counts[eventName] = (counts[eventName] or 0) + 1
            end
            -- Heartbeat closes a cycle PreAnimation opened, so a window that
            -- starts at a PreAnimation holds at most one more of each earlier
            -- phase than of Heartbeat.
            local heartbeats = counts.Heartbeat or 0
            expect(heartbeats >= 1):ToBe(true)
            for _, eventName in FIXED_TICK_ORDER do
                local seen = counts[eventName] or 0
                expect(seen >= heartbeats):ToBe(true)
                expect(seen <= heartbeats + 1):ToBe(true)
            end
        end)

        -- PreRender is render-rate: it may fire many ticks apart, or not at
        -- all in a headless run, but it can never land inside a sim tick.
        it("never fires PreRender between the fixed-tick events of one tick", function()
            local log: { string } = {}
            for _, eventName in FIXED_TICK_ORDER do
                record(eventName, log)
            end
            record("PreRender", log)

            task.wait()
            task.wait()
            task.wait()

            local index = assert(firstIndexOf(log, "PreAnimation"), "PreAnimation never fired")
            while index + 3 <= #log do
                if log[index] ~= "PreAnimation" then
                    index += 1
                    continue
                end
                expect(log[index + 1]):ToBe("PreSimulation")
                expect(log[index + 2]):ToBe("PostSimulation")
                expect(log[index + 3]):ToBe("Heartbeat")
                index += 4
            end
        end)
    end)

    describe("delta time", function()
        it("passes a number to every fixed-tick event", function()
            local log: { string } = {}
            local dts: { number } = {}
            for _, eventName in FIXED_TICK_ORDER do
                record(eventName, log, dts)
            end

            task.wait()
            task.wait()

            expect(#dts >= 4):ToBe(true)
            for _, dt in dts do
                expect(dt):ToBeA("number")
                expect(dt > 0):ToBe(true)
            end
        end)

        it("passes the same fixed dt to every fixed-tick event of every tick", function()
            local log: { string } = {}
            local dts: { number } = {}
            for _, eventName in FIXED_TICK_ORDER do
                record(eventName, log, dts)
            end

            task.wait()
            task.wait()
            task.wait()

            local first = dts[1]
            expect(first):ToBeA("number")
            for _, dt in dts do
                expect(dt):ToBe(first)
            end
        end)

        it("returns the tick dt from Heartbeat:Wait", function()
            local dt = runService.Heartbeat:Wait()
            expect(dt):ToBeA("number")
            expect(dt > 0):ToBe(true)
        end)

        it("stops delivering to a disconnected handler", function()
            local runs = 0
            local connection = runService.Heartbeat:Connect(function()
                runs += 1
            end)
            table.insert(connections, connection)

            -- Two waits rather than one. `task.wait` resumes BETWEEN
            -- PostSimulation and Heartbeat (§3.2), so whether a single wait
            -- spans a Heartbeat depends on where in a tick the previous case
            -- left this coroutine -- and what is under test here is the
            -- disconnect, not the phase this case happens to start in.
            task.wait()
            task.wait()
            local afterFirstWait = runs
            expect(afterFirstWait >= 1):ToBe(true)

            connection:Disconnect()
            task.wait()
            task.wait()
            expect(runs):ToBe(afterFirstWait)
        end)
    end)

    describe("pause", function()
        -- Every case here pauses and resumes without yielding in between: a
        -- paused world stops the sim clock, and a `task.wait` in that window
        -- would never resume.
        it("reports a running world as not paused", function()
            expect(runService:IsPaused()):ToBe(false)
        end)

        it("reports the world as paused between Pause and Resume", function()
            runService:Pause()
            expect(runService:IsPaused()):ToBe(true)
            runService:Resume()
            expect(runService:IsPaused()):ToBe(false)
        end)

        it("treats a second Pause as a no-op", function()
            runService:Pause()
            runService:Pause()
            expect(runService:IsPaused()):ToBe(true)
            runService:Resume()
            expect(runService:IsPaused()):ToBe(false)
        end)

        it("treats Resume on a running world as a no-op", function()
            runService:Resume()
            expect(runService:IsPaused()):ToBe(false)
        end)

        it("returns a boolean from IsPaused", function()
            expect(runService:IsPaused()):ToBeA("boolean")
        end)
    end)
end)
