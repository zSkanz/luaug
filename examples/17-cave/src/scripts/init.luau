--!strict
--[[
	A cave dug into the side of a mountain (F1, ADR 0082).

	**What a height map cannot hold.** A tunnel has rock above it and floor
	below it, so the ground there is not one height per column. Terrain is a
	grid of voxels, so a tunnel is the same data as the hillside it runs into:
	every hole here is a `FillBall` with material zero, which is exactly what
	the editor's carving brush does one stamp at a time.

	The camera flies from the plain, through the mouth, down the tunnel and into
	the chamber, and back, so the whole shape can be seen without a mouse.
]]

local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

--- Palette ids (`engine/asset/src/terrain_palette.cpp`); zero removes.
local Grass = 1
local Sand = 2
local Rock = 3
local Air = 0

Lighting.ClockTime = 15.5
-- **A cave is dim, not black.** `Ambient` is the light of enclosed spaces and
-- `OutdoorAmbient` that of the open air (ADR 0084): the plain keeps the default,
-- and the cave gets a low cool light of its own -- enough to see the walls by
-- -- while the crystals and the torch still do the lighting inside.
Lighting.Ambient = Color3.fromRGB(14, 15, 20)

local terrain = Instance.new("Terrain")
terrain.Name = "Ground"
-- Reserved before anything is sculpted: the collider spreads its precision
-- across this range.
terrain.MinHeight = -32
terrain.MaxHeight = 64
terrain.Parent = Workspace

-- **The plain, from below the floor up to zero**, so it is ground rather than
-- a slab with air under it (the rule example 13 explains).
terrain:FillBlock(vector.create(0, -20, 0), vector.create(192, 40, 192), Grass)

-- **The mountain**: balls added to the top of ground only move its surface up,
-- so all of this is still one height per column.
terrain:FillBall(vector.create(0, -4, 0), 30, Rock)
terrain:FillBall(vector.create(-14, 0, 12), 20, Rock)
terrain:FillBall(vector.create(10, 2, -14), 18, Rock)

-- **The tunnel**, dug in from the mountain's east face: a chain of balls from
-- outside the rock to deep inside it, bending a little on the way so the mouth
-- is not a straight view of the back wall. Its floor stays above the plain,
-- so every column it crosses has rock over air over rock -- a cave.
local TunnelRadius = 3.2
local steps = 28
for i = 0, steps do
    local t = i / steps
    local x = 36 - 30 * t
    local z = math.sin(t * math.pi) * 5
    local y = 3.6 + t * 1.2
    terrain:FillBall(vector.create(x, y, z), TunnelRadius, Air)
end

-- **The chamber** at its end, wider than it is tall: two overlapping balls, a
-- floor that stays level with the tunnel's.
terrain:FillBall(vector.create(2, 8, 0), 9, Air)
terrain:FillBall(vector.create(-5, 7, -3), 7, Air)
-- The floor put back where the balls dug below the tunnel's, flat, and sand.
terrain:FillBlock(vector.create(-2, 1.5, -1), vector.create(24, 3, 22), Sand)

-- **A shaft from the summit down into the chamber**, so daylight reaches it.
for y = 32, 12, -1.5 do
    terrain:FillBall(vector.create(-3, y, 2), 2.6, Air)
end

print(string.format("cave: %d cell(s) of terrain", terrain.CellCount))

-- Light where the sun does not reach: glowing crystals on the chamber floor,
-- and a lamp at the tunnel's bend.
local function lamp(name: string, at: vector, color: Color3, brightness: number, range: number)
    local stone = Instance.new("Part")
    stone.Name = name
    stone.Anchored = true
    stone.Size = vector.create(0.6, 1.4, 0.6)
    stone.Position = at
    stone.Color = color
    stone.Parent = Workspace
    local light = Instance.new("PointLight")
    light.Color = color
    light.Brightness = brightness
    light.Range = range
    light.Parent = stone
end
lamp("CrystalA", vector.create(4, 3.7, 3), Color3.fromRGB(90, 200, 255), 6, 18)
lamp("CrystalB", vector.create(-7, 3.7, -5), Color3.fromRGB(160, 110, 255), 6, 16)
lamp("Torch", vector.create(20, 2.6, 4.5), Color3.fromRGB(255, 170, 90), 5, 14)

-- **The fly-through.** Key frames of (where, looking at), eased between, and
-- looped: the plain, the mouth, the bend, the chamber, back out.
type Key = { at: vector, look: vector }
local keys: { Key } = {
    { at = vector.create(70, 16, 30), look = vector.create(30, 5, 0) },
    { at = vector.create(46, 6, 4), look = vector.create(30, 4, 0) },
    { at = vector.create(30, 4.4, 1), look = vector.create(18, 4.4, 5) },
    { at = vector.create(16, 4.6, 5), look = vector.create(2, 5, 0) },
    { at = vector.create(9, 6, 2), look = vector.create(-6, 4, -3) },
    { at = vector.create(5, 5, -4), look = vector.create(-3, 14, 2) },
}
local SecondsPerKey = 3

local camera = Instance.new("Camera")
camera.Name = "Camera"
camera.CFrame = CFrame.lookAt(keys[1].at, keys[1].look)
camera.Parent = Workspace
Workspace.CurrentCamera = camera

local function ease(t: number): number
    return t * t * (3 - 2 * t)
end

local elapsed = 0
RunService.Heartbeat:Connect(function(dt: number)
    elapsed += dt
    -- There and back: forward through the keys, then the same way out.
    local span = (#keys - 1) * SecondsPerKey
    local phase = elapsed % (span * 2)
    local along = if phase <= span then phase else span * 2 - phase
    local index = math.min(math.floor(along / SecondsPerKey) + 1, #keys - 1)
    local t = ease((along - (index - 1) * SecondsPerKey) / SecondsPerKey)
    local from, to = keys[index], keys[index + 1]
    camera.CFrame = CFrame.lookAt(from.at + (to.at - from.at) * t, from.look + (to.look - from.look) * t)
end)
