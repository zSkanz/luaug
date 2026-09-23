--!strict
--[[
	A world made of blocks (V1, `VoxelService`).

	Register the block types once, build the world with `FillBlocks`, and play
	it with `SetBlock` and `Raycast`. Everything the engine does with the blocks
	-- the mesh, the corner shading, the collider -- follows from the calls.
]]

local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")
local voxels = game:GetService("VoxelService")

-- Colours are linear, like every colour in this engine. Grass is green on top
-- and earth on its sides; the rest are one colour all round.
local Earth = Color3.new(0.20, 0.12, 0.06)
local Grass = voxels:RegisterBlock("Grass", Color3.new(0.12, 0.30, 0.07), Earth)
local Dirt = voxels:RegisterBlock("Dirt", Earth)
local Stone = voxels:RegisterBlock("Stone", Color3.new(0.22, 0.22, 0.23))
local Wood = voxels:RegisterBlock("Wood", Color3.new(0.25, 0.17, 0.09), Color3.new(0.15, 0.09, 0.04))
local Leaves = voxels:RegisterBlock("Leaves", Color3.new(0.05, 0.18, 0.04))

--- The ground's height at a column: two gentle waves, so there are hills.
local function heightAt(x: number, z: number): number
    return math.floor(6 + 3 * math.sin(x * 0.15) + 2 * math.cos(z * 0.2))
end

-- Stone underneath, three blocks of earth, grass on top.
for x = -32, 31 do
    for z = -32, 31 do
        local h = heightAt(x, z)
        voxels:FillBlocks(vector.create(x, 0, z), vector.create(x, h - 4, z), Stone)
        voxels:FillBlocks(vector.create(x, h - 3, z), vector.create(x, h - 1, z), Dirt)
        voxels:SetBlock(vector.create(x, h, z), Grass)
    end
end

local function tree(x: number, z: number)
    local base = heightAt(x, z) + 1
    voxels:FillBlocks(vector.create(x - 2, base + 3, z - 2), vector.create(x + 2, base + 5, z + 2), Leaves)
    voxels:FillBlocks(vector.create(x, base, z), vector.create(x, base + 4, z), Wood)
end
tree(-6, -4)
tree(-12, 6)
tree(10, -14)

print(string.format("voxels: %d chunk(s), %d block type(s)", voxels.ChunkCount, voxels.BlockTypeCount))

-- Something to stand on the hill, so the collider is visible as behaviour.
local crate = Instance.new("Part")
crate.Name = "Crate"
crate.Size = vector.create(1, 1, 1)
crate.Position = vector.create(0.5, 20, 0.5)
crate.Color = Color3.fromRGB(200, 120, 60)
crate.Parent = Workspace

-- **Mining, as a pickaxe does it**: cast a ray, break the first block it enters.
-- Straight down, three blocks deep per column, walking east across the middle of
-- the world, so a trench opens while you watch.
local tick = 0
local column = -20
local dug = 0
RunService.Heartbeat:Connect(function()
    tick += 1
    if tick % 4 ~= 0 or column > 20 then
        return
    end
    local block = voxels:Raycast(vector.create(column + 0.5, 40, 2.5), vector.create(0, -60, 0))
    if block then
        voxels:SetBlock(block, 0)
    end
    dug += 1
    if dug == 3 then
        dug = 0
        column += 1
    end
end)

local camera = Instance.new("Camera")
camera.Name = "Camera"
camera.CFrame = CFrame.lookAt(vector.create(0, 30, 16), vector.create(0, 6, 2))
camera.Parent = Workspace
Workspace.CurrentCamera = camera
game:GetService("Lighting").ClockTime = 15
