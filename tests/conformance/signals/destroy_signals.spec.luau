--!strict
--- Conformance: `Destroy` against queued fires.
--- Written from api-design.md §3.1 ("`Destroy` and queued fires") and
--- divergence #25 of §2.5: Destroy enqueues `Destroying`, then closes the
--- instance's other signals; the handle stops resolving at the end of the drain
--- in which `Destroying` fired.

local testing = require("@luaug/testing")

local describe = testing.describe
local it = testing.it
local expect = testing.expect
local beforeEach = testing.beforeEach
local afterEach = testing.afterEach

describe("Destroy and queued fires", function()
    local root: Folder

    beforeEach(function()
        root = Instance.new("Folder")
        root.Name = "DestroySpec"
        root.Parent = workspace
    end)

    afterEach(function()
        root:Destroy()
    end)

    it("does not fire Destroying during the Destroy call", function()
        local victim = Instance.new("Folder")
        victim.Parent = root
        local ran = false
        victim.Destroying:Connect(function()
            ran = true
        end)

        victim:Destroy()

        expect(ran):ToBe(false)
    end)

    it("fires Destroying at the next resumption point", function()
        local victim = Instance.new("Folder")
        victim.Parent = root
        local count = 0
        victim.Destroying:Connect(function()
            count += 1
        end)

        victim:Destroy()

        task.wait()
        expect(count):ToBe(1)
    end)

    it("fires Destroying exactly once when Destroy is called twice", function()
        local victim = Instance.new("Folder")
        victim.Parent = root
        local count = 0
        victim.Destroying:Connect(function()
            count += 1
        end)

        victim:Destroy()
        victim:Destroy()

        task.wait()
        expect(count):ToBe(1)
    end)

    -- The documented pair: the Destroying handler still runs, and the queued
    -- fire on one of the instance's other signals invokes nothing.
    it("invokes nothing for a ChildAdded queued before the instance was destroyed", function()
        local victim = Instance.new("Folder")
        victim.Parent = root
        local childAdded = 0
        local destroying = 0
        victim.ChildAdded:Connect(function()
            childAdded += 1
        end)
        victim.Destroying:Connect(function()
            destroying += 1
        end)

        local child = Instance.new("Folder")
        child.Parent = victim
        victim:Destroy()

        task.wait()
        expect(childAdded):ToBe(0)
        expect(destroying):ToBe(1)
    end)

    it("invokes nothing for an attribute change queued before the instance was destroyed", function()
        local victim = Instance.new("Folder")
        victim.Parent = root
        local attributeChanged = 0
        victim.AttributeChanged:Connect(function()
            attributeChanged += 1
        end)

        victim:SetAttribute("Doomed", true)
        victim:Destroy()

        task.wait()
        expect(attributeChanged):ToBe(0)
    end)

    it("invokes nothing for a property change queued before the instance was destroyed", function()
        local victim = Instance.new("Folder")
        victim.Name = "Victim"
        victim.Parent = root
        local nameChanged = 0
        victim:GetPropertyChangedSignal("Name"):Connect(function()
            nameChanged += 1
        end)

        victim.Name = "Renamed"
        victim:Destroy()

        task.wait()
        expect(nameChanged):ToBe(0)
    end)

    it("reports the connections of the destroyed instance's other signals as disconnected", function()
        local victim = Instance.new("Folder")
        victim.Parent = root
        local connection = victim.ChildAdded:Connect(function() end)

        victim:Destroy()

        task.wait()
        expect(connection.Connected):ToBe(false)
    end)

    -- Destroy enqueues Destroying *first*, so a connection made afterwards was
    -- not in the connection list that fire captured.
    it("does not invoke a Destroying connection made after Destroy was called", function()
        local victim = Instance.new("Folder")
        victim.Parent = root
        local ran = false

        victim:Destroy()
        victim.Destroying:Connect(function()
            ran = true
        end)

        task.wait()
        expect(ran):ToBe(false)
    end)

    it("fires Destroying on the descendants of a destroyed subtree", function()
        local parent = Instance.new("Folder")
        parent.Name = "Parent"
        parent.Parent = root
        local child = Instance.new("Folder")
        child.Name = "Child"
        child.Parent = parent
        task.wait()

        local seen: { string } = {}
        parent.Destroying:Connect(function()
            table.insert(seen, "parent")
        end)
        child.Destroying:Connect(function()
            table.insert(seen, "child")
        end)

        parent:Destroy()

        task.wait()
        expect(#seen):ToBe(2)
        expect(seen):ToContain("parent")
        expect(seen):ToContain("child")
    end)

    it("raises ChildRemoved on the former parent when a child is destroyed", function()
        local victim = Instance.new("Folder")
        victim.Name = "Victim"
        victim.Parent = root
        task.wait()

        local removed: Instance? = nil
        root.ChildRemoved:Connect(function(child: Instance)
            removed = child
        end)

        victim:Destroy()

        task.wait()
        expect(removed):ToBe(victim)
    end)

    it("unparents the instance immediately, before the drain", function()
        local victim = Instance.new("Folder")
        victim.Name = "Victim"
        victim.Parent = root

        victim:Destroy()

        expect(victim.Parent):ToBeNil()
        expect(root:FindFirstChild("Victim")):ToBeNil()
    end)

    it("locks Parent to nil", function()
        local victim = Instance.new("Folder")
        victim.Parent = root
        victim:Destroy()

        expect(function()
            victim.Parent = root
        end):ToThrow()
    end)

    it("keeps the handle readable inside the Destroying handler", function()
        local victim = Instance.new("Folder")
        victim.Name = "StillReadable"
        victim.Parent = root
        local seenName: string? = nil
        victim.Destroying:Connect(function()
            seenName = victim.Name
        end)

        victim:Destroy()

        task.wait()
        expect(seenName):ToBe("StillReadable")
    end)

    it("keeps the handle readable between Destroy and the drain", function()
        local victim = Instance.new("Folder")
        victim.Name = "StillReadable"
        victim.Parent = root

        victim:Destroy()

        expect(victim.Name):ToBe("StillReadable")
    end)

    -- Divergence #25: use-after-destroy is a keyed error, not a silent read of
    -- a corpse.
    it("stops resolving the handle after the drain in which Destroying fired", function()
        local victim = Instance.new("Folder")
        victim.Name = "Gone"
        victim.Parent = root
        victim:Destroy()

        task.wait()

        expect(function()
            return (victim :: any).Name
        end):ToThrow("[script.err.instance_dead]")
    end)

    it("returns a fresh array from GetChildren, so destroying during iteration is safe", function()
        for index = 1, 5 do
            local child = Instance.new("Folder")
            child.Name = "Child" .. index
            child.Parent = root
        end

        local children = root:GetChildren()
        expect(#children):ToBe(5)
        for _, child in children do
            child:Destroy()
        end

        expect(#children):ToBe(5)
        expect(#root:GetChildren()):ToBe(0)
    end)

    it("returns a fresh array from GetDescendants, so destroying during iteration is safe", function()
        local branch = Instance.new("Folder")
        branch.Name = "Branch"
        branch.Parent = root
        local leaf = Instance.new("Folder")
        leaf.Name = "Leaf"
        leaf.Parent = branch

        local descendants = root:GetDescendants()
        expect(#descendants):ToBe(2)
        for _, descendant in descendants do
            descendant:Destroy()
        end

        expect(#descendants):ToBe(2)
        expect(#root:GetDescendants()):ToBe(0)
    end)

    it("leaves an already-destroyed member of the array readable until the drain ends", function()
        local first = Instance.new("Folder")
        first.Name = "First"
        first.Parent = root
        local second = Instance.new("Folder")
        second.Name = "Second"
        second.Parent = root

        local children = root:GetChildren()
        first:Destroy()

        local names: { string } = {}
        for _, child in children do
            table.insert(names, child.Name)
        end
        expect(names):ToEqual({ "First", "Second" })
    end)

    it("does not fire Destroying for an instance that was only unparented", function()
        local child = Instance.new("Folder")
        child.Parent = root
        task.wait()

        local ran = false
        child.Destroying:Connect(function()
            ran = true
        end)

        child.Parent = nil

        task.wait()
        expect(ran):ToBe(false)
    end)
end)
