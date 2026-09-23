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

-- **Images for every face, and white so they show as drawn**: a block's
-- colours tint its images. Grass has a top, sides with a fringe of green, and a
-- dirt underside; a log shows rings on its ends and bark all round.
local White = Color3.new(1, 1, 1)
local Grass = voxels:RegisterBlock("Grass", White)
voxels:SetBlockTextures(
    Grass,
    "asset://textures/grass_top.png",
    "asset://textures/grass_side.png",
    "asset://textures/dirt.png"
)
local Dirt = voxels:RegisterBlock("Dirt", White)
voxels:SetBlockTextures(Dirt, "asset://textures/dirt.png")
local Stone = voxels:RegisterBlock("Stone", White)
voxels:SetBlockTextures(Stone, "asset://textures/stone.png")
local Wood = voxels:RegisterBlock("Wood", White)
voxels:SetBlockTextures(
    Wood,
    "asset://textures/log_top.png",
    "asset://textures/log_side.png",
    "asset://textures/log_top.png"
)
local Leaves = voxels:RegisterBlock("Leaves", White)
voxels:SetBlockTextures(Leaves, "asset://textures/leaves.png")

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
