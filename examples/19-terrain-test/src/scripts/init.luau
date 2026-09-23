--!strict
--- The terrain test scene (the owner's terrain report, 2026-09-23).
---
--- Every controlled case the report asked for, side by side on one terrain,
--- each on its own block of ground so its walls and its bottom show as well as
--- its top: flat ground, a slope, a hill, a ball added, a ball taken away, a
--- tunnel, a cave, a vertical wall, an overhang, and a ball across a chunk
--- corner. A sign over each names it.
---
--- The camera visits every case from above, from the side and from below, and
--- loops. To look at the MESH rather than the picture, set `ShowMesh` below:
--- the terrain's triangles are drawn green where a triangle faces the way its
--- normals do and red where it does not, with a yellow line along each vertex
--- normal. In the editor the same two views are `Terrain wireframe` and
--- `Terrain normals`.

local DebugService = game:GetService("DebugService")
local Lighting = game:GetService("Lighting")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

local ShowMesh = false

local Grass = 1
local Sand = 2
local Rock = 3
local Air = 0

-- Each case stands on a block of ground this wide, this far apart.
local BlockSize = 24
local Spacing = 40

Lighting.ClockTime = 14.5

local terrain = Instance.new("Terrain")
terrain.Name = "Ground"
terrain.MinHeight = -48
terrain.MaxHeight = 48
terrain.Parent = Workspace

local function block(x: number, depth: number, material: number)
    terrain:FillBlock(vector.create(x, -depth / 2, 0), vector.create(BlockSize, depth, BlockSize), material)
end

local function sign(x: number, height: number, text: string)
    local post = Instance.new("Part")
    post.Name = "Sign " .. text
    post.Anchored = true
    post.Transparency = 1
    post.CanCollide = false
    post.Size = vector.create(0.2, 0.2, 0.2)
    post.Position = vector.create(x, height, -BlockSize / 2 - 2)
    post.Parent = Workspace

    local board = Instance.new("BillboardGui")
    board.Size = UDim2.new(0, 220, 0, 40)
    board.Parent = post

    local label = Instance.new("TextLabel")
    label.Size = UDim2.new(1, 0, 1, 0)
    label.BackgroundColor = Color3.fromRGB(10, 12, 18)
    label.BackgroundTransparency = 0.3
    label.TextColor = Color3.fromRGB(255, 255, 255)
    label.TextSize = 20
    label.Text = text
    label.Parent = board
end

type Case = { name: string, x: number, build: (x: number) -> () }

local cases: { Case } = {
    {
        name = "Flat surface",
        x = 0,
        build = function(x)
            block(x, 12, Grass)
        end,
    },
    {
        name = "Slope",
        x = Spacing,
        build = function(x)
            local columns = BlockSize
            local heights = table.create(columns * columns, 0)
            for row = 0, columns - 1 do
                for column = 0, columns - 1 do
                    heights[row * columns + column + 1] = (column - columns / 2) * 0.6
                end
            end
            terrain:WriteHeights(vector.create(x - columns / 2, 0, -columns / 2), columns, heights, Grass)
        end,
    },
    {
        name = "Hill",
        x = 2 * Spacing,
        build = function(x)
            block(x, 12, Grass)
            for _ = 1, 5 do
                terrain:RaiseBall(vector.create(x, 0, 0), 9, 1.5)
            end
        end,
    },
    {
        name = "Sphere addition",
        x = 3 * Spacing,
        build = function(x)
            block(x, 12, Grass)
            terrain:FillBall(vector.create(x, 0, 0), 5, Rock)
        end,
    },
    {
        name = "Sphere subtraction",
        x = 4 * Spacing,
        build = function(x)
            block(x, 12, Grass)
            terrain:FillBall(vector.create(x, 0, 0), 5, Air)
        end,
    },
    {
        name = "Tunnel",
        x = 5 * Spacing,
        build = function(x)
            block(x, 16, Rock)
            for step = -BlockSize / 2 - 2, BlockSize / 2 + 2, 1.5 do
                terrain:FillBall(vector.create(x + step, -7, 0), 3, Air)
            end
        end,
    },
    {
        name = "Cave",
        x = 6 * Spacing,
        build = function(x)
            block(x, 16, Rock)
            terrain:FillBall(vector.create(x, -8, 0), 6, Air)
            -- A way in from above, so the inside is reachable and lit a little.
            for y = 0, -4, -1 do
                terrain:FillBall(vector.create(x + 4, y, 3), 2, Air)
            end
            terrain:FillBlock(vector.create(x, -13, 0), vector.create(10, 1, 10), Sand)
        end,
    },
    {
        name = "Vertical wall",
        x = 7 * Spacing,
        build = function(x)
            block(x, 6, Grass)
            terrain:FillBlock(vector.create(x, 6, 0), vector.create(4, 12, BlockSize - 4), Rock)
        end,
    },
    {
        name = "Overhang",
        x = 8 * Spacing,
        build = function(x)
            block(x, 12, Grass)
            terrain:FillBlock(vector.create(x - 4, 5, 0), vector.create(8, 10, 12), Rock)
            terrain:FillBall(vector.create(x + 1, 9, 0), 5, Rock)
        end,
    },
    {
        name = "Chunk boundary",
        -- A multiple of the 32-voxel chunk, so the ball below sits on the
        -- corner of four chunks in x and z and on a chunk layer in y.
        x = 352,
        build = function(x)
            block(x, 12, Grass)
            terrain:FillBall(vector.create(x, 0, 0), 6, Air)
            terrain:FillBall(vector.create(x + 0.4, -0.6, 0.4), 3, Sand)
        end,
    },
}

for _, case in cases do
    case.build(case.x)
    sign(case.x, 8, case.name)
end

print(string.format("terrain test: %d cases, %d cell(s) of terrain", #cases, terrain.CellCount))

if ShowMesh then
    DebugService:ShowPanel("Terrain")
end

-- **The tour**: each case from above, from the side and from below.
type Key = { at: vector, look: vector }
local keys: { Key } = {}
for _, case in cases do
    local x = case.x
    table.insert(keys, { at = vector.create(x - 20, 18, 26), look = vector.create(x, 0, 0) })
    table.insert(keys, { at = vector.create(x + 22, 1, 12), look = vector.create(x, -2, 0) })
    table.insert(keys, { at = vector.create(x - 12, -24, 18), look = vector.create(x, -6, 0) })
end
local SecondsPerKey = 2.5

local camera = Instance.new("Camera")
camera.Name = "Camera"
camera.CFrame = CFrame.lookAt(keys[1].at, keys[1].look)
camera.Parent = Workspace
Workspace.CurrentCamera = camera

local elapsed = 0
RunService.Heartbeat:Connect(function(dt: number)
    elapsed += dt
    local span = elapsed / SecondsPerKey
    local index = math.floor(span) % #keys
    local from = keys[index + 1]
    local to = keys[(index + 1) % #keys + 1]
    local t = span - math.floor(span)
    t = t * t * (3 - 2 * t)
    camera.CFrame = CFrame.lookAt(from.at + (to.at - from.at) * t, from.look + (to.look - from.look) * t)
end)
