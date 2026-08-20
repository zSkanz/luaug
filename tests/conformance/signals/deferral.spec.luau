--!strict
--- Conformance: nothing fires synchronously.
--- Written from api-design.md §3.1 ("Resumption points": the queue drains at
--- the resumption points of the frame pipeline, never at the moment of the
--- fire) and §3.2 (`task.spawn` as the one documented exception).

local testing = require("@luaug/testing")

local describe = testing.describe
local it = testing.it
local expect = testing.expect
local beforeEach = testing.beforeEach
local afterEach = testing.afterEach

describe("deferral", function()
    local root: Folder

    beforeEach(function()
        root = Instance.new("Folder")
        root.Name = "DeferralSpec"
        root.Parent = workspace
    end)

    afterEach(function()
        root:Destroy()
    end)

    it("does not invoke a handler during the Fire call", function()
        local sig = Signal.new()
        local ran = false
        sig:Connect(function()
            ran = true
        end)

        sig:Fire()

        expect(ran):ToBe(false)
    end)

    it("invokes the handler by the next resumption point", function()
        local sig = Signal.new()
        local ran = false
        sig:Connect(function()
            ran = true
        end)

        sig:Fire()
        task.wait()

        expect(ran):ToBe(true)
    end)

    it("does not yield the coroutine that fires", function()
        local sig = Signal.new()
        local order: { string } = {}
        sig:Connect(function()
            table.insert(order, "handler")
        end)

        sig:Fire()
        table.insert(order, "afterFire")

        task.wait()
        expect(order):ToEqual({ "afterFire", "handler" })
    end)

    it("invokes the handler once per fire", function()
        local sig = Signal.new()
        local count = 0
        sig:Connect(function()
            count += 1
        end)

        sig:Fire()
        sig:Fire()
        sig:Fire()

        expect(count):ToBe(0)
        task.wait()
        expect(count):ToBe(3)
    end)

    it("does not invoke a handler for a signal that was never fired", function()
        local sig = Signal.new()
        local count = 0
        sig:Connect(function()
            count += 1
        end)

        task.wait()
        expect(count):ToBe(0)
    end)

    it("does not invoke a handler again at a later resumption point", function()
        local sig = Signal.new()
        local count = 0
        sig:Connect(function()
            count += 1
        end)

        sig:Fire()
        task.wait()
        expect(count):ToBe(1)
        task.wait()
        expect(count):ToBe(1)
    end)

    it("runs a task.defer callback only at the next resumption point", function()
        local ran = false
        task.defer(function()
            ran = true
        end)

        expect(ran):ToBe(false)
        task.wait()
        expect(ran):ToBe(true)
    end)

    -- §3.2 calls `task.spawn` "the one non-deferred call, and deliberately so";
    -- the contrast is what makes deferral a property of the queue rather than
    -- of the runtime as a whole.
    it("runs a task.spawn callback immediately, before spawn returns", function()
        local ran = false
        task.spawn(function()
            ran = true
        end)

        expect(ran):ToBe(true)
    end)

    it("does not raise ChildAdded during the Parent assignment", function()
        local ran = false
        root.ChildAdded:Connect(function()
            ran = true
        end)

        local child = Instance.new("Folder")
        child.Parent = root

        expect(ran):ToBe(false)
    end)

    it("raises ChildAdded by the next resumption point", function()
        local seen: Instance? = nil
        root.ChildAdded:Connect(function(child: Instance)
            seen = child
        end)

        local child = Instance.new("Folder")
        child.Parent = root

        task.wait()
        expect(seen):ToBe(child)
    end)

    it("does not raise a property change during the property write", function()
        local ran = false
        root:GetPropertyChangedSignal("Name"):Connect(function()
            ran = true
        end)

        root.Name = "RenamedDuringSpec"

        expect(ran):ToBe(false)
        task.wait()
        expect(ran):ToBe(true)
    end)

    it("does not raise an attribute change during SetAttribute", function()
        local ran = false
        root.AttributeChanged:Connect(function()
            ran = true
        end)

        root:SetAttribute("Level", 3)

        expect(ran):ToBe(false)
        task.wait()
        expect(ran):ToBe(true)
    end)
end)
