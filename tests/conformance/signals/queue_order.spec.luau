--!strict
--- Conformance: one queue, one order.
--- Written from api-design.md §3.1 ("One queue" and "A drain runs to
--- fixpoint"): engine-raised fires, script-raised fires and `task.defer`
--- callbacks all share a single queue, entered in the order they were raised.

local testing = require("@luaug/testing")

local describe = testing.describe
local it = testing.it
local expect = testing.expect
local beforeEach = testing.beforeEach
local afterEach = testing.afterEach

describe("the deferred queue", function()
    local root: Folder

    beforeEach(function()
        root = Instance.new("Folder")
        root.Name = "QueueOrderSpec"
        root.Parent = workspace
    end)

    afterEach(function()
        root:Destroy()
    end)

    -- The sentence §3.1 exists to make testable: "This is what makes the
    -- relative order of a script's own `:Fire()` and the `ChildAdded` caused by
    -- its `part.Parent = x` well defined."
    it("delivers a script fire before the ChildAdded raised after it", function()
        local sig = Signal.new()
        local log: { string } = {}
        sig:Connect(function()
            table.insert(log, "fire")
        end)
        root.ChildAdded:Connect(function()
            table.insert(log, "childAdded")
        end)

        sig:Fire()
        local child = Instance.new("Folder")
        child.Parent = root

        task.wait()
        expect(log):ToEqual({ "fire", "childAdded" })
    end)

    it("delivers a ChildAdded before a script fire raised after it", function()
        local sig = Signal.new()
        local log: { string } = {}
        sig:Connect(function()
            table.insert(log, "fire")
        end)
        root.ChildAdded:Connect(function()
            table.insert(log, "childAdded")
        end)

        local child = Instance.new("Folder")
        child.Parent = root
        sig:Fire()

        task.wait()
        expect(log):ToEqual({ "childAdded", "fire" })
    end)

    it("orders task.defer callbacks against signal fires by when each was raised", function()
        local sig = Signal.new()
        local log: { string } = {}
        sig:Connect(function()
            table.insert(log, "fire")
        end)

        task.defer(function()
            table.insert(log, "deferA")
        end)
        sig:Fire()
        task.defer(function()
            table.insert(log, "deferB")
        end)

        task.wait()
        expect(log):ToEqual({ "deferA", "fire", "deferB" })
    end)

    it("delivers two fires of one signal in fire order", function()
        local sig = Signal.new()
        local log: { number } = {}
        sig:Connect(function(value: number)
            table.insert(log, value)
        end)

        sig:Fire(1)
        sig:Fire(2)
        sig:Fire(3)

        task.wait()
        expect(log):ToEqual({ 1, 2, 3 })
    end)

    it("delivers fires of different signals in raise order", function()
        local first = Signal.new()
        local second = Signal.new()
        local log: { string } = {}
        first:Connect(function()
            table.insert(log, "first")
        end)
        second:Connect(function()
            table.insert(log, "second")
        end)

        second:Fire()
        first:Fire()
        second:Fire()

        task.wait()
        expect(log):ToEqual({ "second", "first", "second" })
    end)

    it("orders an attribute change and a property change by when each was written", function()
        local log: { string } = {}
        root.AttributeChanged:Connect(function()
            table.insert(log, "attribute")
        end)
        root:GetPropertyChangedSignal("Name"):Connect(function()
            table.insert(log, "name")
        end)

        root:SetAttribute("Phase", 1)
        root.Name = "QueueOrderSpecRenamed"

        task.wait()
        expect(log):ToEqual({ "attribute", "name" })
    end)

    -- "A drain runs to fixpoint": the fire a handler raises appends to the same
    -- queue, so it lands *after* everything already queued rather than nesting.
    it("appends a handler-raised fire after the entries already queued", function()
        local first = Signal.new()
        local second = Signal.new()
        local nested = Signal.new()
        local log: { string } = {}

        first:Connect(function()
            table.insert(log, "first")
            nested:Fire()
        end)
        second:Connect(function()
            table.insert(log, "second")
        end)
        nested:Connect(function()
            table.insert(log, "nested")
        end)

        first:Fire()
        second:Fire()

        task.wait()
        expect(log):ToEqual({ "first", "second", "nested" })
    end)

    it("runs a task.defer raised by a handler in the same drain, after the queued entries", function()
        local first = Signal.new()
        local second = Signal.new()
        local log: { string } = {}

        first:Connect(function()
            table.insert(log, "first")
            task.defer(function()
                table.insert(log, "deferred")
            end)
        end)
        second:Connect(function()
            table.insert(log, "second")
        end)

        first:Fire()
        second:Fire()

        task.wait()
        expect(log):ToEqual({ "first", "second", "deferred" })
    end)

    -- A chain this long cannot be delivered one generation per drain: a single
    -- `task.wait()` spans only the handful of resumption points between two
    -- task-resume phases (architecture §3), so observing all nine links is the
    -- observable form of "the drain continues until the queue is empty".
    it("completes a nine-deep chain of handler-raised fires within one wait", function()
        local signals: { Signal<()> } = {}
        local log: { number } = {}
        for index = 1, 9 do
            signals[index] = Signal.new()
        end
        for index = 1, 9 do
            signals[index]:Connect(function()
                table.insert(log, index)
                local following = signals[index + 1]
                if following then
                    following:Fire()
                end
            end)
        end

        signals[1]:Fire()

        task.wait()
        expect(log):ToEqual({ 1, 2, 3, 4, 5, 6, 7, 8, 9 })
    end)

    it("delivers ChildAdded and DescendantAdded from one reparent in the same drain", function()
        local log: { string } = {}
        root.ChildAdded:Connect(function()
            table.insert(log, "childAdded")
        end)
        root.DescendantAdded:Connect(function()
            table.insert(log, "descendantAdded")
        end)

        local child = Instance.new("Folder")
        child.Parent = root

        task.wait()
        expect(#log):ToBe(2)
        expect(log):ToContain("childAdded")
        expect(log):ToContain("descendantAdded")
    end)

    it("delivers ChildAdded before DescendantAdded for the same parent", function()
        local log: { string } = {}
        root.ChildAdded:Connect(function()
            table.insert(log, "childAdded")
        end)
        root.DescendantAdded:Connect(function()
            table.insert(log, "descendantAdded")
        end)

        local child = Instance.new("Folder")
        child.Parent = root

        task.wait()
        expect(log):ToEqual({ "childAdded", "descendantAdded" })
    end)

    it("delivers ChildRemoved before ChildAdded when a child is reparented", function()
        local origin = Instance.new("Folder")
        origin.Name = "Origin"
        origin.Parent = root
        local destination = Instance.new("Folder")
        destination.Name = "Destination"
        destination.Parent = root

        local child = Instance.new("Folder")
        child.Name = "Traveller"
        child.Parent = origin
        task.wait()

        local log: { string } = {}
        origin.ChildRemoved:Connect(function()
            table.insert(log, "removed")
        end)
        destination.ChildAdded:Connect(function()
            table.insert(log, "added")
        end)

        child.Parent = destination

        task.wait()
        expect(log):ToEqual({ "removed", "added" })
    end)
end)
