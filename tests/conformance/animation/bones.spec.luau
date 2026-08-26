--!strict
--- Conformance: `Bone`, from E9's Part B.
---
--- **These cases run against a rig that is not there, and that is the point.**
--- The conformance suite is headless and loads no glTF, so every skeleton here
--- is empty and every `JointName` is a name nothing has. That is not a
--- limitation to work around: it is the exact state a real game is in for the
--- frames between creating a `MeshPart` and its file arriving, and it is the
--- state in which the whole design either holds or produces a sword in another
--- country.
---
--- The contract the class states, which is what is pinned here: *"a bone whose
--- `JointName` the rig does not have follows the mesh part itself, which puts it
--- on the character rather than at the world origin -- the failure a renamed
--- joint should produce is a sword in the wrong place, not a sword in another
--- country."*
---
--- What a RESOLVED bone does -- the pose walk, the palette, an override moving
--- the fingers -- is asserted in C++ (`luaug_render_tests`) and in the skinned
--- capture golden, because those need a file this suite cannot read.
---
--- Everything waits in whole ticks, because the tick is the only clock a spec
--- may read (ADR 0025).

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

describe("Bone", function()
    local mesh: MeshPart = nil :: any
    local bone: Bone = nil :: any

    beforeEach(function()
        mesh = Instance.new("MeshPart")
        mesh.Name = "BoneSpecMesh"
        mesh.Anchored = true
        mesh.Position = vector.create(7, 12, -3)
        mesh.Parent = workspace

        bone = Instance.new("Bone")
        bone.Parent = mesh
    end)

    afterEach(function()
        mesh:Destroy()
    end)

    it("is an Attachment, and inherits one rather than carrying a second", function()
        -- The class deliberately declares no storage of its own: `World::create`
        -- calls every hook down the ancestry, so declaring one here would attach
        -- a second attachment component to the same instance. What a person sees
        -- of that decision is exactly this -- it IS an attachment.
        expect(bone.ClassName):ToBe("Bone")
        expect(bone:IsA("Attachment")):ToBe(true)
        expect(bone:IsA("Instance")):ToBe(true)
    end)

    it("starts unresolved, because a bone that names nothing has found nothing", function()
        expect(bone.JointName):ToBe("")
        expect(bone.JointIndex):ToBe(-1)
    end)

    it("stays unresolved for a joint the rig does not have", function()
        bone.JointName = "mixamorig:LeftHand"
        task.wait(0.1)

        -- Not zero, and not the first joint, and not an error. Minus one is the
        -- answer that can be read.
        expect(bone.JointIndex):ToBe(-1)
        expect(bone.JointName):ToBe("mixamorig:LeftHand")
    end)

    it("refuses a write to JointIndex, which is where the rig answers and not the author", function()
        local ok = pcall(function()
            (bone :: any).JointIndex = 4
        end)
        expect(ok):ToBe(false)
        expect(bone.JointIndex):ToBe(-1)
    end)

    it("follows the mesh part when its joint is unknown, rather than the world origin", function()
        -- **The assertion the class's own documentation is about.** A renamed
        -- joint should put a sword in the wrong place on the character, not at
        -- the origin of the world -- and those two failures look nothing alike
        -- to somebody playing.
        bone.JointName = "NoSuchJoint"
        task.wait(0.1)

        expect(bone.WorldCFrame.Position.x):ToBeCloseTo(7, 0.01)
        expect(bone.WorldCFrame.Position.y):ToBeCloseTo(12, 0.01)
        expect(bone.WorldCFrame.Position.z):ToBeCloseTo(-3, 0.01)
    end)

    it("carries its own CFrame on top of that fallback", function()
        bone.JointName = "NoSuchJoint"
        bone.CFrame = CFrame.new(0, 2, 0)
        task.wait(0.1)

        expect(bone.WorldCFrame.Position.y):ToBeCloseTo(14, 0.01)
    end)

    it("moves with the part the simulation is moving", function()
        local floor = Instance.new("Part")
        floor.Name = "BoneSpecGround"
        floor.Size = vector.create(120, 2, 120)
        floor.Position = vector.create(0, -1, 0)
        floor.Anchored = true
        floor.Parent = workspace

        mesh.Anchored = false
        task.wait(0.5)

        expect(mesh.Position.y < 11.5):ToBe(true)
        expect(bone.WorldCFrame.Position.y):ToBeCloseTo(mesh.Position.y, 0.05)

        floor:Destroy()
    end)

    it("takes a Transform, which is the writable half of a pose", function()
        -- Composed on top of whatever a clip is doing rather than replacing it,
        -- and the identity is a bone nobody drives. With no clip loaded what is
        -- assertable is that the property is real and round-trips -- the
        -- composition itself is a C++ case, because it needs a pose.
        expect(bone.Transform.Position.x):ToBeCloseTo(0, 0.0001)
        expect(bone.Transform.Position.y):ToBeCloseTo(0, 0.0001)
        expect(bone.Transform.Position.z):ToBeCloseTo(0, 0.0001)

        bone.Transform = CFrame.new(0.5, -1.25, 2)
        expect(bone.Transform.Position.x):ToBeCloseTo(0.5, 0.0001)
        expect(bone.Transform.Position.y):ToBeCloseTo(-1.25, 0.0001)
        expect(bone.Transform.Position.z):ToBeCloseTo(2, 0.0001)
    end)

    it("is created on demand, so a rig with tens of joints costs the ones you asked for", function()
        -- The decision is that a `Bone` is NOT instantiated one per joint: a
        -- crowd would otherwise put thousands of them in the world hash and the
        -- change queue to answer a question about three. What that means from
        -- Luau is simply that a fresh mesh part has no bones under it until
        -- somebody makes one, and this is that sentence as an assertion.
        local fresh = Instance.new("MeshPart")
        fresh.Name = "BoneSpecFresh"
        fresh.Anchored = true
        fresh.Parent = workspace
        task.wait(0.1)

        expect(#fresh:GetChildren()):ToBe(0)
        fresh:Destroy()
    end)
end)
