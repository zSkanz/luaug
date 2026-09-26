--!strict
--- examples/11-ocean — an ocean that is a function, and everything that obeys it.
---
--- There is one wave function in this file. The water is drawn from it, the
--- boat is placed by it, and the floating cargo is pushed by it, so nothing on
--- screen can disagree with anything else on screen — which is the whole reason
--- a boat on a fake sea looks wrong: usually the waves are a shader and the
--- buoyancy is a different number.
---
--- **Three things worth reading as technique rather than as plumbing:**
---
---   1. **The water is a surface shader** (ADR 0091), in this project's own
---      `content/shaders/ocean.surface.hlsl`: nine flat grids follow the boat and
---      the GPU moves every vertex of them. The same three waves are written in
---      that file and in this one, and both are evaluated at one clock --
---      `RunService.SimTime` -- so the boat Luau places sits on the water the GPU
---      draws.
---   2. **Nothing about the water is written from here after startup.** The
---      grids are placed once a tick in whole-cell steps, which is nine property
---      writes; the version of this example that moved 676 plates from Luau
---      spent five milliseconds a frame on the writes alone.
---   3. **The sea is translucent, and reads what is under it**: foam where the
---      water is thin against the hull and the cargo, and the scene behind
---      refracted through it -- `SceneDepth` and `SceneColor`, which a blended
---      surface shader reads and an opaque one does not.
---
--- **What is genuinely simulated and what is not**, because the difference is a
--- limit of v1 and not a shortcut:
---
---   * The **cargo floats for real** — Archimedes through `ApplyImpulse`, with
---     drag proportional to how much of the crate is under water. Drop density
---     to 0.3 and it rides high; raise it past 1 and it sinks. That is physics.
---   * The **boat is driven, not simulated.** `ApplyImpulse` applies at the
---     centre of mass and v1 has no impulse-at-a-point and no angular impulse,
---     so buoyancy cannot produce torque and a simulated hull could heave but
---     never roll. The hull is therefore welded to a script-driven helm — which
---     makes it KINEMATIC rather than static (`physics_sync.cpp`), so it still
---     pushes what it hits — and its full frame is read off the wave surface
---     four times a tick. This is also how most shipping games do it.
---
--- **Controls.** W/S throttle, A/D steer, arrow keys or the right stick orbit
--- the camera, Space drops a crate over the side.

local DebugService = game:GetService("DebugService")
local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")

-- --- The sea state ------------------------------------------------------------

--- Each water grid: `luaug://mesh/grid-256`, 256 quads a side, stretched to
--- `GridSpan` metres -- about 0.37 m a quad. Nine of them, the middle one under
--- the boat, snapped to the quad spacing so the vertices never slide under the
--- waves they carry.
local GridSpan = 96
local GridQuads = 256
--- How far the waves may lift the grid before a grid is culled while its crests
--- are still on screen: the three amplitudes summed, with room.
local GridHeadroom = 8

--- Deep-water gravity waves: `omega = sqrt(g * k)`, so a long swell outruns a
--- short chop instead of every wave sliding at one speed. It is one `sqrt` at
--- load time and it is the difference between a sea and a tablecloth.
local Gravity = 9.81

--- **The same three waves are written in `content/shaders/ocean.surface.hlsl`**,
--- which draws the water. Change one, change both.
local WaveDefinitions = {
    -- Wavelength (m), amplitude (m), direction (unnormalised), phase offset.
    { Length = 139, Amplitude = 1.9, X = 1.0, Z = 0.15, Phase = 0.0 },
    { Length = 67, Amplitude = 1.15, X = 0.75, Z = -0.66, Phase = 1.7 },
    { Length = 41, Amplitude = 0.62, X = -0.35, Z = 0.94, Phase = 3.9 },
}

-- Flattened into parallel number arrays at load time. The height function runs
-- 1600 times a tick and a nested table index inside that loop is a real cost in
-- an interpreter with no JIT (R16) — this is the same work, arranged so the hot
-- loop only ever indexes numbers.
local WaveCount = #WaveDefinitions
local WaveKx: { number } = table.create(WaveCount) :: { number }
local WaveKz: { number } = table.create(WaveCount) :: { number }
local WaveAmplitude: { number } = table.create(WaveCount) :: { number }
local WaveOmega: { number } = table.create(WaveCount) :: { number }
local WavePhase: { number } = table.create(WaveCount) :: { number }

for index, wave in WaveDefinitions do
    local k = (2 * math.pi) / wave.Length
    local length = math.sqrt(wave.X * wave.X + wave.Z * wave.Z)
    WaveKx[index] = (wave.X / length) * k
    WaveKz[index] = (wave.Z / length) * k
    WaveAmplitude[index] = wave.Amplitude
    WaveOmega[index] = math.sqrt(Gravity * k)
    WavePhase[index] = wave.Phase
end

--- The height of the sea at a world column, at time `t`. Pure: the same three
--- numbers always give the same answer, on any machine, which is what keeps R10
--- intact when this drives a body the replay can see.
local function surfaceHeight(x: number, z: number, t: number): number
    local height = 0
    for index = 1, WaveCount do
        height += WaveAmplitude[index] * math.sin(
            WaveKx[index] * x + WaveKz[index] * z - WaveOmega[index] * t + WavePhase[index]
        )
    end
    return height
end

-- --- The look -----------------------------------------------------------------

local Horizon = Color3.fromRGB(146, 158, 172)

Lighting.ClockTime = 8.4
Lighting.GeographicLatitude = 22
Lighting.Ambient = Color3.fromRGB(26, 34, 46)
Lighting.OutdoorAmbient = Color3.fromRGB(26, 34, 46)
Lighting.Brightness = 2.6
-- Auto exposure is on by default, and an ocean is a big even field of one
-- brightness -- exactly the scene a metering system lifts until the water is
-- pale. This is the in-game correction, so the example looks right without
-- anybody passing `--no-auto-exposure` on the command line. The number was
-- found by measuring the water's pixels against a render with metering off,
-- not by looking: at -0.85 the image still read as "about right" by eye and was
-- a third of the way there.
Lighting.ExposureCompensation = -0.7
Lighting.FogColor = Horizon
-- Closing at 62 m, well inside the grid's 78 m half-extent. Ending the fog
-- nearer the edge was tried and the far plates then silhouette against the sky
-- as a row of rectangles: a haze that has not finished by the time the water
-- runs out is worse than one that finishes early.
Lighting.FogStart = 34
Lighting.FogEnd = 62

-- --- The ocean ----------------------------------------------------------------

local ocean = Instance.new("Folder")
ocean.Name = "Ocean"
ocean.Parent = workspace

local Water = Material.load("asset://materials/sea.material.json")

--- The nine grids, row-major, the middle one under the boat.
local Grids: { MeshPart } = table.create(9) :: { MeshPart }
for index = 1, 9 do
    local grid = Instance.new("MeshPart")
    grid.Name = `Water{index}`
    grid.MeshContent = `luaug://mesh/grid-{GridQuads}`
    grid.Size = vector.create(GridSpan, GridHeadroom, GridSpan)
    grid.Material = Water
    grid.Anchored = true
    -- Scenery for the eye: the cargo is floated by the wave function, not by
    -- touching the water.
    grid.CanCollide = false
    grid.CanQuery = false
    grid.Parent = ocean
    Grids[index] = grid
end

-- --- The boat -----------------------------------------------------------------

--- The thing the script actually moves. Invisible and intangible; its only job
--- is to be `Part0` of the welds, because a welded part is DRIVEN and a driven
--- body is kinematic rather than static — which is what lets the hull carry
--- what is standing on it instead of passing through it.
local helm = Instance.new("Part")
helm.Name = "Helm"
helm.Size = vector.create(0.5, 0.5, 0.5)
helm:SetMaterialParameter("Transparency", 1)
helm.Anchored = true
helm.CanCollide = false
helm.CanQuery = false
helm.Parent = workspace

local HullColor = Color3.fromRGB(112, 72, 48)
local DeckColor = Color3.fromRGB(182, 148, 102)
local SailColor = Color3.fromRGB(226, 222, 210)

--- Welds `part` to the helm at `offset`. Every piece of the boat hangs off the
--- helm directly rather than off the piece before it: welds form a graph and a
--- chain that closes on itself is refused, so a star has no ordering question
--- to get wrong.
local function boatPiece(name: string, offset: CFrame, size: vector, color: Color3, shape: EnumPartShape): Part
    local part = Instance.new("Part")
    part.Name = name
    part.Shape = shape
    part.Size = size
    part:SetMaterialParameter("Color", color)
    part.CFrame = offset
    part.Parent = workspace

    local weld = Instance.new("Weld")
    weld.Part0 = helm
    weld.Part1 = part
    weld.C0 = offset
    weld.Parent = helm
    return part
end

local BoatLength = 9.5
local BoatBeam = 3.2

boatPiece("Hull", CFrame.new(0, 0, 0), vector.create(BoatBeam, 2.1, BoatLength), HullColor, Enum.PartShape.Block)
boatPiece(
    "Deck",
    CFrame.new(0, 0.85, 0),
    vector.create(BoatBeam - 0.3, 0.2, BoatLength - 0.4),
    DeckColor,
    Enum.PartShape.Block
)
-- The wedge's tall face points at -Z and -Z is forward, so an unrotated one is
-- already a bow. Turning it would give a boat that sails stern first.
boatPiece(
    "Bow",
    CFrame.new(0, 0, -(BoatLength * 0.5 + 1.3)) * CFrame.fromEuler(0, math.pi, 0, Enum.RotationOrder.YXZ),
    vector.create(BoatBeam, 2.1, 2.6),
    HullColor,
    Enum.PartShape.Wedge
)
boatPiece("Cabin", CFrame.new(0, 1.75, 2.6), vector.create(1.9, 1.2, 2.2), DeckColor, Enum.PartShape.Block)
boatPiece("Mast", CFrame.new(0, 4.3, -1.2), vector.create(0.26, 6.4, 0.26), HullColor, Enum.PartShape.Cylinder)
-- A square sail, across the beam. A fore-and-aft one is more of a boat and was
-- tried: seen from astern, which is where the chase camera lives, it is a sliver
-- one pixel wide.
boatPiece("Sail", CFrame.new(0, 3.9, -0.55), vector.create(3.0, 4.0, 0.12), SailColor, Enum.PartShape.Block)
boatPiece("Yard", CFrame.new(0, 5.95, -1.2), vector.create(3.6, 0.16, 0.16), HullColor, Enum.PartShape.Block)

-- --- The cargo ----------------------------------------------------------------
--
-- These ARE simulated. Gravity pulls them down, the wave function pushes them
-- up, and where the two balance is Archimedes' answer and not a number anybody
-- tuned: a crate settles with `Density / WaterDensity` of itself under water.

local WaterDensity = 1.0
--- How hard the water resists a body moving through it, per second, at full
--- submersion. Without it a crate is a spring with no losses and oscillates
--- forever; this is what makes one settle.
local WaterDrag = 2.6
--- A slow surface current, in metres per second. It is what makes floating
--- things drift apart into a scatter rather than staying in the tidy line they
--- were dropped in.
local Current = vector.create(0.55, 0, -0.2)

local flotsam = Instance.new("Folder")
flotsam.Name = "Flotsam"
flotsam.Parent = workspace

local Floaters: { BasePart } = {}

local function addFloater(
    name: string,
    position: vector,
    size: vector,
    density: number,
    color: Color3,
    shape: EnumPartShape
): Part
    local part = Instance.new("Part")
    part.Name = name
    part.Shape = shape
    part.Size = size
    part:SetMaterialParameter("Color", color)
    part.Density = density
    part.CFrame = CFrame.new(position)
    part.Parent = flotsam
    table.insert(Floaters, part)
    return part
end

local CrateColor = Color3.fromRGB(186, 140, 82)
local BarrelColor = Color3.fromRGB(138, 96, 62)

for index = 0, 7 do
    local angle = index * 0.7854
    local radius = 14 + index * 2.5
    addFloater(
        `Crate{index}`,
        vector.create(math.cos(angle) * radius, 3, math.sin(angle) * radius),
        vector.create(1.6, 1.6, 1.6),
        -- Spread across the interesting range on purpose: 0.35 rides high like
        -- balsa, 0.85 sits low like a waterlogged one, and the difference is
        -- visible from the deck.
        0.35 + index * 0.07,
        CrateColor,
        Enum.PartShape.Block
    )
end

for index = 0, 3 do
    addFloater(
        `Barrel{index}`,
        vector.create(-10 - index * 4, 3, 8 + index * 3),
        vector.create(1.3, 1.9, 1.3),
        0.48,
        BarrelColor,
        Enum.PartShape.Cylinder
    )
end

-- --- The camera ---------------------------------------------------------------

local camera = Instance.new("Camera")
camera.Name = "ChaseCamera"
camera.FieldOfView = 68
camera.NearPlane = 0.15
camera.FarPlane = 900
camera.Parent = workspace
workspace.CurrentCamera = camera

local CameraDistance = 17
local CameraTurnSpeed = 2.0

-- --- Input --------------------------------------------------------------------

-- Sailing is a decision, so it is on the simulation clock where the replay can
-- see it (ADR 0039). The camera is not, so it is not.
local Gameplay = Instance.new("InputContext")
Gameplay.Name = "Gameplay"
Gameplay.Parent = workspace

local SailAction = Instance.new("InputAction")
SailAction.Name = "Sail"
SailAction.Type = Enum.InputActionType.Direction2D
SailAction.Parent = Gameplay

local SailKeys = Instance.new("InputBinding")
SailKeys.Up = Enum.KeyCode.W
SailKeys.Down = Enum.KeyCode.S
SailKeys.Left = Enum.KeyCode.A
SailKeys.Right = Enum.KeyCode.D
SailKeys.DisplayName = "WASD"
SailKeys.Parent = SailAction

local SailStick = Instance.new("InputBinding")
SailStick.KeyCode = Enum.KeyCode.LeftThumbstick
SailStick.Parent = SailAction

local DropAction = Instance.new("InputAction")
DropAction.Name = "DropCrate"
DropAction.Type = Enum.InputActionType.Bool
DropAction.Parent = Gameplay

local DropKey = Instance.new("InputBinding")
DropKey.KeyCode = Enum.KeyCode.Space
DropKey.DisplayName = "Space"
DropKey.Parent = DropAction

local DropButton = Instance.new("InputBinding")
DropButton.KeyCode = Enum.KeyCode.ButtonSouth
DropButton.Parent = DropAction

local CameraControls = Instance.new("InputContext")
CameraControls.Name = "CameraControls"
CameraControls.Rate = Enum.InputRate.Render
CameraControls.Parent = workspace

local OrbitAction = Instance.new("InputAction")
OrbitAction.Name = "Orbit"
OrbitAction.Type = Enum.InputActionType.Direction2D
OrbitAction.Parent = CameraControls

local OrbitKeys = Instance.new("InputBinding")
OrbitKeys.Up = Enum.KeyCode.Up
OrbitKeys.Down = Enum.KeyCode.Down
OrbitKeys.Left = Enum.KeyCode.Left
OrbitKeys.Right = Enum.KeyCode.Right
OrbitKeys.Parent = OrbitAction

local OrbitStick = Instance.new("InputBinding")
OrbitStick.KeyCode = Enum.KeyCode.RightThumbstick
OrbitStick.Parent = OrbitAction

-- --- State --------------------------------------------------------------------

--- The wave clock. Accumulated from the fixed tick rather than read from a
--- wall clock, because a wall clock is exactly what R10 forbids and because two
--- machines must agree on where the sea is.

--- Where the boat is and which way it points. Two numbers and a vector rather
--- than a CFrame, because heave and roll are read off the sea every tick and
--- would overwrite anything stored here.
local boatX = 0
local boatZ = 0
local boatHeading = 0
local boatSpeed = 0

local cameraYaw = 0
local cameraHeight = 8

local BoatTopSpeed = 9.5
local BoatAcceleration = 3.2
local BoatTurnRate = 0.85

local droppedCrates = 0
local dropHeld = false

-- --- The tick -----------------------------------------------------------------

RunService.Heartbeat:Connect(function(dt: number)
    -- The clock the water is drawn at, which is what keeps the two agreeing.
    local elapsed = RunService.SimTime

    -- --- Sailing ---------------------------------------------------------------

    local sail = SailAction:GetState() :: Vector2

    -- Speed first, so steering authority can depend on it: a boat dead in the
    -- water does not turn, which is the one thing that makes a boat feel like a
    -- boat rather than like a car.
    local wanted = sail.Y * BoatTopSpeed
    boatSpeed += (wanted - boatSpeed) * math.min(1, BoatAcceleration * dt)
    boatHeading -= sail.X * BoatTurnRate * dt * math.clamp(boatSpeed / BoatTopSpeed, -1, 1)

    local forward = vector.create(-math.sin(boatHeading), 0, -math.cos(boatHeading))
    local right = vector.create(math.cos(boatHeading), 0, -math.sin(boatHeading))

    boatX += forward.x * boatSpeed * dt
    boatZ += forward.z * boatSpeed * dt

    -- The hull spans the sea rather than sitting on one point of it. Four
    -- samples give the average height under the boat and the two slopes across
    -- it, so a nine-metre hull ignores a wave shorter than itself instead of
    -- following every ripple like a cork.
    local halfLength = BoatLength * 0.5
    local halfBeam = BoatBeam * 0.5
    local fore = surfaceHeight(boatX + forward.x * halfLength, boatZ + forward.z * halfLength, elapsed)
    local aft = surfaceHeight(boatX - forward.x * halfLength, boatZ - forward.z * halfLength, elapsed)
    local port = surfaceHeight(boatX + right.x * halfBeam, boatZ + right.z * halfBeam, elapsed)
    local starboard = surfaceHeight(boatX - right.x * halfBeam, boatZ - right.z * halfBeam, elapsed)

    local pitchSlope = (fore - aft) / BoatLength
    local rollSlope = (port - starboard) / BoatBeam

    -- The surface normal, built from the two slopes rather than from a cross
    -- product of sampled points: same answer, no square roots until the end.
    local up = vector.normalize(vector.create(0, 1, 0) - forward * pitchSlope - right * rollSlope)

    -- Re-orthogonalised against that normal, so the hull lies ALONG the water
    -- rather than through it. `fromMatrix`'s third axis is back, and back is
    -- -look, which is the one sign in this file worth checking twice.
    local hullRight = vector.normalize(vector.cross(forward, up))
    local hullForward = vector.cross(up, hullRight)
    local centre = (fore + aft + port + starboard) * 0.25

    -- Riding a touch low, so the waterline sits on the hull instead of under it.
    local boatPosition = vector.create(boatX, centre - 0.35, boatZ)
    helm.CFrame = CFrame.fromMatrix(boatPosition, hullRight, up, -hullForward)

    -- --- The water -------------------------------------------------------------

    -- Snapped to the quad spacing: the grids follow the boat, but every vertex
    -- lands where a vertex was, so the surface does not slide under its waves.
    local cell = GridSpan / GridQuads
    local originX = math.floor(boatX / cell + 0.5) * cell
    local originZ = math.floor(boatZ / cell + 0.5) * cell
    for row = 0, 2 do
        for column = 0, 2 do
            Grids[row * 3 + column + 1].CFrame =
                CFrame.new(originX + (column - 1) * GridSpan, 0, originZ + (row - 1) * GridSpan)
        end
    end

    -- --- Buoyancy --------------------------------------------------------------

    for _, floater in Floaters do
        local position = floater.Position
        local size = floater.Size
        local halfHeight = size.y * 0.5
        local surface = surfaceHeight(position.x, position.z, elapsed)

        -- How much of it is under water, 0 to 1. Linear in depth, which is exact
        -- for a box and close enough for a barrel.
        local submerged = math.clamp((surface - (position.y - halfHeight)) / size.y, 0, 1)
        if submerged <= 0 then
            continue
        end

        local volume = size.x * size.y * size.z
        local mass = floater.Density * volume

        -- Archimedes: the weight of the water pushed out of the way. Nothing
        -- here was tuned to make things float — where it balances gravity IS
        -- where the thing floats.
        local lift = WaterDensity * volume * submerged * Gravity

        -- Drag and current are forces too, so all three become one impulse: an
        -- impulse is a force times the time it acted for, and that time is `dt`.
        local velocity = floater.LinearVelocity
        local drag = velocity * (-WaterDrag * submerged * mass)
        local push = Current * (submerged * mass * 1.4)

        floater:ApplyImpulse((vector.create(0, lift, 0) + drag + push) * dt)
    end

    -- --- Dropping cargo --------------------------------------------------------

    -- Edge-detected by hand rather than through `Pressed`, because the action is
    -- read here anyway and one boolean is cheaper than a signal round trip.
    local dropWanted = DropAction:GetState() :: boolean
    if dropWanted and not dropHeld and droppedCrates < 24 then
        droppedCrates += 1
        local drop = boatPosition + hullRight * (halfBeam + 1.2) + vector.create(0, 3, 0)
        addFloater(
            `Dropped{droppedCrates}`,
            drop,
            vector.create(1.4, 1.4, 1.4),
            0.42,
            Color3.fromRGB(206, 118, 74),
            Enum.PartShape.Block
        )
    end
    dropHeld = dropWanted

    -- --- The camera ------------------------------------------------------------

    local orbit = OrbitAction:GetState() :: Vector2
    cameraYaw += CameraTurnSpeed * orbit.X * dt
    cameraHeight = math.clamp(cameraHeight + 7 * orbit.Y * dt, 2.5, 26)

    -- Behind the boat's HEADING, not behind its hull: chasing the hull would
    -- put the camera through every wave the boat rolls into.
    local chase = vector.create(-math.sin(boatHeading + cameraYaw), 0, -math.cos(boatHeading + cameraYaw))
    local focus = vector.create(boatX, centre + 2.2, boatZ)
    local target = focus - chase * CameraDistance + vector.create(0, cameraHeight, 0)

    -- Held above the water whatever the orbit asks for, so the camera never
    -- ends up inside the sea looking at the underside of the tiles.
    local floor = surfaceHeight(target.x, target.z, elapsed) + 1.4
    if target.y < floor then
        target = vector.create(target.x, floor, target.z)
    end

    local eye = camera.CFrame.Position
    eye += (target - eye) * math.min(1, 5 * dt)
    camera.CFrame = CFrame.lookAt(eye, focus)
end)

DebugService:ShowPanel("Stats")

print("11-ocean: W/S throttle, A/D steer, arrows orbit, Space drops a crate over the side")
