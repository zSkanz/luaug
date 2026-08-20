--!strict
--- Conformance: arguments are captured, not copied.
--- Written from api-design.md §3.1: "Enqueuing a fire records its arguments"
--- and "Tables and instances are passed by reference: mutating a table between
--- `:Fire(t)` and the drain is visible to the handlers."

local testing = require("@luaug/testing")

local describe = testing.describe
local it = testing.it
local expect = testing.expect
local beforeEach = testing.beforeEach
local afterEach = testing.afterEach

describe("fire arguments", function()
    local root: Folder

    beforeEach(function()
        root = Instance.new("Folder")
        root.Name = "ArgumentsSpec"
        root.Parent = workspace
    end)

    afterEach(function()
        root:Destroy()
    end)

    it("delivers the argument given to Fire", function()
        local sig = Signal.new()
        local seen: string? = nil
        sig:Connect(function(value: string)
            seen = value
        end)

        sig:Fire("hello")

        task.wait()
        expect(seen):ToBe("hello")
    end)

    it("delivers several arguments in order", function()
        local sig = Signal.new()
        local seen: any = nil
        sig:Connect(function(...)
            seen = table.pack(...)
        end)

        sig:Fire(1, "two", true)

        task.wait()
        expect(seen[1]):ToBe(1)
        expect(seen[2]):ToBe("two")
        expect(seen[3]):ToBe(true)
    end)

    it("delivers no arguments for a bare Fire", function()
        local sig = Signal.new()
        local count = -1
        sig:Connect(function(...)
            count = select("#", ...)
        end)

        sig:Fire()

        task.wait()
        expect(count):ToBe(0)
    end)

    it("preserves an interior nil in the argument list", function()
        local sig = Signal.new()
        local count = -1
        local third: any = false
        sig:Connect(function(...)
            count = select("#", ...)
            third = select(3, ...)
        end)

        sig:Fire(1, nil, 3)

        task.wait()
        expect(count):ToBe(3)
        expect(third):ToBe(3)
    end)

    it("records the value at fire time, not the variable", function()
        local sig = Signal.new()
        local seen: number? = nil
        sig:Connect(function(value: number)
            seen = value
        end)

        local sent = 1
        sig:Fire(sent)
        sent = 2

        task.wait()
        expect(seen):ToBe(1)
    end)

    -- The headline sentence of "Arguments are captured, not copied".
    it("makes a mutation between Fire and the drain visible to the handler", function()
        local sig = Signal.new()
        local seen: number? = nil
        sig:Connect(function(payload: { count: number })
            seen = payload.count
        end)

        local payload = { count = 1 }
        sig:Fire(payload)
        payload.count = 2

        task.wait()
        expect(seen):ToBe(2)
    end)

    it("delivers the same table to every handler of one fire", function()
        local sig = Signal.new()
        local first: any = nil
        local second: any = nil
        sig:Connect(function(payload: any)
            first = payload
        end)
        sig:Connect(function(payload: any)
            second = payload
        end)

        local payload = {}
        sig:Fire(payload)

        task.wait()
        expect(first):ToBe(payload)
        expect(second):ToBe(payload)
    end)

    it("makes a mutation by one handler visible to the next handler of the same fire", function()
        local sig = Signal.new()
        local seen: number? = nil
        sig:Connect(function(payload: { count: number })
            payload.count += 1
        end)
        sig:Connect(function(payload: { count: number })
            seen = payload.count
        end)

        sig:Fire({ count = 1 })

        task.wait()
        expect(seen):ToBe(2)
    end)

    it("delivers an instance argument as the same instance, not a copy", function()
        local sig = Signal.new()
        local seen: Instance? = nil
        sig:Connect(function(instance: Instance)
            seen = instance
        end)

        local child = Instance.new("Folder")
        child.Name = "Passed"
        child.Parent = root
        sig:Fire(child)

        task.wait()
        expect(seen):ToBe(child)
        expect((seen :: Instance).Name):ToBe("Passed")
    end)

    it("keeps the arguments of two fires independent", function()
        local sig = Signal.new()
        local seen: { string } = {}
        sig:Connect(function(value: string)
            table.insert(seen, value)
        end)

        sig:Fire("first")
        sig:Fire("second")

        task.wait()
        expect(seen):ToEqual({ "first", "second" })
    end)

    it("delivers the fire's arguments to a Once handler", function()
        local sig = Signal.new()
        local seen: string? = nil
        sig:Once(function(value: string)
            seen = value
        end)

        sig:Fire("once")

        task.wait()
        expect(seen):ToBe("once")
    end)

    it("delivers the added child to a ChildAdded handler", function()
        local seen: Instance? = nil
        local count = -1
        root.ChildAdded:Connect(function(...)
            count = select("#", ...)
            seen = select(1, ...)
        end)

        local child = Instance.new("Folder")
        child.Parent = root

        task.wait()
        expect(count):ToBe(1)
        expect(seen):ToBe(child)
    end)
end)
