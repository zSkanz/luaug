--!strict
--- The constraint solver, asserted against R10 (E9's declared verification).
---
--- **Constraints are the largest thing ever added below the world hash.** Every
--- other determinism scenario exercises code the engine wrote: `churn` drives
--- the SlotMap, the signal drain and CFrame accumulation; `character` drives the
--- input resolver and a character controller. This one drives Jolt's own
--- solver -- sixty joints held for three thousand ticks, each one an iterative
--- solve whose result depends on the order the constraints were created in, on
--- which bodies the island builder grouped together, and on which of them the
--- sleep heuristic put away. Any of those becoming a fact about an address
--- rather than about the operation sequence is a divergence this catches and
--- nothing else does.
---
--- **Four bodies rather than one**, and they are identical by construction: the
--- same sixteen limbs, built by the same loop, offset along X. That is the shape
--- that catches an island-order bug, because four independent articulated bodies
--- are exactly what a broadphase has a choice about how to group.
---
--- **Built by hand and not by `Ragdoll:Build`**, which is the honest limitation
--- and is worth stating rather than working around. `Build` reads a SKELETON,
--- the replay harness is headless and loads no glTF, so there is no rig here for
--- it to read -- and a scenario that quietly built nothing would record an empty
--- world as a golden. What `Build` produces is asserted in C++ over an invented
--- rig (`engine/scene/tests/ragdoll_build_tests.cpp`); what it produces is
--- exactly the structure below, so this scenario simulates what that function
--- writes even though it did not write it.
---
--- `sameBuildOnly`, like `character`: this accumulates into `CFrame`, whose
--- position is f64 engine-side, so the last-ULP disagreement between two libms
--- survives instead of being rounded away (README, "Why the traces are
--- per-platform").
---
--- The assertions are `error` calls, and the harness fails a run that logged
--- one -- which is what stops a scenario dying on tick one and having its
--- emptiness recorded as a golden.

local PhysicsService = game:GetService("PhysicsService")
local RunService = game:GetService("RunService")

local Rng = Random.new(20260826)

local Ground = Color3.fromRGB(96, 104, 96)
local Limb = Color3.fromRGB(196, 168, 140)

-- One humanoid, as the numbers rather than as sixteen paragraphs. Each row is
-- the limb's parent (0 for the root), its half-length down its own -Y from the
-- parent's end, its radius, and what holds it: 0 fixed, 1 ball socket, 2 hinge.
--
-- The shape is `@luaug/ragdoll`'s `Humanoid` with the same joint counts and the
-- same limits, laid out along an idealised T-pose so that the scenario needs no
-- rig to place it. What is under test is the SOLVER, and the solver does not
-- know where the numbers came from.
type Row = { Parent: number, Offset: vector, Length: number, Radius: number, Kind: number }

local Rows: { Row } = {
    { Parent = 0, Offset = vector.create(0, 0, 0), Length = 0.24, Radius = 0.13, Kind = 0 },
    { Parent = 1, Offset = vector.create(0, 0.24, 0), Length = 0.22, Radius = 0.12, Kind = 1 },
    { Parent = 2, Offset = vector.create(0, 0.22, 0), Length = 0.24, Radius = 0.13, Kind = 1 },
    { Parent = 3, Offset = vector.create(0, 0.24, 0), Length = 0.22, Radius = 0.10, Kind = 1 },

    { Parent = 3, Offset = vector.create(-0.18, 0.20, 0), Length = 0.28, Radius = 0.055, Kind = 1 },
    { Parent = 5, Offset = vector.create(-0.28, 0, 0), Length = 0.26, Radius = 0.045, Kind = 2 },
    { Parent = 6, Offset = vector.create(-0.26, 0, 0), Length = 0.14, Radius = 0.035, Kind = 1 },
    { Parent = 3, Offset = vector.create(0.18, 0.20, 0), Length = 0.28, Radius = 0.055, Kind = 1 },
    { Parent = 8, Offset = vector.create(0.28, 0, 0), Length = 0.26, Radius = 0.045, Kind = 2 },
    { Parent = 9, Offset = vector.create(0.26, 0, 0), Length = 0.14, Radius = 0.035, Kind = 1 },

    { Parent = 1, Offset = vector.create(-0.10, 0, 0), Length = 0.42, Radius = 0.075, Kind = 1 },
    { Parent = 11, Offset = vector.create(0, -0.42, 0), Length = 0.40, Radius = 0.06, Kind = 2 },
    { Parent = 12, Offset = vector.create(0, -0.40, 0), Length = 0.18, Radius = 0.045, Kind = 1 },
    { Parent = 1, Offset = vector.create(0.10, 0, 0), Length = 0.42, Radius = 0.075, Kind = 1 },
    { Parent = 14, Offset = vector.create(0, -0.42, 0), Length = 0.40, Radius = 0.06, Kind = 2 },
    { Parent = 15, Offset = vector.create(0, -0.40, 0), Length = 0.18, Radius = 0.045, Kind = 1 },
}

local function block(name: string, cframe: CFrame, size: vector, anchored: boolean): Part
    local part = Instance.new("Part")
    part.Name = name
    part.Size = size
    part.CFrame = cframe
    part.Color = anchored and Ground or Limb
    part.Anchored = anchored
    part.Parent = workspace
    return part
end

block("Ground", CFrame.new(0, -1, 0), vector.create(120, 2, 120), true)

-- **The limbs do not collide with each other, and that is what a real ragdoll
-- does.** `CollideConnected = false` handles a joined pair -- an upper arm and a
-- lower arm overlap at the elbow by construction -- and it does nothing for the
-- pairs that are not joined. A chest and the upper arms beside it overlap too,
-- and so do the hips and both thighs, so an unfiltered ragdoll spends its first
-- step pushing sixteen interpenetrating capsules apart with whatever force it
-- takes. It takes enough: without this the solver produced NaN positions.
--
-- One group for all four bodies rather than one each, because two ragdolls
-- passing through each other is not what is under test here, and the extra
-- groups would put the thing under test -- sixty joints and the ground -- behind
-- a second variable.
PhysicsService:RegisterCollisionGroup("Ragdoll")
PhysicsService:CollisionGroupSetCollidable("Ragdoll", "Ragdoll", false)

--- One articulated body at `origin`. Returns its limbs, root first.
---
--- **Created in row order, and that is load-bearing.** Jolt solves constraints
--- in insertion order, so a build that walked the rows in any other order would
--- produce a different -- and equally valid -- simulation. R10 is about the
--- order being a property of the script rather than of a container.
local function ragdoll(index: number, origin: vector, holds: { number }): { Part }
    local limbs: { Part } = {}
    local ends: { vector } = {}

    for row = 1, #Rows do
        local spec = Rows[row]
        local from = if spec.Parent == 0 then origin else ends[spec.Parent] + spec.Offset
        -- Every limb hangs straight down its own -Y from where it starts, so a
        -- capsule's centre is half its length below that and its far end is a
        -- full length below. A T-pose built out of offsets rather than out of
        -- rotations: what the solver has to hold is the same either way, and
        -- offsets are a thing a diff can read.
        local centre = from - vector.create(0, spec.Length * 0.5, 0)
        local limb = block(
            `Ragdoll{index}Limb{row}`,
            CFrame.new(centre.x, centre.y, centre.z),
            vector.create(spec.Radius * 2, spec.Length, spec.Radius * 2),
            false
        )
        limb.Shape = Enum.PartShape.Capsule
        limb.CollisionGroup = "Ragdoll"
        -- **Flesh, and it matters here more than anywhere else.** `Density`
        -- defaults to 1 kg per cubic metre, which is a hips capsule of nine
        -- GRAMS -- so the two-newton-second nudge below launched it at two
        -- hundred metres a second and the body left the world. A ragdoll is the
        -- one thing in a scene made entirely of small parts, so it is the one
        -- thing for which the default density is not a rounding difference.
        limb.Density = 1000
        limbs[row] = limb
        ends[row] = from - vector.create(0, spec.Length, 0)
    end

    for row = 1, #Rows do
        local spec = Rows[row]
        if spec.Parent == 0 then
            continue
        end
        local parent = limbs[spec.Parent]
        local child = limbs[row]

        -- Both ends of the joint are the SAME world point -- the child's own
        -- top -- expressed in each part's local space. Two ends at different
        -- points is a joint that snaps apart on the first step, and it is the
        -- mistake that looks correct until something moves.
        --
        -- **Which way its X points is the other half, and the two kinds want
        -- opposite answers.** A swing-twist measures its cone from that axis and
        -- twists about it, so for a shoulder it runs DOWN the bone; a hinge turns
        -- about it, and an elbow turns ACROSS the bone. Getting it wrong is not
        -- subtle and is not visible either: every ball socket starts thirty
        -- degrees outside a sixty-degree cone, sixty joints push at once, and the
        -- character launches. Which is what it did.
        -- Negative, because these limbs are built unrotated with the joint at
        -- the capsule's TOP -- so the bone runs down the part's own -Y, and a
        -- cone aimed along +Y would start every joint a full turn out. What
        -- `Ragdoll:Build` produces is rotated the other way, with +Y running
        -- from the joint to the tip, and it turns the frame the other way to
        -- match. The rule is the same either way: X follows the bone.
        local along = if spec.Kind == 2
            then CFrame.identity
            else CFrame.fromAxisAngle(vector.create(0, 0, 1), -math.pi * 0.5)
        local at = child.CFrame * CFrame.new(0, spec.Length * 0.5, 0) * along

        -- **How far apart the joint can ever hold these two.** Both centres are
        -- a fixed distance from the joint point -- the child's is half its own
        -- length by construction, the parent's is whatever the offset made it --
        -- so the furthest the two centres can get while the joint holds is the
        -- sum, reached when the child has swung to point straight away. Measured
        -- here rather than derived in the check, because the arms and legs hang
        -- off an offset and the arithmetic for those is the build's, not the
        -- assertion's.
        holds[row] = vector.magnitude(parent.Position - at.Position) + spec.Length * 0.5

        local end0 = Instance.new("Attachment")
        end0.Name = "Joint0"
        end0.CFrame = parent.CFrame:Inverse() * at
        end0.Parent = parent

        local end1 = Instance.new("Attachment")
        end1.Name = "Joint1"
        end1.CFrame = child.CFrame:Inverse() * at
        end1.Parent = child

        -- **`any` rather than `Instance`**, so the three properties below are
        -- written plainly. The alternative is a `(joint :: any).Attachment0`
        -- per line, and a statement starting with a parenthesis needs a leading
        -- semicolon that the formatter and the comments then fight over. One
        -- local says the same thing.
        local joint: any
        if spec.Kind == 2 then
            joint = Instance.new("HingeConstraint")
            joint.LimitsEnabled = true
            joint.LowerAngle = -140
            joint.UpperAngle = 0
        else
            joint = Instance.new("BallSocketConstraint")
            joint.LimitsEnabled = true
            joint.UpperAngle = 60
            joint.TwistLimit = 35
        end
        joint.Name = "Joint"
        joint.Attachment0 = end0
        joint.Attachment1 = end1
        -- **False, and a ragdoll needs it.** An upper arm and a lower arm
        -- overlap at the elbow by construction; left colliding they shove each
        -- other apart on the first step, which is a character that explodes.
        joint.CollideConnected = false
        joint.Parent = child
    end

    return limbs
end

local Bodies: { { Part } } = {}
local Holds: { number } = {}
for index = 1, 4 do
    -- Every body is identical, so one table of hold radii serves all four --
    -- and writing it four times over is what proves that.
    Bodies[index] = ragdoll(index, vector.create(-4.5 + (index - 1) * 3, 2.0, 0), Holds)
end

-- A shove on tick 20, from the seeded stream. Not for the physics -- gravity
-- alone would settle these -- but because a divergence needs the bodies to
-- separate rather than land in one identical heap: four ragdolls in the same
-- pose have the same hash whatever order the islands were built in.
local Shove = 20
local Settled = 900

local tick = 0
RunService.Heartbeat:Connect(function()
    tick += 1

    if tick == Shove then
        for index = 1, #Bodies do
            -- **The root only, and gently.** What this is for is separating four
            -- bodies that would otherwise land in one identical heap -- and a
            -- heap is a pose whose hash says nothing about which order the
            -- islands were built in. Shoving every limb in its own random
            -- direction does that too, and it also tears sixteen deeply
            -- interpenetrating capsules apart hard enough that the solver
            -- produces NaN positions, which is not a test of anything. The chain
            -- carries the push to the rest.
            Bodies[index][1]:ApplyImpulse(
                vector.create(Rng:NextNumber(-12, 12), Rng:NextNumber(0, 6), Rng:NextNumber(-12, 12))
            )
        end
    end

    -- **What a broken joint looks like**, checked once the bodies are down.
    -- A constraint that stopped holding does not raise -- it lets its two parts
    -- drift apart, forever, quietly -- so the only way this scenario notices is
    -- to measure the distance it was supposed to keep.
    if tick == Settled then
        for index = 1, #Bodies do
            for row = 1, #Rows do
                local parent0 = Rows[row].Parent
                if parent0 == 0 then
                    continue
                end
                local parent = Bodies[index][parent0]
                local child = Bodies[index][row]
                local apart = vector.magnitude(parent.Position - child.Position)
                -- A centimetre of slack, because a solver holds a joint to a
                -- tolerance rather than exactly and three thousand ticks of
                -- contact is what that tolerance is for. Anything past it is a
                -- joint that stopped holding, which is silent otherwise: a
                -- broken constraint does not raise, it lets two parts drift
                -- apart forever.
                local reach = Holds[row] + 0.01
                if apart > reach then
                    error(
                        `ragdoll {index} limb {row} is {apart} from its parent, past the {reach} its joint should hold`
                    )
                end
            end
        end

        -- And the floor. A body that fell through it is a collision that stopped
        -- happening, which a joint check would not notice at all.
        for index = 1, #Bodies do
            if Bodies[index][1].Position.y < -2 then
                error(`ragdoll {index} fell through the ground`)
            end
        end
    end
end)
