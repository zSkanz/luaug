--!strict
--- `Destroy` from api-design.md §2.2, §3.1 ("Destroy and queued fires") and
--- divergence #25: the handle stops resolving at the end of the drain in which
--- `Destroying` fired, and using it afterwards raises `script.err.instance_dead`.
---
--- Every case that needs the handle to be dead yields once: `task.wait` resumes
--- at a task-resume phase (§3.2), which is necessarily after the drain that
--- carried the `Destroying` fire has run to fixpoint.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

local function namedFolder(name: string, parent: Instance?): Folder
    local folder = Instance.new("Folder")
    folder.Name = name
    folder.Parent = parent
    return folder
end

local function names(instances: { Instance }): { string }
    local result: { string } = {}
    for _, instance in instances do
        table.insert(result, instance.Name)
    end
    return result
end

describe("Instance Destroy", function()
    local root: Folder = nil :: any

    beforeEach(function()
        root = Instance.new("Folder")
        root.Name = "DestroySpecRoot"
        root.Parent = workspace
    end)

    afterEach(function()
        root:Destroy()
    end)

    describe("effect on the tree", function()
        it("removes the instance from its parent's children", function()
            local victim = namedFolder("Victim", root)
            local survivor = namedFolder("Survivor", root)

            victim:Destroy()

            expect(root:GetChildren()):ToEqual({ survivor })
            expect(root:FindFirstChild("Victim")):ToBeNil()
        end)

        it("sets Parent to nil", function()
            local victim = namedFolder("Victim", root)
            victim:Destroy()
            expect(victim.Parent):ToBeNil()
        end)

        it("refuses to re-parent a destroyed instance", function()
            local victim = namedFolder("Victim", root)
            victim:Destroy()

            expect(function()
                victim.Parent = root
            end):ToThrow()
            expect(#root:GetChildren()):ToBe(0)
        end)

        it("detaches children from the destroyed instance", function()
            local victim = namedFolder("Victim", root)
            namedFolder("Child", victim)

            victim:Destroy()

            expect(victim:GetChildren()):ToEqual({})
        end)

        it("leaves siblings and the parent alone", function()
            local victim = namedFolder("Victim", root)
            namedFolder("Left", root)
            namedFolder("Right", root)

            victim:Destroy()

            expect(names(root:GetChildren())):ToEqual({ "Left", "Right" })
            expect(root.Parent):ToBe(workspace)
        end)

        it("works on an unparented instance", function()
            local orphan = namedFolder("Orphan", nil)
            orphan:Destroy()
            task.wait()

            expect(function()
                return orphan.Name
            end):ToThrow("script.err.instance_dead")
        end)

        it("is a no-op when repeated inside the same drain", function()
            local victim = namedFolder("Victim", root)
            victim:Destroy()
            victim:Destroy()
            expect(#root:GetChildren()):ToBe(0)
        end)
    end)

    describe("Destroying", function()
        it("fires at the next drain, not synchronously", function()
            local victim = namedFolder("Victim", root)
            local fired = false
            victim.Destroying:Connect(function()
                fired = true
            end)

            victim:Destroy()
            expect(fired):ToBeFalsy()

            task.wait()
            expect(fired):ToBeTruthy()
        end)

        it("fires for every descendant of the destroyed instance", function()
            local branch = namedFolder("Branch", root)
            local leaf = namedFolder("Leaf", branch)
            local fired: { string } = {}

            branch.Destroying:Connect(function()
                table.insert(fired, "Branch")
            end)
            leaf.Destroying:Connect(function()
                table.insert(fired, "Leaf")
            end)

            branch:Destroy()
            task.wait()

            expect(fired):ToContain("Branch")
            expect(fired):ToContain("Leaf")
        end)
    end)

    describe("the dead handle", function()
        it("still resolves synchronously, before the drain ends", function()
            local victim = namedFolder("Victim", root)
            victim:Destroy()

            expect(victim.Name):ToBe("Victim")
            expect(victim.ClassName):ToBe("Folder")
            expect(victim.Parent):ToBeNil()
        end)

        it("raises script.err.instance_dead on a property read", function()
            local victim = namedFolder("Victim", root)
            victim:Destroy()
            task.wait()

            expect(function()
                return victim.Name
            end):ToThrow("script.err.instance_dead")
        end)

        it("raises script.err.instance_dead on a property write", function()
            local victim = namedFolder("Victim", root)
            victim:Destroy()
            task.wait()

            expect(function()
                victim.Name = "Renamed"
            end):ToThrow("script.err.instance_dead")
        end)

        it("raises script.err.instance_dead on a method call", function()
            local victim = namedFolder("Victim", root)
            victim:Destroy()
            task.wait()

            expect(function()
                return victim:GetChildren()
            end):ToThrow("script.err.instance_dead")
            expect(function()
                return victim:FindFirstChild("Anything")
            end):ToThrow("script.err.instance_dead")
            expect(function()
                return victim:IsA("Folder")
            end):ToThrow("script.err.instance_dead")
        end)

        it("raises script.err.instance_dead on Clone", function()
            local victim = namedFolder("Victim", root)
            victim:Destroy()
            task.wait()

            expect(function()
                return victim:Clone()
            end):ToThrow("script.err.instance_dead")
        end)

        it("raises script.err.instance_dead on a second Destroy", function()
            local victim = namedFolder("Victim", root)
            victim:Destroy()
            task.wait()

            expect(function()
                victim:Destroy()
            end):ToThrow("script.err.instance_dead")
        end)

        it("dies for every descendant too", function()
            local branch = namedFolder("Branch", root)
            local leaf = namedFolder("Leaf", branch)
            local deepLeaf = namedFolder("DeepLeaf", leaf)

            branch:Destroy()
            task.wait()

            expect(function()
                return leaf.Name
            end):ToThrow("script.err.instance_dead")
            expect(function()
                return deepLeaf.Name
            end):ToThrow("script.err.instance_dead")
        end)

        it("does not take living instances with it", function()
            local victim = namedFolder("Victim", root)
            local survivor = namedFolder("Survivor", root)

            victim:Destroy()
            task.wait()

            expect(survivor.Name):ToBe("Survivor")
            expect(survivor.Parent):ToBe(root)
        end)
    end)

    describe("destroying while iterating", function()
        it("is safe to destroy every child while iterating a GetChildren snapshot", function()
            -- §3.1: the array is a fresh snapshot, so the loop is not walking the
            -- structure it is mutating.
            local holder = namedFolder("Holder", root)
            namedFolder("A", holder)
            namedFolder("B", holder)
            namedFolder("C", holder)

            for _, child in holder:GetChildren() do
                child:Destroy()
            end

            expect(holder:GetChildren()):ToEqual({})
        end)

        it("leaves the snapshot array holding its entries after the tree is emptied", function()
            local holder = namedFolder("Holder", root)
            namedFolder("A", holder)
            namedFolder("B", holder)

            local snapshot = holder:GetChildren()
            for _, child in snapshot do
                child:Destroy()
            end

            expect(#snapshot):ToBe(2)
        end)

        it("keeps the remaining entries readable when their parent is destroyed mid-loop", function()
            local holder = namedFolder("Holder", root)
            namedFolder("A", holder)
            namedFolder("B", holder)
            namedFolder("C", holder)

            local seen: { string } = {}
            for index, child in holder:GetChildren() do
                if index == 1 then
                    holder:Destroy()
                end
                table.insert(seen, child.Name)
            end

            expect(seen):ToEqual({ "A", "B", "C" })
        end)

        it("is safe to destroy every descendant while iterating a GetDescendants snapshot", function()
            local holder = namedFolder("Holder", root)
            local branch = namedFolder("Branch", holder)
            namedFolder("Leaf", branch)
            namedFolder("Other", holder)

            for _, descendant in holder:GetDescendants() do
                descendant:Destroy()
            end

            expect(holder:GetDescendants()):ToEqual({})
        end)
    end)
end)
