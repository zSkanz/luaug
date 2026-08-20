--!strict
--- The lookup family from api-design.md §2.2: `FindFirstChild`,
--- `FindFirstChildOfClass`, `FindFirstChildWhichIsA`, `FindFirstAncestor`,
--- `FindFirstAncestorOfClass`.
---
--- Duplicate sibling names are fully supported and `FindFirstChild` returns the
--- first match in child (document) order -- api-design.md §2.2 and ADR 0026,
--- which names duplicate lookup, rename and reparent ordering as the cases this
--- suite must carry.

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

describe("Instance lookup", function()
    local root: Folder = nil :: any

    beforeEach(function()
        root = Instance.new("Folder")
        root.Name = "FindSpecRoot"
        root.Parent = workspace
    end)

    afterEach(function()
        root:Destroy()
    end)

    describe("FindFirstChild", function()
        it("returns the child with that name", function()
            local child = namedFolder("Target", root)
            expect(root:FindFirstChild("Target")):ToBe(child)
        end)

        it("returns nil when no child has that name", function()
            namedFolder("Other", root)
            expect(root:FindFirstChild("Target")):ToBeNil()
        end)

        it("returns nil for an instance with no children", function()
            local leaf = namedFolder("Leaf", root)
            expect(leaf:FindFirstChild("Anything")):ToBeNil()
        end)

        it("searches direct children only", function()
            local child = namedFolder("Child", root)
            namedFolder("Grandchild", child)
            expect(root:FindFirstChild("Grandchild")):ToBeNil()
            expect(child:FindFirstChild("Grandchild")).Never:ToBeNil()
        end)

        it("never returns the instance itself", function()
            local folder = namedFolder("Self", root)
            expect(folder:FindFirstChild("Self")):ToBeNil()
        end)

        it("matches names exactly and case-sensitively", function()
            namedFolder("Tree", root)
            expect(root:FindFirstChild("tree")):ToBeNil()
            expect(root:FindFirstChild("Tre")):ToBeNil()
            expect(root:FindFirstChild("Tree ")):ToBeNil()
        end)

        it("finds a child that was renamed after it was parented", function()
            local child = namedFolder("Before", root)
            child.Name = "After"
            expect(root:FindFirstChild("Before")):ToBeNil()
            expect(root:FindFirstChild("After")):ToBe(child)
        end)

        it("finds a child that was named before it was parented", function()
            local child = Instance.new("Folder")
            child.Name = "Named"
            child.Parent = root
            expect(root:FindFirstChild("Named")):ToBe(child)
        end)

        it("stops finding a child that has been detached", function()
            local child = namedFolder("Gone", root)
            child.Parent = nil
            expect(root:FindFirstChild("Gone")):ToBeNil()
        end)
    end)

    describe("FindFirstChild with duplicate sibling names", function()
        -- ADR 0026: three siblings named "Tree" is the case the child-name index
        -- exists to serve, and the first in child order must win every time.
        local first: Folder = nil :: any
        local second: Folder = nil :: any
        local third: Folder = nil :: any

        beforeEach(function()
            first = namedFolder("Tree", root)
            second = namedFolder("Tree", root)
            third = namedFolder("Tree", root)
        end)

        it("keeps all three siblings in the tree", function()
            expect(#root:GetChildren()):ToBe(3)
            expect(first).Never:ToBe(second)
            expect(second).Never:ToBe(third)
        end)

        it("returns the first of three siblings sharing a name", function()
            expect(root:FindFirstChild("Tree")):ToBe(first)
        end)

        it("returns the second once the first is renamed away", function()
            first.Name = "Stump"
            expect(root:FindFirstChild("Tree")):ToBe(second)
            expect(root:FindFirstChild("Stump")):ToBe(first)
        end)

        it("returns the third once the first two are renamed away", function()
            first.Name = "Stump"
            second.Name = "Log"
            expect(root:FindFirstChild("Tree")):ToBe(third)
        end)

        it("returns nil once every duplicate is renamed away", function()
            first.Name = "A"
            second.Name = "B"
            third.Name = "C"
            expect(root:FindFirstChild("Tree")):ToBeNil()
        end)

        it("returns the first again when it is renamed back", function()
            first.Name = "Stump"
            expect(root:FindFirstChild("Tree")):ToBe(second)
            first.Name = "Tree"
            expect(root:FindFirstChild("Tree")):ToBe(first)
        end)

        it("is unaffected by renaming a later duplicate", function()
            third.Name = "Stump"
            expect(root:FindFirstChild("Tree")):ToBe(first)
            second.Name = "Log"
            expect(root:FindFirstChild("Tree")):ToBe(first)
        end)

        it("is unaffected by a sibling renamed into the duplicate set", function()
            local newcomer = namedFolder("Bush", root)
            newcomer.Name = "Tree"
            expect(root:FindFirstChild("Tree")):ToBe(first)
        end)

        it("returns a renamed-in sibling only when it is first in child order", function()
            local newcomer = namedFolder("Bush", root)
            first.Name = "Stump"
            second.Name = "Log"
            third.Name = "Branch"
            newcomer.Name = "Tree"
            expect(root:FindFirstChild("Tree")):ToBe(newcomer)
        end)

        it("returns the second once the first is detached", function()
            first.Parent = nil
            expect(root:FindFirstChild("Tree")):ToBe(second)
        end)

        it("returns the second once the first is re-parented elsewhere", function()
            local elsewhere = namedFolder("Elsewhere", root)
            first.Parent = elsewhere
            expect(root:FindFirstChild("Tree")):ToBe(second)
            expect(elsewhere:FindFirstChild("Tree")):ToBe(first)
        end)

        it("puts a re-parented duplicate back at the end of the chain", function()
            first.Parent = nil
            first.Parent = root
            expect(root:FindFirstChild("Tree")):ToBe(second)
        end)

        it("returns the newly first duplicate after the middle one is detached", function()
            second.Parent = nil
            expect(root:FindFirstChild("Tree")):ToBe(first)
            first.Parent = nil
            expect(root:FindFirstChild("Tree")):ToBe(third)
        end)

        it("finds a duplicate moved in from another parent, behind the residents", function()
            local elsewhere = namedFolder("Elsewhere", root)
            local visitor = namedFolder("Tree", elsewhere)
            visitor.Parent = root
            expect(root:FindFirstChild("Tree")):ToBe(first)
            expect(elsewhere:FindFirstChild("Tree")):ToBeNil()
        end)

        it("survives destroying the first duplicate", function()
            first:Destroy()
            expect(root:FindFirstChild("Tree")):ToBe(second)
        end)
    end)

    describe("FindFirstChildOfClass", function()
        it("returns the first child with exactly that class name", function()
            namedFolder("F", root)
            local part = namedPart("P", root)
            expect(root:FindFirstChildOfClass("Part")):ToBe(part)
        end)

        it("returns the first match in child order", function()
            local first = namedPart("First", root)
            namedPart("Second", root)
            expect(root:FindFirstChildOfClass("Part")):ToBe(first)
        end)

        it("returns nil when no child has that class", function()
            namedFolder("F", root)
            expect(root:FindFirstChildOfClass("Part")):ToBeNil()
        end)

        it("does not match a base class of a child", function()
            namedPart("P", root)
            expect(root:FindFirstChildOfClass("BasePart")):ToBeNil()
            expect(root:FindFirstChildOfClass("Instance")):ToBeNil()
        end)

        it("searches direct children only", function()
            local holder = namedFolder("Holder", root)
            namedPart("Deep", holder)
            expect(root:FindFirstChildOfClass("Part")):ToBeNil()
        end)
    end)

    describe("FindFirstChildWhichIsA", function()
        it("returns the first child that is of that class", function()
            local part = namedPart("P", root)
            expect(root:FindFirstChildWhichIsA("Part")):ToBe(part)
        end)

        it("matches a child through its base class", function()
            namedFolder("F", root)
            local part = namedPart("P", root)
            expect(root:FindFirstChildWhichIsA("BasePart")):ToBe(part)
        end)

        it("matches every child through Instance", function()
            local first = namedFolder("First", root)
            namedPart("Second", root)
            expect(root:FindFirstChildWhichIsA("Instance")):ToBe(first)
        end)

        it("returns the first match in child order", function()
            local first = namedPart("First", root)
            namedPart("Second", root)
            expect(root:FindFirstChildWhichIsA("BasePart")):ToBe(first)
        end)

        it("returns nil when nothing matches", function()
            namedFolder("F", root)
            expect(root:FindFirstChildWhichIsA("BasePart")):ToBeNil()
        end)

        it("searches direct children only", function()
            local holder = namedFolder("Holder", root)
            namedPart("Deep", holder)
            expect(root:FindFirstChildWhichIsA("BasePart")):ToBeNil()
        end)
    end)

    describe("FindFirstAncestor", function()
        it("returns the ancestor with that name", function()
            local branch = namedFolder("Branch", root)
            local leaf = namedFolder("Leaf", branch)
            expect(leaf:FindFirstAncestor("Branch")):ToBe(branch)
            expect(leaf:FindFirstAncestor("FindSpecRoot")):ToBe(root)
        end)

        it("returns the nearest ancestor when two share a name", function()
            local outer = namedFolder("Shared", root)
            local inner = namedFolder("Shared", outer)
            local leaf = namedFolder("Leaf", inner)
            expect(leaf:FindFirstAncestor("Shared")):ToBe(inner)
        end)

        it("returns nil when no ancestor has that name", function()
            local leaf = namedFolder("Leaf", root)
            expect(leaf:FindFirstAncestor("Nowhere")):ToBeNil()
        end)

        it("never returns the instance itself", function()
            local leaf = namedFolder("Leaf", root)
            expect(leaf:FindFirstAncestor("Leaf")):ToBeNil()
        end)

        it("does not search siblings or descendants", function()
            local branch = namedFolder("Branch", root)
            namedFolder("Sibling", root)
            local leaf = namedFolder("Leaf", branch)
            namedFolder("Below", leaf)
            expect(leaf:FindFirstAncestor("Sibling")):ToBeNil()
            expect(leaf:FindFirstAncestor("Below")):ToBeNil()
        end)

        it("returns nil for an unparented instance", function()
            local orphan = namedFolder("Orphan", nil)
            expect(orphan:FindFirstAncestor("FindSpecRoot")):ToBeNil()
        end)
    end)

    describe("FindFirstAncestorOfClass", function()
        it("returns the nearest ancestor with that class name", function()
            local branch = namedFolder("Branch", root)
            local leaf = namedFolder("Leaf", branch)
            expect(leaf:FindFirstAncestorOfClass("Folder")):ToBe(branch)
        end)

        it("reaches past intervening classes", function()
            local model = Instance.new("Model")
            model.Parent = root
            local leaf = namedFolder("Leaf", model)
            expect(leaf:FindFirstAncestorOfClass("Folder")):ToBe(root)
            expect(leaf:FindFirstAncestorOfClass("Model")):ToBe(model)
        end)

        it("reaches the services above the spec tree", function()
            local leaf = namedFolder("Leaf", root)
            expect(leaf:FindFirstAncestorOfClass("Workspace")):ToBe(workspace)
            expect(leaf:FindFirstAncestorOfClass("DataModel")):ToBe(game)
        end)

        it("returns nil when no ancestor has that class", function()
            local leaf = namedFolder("Leaf", root)
            expect(leaf:FindFirstAncestorOfClass("Part")):ToBeNil()
        end)

        it("never returns the instance itself", function()
            local leaf = namedFolder("Leaf", nil)
            expect(leaf:FindFirstAncestorOfClass("Folder")):ToBeNil()
        end)
    end)
end)
