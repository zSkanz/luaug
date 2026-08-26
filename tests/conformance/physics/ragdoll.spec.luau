--!strict
--- Conformance: `Ragdoll`, from E9's Part B.
---
--- **These cases run against a rig that is not there**, exactly as
--- `animation.spec.luau`'s do, and for the same reason: the conformance suite is
--- headless and loads no glTF, so no `MeshPart` here has a skeleton. That is not
--- a gap being worked around -- it is the state a real game is in for the frames
--- between creating a `MeshPart` and its file being read, and it is the state in
--- which a silent success is worst. A `Build` that quietly made nothing would
--- leave somebody staring at a character that will not fall with no message
--- anywhere.
---
--- What a build that DOES find a rig produces -- limb lengths, the bone at the
--- joint rather than at the capsule's centre, a partial ragdoll reattaching to
--- the nearest ancestor -- is asserted in C++ over an invented rig
--- (`engine/scene/tests/ragdoll_build_tests.cpp`, eleven cases), which is the
--- only place that can invent one. What is asserted here is the surface: what
--- the properties do, what refuses, and that a refusal is a message.
---
--- Everything waits in whole ticks through `task.wait`, because the tick is the
--- only clock a spec may read (ADR 0025).

local Ragdoll = require("@luaug/ragdoll")
local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

describe("Ragdoll", function()
    local mesh: MeshPart = nil :: any
    local ragdoll: Ragdoll = nil :: any

    beforeEach(function()
        mesh = Instance.new("MeshPart")
        mesh.Name = "RagdollSpecMesh"
        mesh.Anchored = true
        mesh.Parent = workspace
        ragdoll = Instance.new("Ragdoll")
        ragdoll.Parent = mesh
    end)

    afterEach(function()
        mesh:Destroy()
    end)

    it("is off until somebody switches it on", function()
        -- Off has to be the default, or building one would be impossible to
        -- author: every limb would start falling the moment it was parented.
        expect(ragdoll.Enabled):ToBe(false)
    end)

    it("blends fully by default, so switching it on is the whole pose", function()
        expect(ragdoll.Blend):ToBe(1)
    end)

    it("clamps Blend rather than refusing it", function()
        -- A blend is a dial, and every dial in this API clamps. A script ramping
        -- one with `elapsed / duration` overshoots on the frame that finishes
        -- it, and refusing that write would leave a character halfway down
        -- forever.
        ragdoll.Blend = 2.5
        expect(ragdoll.Blend):ToBe(1)
        ragdoll.Blend = -3
        expect(ragdoll.Blend):ToBe(0)
    end)

    it("keeps a Blend between the two ends", function()
        ragdoll.Blend = 0.25
        expect(ragdoll.Blend):ToBeCloseTo(0.25)
    end)

    it("refuses to build on a mesh with no skeleton, and says so", function()
        -- The case a headless run is IN, and the one a silent success would be
        -- worst for: somebody staring at a character that will not fall, with
        -- nothing anywhere saying why.
        expect(function()
            ragdoll:Build(Ragdoll.Humanoid)
        end):ToThrow()
    end)

    it("refuses to build under something that is not a rigged mesh at all", function()
        local crate = Instance.new("Part")
        crate.Name = "RagdollSpecCrate"
        crate.Parent = workspace
        local orphan = Instance.new("Ragdoll")
        orphan.Parent = crate

        expect(function()
            orphan:Build(Ragdoll.Humanoid)
        end):ToThrow()
        -- Refused means nothing created: a half-built ragdoll under a crate is
        -- worse than none, because it looks like it worked.
        expect(#orphan:GetChildren()):ToBe(0)
        crate:Destroy()
    end)

    it("refuses a profile that is not a list of limbs", function()
        expect(function()
            (ragdoll :: any):Build("Humanoid")
        end):ToThrow()
    end)
end)

describe("@luaug/ragdoll", function()
    it("ships a humanoid profile", function()
        expect(Ragdoll.Humanoid):ToBeA("table")
        expect(#Ragdoll.Humanoid > 0):ToBe(true)
    end)

    it("declares a root first, and every other limb after the one it hangs from", function()
        -- **The rule `Build` enforces, asserted on the profile that ships.** A
        -- forward reference would name a limb that does not exist yet, so a
        -- built-in profile that broke it would refuse on every rig -- and the
        -- error would look like the rig's fault.
        expect(Ragdoll.Humanoid[1].Parent):ToBeNil()
        for index = 2, #Ragdoll.Humanoid do
            local parent = Ragdoll.Humanoid[index].Parent
            expect(parent):ToBeA("number")
            expect((parent :: number) < index):ToBe(true)
        end
    end)

    it("gives every limb at least one name to look for", function()
        for index = 1, #Ragdoll.Humanoid do
            local joint = Ragdoll.Humanoid[index].Joint
            if type(joint) == "table" then
                expect(#joint > 0):ToBe(true)
            else
                expect(joint):ToBeA("string")
            end
        end
    end)
end)
