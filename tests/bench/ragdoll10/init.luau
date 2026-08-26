--!strict
--- What ten ragdolls cost, measured rather than estimated (E9).
---
--- **`sockets200` prices a joint and this prices a character**, and they are not
--- the same question. Two hundred sockets are two hundred independent
--- two-body islands, which is the cheapest shape a constraint can be in: the
--- solver splits them apart and each one converges on its own. A ragdoll is the
--- opposite -- sixteen bodies and fifteen joints in ONE island, all of which have
--- to be solved together, every iteration, every substep. Ten of them is a
--- hundred and sixty bodies and a hundred and fifty joints in ten islands, which
--- is what a fight in a game actually looks like.
---
--- **They are dropped rather than posed.** Everything expensive about a ragdoll
--- happens while it is moving: an island that has gone to sleep costs nothing, so
--- a bench that measured ten settled heaps would measure the sleep heuristic and
--- report it as a ragdoll price. Three hundred ticks is five seconds, and they
--- are still moving at the end.
---
--- **Nothing is written per tick**, like `sockets200` and for the same reason: no
--- script traffic and no property churn, so the number is the simulation.
---
--- Built by hand rather than by `Ragdoll:Build`, because the bench harness is
--- headless and loads no glTF -- there is no rig for `Build` to read. The
--- structure is the one it writes; `tests/determinism/ragdoll` says the same
--- thing at more length.
---
--- The 16 ms budget is a whole frame and a catastrophe detector, like every other
--- scene here. The number that matters is the one recorded in
--- `docs/perf-baselines.md`.

local PhysicsService = game:GetService("PhysicsService")

local Rng = Random.new(2609)

-- The humanoid of `@luaug/ragdoll`, as numbers: each row is its parent (0 for
-- the root), where it starts relative to the parent's far end, its length, its
-- radius, and what holds it -- 1 a ball socket, 2 a hinge.
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

local Root = Instance.new("Folder")
Root.Name = "Ragdolls"
Root.Parent = workspace

local ground = Instance.new("Part")
ground.Name = "Ground"
ground.Size = vector.create(200, 2, 200)
ground.CFrame = CFrame.new(0, -1, 0)
ground.Anchored = true
ground.Parent = workspace

-- One group for all ten, so the limbs pass through each other and hit only the
-- ground. `CollideConnected = false` covers a joined pair -- an upper arm and a
-- lower arm overlap at the elbow by construction -- and does nothing for the
-- pairs that are not joined, of which a humanoid has plenty. Left unfiltered a
-- ragdoll spends its first step pushing sixteen interpenetrating capsules apart,
-- and what this bench would then be measuring is contact resolution.
PhysicsService:RegisterCollisionGroup("Ragdoll")
PhysicsService:CollisionGroupSetCollidable("Ragdoll", "Ragdoll", false)

local function ragdoll(index: number, origin: vector)
    local body = Instance.new("Folder")
    body.Name = `Ragdoll{index}`
    body.Parent = Root

    local limbs: { Part } = {}
    local ends: { vector } = {}

    for row = 1, #Rows do
        local spec = Rows[row]
        local from = if spec.Parent == 0 then origin else ends[spec.Parent] + spec.Offset
        local centre = from - vector.create(0, spec.Length * 0.5, 0)

        local limb = Instance.new("Part")
        limb.Name = `Limb{row}`
        limb.Size = vector.create(spec.Radius * 2, spec.Length, spec.Radius * 2)
        limb.CFrame = CFrame.new(centre.x, centre.y, centre.z)
        limb.Shape = Enum.PartShape.Capsule
        limb.CollisionGroup = "Ragdoll"
        -- Flesh. `Density` defaults to 1 kg per cubic metre, which makes a hips
        -- capsule weigh nine grams -- and a bench whose bodies are made of foam
        -- is not measuring anything a game will run.
        limb.Density = 1000
        limb.Parent = body

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

        -- Both ends on the child's own top, and the frame's X down the bone for a
        -- socket, across it for a hinge -- a shoulder twists about the bone and an
        -- elbow turns across it. A cone aimed the wrong way starts every joint
        -- outside its own limit, which is not a bench, it is an explosion.
        local along = if spec.Kind == 2
            then CFrame.identity
            else CFrame.fromAxisAngle(vector.create(0, 0, 1), -math.pi * 0.5)
        local at = child.CFrame * CFrame.new(0, spec.Length * 0.5, 0) * along

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

    -- A shove at build time rather than on a tick, because nothing here runs per
    -- tick: what this measures is the simulation with no script in the frame.
    limbs[1]:ApplyImpulse(vector.create(Rng:NextNumber(-12, 12), Rng:NextNumber(0, 6), Rng:NextNumber(-12, 12)))
end

-- Two rows of five, three metres apart, dropped from two. Far enough that the
-- ten islands stay ten islands: what this prices is one character's chain, and
-- ten of them merging into one island would price something no game has.
for index = 1, 10 do
    local column = (index - 1) % 5
    local row = (index - 1) // 5
    ragdoll(index, vector.create(-6 + column * 3, 2.0, -1.5 + row * 3))
end
