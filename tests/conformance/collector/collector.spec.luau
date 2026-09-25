--!strict
--- Conformance: Collector (ADR 0094), on Janitor's surface. What a thing is
--- decides how it is cleaned, and `Cleanup` runs newest first (R10).

local testing = require("@luaug/testing")

local describe = testing.describe
local it = testing.it
local expect = testing.expect

describe("Collector", function()
    it("cleans each kind the way it is ended", function()
        local collector = Collector.new()

        local called = false
        collector:Add(function()
            called = true
        end)

        local signal = Signal.new()
        local connection = signal:Connect(function() end)
        collector:Add(connection)

        local part = Instance.new("Part")
        part.Parent = workspace
        collector:Add(part)

        local ownSignal = Signal.new()
        collector:Add(ownSignal)

        local destroyed = false
        collector:Add({
            Destroy = function()
                destroyed = true
            end,
        })

        local custom = false
        collector:Add({
            Close = function()
                custom = true
            end,
        }, "Close")

        local pending = Promise.new(function() end)
        collector:Add(pending)

        local timerRan = false
        collector:Add(task.delay(0.05, function()
            timerRan = true
        end))

        collector:Cleanup()
        expect(called):ToBe(true)
        expect(connection.Connected):ToBe(false)
        expect(part.Parent):ToBeNil()
        expect(destroyed):ToBe(true)
        expect(custom):ToBe(true)
        expect(pending:GetStatus()):ToBe(Enum.PromiseState.Cancelled)
        task.wait(0.1)
        expect(timerRan):ToBe(false)
    end)

    it("calls a Destroy found through a class's metatable, as Janitor does", function()
        local Class = {}
        Class.__index = Class
        local destroyed = 0
        function Class.new()
            return setmetatable({}, Class)
        end
        function Class:Destroy()
            destroyed += 1
        end

        local collector = Collector.new()
        collector:Add(Class.new())
        collector:Add(Class.new())
        collector:Cleanup()
        expect(destroyed):ToBe(2)
    end)

    it("refuses a table whose Destroy is not a function, and says what to do", function()
        local collector = Collector.new()
        collector:Add({ Destroy = true })
        expect(function()
            collector:Cleanup()
        end):ToThrow("give the method to call")
    end)

    it("cleans newest first, and stays usable after Cleanup", function()
        local collector = Collector.new()
        local order: { string } = {}
        for _, name in { "first", "second", "third" } do
            collector:Add(function()
                table.insert(order, name)
            end)
        end
        collector:Cleanup()
        expect(order[1]):ToBe("third")
        expect(order[2]):ToBe("second")
        expect(order[3]):ToBe("first")

        collector:Add(function()
            table.insert(order, "again")
        end)
        collector:Cleanup()
        expect(order[4]):ToBe("again")
    end)

    it("keys: Get, Remove, RemoveNoClean, and a key reused cleans what it held", function()
        local collector = Collector.new()
        local cleaned: { string } = {}
        local object = collector:Add(function()
            table.insert(cleaned, "one")
        end, nil, "slot")
        expect(collector:Get("slot")):ToBe(object)

        collector:Add(function()
            table.insert(cleaned, "two")
        end, nil, "slot")
        expect(cleaned[1]):ToBe("one")

        collector:Remove("slot")
        expect(cleaned[2]):ToBe("two")
        expect(collector:Get("slot")):ToBeNil()

        collector:Add(function()
            table.insert(cleaned, "kept")
        end, nil, "other")
        collector:RemoveNoClean("other")
        collector:Cleanup()
        expect(#cleaned):ToBe(2)
    end)

    it("refuses to be used after Destroy", function()
        local collector = Collector.new()
        collector:Destroy()
        expect(function()
            collector:Add(function() end)
        end):ToThrow("destroyed")
    end)

    it("LinkToInstance cleans when the instance is destroyed", function()
        local collector = Collector.new()
        local cleaned = false
        collector:Add(function()
            cleaned = true
        end)
        local holder = Instance.new("Folder")
        holder.Parent = workspace
        collector:LinkToInstance(holder)
        holder:Destroy()
        task.wait()
        task.wait()
        expect(cleaned):ToBe(true)
    end)

    it("AddPromise cancels on cleanup, and lets go of one that settled", function()
        local collector = Collector.new()
        local pending = collector:AddPromise(Promise.new(function() end))
        local settled = collector:AddPromise(Promise.resolve("done"))
        task.wait()
        collector:Cleanup()
        expect(pending:GetStatus()):ToBe(Enum.PromiseState.Cancelled)
        expect(settled:GetStatus()):ToBe(Enum.PromiseState.Resolved)
    end)
end)
