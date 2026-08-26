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
---   1. **The water is opaque, deliberately.** The renderer batches identical
---      geometry into one instanced draw (ADR 0043) and explicitly refuses to
---      batch transparent draws — "their ORDER is their correctness". A
---      translucent sea of 1600 tiles is 1600 draw calls; an opaque one is a
---      handful. Deep water is nearly opaque anyway, so this costs the look
---      almost nothing and buys the whole frame budget back.
---   2. **A fixed pool of tiles follows the boat.** The grid is allocated once
---      and its origin is snapped to the tile size every tick, so the ocean is
---      unbounded while the part count never changes. Nothing is created or
---      destroyed after startup.
---   3. **Position is exact every tick; colour is not.** The tile transforms
---      must all be right or the surface tears, but the crest tint is a slow
---      gradient nobody can time — so it is refreshed a quarter of the grid at a
---      time. That asymmetry is where the interpreter budget actually goes
---      (R16), and it is worth a comment because it is invisible in a
---      screenshot.
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

--- Metres of water per tile, and how many tiles on a side. 676 tiles of 6 m is
--- 156 m of ocean; the fog closes at 62 m, well inside the 78 m half-extent, so the
--- grid's edge is never a thing you can see.
---
--- **These two numbers are the example's entire performance budget, and they
--- were set by measuring.** The ceiling is not the renderer -- 676 tiles draw in
--- one instanced batch and the whole frame is 83 draws -- and it is not the
--- wave arithmetic either. It is the property write: moving one part from Luau
--- costs about **4.7 microseconds**, measured by taking the write out and
--- leaving everything else in, so a thousand moving parts is five milliseconds
--- before anything has been drawn.
---
--- So: raise `TileSize` to cover the same water with fewer plates, and the
--- crests get blockier. The shortest wave below is 41 m, so at 6 m there are
--- seven tiles across it. Past about 8 m it aliases, and a wave that aliases
--- does not look like a coarser wave -- it looks like the sea is crawling.
---
--- The plates are THIN and TILTED rather than thick and flat. A stepped surface
--- of level boxes is a grid of little cliffs, and a low sun draws a shadow off
--- every one of them: the first version of this example looked like tiled
--- bathroom floor for exactly that reason.
local TileSize = 6
local GridSide = 26
local TileThickness = 0.5

--- Deep-water gravity waves: `omega = sqrt(g * k)`, so a long swell outruns a
--- short chop instead of every wave sliding at one speed. It is one `sqrt` at
--- load time and it is the difference between a sea and a tablecloth.
local Gravity = 9.81

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
local WaveKx: { number } = table.create(WaveCount)
local WaveKz: { number } = table.create(WaveCount)
local WaveAmplitude: { number } = table.create(WaveCount)
local WaveOmega: { number } = table.create(WaveCount)
local WavePhase: { number } = table.create(WaveCount)

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

--- The sum of every amplitude: the highest the sea can possibly reach, used to
--- normalise the crest tint so the colour ramp does not depend on the numbers
--- above staying what they are today.
local WaveCeiling = 0
for index = 1, WaveCount do
    WaveCeiling += WaveAmplitude[index]
end

-- --- The look -----------------------------------------------------------------

local Trough = Color3.fromRGB(6, 26, 54)
local Crest = Color3.fromRGB(54, 114, 150)
local Horizon = Color3.fromRGB(146, 158, 172)

--- **Colour is part of a material, and a material is what a batch is keyed on.**
--- Tinting every tile by its own height looked free — the model matrix is a
--- per-instance vertex attribute, so surely the colour is too — and measuring it
--- said otherwise: 3629 draws and 14.2 ms with a continuous ramp against 47
--- draws and 6.7 ms without one. So the ramp is QUANTISED: twelve colours are
--- twelve materials and therefore twelve batches, which is a cost measured in
--- draw calls rather than in milliseconds.
---
--- Twelve and not seven because seven BANDS. The steps land on plates six
--- metres wide, and at seven the eye picks out the rectangles; the fix was to
--- spend more materials, which the measurement said were the cheap thing.
---
--- This is the whole reason to have measured rather than reasoned: the vertex
--- layout genuinely does carry a per-instance colour, and it is not the thing
--- that decides the batch.
local PaletteSize = 12
local Palette: { Color3 } = table.create(PaletteSize)
for step = 1, PaletteSize do
    local lift = (step - 1) / (PaletteSize - 1)
    Palette[step] = Color3.new(
        Trough.R + (Crest.R - Trough.R) * lift,
        Trough.G + (Crest.G - Trough.G) * lift,
        Trough.B + (Crest.B - Trough.B) * lift
    )
end

Lighting.ClockTime = 8.4
Lighting.GeographicLatitude = 22
Lighting.Ambient = Color3.fromRGB(26, 34, 46)
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

--- Every tile, in row-major order, allocated once. `GridSide * GridSide` parts
--- is the entire cost of the water for the rest of the session.
local Tiles: { Part } = table.create(GridSide * GridSide)
--- Which palette step each tile is wearing. A tile only pays for a colour write
--- when its step actually changes, which on a slow swell is a small fraction of
--- the grid on any given tick.
local TileLevel: { number } = table.create(GridSide * GridSide)
--- This tick's height under every tile. Filled first, read second: the tilt of a
--- plate is the slope between its NEIGHBOURS, and those are heights the grid has
--- already paid for. Taking the derivative analytically instead means a
--- `math.cos` per wave per tile on top of the `math.sin`, and that measured at
--- five milliseconds a frame -- for a number two array reads already answer.
local Heights: { number } = table.create(GridSide * GridSide)

for row = 0, GridSide - 1 do
    for column = 0, GridSide - 1 do
        local tile = Instance.new("Part")
        tile.Name = `Water{row}_{column}`
        -- Wider than the spacing, because a TILTED plate covers less ground
        -- than a level one and the corners would otherwise open into gaps.
        tile.Size = vector.create(TileSize * 1.22, TileThickness, TileSize * 1.22)
        tile.Color = Trough
        tile.Anchored = true
        -- The water is scenery for the eye and nothing else: the cargo is
        -- floated by the wave function, not by touching a box. Turning both of
        -- these off keeps 1600 bodies out of the solver's broadphase and out of
        -- every raycast in the example.
        tile.CanCollide = false
        tile.CanQuery = false
        tile.Parent = ocean
        Tiles[row * GridSide + column + 1] = tile
        TileLevel[row * GridSide + column + 1] = 0
        Heights[row * GridSide + column + 1] = 0
    end
end

-- --- The boat -----------------------------------------------------------------

--- The thing the script actually moves. Invisible and intangible; its only job
--- is to be `Part0` of the welds, because a welded part is DRIVEN and a driven
--- body is kinematic rather than static — which is what lets the hull carry
--- what is standing on it instead of passing through it.
local helm = Instance.new("Part")
helm.Name = "Helm"
helm.Size = vector.create(0.5, 0.5, 0.5)
helm.Transparency = 1
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
    part.Color = color
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
    part.Color = color
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
local elapsed = 0

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
    elapsed += dt

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

    -- Snapped to the tile size: the grid follows the boat, but it moves in whole
    -- tiles, so a tile always lands where a tile was. Without the snap the whole
    -- surface would slide under itself and the sea would look like it was on a
    -- conveyor belt.
    local originX = math.floor(boatX / TileSize + 0.5) * TileSize
    local originZ = math.floor(boatZ / TileSize + 0.5) * TileSize
    local half = GridSide * 0.5

    -- Hoisted out of the loop: two thousand iterations is where a table lookup
    -- or a global read stops being free (R16).
    local ceiling = WaveCeiling
    local sink = TileThickness * 0.5
    local levels = PaletteSize
    local sunward = vector.create(0, 0, -1)

    -- Pass one: every height, and nothing else. One `math.sin` per wave per
    -- tile, which is the floor this example cannot go under.
    for row = 0, GridSide - 1 do
        local worldZ = originZ + (row - half) * TileSize
        local rowBase = row * GridSide
        for column = 0, GridSide - 1 do
            Heights[rowBase + column + 1] = surfaceHeight(originX + (column - half) * TileSize, worldZ, elapsed)
        end
    end

    -- Pass two: place them. Split in two rather than branching inside one loop
    -- -- the interior needs four neighbours and the border does not have them,
    -- and a bounds test per tile costs more than the border ring does. The ring
    -- lies flat, which is invisible: it sits 78 metres out and the fog has been
    -- solid since 62.
    --
    -- Leaving the ring UNPLACED is not an option and was tried: every one of the
    -- 172 tiles then stays at the world origin, in a pile, directly under the
    -- camera. It cost four milliseconds a frame and looked like nothing at all.
    local inverseSpan = 1 / (2 * TileSize)
    for row = 1, GridSide - 2 do
        local worldZ = originZ + (row - half) * TileSize
        local rowBase = row * GridSide
        for column = 1, GridSide - 2 do
            local slot = rowBase + column + 1
            local height = Heights[slot]
            local tile = Tiles[slot]

            -- Central differences against the neighbours: the same slope the
            -- derivative would have given, to within the grid spacing, from two
            -- array reads instead of a `math.cos` per wave.
            local slopeX = (Heights[slot + 1] - Heights[slot - 1]) * inverseSpan
            local slopeZ = (Heights[slot + GridSide] - Heights[slot - GridSide]) * inverseSpan

            -- Laid ALONG the water rather than level with the world. Adjacent
            -- plates then meet almost edge to edge, and the surface reads as one
            -- continuous thing instead of as a grid of little cliffs -- which is
            -- what the first version of this example looked like.
            local plateUp = vector.normalize(vector.create(-slopeX, 1, -slopeZ))
            local plateRight = vector.normalize(vector.cross(sunward, plateUp))
            local plateForward = vector.cross(plateUp, plateRight)
            tile.CFrame = CFrame.fromMatrix(
                vector.create(originX + (column - half) * TileSize, height - sink, worldZ),
                plateRight,
                plateUp,
                -plateForward
            )

            -- Crests catch the sky and troughs do not, in seven steps. The
            -- comparison is what makes this cheap: most tiles keep the colour
            -- they already had, and an unchanged property is a write nobody
            -- pays for.
            local level = math.clamp(math.floor((height / ceiling * 0.5 + 0.5) * levels) + 1, 1, levels)
            if TileLevel[slot] ~= level then
                TileLevel[slot] = level
                tile.Color = Palette[level]
            end
        end
    end

    -- The border ring, laid flat.
    local edge = GridSide - 1
    for step = 0, edge do
        local nearZ = originZ - half * TileSize
        local farZ = originZ + (edge - half) * TileSize
        local worldX = originX + (step - half) * TileSize
        Tiles[step + 1].CFrame = CFrame.new(worldX, Heights[step + 1] - sink, nearZ)
        Tiles[edge * GridSide + step + 1].CFrame = CFrame.new(worldX, Heights[edge * GridSide + step + 1] - sink, farZ)
    end
    for row = 1, edge - 1 do
        local worldZ = originZ + (row - half) * TileSize
        local rowBase = row * GridSide
        Tiles[rowBase + 1].CFrame = CFrame.new(originX - half * TileSize, Heights[rowBase + 1] - sink, worldZ)
        Tiles[rowBase + edge + 1].CFrame =
            CFrame.new(originX + (edge - half) * TileSize, Heights[rowBase + edge + 1] - sink, worldZ)
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
