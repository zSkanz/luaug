--!strict
--- `Clone` from api-design.md §2.2 and §2.6 ("any instance tree can be kept as
--- a template ... and deep-cloned on demand, with internal references fixed
--- up") -- the prefab workhorse of architecture.md §4.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

local function namedFolder(name: string, parent: Instance?): Folder
    local folder = Instance.new("Folder")
    folder.Name = name
    folder.Parent = parent
    return folder
end

local function namedPart(name: string, parent: Instance?): Part
    local part = Instance.new("Part")
    part.Name = name
    part.Parent = parent
    return part
end

local function names(instances: { Instance }): { string }
    local result: { string } = {}
    for _, instance in instances do
        table.insert(result, instance.Name)
    end
    return result
end

describe("Instance Clone", function()
    local root: Folder = nil :: any

    beforeEach(function()
        root = Instance.new("Folder")
        root.Name = "CloneSpecRoot"
        root.Parent = workspace
    end)

    afterEach(function()
        root:Destroy()
    end)

    describe("the copy itself", function()
        it("returns a new instance, not the original", function()
            local source = namedFolder("Source", root)
            local clone = source:Clone()
            expect(clone).Never:ToBe(source)
        end)

        it("returns a clone with no Parent", function()
            local source = namedFolder("Source", root)
            expect(source:Clone().Parent):ToBeNil()
        end)

        it("leaves the source parented where it was", function()
            local source = namedFolder("Source", root)
            source:Clone()
            expect(source.Parent):ToBe(root)
            expect(#root:GetChildren()):ToBe(1)
        end)

        it("copies ClassName and Name", function()
            local source = namedPart("Crate", root)
            local clone = source:Clone()
            expect(clone.ClassName):ToBe("Part")
            expect(clone.Name):ToBe("Crate")
        end)

        it("copies structural property values", function()
            local source = namedPart("Crate", root)
            source.Size = Vector3.new(2, 4, 6)
            source.Transparency = 0.5
            source.Color = Color3.new(1, 0, 0)

            -- `Clone` is declared as returning `Instance`, because an
            -- `extern type` cannot carry a per-instance generic at this pin.
            -- The cast is what a caller writes too, so the spec writes it.
            local clone = source:Clone() :: Part

            expect(clone.Size.x):ToBeCloseTo(2)
            expect(clone.Size.y):ToBeCloseTo(4)
            expect(clone.Size.z):ToBeCloseTo(6)
            expect(clone.Transparency):ToBeCloseTo(0.5)
            expect(clone.Color.R):ToBeCloseTo(1)
            expect(clone.Color.G):ToBeCloseTo(0)
            expect(clone.Color.B):ToBeCloseTo(0)
        end)

        it("clones an unparented template", function()
            -- §2.6: a template parented to nil is the v1 prefab pattern.
            local template = namedFolder("Template", nil)
            namedPart("Body", template)

            local clone = template:Clone()

            expect(clone.Parent):ToBeNil()
            expect(names(clone:GetChildren())):ToEqual({ "Body" })
        end)

        it("clones a clone", function()
            local source = namedFolder("Source", root)
            namedPart("Body", source)

            local second = source:Clone():Clone()

            expect(second.Name):ToBe("Source")
            expect(names(second:GetChildren())):ToEqual({ "Body" })
        end)
    end)

    describe("the copied subtree", function()
        it("copies children", function()
            local source = namedFolder("Source", root)
            namedPart("Body", source)

            local clone = source:Clone()

            expect(#clone:GetChildren()):ToBe(1)
            expect(clone:FindFirstChild("Body")).Never:ToBe(source:FindFirstChild("Body"))
        end)

        it("copies grandchildren", function()
            local source = namedFolder("Source", root)
            local branch = namedFolder("Branch", source)
            namedFolder("Leaf", branch)

            local clone = source:Clone()

            expect(#clone:GetDescendants()):ToBe(2)
            local clonedBranch = clone:FindFirstChild("Branch")
            expect(clonedBranch).Never:ToBeNil()
            expect((clonedBranch :: Instance):FindFirstChild("Leaf")).Never:ToBeNil()
        end)

        it("preserves child order in the copy", function()
            local source = namedFolder("Source", root)
            namedFolder("First", source)
            namedFolder("Second", source)
            namedFolder("Third", source)

            expect(names(source:Clone():GetChildren())):ToEqual({ "First", "Second", "Third" })
        end)

        it("preserves duplicate sibling names in the copy", function()
            local source = namedFolder("Source", root)
            namedFolder("Tree", source)
            namedFolder("Tree", source)

            local clone = source:Clone()

            expect(names(clone:GetChildren())):ToEqual({ "Tree", "Tree" })
            expect(clone:FindFirstChild("Tree")):ToBe(clone:GetChildren()[1])
        end)

        it("leaves the clone independent of the source", function()
            local source = namedFolder("Source", root)
            local body = namedPart("Body", source)

            local clone = source:Clone()
            clone.Parent = root
            clone.Name = "Copy"
            body.Name = "Renamed"
            namedPart("Extra", source)

            expect(source.Name):ToBe("Source")
            expect(names(clone:GetChildren())):ToEqual({ "Body" })
            expect(names(source:GetChildren())):ToEqual({ "Renamed", "Extra" })
        end)
    end)

    describe("references", function()
        it("rewires an internal reference to the cloned instance", function()
            local model = Instance.new("Model")
            model.Name = "Rig"
            model.Parent = root
            local body = namedPart("Body", model)
            model.PrimaryPart = body

            local clone = model:Clone() :: Model

            expect(clone.PrimaryPart).Never:ToBe(body)
            expect(clone.PrimaryPart):ToBe(clone:FindFirstChild("Body"))
        end)

        it("rewires an internal reference from deep inside the subtree", function()
            local outer = Instance.new("Model")
            outer.Name = "Outer"
            outer.Parent = root
            local inner = Instance.new("Model")
            inner.Name = "Inner"
            inner.Parent = outer
            local body = namedPart("Body", inner)
            inner.PrimaryPart = body

            local clone = outer:Clone()
            local clonedInner = clone:FindFirstChild("Inner") :: Model

            expect(clonedInner.PrimaryPart):ToBe(clonedInner:FindFirstChild("Body"))
            expect(clonedInner.PrimaryPart).Never:ToBe(body)
        end)

        it("keeps a reference to an instance outside the subtree pointing at the original", function()
            local outsider = namedPart("Outsider", root)
            local model = Instance.new("Model")
            model.Name = "Rig"
            model.Parent = root
            model.PrimaryPart = outsider

            local clone = model:Clone() :: Model

            expect(clone.PrimaryPart):ToBe(outsider)
        end)
    end)

    describe("attributes and tags", function()
        it("copies attributes", function()
            local source = namedFolder("Source", root)
            source:SetAttribute("Health", 100)
            source:SetAttribute("Title", "Hero")

            local clone = source:Clone()

            expect(clone:GetAttribute("Health")):ToBe(100)
            expect(clone:GetAttribute("Title")):ToBe("Hero")
        end)

        it("copies the whole attribute set", function()
            local source = namedFolder("Source", root)
            source:SetAttribute("Health", 100)
            source:SetAttribute("Ready", true)

            expect(source:Clone():GetAttributes()):ToEqual(source:GetAttributes())
        end)

        it("copies descendants' attributes", function()
            local source = namedFolder("Source", root)
            local body = namedPart("Body", source)
            body:SetAttribute("Health", 100)

            local cloned = source:Clone():FindFirstChild("Body") :: Instance

            expect(cloned:GetAttribute("Health")):ToBe(100)
        end)

        it("leaves the clone's attributes independent of the source's", function()
            local source = namedFolder("Source", root)
            source:SetAttribute("Health", 100)

            local clone = source:Clone()
            clone:SetAttribute("Health", 5)

            expect(source:GetAttribute("Health")):ToBe(100)
            expect(clone:GetAttribute("Health")):ToBe(5)
        end)

        it("copies tags", function()
            local source = namedFolder("Source", root)
            source:AddTag("Climbable")
            source:AddTag("Spawn")

            local clone = source:Clone()

            expect(clone:HasTag("Climbable")):ToBeTruthy()
            expect(clone:HasTag("Spawn")):ToBeTruthy()
            expect(clone:GetTags()):ToEqual(source:GetTags())
        end)

        it("copies descendants' tags", function()
            local source = namedFolder("Source", root)
            local body = namedPart("Body", source)
            body:AddTag("Climbable")

            local cloned = source:Clone():FindFirstChild("Body") :: Instance

            expect(cloned:HasTag("Climbable")):ToBeTruthy()
        end)

        it("leaves the clone's tags independent of the source's", function()
            local source = namedFolder("Source", root)
            source:AddTag("Climbable")

            local clone = source:Clone()
            clone:RemoveTag("Climbable")

            expect(source:HasTag("Climbable")):ToBeTruthy()
            expect(clone:HasTag("Climbable")):ToBeFalsy()
        end)
    end)
end)
