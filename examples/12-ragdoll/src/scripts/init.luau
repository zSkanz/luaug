--!strict
--- examples/12-ragdoll — a character that stops being animated and starts falling.
---
--- Six figures on a platform. Press Space and they go down; press it again and
--- they get up. What happens in between is the whole example: the pose stops
--- coming from the skeleton and starts coming from a set of parts the physics
--- solver is pushing around, and the mesh follows.
---
--- **A ragdoll here is nothing but instances.** `Ragdoll:Build` creates a `Part`
--- per joint, a `Bone` on each saying which joint it stands for, and a
--- constraint holding it to the limb above — every one of them a thing you can
--- select in the editor, move, retune and delete. There is no hidden body list,
--- because a class that owned its own bodies would be a second owner of things
--- the physics mirror already creates from the tree. What `Build` saves you is
--- the arithmetic: where each limb goes, how long it is, and which way it
--- points.
---
--- **Three things worth reading as technique rather than as plumbing:**
---
---   1. **`Blend` is a POSE blend and not a solver one.** The limbs fall exactly
---      as hard at 0.5 as at 1; what changes is how far the drawn joint is
---      carried towards them. That is why going down is a RAMP here rather than
---      a flag — at 1 on the frame the key is pressed, the character snaps from
---      standing to sprawled in a sixtieth of a second, and every ragdoll that
---      looks cheap looks cheap for that reason.
---   2. **The profile is data, and one profile fits several exporters.** Each
---      limb names its joint with a LIST of spellings, because the same shoulder
---      is `mixamorig:LeftArm`, `LeftArm` or `upper_arm.L` depending on who
---      exported the rig. A limb the rig has no joint for is SKIPPED rather than
---      refused, and its children hang from the nearest limb that was built — so
---      one humanoid profile works on a rig with no toes.
---   3. **Sixteen limbs, not fifty.** A rig carries fingers, toes and twist
---      bones, and simulating them buys nothing you can see while costing a body
---      and a joint each. The joints nobody drives keep their place relative to
---      their parent when the pose is rebuilt, so they ride along for free.
---      `tests/bench/ragdoll10` prices it: about 26 microseconds of solver per
---      ragdoll while it is moving, so ten of them is a quarter of a millisecond.
---
--- **What is deliberately not here.** There is no walk cycle, so a ragdoll goes
--- down from a standing pose rather than mid-stride. A clip would make the
--- blend more convincing and would make this example about animation; the seam
--- it would exercise — `Blend` reading the animated joint rather than the bind
--- pose — is the same either way, and `physics/ragdoll.spec.luau` covers it.

local PhysicsService = game:GetService("PhysicsService")
local RunService = game:GetService("RunService")

local Ragdoll = require("@luaug/ragdoll")

--- How long going down and getting up take, in seconds. A ragdoll that arrives
--- in one frame reads as a glitch; a fifth of a second reads as a fall, and
--- coming back is slower because a character standing up is doing something
--- deliberate.
local FallSeconds = 0.18
local RiseSeconds = 0.45

--- Where the figures stand. Two rows of three, far enough apart that they do not
--- start inside each other.
local Columns = 3
local Rows = 2
local Spacing = 2.4

-- --- The stage ------------------------------------------------------------------

local ground = Instance.new("Part")
ground.Name = "Ground"
ground.Size = vector.create(60, 1, 60)
ground.CFrame = CFrame.new(0, -0.5, 0)
ground.Color = Color3.fromRGB(96, 102, 96)
ground.Anchored = true
ground.Parent = workspace

-- A low wall on one side, so a figure that is shoved has something to land
-- against and the constraints have something to argue with.
local wall = Instance.new("Part")
wall.Name = "Wall"
wall.Size = vector.create(14, 1.2, 0.6)
wall.CFrame = CFrame.new(0, 0.6, -4.2)
wall.Color = Color3.fromRGB(132, 124, 112)
wall.Anchored = true
wall.Parent = workspace

-- **One collision group for every limb, and they do not collide with each
-- other.** `CollideConnected = false` covers a JOINED pair — an upper arm and a
-- lower arm overlap at the elbow by construction — and does nothing for the
-- pairs that are not joined, of which a humanoid has plenty: a chest and the
-- arms beside it, hips and both thighs. Left unfiltered a ragdoll spends its
-- first step pushing sixteen interpenetrating capsules apart with whatever force
-- that takes, and it takes enough to throw the character off the platform.
--
-- One group for all six rather than one each, because two ragdolls passing
-- through each other is a trade this example is happy to make: what it is
-- showing is a character falling, not a crowd.
PhysicsService:RegisterCollisionGroup("Ragdoll")
PhysicsService:CollisionGroupSetCollidable("Ragdoll", "Ragdoll", false)

-- **A world with no camera is not rendered**, and the fallback it falls into is
-- worth recognising once: the host clears to a pulsing colour and draws the
-- debug lines through an orbit view, which looks like a broken renderer and is
-- actually a scene that never said where to look from. A project whose world is
-- a FILE gets its camera from the scene; this one builds its world in code, so
-- it builds its camera too.
local camera = Instance.new("Camera")
camera.Name = "MainCamera"
camera.FieldOfView = 62
camera.NearPlane = 0.1
camera.FarPlane = 200
camera.CFrame = CFrame.lookAt(vector.create(0, 4.2, 9.5), vector.create(0, 1.0, 0))
camera.Parent = workspace
workspace.CurrentCamera = camera

-- --- The figures ----------------------------------------------------------------

type Figure = {
    Mesh: MeshPart,
    Ragdoll: Ragdoll,
    Limbs: { BasePart },
    Built: boolean,
}

local Figures: { Figure } = {}

--- Stands one character up. It is not a ragdoll yet: the rig it will be built
--- against does not exist at this point in the frame.
---
--- **`MeshContent` is a request and not a load**, which is the thing worth
--- knowing here. Parenting the part asks the engine for the file; the skeleton
--- arrives when the loader has read it, which is the next tick at the earliest.
--- `Ragdoll:Build` reads that skeleton, so calling it from the top of a script
--- refuses every time — with `ragdoll_no_rig`, which is a correct and extremely
--- confusing answer if you do not know why.
local function figure(index: number, x: number, z: number): Figure
    local mesh = Instance.new("MeshPart")
    mesh.Name = `Figure{index}`
    mesh.MeshContent = "asset://models/humanoid.gltf" :: Content
    mesh.CFrame = CFrame.new(x, 0, z)
    -- The mesh itself is never simulated. While the ragdoll is off it is a
    -- character standing there; while it is on, the pose comes from the limbs
    -- and the part it is drawn from does not move at all.
    mesh.Anchored = true
    mesh.Parent = workspace

    local ragdoll = Instance.new("Ragdoll")
    ragdoll.Name = "Ragdoll"
    ragdoll.Parent = mesh

    return { Mesh = mesh, Ragdoll = ragdoll, Limbs = {}, Built = false }
end

--- Tries to build one figure's limbs, and says whether it managed.
---
--- **It can refuse, and the refusal is the useful part.** A rig with none of the
--- profile's joints, a ragdoll that already has limbs under it, a mesh whose
--- file has not arrived — each is an error with a name rather than a character
--- that quietly will not fall.
local function build(index: number, made: Figure): boolean
    local ok, built = pcall(function()
        return made.Ragdoll:Build(Ragdoll.Humanoid)
    end)
    if not ok then
        return false
    end

    for _, child in made.Ragdoll:GetChildren() do
        if child:IsA("BasePart") then
            local limb = child :: BasePart
            limb.CollisionGroup = "Ragdoll"
            -- **Flesh, and it matters more here than anywhere else.** `Density`
            -- is one kilogram per cubic metre by default, which makes a hand
            -- weigh under a gram — and a body of grams is thrown across the
            -- world by the smallest contact. A ragdoll is the one thing in a
            -- scene made entirely of small parts, so it is the one thing for
            -- which the default is not a rounding difference.
            limb.Density = 1000
            -- **Invisible, because they are the simulation and not the
            -- character.** What a player is meant to see is the skinned mesh;
            -- the capsules are what the solver pushes around so the mesh has
            -- something to follow. `G` shows them, which is the view worth
            -- having when a ragdoll is behaving strangely -- a limb through the
            -- floor or a joint pulled apart is obvious there and invisible in
            -- the mesh.
            limb.Transparency = 1
            table.insert(made.Limbs, limb)
        end
    end

    made.Built = true
    print(`12-ragdoll: figure {index} built {built} limb(s).`)
    return true
end

for row = 0, Rows - 1 do
    for column = 0, Columns - 1 do
        local x = (column - (Columns - 1) * 0.5) * Spacing
        local z = (row - (Rows - 1) * 0.5) * Spacing
        table.insert(Figures, figure(#Figures + 1, x, z))
    end
end

--- How many ticks to keep trying before saying so. A model that has not arrived
--- after a second is a model that is not going to: the path is wrong, the file
--- is broken, or the rig has none of the joints the profile names. Retrying
--- forever would turn any of those into a scene that is simply empty.
local BuildDeadline = 60
local builtCount = 0
local buildTicks = 0
local buildGaveUp = false

--- Builds whatever is ready, and reports once when it stops trying.
local function buildPending()
    if builtCount == #Figures or buildGaveUp then
        return
    end
    buildTicks += 1
    for index, made in Figures do
        if not made.Built and build(index, made) then
            builtCount += 1
        end
    end
    if builtCount == #Figures then
        return
    end
    if buildTicks >= BuildDeadline then
        buildGaveUp = true
        warn(`12-ragdoll: {#Figures - builtCount} figure(s) never found a rig to build against.`)
    end
end

-- --- Input -----------------------------------------------------------------------

-- On the simulation clock, because going down is a DECISION and a replay has to
-- see it (ADR 0039).
local Gameplay = Instance.new("InputContext")
Gameplay.Name = "Gameplay"
Gameplay.Parent = workspace

local FallAction = Instance.new("InputAction")
FallAction.Name = "Fall"
FallAction.Type = Enum.InputActionType.Bool
FallAction.Parent = Gameplay

local FallKey = Instance.new("InputBinding")
FallKey.KeyCode = Enum.KeyCode.Space
FallKey.DisplayName = "Space"
FallKey.Parent = FallAction

local ShoveAction = Instance.new("InputAction")
ShoveAction.Name = "Shove"
ShoveAction.Type = Enum.InputActionType.Bool
ShoveAction.Parent = Gameplay

local ShoveKey = Instance.new("InputBinding")
ShoveKey.KeyCode = Enum.KeyCode.F
ShoveKey.DisplayName = "F"
ShoveKey.Parent = ShoveAction

local ProxyAction = Instance.new("InputAction")
ProxyAction.Name = "ShowProxy"
ProxyAction.Type = Enum.InputActionType.Bool
ProxyAction.Parent = Gameplay

local ProxyKey = Instance.new("InputBinding")
ProxyKey.KeyCode = Enum.KeyCode.G
ProxyKey.DisplayName = "G"
ProxyKey.Parent = ProxyAction

-- --- The one decision, per tick ---------------------------------------------------

--- How far down each figure is, 0 standing and 1 fully simulated. One number per
--- figure rather than one for all of them, so they can be staggered — six
--- characters folding in perfect unison is one of the two ways a ragdoll reads
--- as a toy. (The other is arriving in a single frame, which is what `Blend`
--- being a ramp rather than a flag is for.)
local Blend: { number } = {}
--- What each figure's blend was at the moment the key was last pressed. The ramp
--- runs from THERE rather than from 0 or 1, so a toggle halfway down turns the
--- character around from where it is instead of snapping it to an end.
local Anchor: { number } = {}
for index = 1, #Figures do
    Blend[index] = 0
    Anchor[index] = 0
end

--- How long after the key each figure starts. Small: what this is buying is six
--- people rather than one animation played six times, and a long stagger reads
--- as a queue.
local Stagger = 0.05

local wantDown = false
local since = 0
local fallHeld = false
local shoveHeld = false
local showProxy = false
local proxyHeld = false

RunService.Heartbeat:Connect(function(delta: number)
    -- Before anything else, because a figure that became buildable this tick
    -- should fall on this tick rather than on the next one.
    buildPending()

    since += delta

    local fall = FallAction:GetState() :: boolean
    if fall and not fallHeld then
        wantDown = not wantDown
        since = 0
        for index = 1, #Figures do
            Anchor[index] = Blend[index]
        end
    end
    fallHeld = fall

    local shove = ShoveAction:GetState() :: boolean
    if shove and not shoveHeld and wantDown then
        -- Only while they are down: an impulse on a limb whose pose is still the
        -- animation's would move a body nobody can see, and the character would
        -- appear to ignore it.
        for _, made in Figures do
            for _, limb in made.Limbs do
                limb:ApplyImpulse(vector.create(0, 30, 260))
            end
        end
    end
    shoveHeld = shove

    local proxy = ProxyAction:GetState() :: boolean
    if proxy and not proxyHeld then
        showProxy = not showProxy
        for _, made in Figures do
            for _, limb in made.Limbs do
                limb.Transparency = if showProxy then 0.35 else 1
            end
        end
    end
    proxyHeld = proxy

    local goal = if wantDown then 1 else 0
    local duration = if wantDown then FallSeconds else RiseSeconds

    for index, made in Figures do
        if not made.Built then
            continue
        end

        -- **The delay moves the START, not the rate.** Subtracting it from the
        -- blend every tick — which is what the first cut of this did — makes the
        -- later figures ramp more slowly and the last ones never arrive at all.
        -- Three of six stayed standing, which looked like a ragdoll defect and
        -- was arithmetic.
        local elapsed = since - (index - 1) * Stagger
        local t = math.clamp(elapsed / duration, 0, 1)
        Blend[index] = Anchor[index] + (goal - Anchor[index]) * t

        -- **`Enabled` is what makes the ragdoll drive the pose at all, and
        -- `Blend` is how much.** Switching `Enabled` off at zero rather than
        -- leaving it on with a blend of nothing is what hands the character back
        -- to the animation properly: an overridden joint at a blend of zero is
        -- written at the animation's own value every tick, which is correct and
        -- is still work nobody needs done.
        made.Ragdoll.Enabled = Blend[index] > 0
        made.Ragdoll.Blend = Blend[index]
    end
end)
