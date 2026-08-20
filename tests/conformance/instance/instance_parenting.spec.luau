--!strict
--- Parent assignment and the cycle check, from api-design.md §2.2 and the
--- Instance semantics checklist in architecture.md §4
--- (`Parent` cycle check -> `scene.err.parent_cycle`).

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

local function namedFolder(name: string, parent: Instance?): Folder
    local folder = Instance.new("Folder")
    folder.Name = name
    folder.Parent = parent
    return folder
end

local function childNames(instance: Instance): { string }
    local names: { string } = {}
    for _, child in instance:GetChildren() do
        table.insert(names, child.Name)
    end
    return names
end

describe("Instance parenting", function()
    local root: Folder = nil :: any

    beforeEach(function()
        root = Instance.new("Folder")
        root.Name = "ParentingSpecRoot"
        root.Parent = workspace
    end)

    afterEach(function()
        root:Destroy()
    end)

    describe("Parent assignment", function()
        it("appends the child to the end of the parent's children", function()
            namedFolder("First", root)
            namedFolder("Second", root)
            namedFolder("Third", root)
            expect(childNames(root)):ToEqual({ "First", "Second", "Third" })
        end)

        it("makes the instance reachable from its new parent", function()
            local child = namedFolder("Child", root)
            expect(root:FindFirstChild("Child")):ToBe(child)
            expect(child.Parent):ToBe(root)
        end)

        it("removes the child from its previous parent", function()
            local left = namedFolder("Left", root)
            local right = namedFolder("Right", root)
            local child = namedFolder("Child", left)

            child.Parent = right

            expect(childNames(left)):ToEqual({})
            expect(childNames(right)):ToEqual({ "Child" })
            expect(left:FindFirstChild("Child")):ToBeNil()
            expect(child.Parent):ToBe(right)
        end)

        it("closes the gap left behind in the previous parent's child order", function()
            local first = namedFolder("First", root)
            namedFolder("Second", root)
            namedFolder("Third", root)
            local elsewhere = namedFolder("Elsewhere", root)

            first.Parent = elsewhere

            expect(childNames(root)):ToEqual({ "Second", "Third", "Elsewhere" })
        end)

        it("appends a re-parented child at the end of the new parent", function()
            local holder = namedFolder("Holder", root)
            local moved = namedFolder("Moved", root)
            namedFolder("Resident", holder)

            moved.Parent = holder

            expect(childNames(holder)):ToEqual({ "Resident", "Moved" })
        end)

        it("appends a child that returns to its original parent at the end", function()
            local first = namedFolder("First", root)
            namedFolder("Second", root)

            first.Parent = nil
            first.Parent = root

            expect(childNames(root)):ToEqual({ "Second", "First" })
        end)

        it("leaves child order unchanged when a child is assigned its current parent", function()
            local first = namedFolder("First", root)
            namedFolder("Second", root)

            first.Parent = root

            expect(childNames(root)):ToEqual({ "First", "Second" })
        end)

        it("detaches without destroying when Parent is set to nil", function()
            local child = namedFolder("Child", root)
            local grandchild = namedFolder("Grandchild", child)

            child.Parent = nil

            expect(child.Parent):ToBeNil()
            expect(childNames(root)):ToEqual({})
            expect(child.Name):ToBe("Child")
            expect(child:FindFirstChild("Grandchild")):ToBe(grandchild)
            expect(grandchild.Parent):ToBe(child)
        end)

        it("carries the whole subtree along when a branch is moved", function()
            local branch = namedFolder("Branch", root)
            local leaf = namedFolder("Leaf", branch)
            local newHome = namedFolder("NewHome", root)

            branch.Parent = newHome

            expect(leaf.Parent):ToBe(branch)
            expect(branch.Parent):ToBe(newHome)
            expect(leaf:IsDescendantOf(root)):ToBeTruthy()
        end)

        it("keeps an unparented instance usable as a template", function()
            local template = namedFolder("Template", nil)
            namedFolder("Part1", template)

            expect(template.Parent):ToBeNil()
            expect(childNames(template)):ToEqual({ "Part1" })
        end)
    end)

    describe("Parent cycles", function()
        it("refuses to make an instance its own parent", function()
            local folder = namedFolder("Self", root)
            expect(function()
                folder.Parent = folder
            end):ToThrow("scene.err.parent_cycle")
        end)

        it("refuses to parent an instance under its own child", function()
            local parent = namedFolder("Parent", root)
            local child = namedFolder("Child", parent)
            expect(function()
                parent.Parent = child
            end):ToThrow("scene.err.parent_cycle")
        end)

        it("refuses to parent an instance under a deep descendant", function()
            local top = namedFolder("Top", root)
            local middle = namedFolder("Middle", top)
            local bottom = namedFolder("Bottom", middle)
            expect(function()
                top.Parent = bottom
            end):ToThrow("scene.err.parent_cycle")
        end)

        it("refuses a cycle even between unparented instances", function()
            local outer = namedFolder("Outer", nil)
            local inner = namedFolder("Inner", outer)
            expect(function()
                outer.Parent = inner
            end):ToThrow("scene.err.parent_cycle")
        end)

        it("leaves the tree untouched after a refused cycle", function()
            local parent = namedFolder("Parent", root)
            local child = namedFolder("Child", parent)

            pcall(function()
                parent.Parent = child
            end)

            expect(parent.Parent):ToBe(root)
            expect(child.Parent):ToBe(parent)
            expect(childNames(root)):ToEqual({ "Parent" })
            expect(childNames(parent)):ToEqual({ "Child" })
        end)

        it("allows parenting into a sibling subtree", function()
            local left = namedFolder("Left", root)
            local right = namedFolder("Right", root)
            local leaf = namedFolder("Leaf", left)

            leaf.Parent = right

            expect(leaf.Parent):ToBe(right)
        end)
    end)
end)
