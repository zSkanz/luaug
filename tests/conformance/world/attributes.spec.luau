--!strict
--- Conformance: instance attributes -- storage (docs/api-design.md 2.2, 2.6).
---
--- Section 3 tells script authors to keep per-script configuration in
--- attributes and 2.6 lists them among the things a prefab declares, so what
--- an attribute may hold, what removal means, and what a Clone carries are
--- public contract rather than storage detail.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

local function countKeys(map: { [string]: any }): number
    local total = 0
    for _ in map do
        total += 1
    end
    return total
end

describe("Instance attributes", function()
    local scratch: Folder
    local subject: Folder

    beforeEach(function()
        scratch = Instance.new("Folder")
        scratch.Name = "ConformanceAttributes"
        scratch.Parent = workspace

        subject = Instance.new("Folder")
        subject.Name = "Subject"
        subject.Parent = scratch
    end)

    -- Every case shares one world, so the tree this file builds has to leave
    -- with it.
    afterEach(function()
        scratch:Destroy()
    end)

    describe("GetAttribute and SetAttribute", function()
        it("returns nil for an attribute that was never set", function()
            expect(subject:GetAttribute("Health")):ToBeNil()
        end)

        it("round-trips a string value", function()
            subject:SetAttribute("Faction", "blue")
            expect(subject:GetAttribute("Faction")):ToBe("blue")
        end)

        it("round-trips a number value", function()
            subject:SetAttribute("Health", 42.5)
            expect(subject:GetAttribute("Health")):ToBe(42.5)
        end)

        it("round-trips a boolean value", function()
            subject:SetAttribute("Hostile", true)
            expect(subject:GetAttribute("Hostile")):ToBe(true)
            subject:SetAttribute("Hostile", false)
            expect(subject:GetAttribute("Hostile")):ToBe(false)
        end)

        it("round-trips a vector value", function()
            subject:SetAttribute("SpawnOffset", Vector3.new(1, 2, 3))
            expect(subject:GetAttribute("SpawnOffset")):ToBe(Vector3.new(1, 2, 3))
        end)

        it("round-trips a CFrame value", function()
            subject:SetAttribute("Anchor", CFrame.new(10, 20, 30))
            local stored = subject:GetAttribute("Anchor") :: CFrame
            expect(typeof(stored)):ToBe("CFrame")
            expect(stored.Position):ToBe(Vector3.new(10, 20, 30))
        end)

        it("round-trips a Color3 value", function()
            subject:SetAttribute("Tint", Color3.new(0.25, 0.5, 0.75))
            local stored = subject:GetAttribute("Tint") :: Color3
            expect(stored.R):ToBeCloseTo(0.25)
            expect(stored.G):ToBeCloseTo(0.5)
            expect(stored.B):ToBeCloseTo(0.75)
        end)

        -- `Vector2`, `UDim`, `UDim2` and `Rect` are in the attribute domain
        -- (§2.2) and are not in the M2 surface: each arrives with the milestone
        -- that gives it meaning (§2.3). Their round-trip cases arrive with them.

        it("overwrites the previous value of an attribute", function()
            subject:SetAttribute("Health", 1)
            subject:SetAttribute("Health", 2)
            expect(subject:GetAttribute("Health")):ToBe(2)
        end)

        it("replaces the value type of an existing attribute", function()
            subject:SetAttribute("Health", 1)
            subject:SetAttribute("Health", "full")
            expect(subject:GetAttribute("Health")):ToBe("full")
        end)

        it("treats attribute names as case-sensitive", function()
            subject:SetAttribute("Health", 1)
            expect(subject:GetAttribute("health")):ToBeNil()
            expect(subject:GetAttribute("HEALTH")):ToBeNil()
        end)

        it("keeps attributes of two instances independent", function()
            local other = Instance.new("Folder")
            other.Parent = scratch

            subject:SetAttribute("Health", 1)
            expect(other:GetAttribute("Health")):ToBeNil()

            other:SetAttribute("Health", 2)
            expect(subject:GetAttribute("Health")):ToBe(1)
        end)

        it("stores attributes on an instance whose Parent is nil", function()
            local orphan = Instance.new("Folder")
            orphan:SetAttribute("Health", 7)
            expect(orphan:GetAttribute("Health")):ToBe(7)
            orphan:Destroy()
        end)

        it("stores attributes on a service", function()
            local tagService = game:GetService("TagService")
            tagService:SetAttribute("ConformanceProbe", 1)
            expect(tagService:GetAttribute("ConformanceProbe")):ToBe(1)
            tagService:SetAttribute("ConformanceProbe", nil)
            expect(tagService:GetAttribute("ConformanceProbe")):ToBeNil()
        end)
    end)

    describe("removal", function()
        it("removes the attribute when it is set to nil", function()
            subject:SetAttribute("Health", 1)
            subject:SetAttribute("Health", nil)
            expect(subject:GetAttribute("Health")):ToBeNil()
        end)

        it("drops the key from GetAttributes when the attribute is removed", function()
            subject:SetAttribute("Health", 1)
            subject:SetAttribute("Faction", "blue")
            subject:SetAttribute("Health", nil)

            local attributes = subject:GetAttributes()
            expect(attributes.Health):ToBeNil()
            expect(attributes.Faction):ToBe("blue")
            expect(countKeys(attributes)):ToBe(1)
        end)

        it("treats removing an attribute that was never set as a no-op", function()
            subject:SetAttribute("NeverSet", nil)
            expect(subject:GetAttribute("NeverSet")):ToBeNil()
            expect(countKeys(subject:GetAttributes())):ToBe(0)
        end)

        it("allows an attribute to be set again after removal", function()
            subject:SetAttribute("Health", 1)
            subject:SetAttribute("Health", nil)
            subject:SetAttribute("Health", 3)
            expect(subject:GetAttribute("Health")):ToBe(3)
        end)
    end)

    describe("GetAttributes", function()
        it("returns an empty table when nothing is set", function()
            expect(countKeys(subject:GetAttributes())):ToBe(0)
        end)

        it("returns every attribute keyed by name", function()
            subject:SetAttribute("Health", 1)
            subject:SetAttribute("Faction", "blue")
            subject:SetAttribute("Hostile", true)

            local attributes = subject:GetAttributes()
            expect(attributes.Health):ToBe(1)
            expect(attributes.Faction):ToBe("blue")
            expect(attributes.Hostile):ToBe(true)
            expect(countKeys(attributes)):ToBe(3)
        end)

        -- Same guarantee 3.1 gives GetChildren: the caller owns the table it
        -- is handed, so mutating it cannot reach back into the instance.
        it("returns a fresh table on every call", function()
            subject:SetAttribute("Health", 1)

            local first = subject:GetAttributes()
            first.Health = 99
            first.Injected = true

            local second = subject:GetAttributes()
            expect(second.Health):ToBe(1)
            expect(second.Injected):ToBeNil()
            expect(subject:GetAttribute("Health")):ToBe(1)
        end)
    end)

    describe("value validation", function()
        it("rejects a table as an attribute value", function()
            local target = subject :: any
            expect(function()
                target:SetAttribute("Bad", { 1, 2, 3 })
            end):ToThrow()
        end)

        it("rejects an Instance as an attribute value", function()
            local target = subject :: any
            expect(function()
                target:SetAttribute("Bad", scratch)
            end):ToThrow()
        end)

        it("rejects a function as an attribute value", function()
            local target = subject :: any
            expect(function()
                target:SetAttribute("Bad", function() end)
            end):ToThrow()
        end)

        it("leaves the attribute unset after a rejected write", function()
            local target = subject :: any
            pcall(function()
                target:SetAttribute("Bad", { 1, 2, 3 })
            end)
            expect(subject:GetAttribute("Bad")):ToBeNil()
            expect(countKeys(subject:GetAttributes())):ToBe(0)
        end)
    end)

    describe("Clone", function()
        it("copies attributes onto the clone", function()
            subject:SetAttribute("Health", 1)
            subject:SetAttribute("Faction", "blue")

            local clone = subject:Clone()
            expect(clone:GetAttribute("Health")):ToBe(1)
            expect(clone:GetAttribute("Faction")):ToBe("blue")
            clone:Destroy()
        end)

        it("copies exactly the attributes the original carries", function()
            subject:SetAttribute("Health", 1)

            local clone = subject:Clone()
            expect(countKeys(clone:GetAttributes())):ToBe(1)
            clone:Destroy()
        end)

        it("keeps the clone's attributes independent of the original", function()
            subject:SetAttribute("Health", 1)

            local clone = subject:Clone()
            clone:SetAttribute("Health", 2)
            expect(subject:GetAttribute("Health")):ToBe(1)

            subject:SetAttribute("Health", 3)
            expect(clone:GetAttribute("Health")):ToBe(2)
            clone:Destroy()
        end)

        it("copies attributes of descendants onto the clone", function()
            local child = Instance.new("Folder")
            child.Name = "Child"
            child:SetAttribute("Health", 5)
            child.Parent = subject

            local clone = subject:Clone()
            local clonedChild = assert(clone:FindFirstChild("Child"), "the clone lost its child")
            expect(clonedChild:GetAttribute("Health")):ToBe(5)
            clone:Destroy()
        end)

        it("copies attributes onto a clone taken while the original has no parent", function()
            local orphan = Instance.new("Folder")
            orphan:SetAttribute("Health", 9)

            local clone = orphan:Clone()
            expect(clone:GetAttribute("Health")):ToBe(9)

            orphan:Destroy()
            clone:Destroy()
        end)
    end)
end)
