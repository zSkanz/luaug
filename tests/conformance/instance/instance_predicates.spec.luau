--!strict
--- The tree predicates from api-design.md §2.2: `IsA`, `IsAncestorOf`,
--- `IsDescendantOf`. `IsA` answers over the class hierarchy drawn in §2.2;
--- the other two answer over the live tree.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

local function namedFolder(name: string, parent: Instance?): Folder
    local folder = Instance.new("Folder")
    folder.Name = name
    folder.Parent = parent
    return folder
end

describe("Instance predicates", function()
    local root: Folder = nil :: any

    beforeEach(function()
        root = Instance.new("Folder")
        root.Name = "PredicateSpecRoot"
        root.Parent = workspace
    end)

    afterEach(function()
        root:Destroy()
    end)

    describe("IsA", function()
        it("is true for the instance's own class", function()
            expect(Instance.new("Folder"):IsA("Folder")):ToBeTruthy()
            expect(Instance.new("Part"):IsA("Part")):ToBeTruthy()
            expect(Instance.new("Model"):IsA("Model")):ToBeTruthy()
        end)

        it("is true for an abstract base class", function()
            expect(Instance.new("Part"):IsA("BasePart")):ToBeTruthy()
        end)

        it("is true for Instance, which every class extends", function()
            expect(Instance.new("Folder"):IsA("Instance")):ToBeTruthy()
            expect(Instance.new("Part"):IsA("Instance")):ToBeTruthy()
            expect(workspace:IsA("Instance")):ToBeTruthy()
        end)

        it("is false for an unrelated class", function()
            expect(Instance.new("Folder"):IsA("Part")):ToBeFalsy()
            expect(Instance.new("Part"):IsA("Folder")):ToBeFalsy()
            expect(Instance.new("Model"):IsA("BasePart")):ToBeFalsy()
        end)

        it("is false for a subclass of the instance's own class", function()
            -- MeshPart extends BasePart alongside Part, so a Part is not one.
            expect(Instance.new("Part"):IsA("MeshPart")):ToBeFalsy()
        end)

        it("is false for a name that is not a class", function()
            expect(Instance.new("Folder"):IsA("NoSuchClassExistsHere")):ToBeFalsy()
        end)

        it("is unaffected by the instance's Name or Parent", function()
            local part = Instance.new("Part")
            part.Name = "Folder"
            part.Parent = root
            expect(part:IsA("Folder")):ToBeFalsy()
            expect(part:IsA("BasePart")):ToBeTruthy()
        end)

        it("answers for services too", function()
            expect(workspace:IsA("Workspace")):ToBeTruthy()
            expect(game:IsA("DataModel")):ToBeTruthy()
        end)
    end)

    describe("IsAncestorOf", function()
        it("is true for a direct child", function()
            local child = namedFolder("Child", root)
            expect(root:IsAncestorOf(child)):ToBeTruthy()
        end)

        it("is true for a deep descendant", function()
            local branch = namedFolder("Branch", root)
            local leaf = namedFolder("Leaf", branch)
            expect(root:IsAncestorOf(leaf)):ToBeTruthy()
            expect(branch:IsAncestorOf(leaf)):ToBeTruthy()
        end)

        it("is false in the other direction", function()
            local child = namedFolder("Child", root)
            expect(child:IsAncestorOf(root)):ToBeFalsy()
        end)

        it("is false for a sibling", function()
            local left = namedFolder("Left", root)
            local right = namedFolder("Right", root)
            expect(left:IsAncestorOf(right)):ToBeFalsy()
            expect(right:IsAncestorOf(left)):ToBeFalsy()
        end)

        it("is false for itself", function()
            local folder = namedFolder("Self", root)
            expect(folder:IsAncestorOf(folder)):ToBeFalsy()
        end)

        it("is false for an instance in another tree", function()
            local outsider = namedFolder("Outsider", nil)
            expect(root:IsAncestorOf(outsider)):ToBeFalsy()
        end)

        it("stops being true once the descendant is detached", function()
            local branch = namedFolder("Branch", root)
            local leaf = namedFolder("Leaf", branch)
            branch.Parent = nil
            expect(root:IsAncestorOf(leaf)):ToBeFalsy()
            expect(branch:IsAncestorOf(leaf)):ToBeTruthy()
        end)

        it("becomes true when a subtree is moved in", function()
            local branch = namedFolder("Branch", nil)
            local leaf = namedFolder("Leaf", branch)
            expect(root:IsAncestorOf(leaf)):ToBeFalsy()
            branch.Parent = root
            expect(root:IsAncestorOf(leaf)):ToBeTruthy()
        end)
    end)

    describe("IsDescendantOf", function()
        it("is true for its own parent", function()
            local child = namedFolder("Child", root)
            expect(child:IsDescendantOf(root)):ToBeTruthy()
        end)

        it("is true for a distant ancestor", function()
            local branch = namedFolder("Branch", root)
            local leaf = namedFolder("Leaf", branch)
            expect(leaf:IsDescendantOf(root)):ToBeTruthy()
        end)

        it("is true for the services above the spec tree", function()
            local leaf = namedFolder("Leaf", root)
            expect(leaf:IsDescendantOf(workspace)):ToBeTruthy()
            expect(leaf:IsDescendantOf(game)):ToBeTruthy()
        end)

        it("is false in the other direction", function()
            local child = namedFolder("Child", root)
            expect(root:IsDescendantOf(child)):ToBeFalsy()
        end)

        it("is false for itself", function()
            local folder = namedFolder("Self", root)
            expect(folder:IsDescendantOf(folder)):ToBeFalsy()
        end)

        it("is false for a sibling", function()
            local left = namedFolder("Left", root)
            local right = namedFolder("Right", root)
            expect(left:IsDescendantOf(right)):ToBeFalsy()
        end)

        it("is false while unparented", function()
            local orphan = namedFolder("Orphan", nil)
            expect(orphan:IsDescendantOf(root)):ToBeFalsy()
            expect(orphan:IsDescendantOf(game)):ToBeFalsy()
        end)

        it("agrees with IsAncestorOf on every pair", function()
            local branch = namedFolder("Branch", root)
            local leaf = namedFolder("Leaf", branch)
            expect(leaf:IsDescendantOf(branch)):ToBe(branch:IsAncestorOf(leaf))
            expect(leaf:IsDescendantOf(root)):ToBe(root:IsAncestorOf(leaf))
            expect(root:IsDescendantOf(leaf)):ToBe(leaf:IsAncestorOf(root))
        end)
    end)
end)
