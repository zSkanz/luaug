--!strict
--- Conformance: `task.defer` (api-design.md §3.2) against the single deferred
--- queue of §3.1.
---
--- `task.defer` enqueues on the *same* queue as engine-raised signal fires and
--- script `Signal:Fire`, "ordered against signal fires by when it was raised".
--- The cases that matter therefore interleave a defer with a fire and assert the
--- resulting order; a spec that only checks "it eventually ran" would pass on an
--- engine with three private queues and no ordering at all.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

local connections: { Connection } = {}
local sandbox: Folder? = nil

local function track(connection: Connection): Connection
    table.insert(connections, connection)
    return connection
end

local function sandboxFolder(): Folder
    return assert(sandbox, "the sandbox folder is created in beforeEach")
end

describe("task.defer", function()
    beforeEach(function()
        local folder = Instance.new("Folder")
        folder.Name = "TaskDeferSpec"
        folder.Parent = workspace
        sandbox = folder
    end)

    afterEach(function()
        for _, connection in connections do
            connection:Disconnect()
        end
        table.clear(connections)
        local folder = sandbox
        sandbox = nil
        if folder then
            folder:Destroy()
        end
    end)

    it("does not run the callback before the call returns", function()
        local ran = false
        task.defer(function()
            ran = true
        end)
        expect(ran):ToBe(false)
    end)

    it("runs the callback at the next resumption point", function()
        local ran = false
        task.defer(function()
            ran = true
        end)
        task.wait()
        expect(ran):ToBe(true)
    end)

    it("returns the thread it will resume", function()
        local inner: thread? = nil
        local returned = task.defer(function()
            inner = coroutine.running()
        end)
        expect(returned):ToBeA("thread")
        task.wait()
        expect(returned):ToBe(inner)
    end)

    it("passes its extra arguments to the callback", function()
        local received: { any } = {}
        task.defer(function(first: number, second: string, third: boolean)
            received = { first, second, third }
        end, 1, "two", true)
        task.wait()
        -- Annotated: an array literal infers its element type from the first
        -- entry, and this one is deliberately mixed.
        expect(received):ToEqual({ 1, "two", true } :: { any })
    end)

    it("runs callbacks in the order they were deferred", function()
        local order: { string } = {}
        for index = 1, 5 do
            task.defer(function()
                table.insert(order, tostring(index))
            end)
        end
        task.wait()
        expect(order):ToEqual({ "1", "2", "3", "4", "5" })
    end)

    -- §3.1's whole point: a script's own `:Fire()` and its deferred work share one
    -- queue, so their relative order is a fact rather than an implementation detail.
    it("orders a deferred callback against a script signal fire raised between two defers", function()
        local order: { string } = {}
        local signal = Signal.new()
        track(signal:Connect(function()
            table.insert(order, "fire")
        end))
        task.defer(function()
            table.insert(order, "before")
        end)
        signal:Fire()
        task.defer(function()
            table.insert(order, "after")
        end)
        task.wait()
        expect(order):ToEqual({ "before", "fire", "after" })
        signal:Destroy()
    end)

    -- The document's own worked example: "the relative order of a script's own
    -- `:Fire()` and the ChildAdded caused by its `part.Parent = x`".
    it("orders a deferred callback against the ChildAdded raised by a reparent", function()
        local order: { string } = {}
        local folder = sandboxFolder()
        track(folder.ChildAdded:Connect(function()
            table.insert(order, "childAdded")
        end))
        task.defer(function()
            table.insert(order, "before")
        end)
        local child = Instance.new("Folder")
        child.Parent = folder
        task.defer(function()
            table.insert(order, "after")
        end)
        task.wait()
        expect(order):ToEqual({ "before", "childAdded", "after" })
    end)

    it("appends a callback deferred by a handler after the rest of that fire's handlers", function()
        local order: { string } = {}
        local signal = Signal.new()
        track(signal:Connect(function()
            table.insert(order, "first-handler")
            task.defer(function()
                table.insert(order, "deferred")
            end)
        end))
        track(signal:Connect(function()
            table.insert(order, "second-handler")
        end))
        signal:Fire()
        task.wait()
        expect(order):ToEqual({ "first-handler", "second-handler", "deferred" })
        signal:Destroy()
    end)

    -- Ruling R-B: the re-entrancy cap covers `task.defer` too. This case was
    -- written reading §3.1's depth rule as applying to fires only, and asserted
    -- that a twelve-generation chain completes. Under that reading a
    -- self-deferring callback would be an unbounded drain -- a hang, not a wrong
    -- number -- so the cap counts everything on the queue and the chain stops at
    -- eleven generations, exactly as a self-refiring signal does.
    it("caps a self-deferring chain at eleven generations, like a fire", function()
        local generations = 0
        local function deferAgain()
            generations += 1
            if generations < 12 then
                task.defer(deferAgain)
            end
        end
        task.defer(deferAgain)
        task.wait()
        expect(generations):ToBe(11)
    end)

    it("runs a callback deferred by a thread that has already finished", function()
        local ran = false
        task.spawn(function()
            task.defer(function()
                ran = true
            end)
        end)
        expect(ran):ToBe(false)
        task.wait()
        expect(ran):ToBe(true)
    end)

    it("runs each callback on its own coroutine, so one callback yielding does not hold up the next", function()
        local order: { string } = {}
        task.defer(function()
            table.insert(order, "first-start")
            task.wait()
            table.insert(order, "first-resumed")
        end)
        task.defer(function()
            table.insert(order, "second")
        end)
        task.wait()
        expect(order):ToEqual({ "first-start", "second" })
        task.wait()
        task.wait()
        expect(order):ToEqual({ "first-start", "second", "first-resumed" })
    end)

    it("runs the callback on a coroutine of its own, not the deferring thread's", function()
        local caller = coroutine.running()
        local inner: thread? = nil
        task.defer(function()
            inner = coroutine.running()
        end)
        task.wait()
        expect(inner):ToBeA("thread")
        expect(inner).Never:ToBe(caller)
    end)

    it("keeps draining after a deferred callback errors", function()
        local ran = false
        task.defer(function()
            error("deferred failure")
        end)
        task.defer(function()
            ran = true
        end)
        task.wait()
        expect(ran):ToBe(true)
    end)

    it("does not let an erroring deferred callback stop the thread that deferred it", function()
        task.defer(function()
            error("deferred failure")
        end)
        local elapsed = task.wait()
        expect(elapsed):ToBeA("number")
    end)
end)
