--!strict
--- Conformance: `Attachment` and the constraint family (E9, Part B).
---
--- **This is the Luau half of a seam whose C++ half is already covered.**
--- `engine/physics/tests/constraint_tests.cpp` proves what the BACKEND does --
--- that a hinge never leaves its limits, that a cone holds, that `updateBody`
--- re-anchors rather than dangling, that two worlds built by the same calls
--- agree after three hundred steps. None of that touches the tree, and every
--- one of those guarantees reaches a person through instances they parent and
--- properties they write. What is asserted here is that journey: the scene
--- resolves attachments, the mirror creates a constraint from what the tree
--- says, and turning a property off releases what it was holding.
---
--- **A constraint takes two `Attachment`s and not two parts**, which is the
--- decision the whole family rests on: it is what makes the joint FRAME
--- authorable and visible, and it is why there is no `C0`/`C1` here the way
--- there is on a `Weld`. Point the attachment and you have pointed the hinge.
---
--- Everything waits in whole ticks through `task.wait`, because the tick is the
--- only clock a spec may read (ADR 0025).

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local afterEach = testing.afterEach

local spawned: { Instance } = {}

local function part(name: string, position: vector): Part
    local created = Instance.new("Part")
    created.Name = name
    created.Size = vector.create(2, 2, 2)
    created.Position = position
    created.Parent = workspace
    table.insert(spawned, created)
    return created
end

local function anchoredPart(name: string, position: vector): Part
    local created = part(name, position)
    created.Anchored = true
    return created
end

local function ground(): Part
    local floor = anchoredPart("Ground", vector.create(0, -1, 0))
    floor.Size = vector.create(120, 2, 120)
    return floor
end

local function attachmentOn(host: BasePart, offset: CFrame): Attachment
    local created = Instance.new("Attachment")
    created.CFrame = offset
    created.Parent = host
    table.insert(spawned, created)
    return created
end

--- Distance between two attachments in the world, which is what "the joint is
--- still a joint" means when the bodies are free to move.
local function span(a: Attachment, b: Attachment): number
    local difference = a.WorldCFrame.Position - b.WorldCFrame.Position
    return math.sqrt(difference.x * difference.x + difference.y * difference.y + difference.z * difference.z)
end

describe("Attachment", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("reports a world frame that is its own frame carried by its part", function()
        local host = anchoredPart("Host", vector.create(4, 10, 0))
        local point = attachmentOn(host, CFrame.new(0, 3, 0))

        task.wait(0.1)
        expect(point.WorldCFrame.Position.x):ToBeCloseTo(4, 0.01)
        expect(point.WorldCFrame.Position.y):ToBeCloseTo(13, 0.01)
        expect(point.WorldCFrame.Position.z):ToBeCloseTo(0, 0.01)
    end)

    it("follows the part the simulation is moving, rather than where it started", function()
        ground()
        local faller = part("Faller", vector.create(0, 30, 0))
        local point = attachmentOn(faller, CFrame.new(0, 1, 0))

        task.wait(0.5)
        expect(faller.Position.y < 29):ToBe(true)
        -- One metre above whatever the part reached, not one metre above 30.
        expect(point.WorldCFrame.Position.y):ToBeCloseTo(faller.Position.y + 1, 0.05)
    end)
end)

describe("FixedConstraint", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("makes two bodies one assembly, so an anchor holds what is fixed to it", function()
        -- **The one a `Weld` is not.** A weld MOVES its second part and asks
        -- the solver nothing; this hands both bodies over, so the anchored one
        -- holding the free one is the solver's answer and not a transform
        -- write.
        ground()
        local anchor = anchoredPart("Anchor", vector.create(0, 20, 0))
        local hanging = part("Hanging", vector.create(0, 17, 0))

        local top = attachmentOn(anchor, CFrame.new(0, -1, 0))
        local bottom = attachmentOn(hanging, CFrame.new(0, 1, 0))

        local joint = Instance.new("FixedConstraint")
        joint.Attachment0 = top
        joint.Attachment1 = bottom
        joint.Parent = workspace
        table.insert(spawned, joint)

        task.wait(1.5)
        -- It has not fallen to the ground two dozen metres below.
        expect(hanging.Position.y > 15):ToBe(true)
        expect(span(top, bottom) < 0.5):ToBe(true)
    end)

    it("holds nothing until both attachments are named", function()
        -- A constraint with one end is not half a joint; it is not a joint.
        ground()
        local anchor = anchoredPart("Anchor", vector.create(0, 20, 0))
        local hanging = part("Hanging", vector.create(0, 17, 0))
        local top = attachmentOn(anchor, CFrame.new(0, -1, 0))
        attachmentOn(hanging, CFrame.new(0, 1, 0))

        local joint = Instance.new("FixedConstraint")
        joint.Attachment0 = top
        joint.Parent = workspace
        table.insert(spawned, joint)

        task.wait(1)
        expect(hanging.Position.y < 16):ToBe(true)
    end)

    it("releases what it held when it is disabled, and holds again when it is not", function()
        ground()
        local anchor = anchoredPart("Anchor", vector.create(0, 20, 0))
        local hanging = part("Hanging", vector.create(0, 17, 0))
        local top = attachmentOn(anchor, CFrame.new(0, -1, 0))
        local bottom = attachmentOn(hanging, CFrame.new(0, 1, 0))

        local joint = Instance.new("FixedConstraint")
        joint.Attachment0 = top
        joint.Attachment1 = bottom
        joint.Parent = workspace
        table.insert(spawned, joint)

        task.wait(0.5)
        local held = hanging.Position.y
        expect(held > 15):ToBe(true)

        joint.Enabled = false
        task.wait(1)
        expect(hanging.Position.y < held - 1):ToBe(true)

        -- And back on, where it now is. A re-enabled joint is the joint again,
        -- not a joint remembering where the part used to be.
        joint.Enabled = true
        task.wait(0.5)
        local resumed = hanging.Position.y
        task.wait(0.5)
        expect(hanging.Position.y):ToBeCloseTo(resumed, 0.5)
    end)
end)

describe("HingeConstraint", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("keeps its two ends together while letting the free part swing", function()
        ground()
        local anchor = anchoredPart("Anchor", vector.create(0, 20, 0))
        local arm = part("Arm", vector.create(3, 20, 0))

        local pivot = attachmentOn(anchor, CFrame.new(1, 0, 0))
        local elbow = attachmentOn(arm, CFrame.new(-2, 0, 0))

        local joint = Instance.new("HingeConstraint")
        joint.Attachment0 = pivot
        joint.Attachment1 = elbow
        joint.Parent = workspace
        table.insert(spawned, joint)

        task.wait(1.5)
        -- Gravity has swung it down, and the joint is still a joint.
        expect(arm.Position.y < 20):ToBe(true)
        expect(span(pivot, elbow) < 0.5):ToBe(true)
    end)

    it("does not let go of a heavy load, however long gravity pulls", function()
        -- The Luau-side echo of the C++ limit test: what matters to a person is
        -- not the angle in radians, it is that the door is still on its hinge.
        ground()
        local anchor = anchoredPart("Anchor", vector.create(0, 30, 0))
        local door = part("Door", vector.create(4, 30, 0))
        door.Size = vector.create(6, 6, 1)

        local pivot = attachmentOn(anchor, CFrame.new(1, 0, 0))
        local hinge = attachmentOn(door, CFrame.new(-3, 0, 0))

        local joint = Instance.new("HingeConstraint")
        joint.Attachment0 = pivot
        joint.Attachment1 = hinge
        joint.LimitsEnabled = true
        joint.LowerAngle = -45
        joint.UpperAngle = 45
        joint.Parent = workspace
        table.insert(spawned, joint)

        task.wait(3)
        expect(span(pivot, hinge) < 0.5):ToBe(true)
        expect(door.Position.y > 25):ToBe(true)
    end)
end)

describe("BallSocketConstraint", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("swings freely and does not let go", function()
        ground()
        local anchor = anchoredPart("Anchor", vector.create(0, 30, 0))
        local bob = part("Bob", vector.create(3, 30, 0))

        local top = attachmentOn(anchor, CFrame.new(0, 0, 0))
        local eye = attachmentOn(bob, CFrame.new(-3, 0, 0))

        local joint = Instance.new("BallSocketConstraint")
        joint.Attachment0 = top
        joint.Attachment1 = eye
        joint.Parent = workspace
        table.insert(spawned, joint)

        -- **Sampled across the swing rather than read at one instant**, which
        -- is the difference between asserting physics and asserting a phase: a
        -- three-metre pendulum has a period near three and a half seconds, so
        -- "where is it at two seconds" is a question whose answer moves when
        -- anything about the solver's step does. What is true of every swing is
        -- that it goes DOWN at some point and never comes apart at any point.
        local lowest = bob.Position.y
        local widest = 0
        for _ = 1, 60 do
            task.wait(0.05)
            lowest = math.min(lowest, bob.Position.y)
            widest = math.max(widest, span(top, eye))
        end

        expect(lowest < 29):ToBe(true)
        expect(widest < 0.5):ToBe(true)
    end)

    it("with limits on is a shoulder rather than a rope's end", function()
        -- The limited form is a DIFFERENT solver joint, not a free one with
        -- corrections bolted on -- so it is not exceeded and pulled back, it is
        -- not exceeded. What that buys a person: a limb that hangs at an angle
        -- an arm can reach.
        ground()
        local anchor = anchoredPart("Anchor", vector.create(0, 30, 0))
        local limb = part("Limb", vector.create(3, 30, 0))

        local socket = attachmentOn(anchor, CFrame.new(0, 0, 0))
        local head = attachmentOn(limb, CFrame.new(-3, 0, 0))

        local joint = Instance.new("BallSocketConstraint")
        joint.Attachment0 = socket
        joint.Attachment1 = head
        joint.LimitsEnabled = true
        joint.UpperAngle = 30
        joint.TwistLimit = 10
        joint.Parent = workspace
        table.insert(spawned, joint)

        task.wait(2.5)
        expect(span(socket, head) < 0.5):ToBe(true)
        -- A thirty-degree cone about the anchor's own X axis cannot reach
        -- straight down, so the limb hangs well above where a free joint would
        -- have put it.
        expect(limb.Position.y > 27):ToBe(true)
    end)
end)
