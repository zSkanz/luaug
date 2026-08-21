--!strict

-- The horde: the scene `docs/perf-baselines.md` prices instanced draws against.
--
-- Asked on 2026-08-20 as "could I build a survivors-like on this engine, it has
-- to be optimized", and answered at M2 by building one and measuring it rather
-- than by estimating. That measurement was taken OUTSIDE the repository, which
-- the M2 table says out loud -- and it is why the M7.5 gate asks for it again,
-- with the count of draw *calls* stated beside the count of visible objects.
--
-- The shape is the genre's own, and every part of it is load-bearing:
--
--   * **Enemies are anchored `MeshPart`s whose `Position` is written from Luau
--     every tick.** They do not collide with each other -- an enemy that
--     collides costs 4.7 microseconds on top (M5's crowd table), and a
--     survivors-like with two thousand colliding enemies is a different game.
--     What is being measured here is the renderer, not the broad phase.
--   * **One mesh and one material for the whole horde**, which is what makes it
--     an instancing benchmark rather than a draw-call benchmark with extra
--     steps. Two thousand enemies that shared nothing would be two thousand
--     calls whatever the renderer did.
--   * **The player circles**, so the horde never settles into a static
--     configuration and the frame never becomes a measurement of a cached one.
--   * **The sun casts shadows**, because the shadow pass submits the same
--     geometry again and a benchmark that skipped it would price half the
--     frame.
--
-- Run by hand rather than by CTest: a frame time is a property of the machine,
-- and a gate that fails when CI allocates a slower runner is a gate people
-- switch off (`tests/screenshots/README.md` makes the same argument about pixel
-- goldens). The command is in `tests/perf/README.md`.

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")

--- How many enemies. The perf table records 200, 500, 1000 and 2000; this is
--- the point the gate quotes, and the others are one edit.
local EnemyCount = 2000

--- Metres. The ring the player walks, and the field the horde spreads over.
local PlayerOrbitRadius = 18
local PlayerOrbitPeriod = 9
local SpawnRadius = 34

--- Metres per second. Slow enough that the horde stays a horde rather than
--- collapsing onto the player in the first second of the run.
local EnemySpeed = 2.2

local EnemyContent: Content = "asset://models/enemy.gltf"

local camera = Instance.new("Camera")
camera.Name = "HordeCamera"
camera.FieldOfView = 60
camera.NearPlane = 0.1
camera.FarPlane = 400
camera.Parent = workspace
workspace.CurrentCamera = camera

local ground = Instance.new("Part")
ground.Name = "Ground"
ground.Size = Vector3.new(160, 1, 160)
ground.CFrame = CFrame.new(0, -0.5, 0)
ground.Color = Color3.fromRGB(96, 104, 92)
ground.Anchored = true
ground.Parent = workspace

local player = Instance.new("Part")
player.Name = "Player"
player.Size = Vector3.new(1, 2, 1)
player.CFrame = CFrame.new(PlayerOrbitRadius, 1, 0)
player.Color = Color3.fromRGB(240, 240, 240)
player.Anchored = true
player.Parent = workspace

-- A deterministic layout: the same run twice is the same scene, which is what
-- makes two measurements comparable at all (R10 applies to the benchmark as
-- much as to the engine).
local random = Random.new(20260821)

local enemies: { MeshPart } = {}
for index = 1, EnemyCount do
    local angle = random:NextNumber() * math.pi * 2
    local distance = math.sqrt(random:NextNumber()) * SpawnRadius

    local enemy = Instance.new("MeshPart")
    enemy.Name = "Enemy" .. tostring(index)
    enemy.MeshContent = EnemyContent
    enemy.Anchored = true
    enemy.CFrame = CFrame.new(math.cos(angle) * distance, 0.6, math.sin(angle) * distance)
    enemy.Parent = workspace
    enemies[index] = enemy
end

Lighting.ClockTime = 15
Lighting.GeographicLatitude = 35
Lighting.Ambient = Color3.fromRGB(10, 11, 15)
Lighting.Brightness = 2.6
Lighting.FogColor = Color3.fromRGB(150, 178, 214)
Lighting.FogStart = 120
Lighting.FogEnd = 400

local elapsed = 0

RunService.Heartbeat:Connect(function(dt: number)
    elapsed += dt

    local playerAngle = (elapsed / PlayerOrbitPeriod) * math.pi * 2
    local target = Vector3.new(math.cos(playerAngle) * PlayerOrbitRadius, 1, math.sin(playerAngle) * PlayerOrbitRadius)
    player.CFrame = CFrame.new(target)

    -- The chase, written from Luau every tick. This loop IS the "simulation"
    -- column of the perf table: two thousand property writes plus the arithmetic
    -- around them.
    local step = EnemySpeed * dt
    for index = 1, EnemyCount do
        local enemy = enemies[index]
        local position = enemy.Position
        local toPlayer = target - position
        -- `vector.magnitude` rather than a `.Magnitude` property: the native
        -- vector is Luau's own type (ADR 0013), so its library is a module and
        -- its names are camelCase (ADR 0034).
        local distance = vector.magnitude(toPlayer)
        if distance > 1.5 then
            enemy.Position = position + toPlayer * (step / distance)
        end
    end

    -- Behind and above the player, looking at the middle of the field, so the
    -- camera sees most of the horde most of the time.
    local eye = Vector3.new(
        math.cos(playerAngle) * (PlayerOrbitRadius + 26),
        16,
        math.sin(playerAngle) * (PlayerOrbitRadius + 26)
    )
    camera.CFrame = CFrame.lookAt(eye, Vector3.new(0, 1, 0))
end)
