--!strict
--[[
	Sculpts the middle of the island as TERRAIN (F1, phase-2-4-plan H3), for
	`tools/merge_ground.luau` to put into the flagship's scene.

	**Why the flagship gets terrain at all.** Its ground was eleven thousand
	boxes, each 16 m across with its top at the island's height at its centre --
	a staircase the character climbs one step at a time and cannot dig into.
	The middle 512 m, all around where a game starts, is now one `Terrain`:
	the same island function, sampled every metre instead of every sixteen,
	with a hill and a tunnel through it that boxes could not have been. That is
	the region a player sees first and walks most, and it is sixty-four
	streamed cells -- so the flagship streams terrain the way it streams
	everything else, which is the end-to-end proof the plan asked for.

	**A project of its own, run once, rather than part of the game.** The
	engine captures the world a script builds with `--save-scene`, and that is
	how this becomes a file (ADR 0047): a game that sculpted its ground every
	time it booted would spend seconds on it, and could not stream it.

	Run from the repository root, then merge:

	    luaug-host examples/10-open-world/tools/sculpt-ground --save-scene=<ground.scene.json>
	    lute examples/10-open-world/tools/merge_ground.luau <ground.scene.json>
]]

local Workspace = game:GetService("Workspace")

--- Half the square the terrain covers, in metres. **It moves with
--- `TerrainRegion` in `generate_world.luau` and `merge_ground.luau`**, which
--- leave out the boxes under it.
local HalfRegion = 256
--- One metre: the island rises thirteen metres over eight hundred, so a finer
--- lattice would buy nothing a player could see, at four times the file.
local VoxelSize = 1
local Grass = 1
local Rock = 3

--- The island, the third copy of it, with the same note the other two carry:
--- `tools/generate_world.luau` writes the boxes around this from it, and
--- `src/scripts/init.luau` puts a fallen character back on it. All three move
--- together.
local function heightAt(x: number, z: number): number
    local distance = math.sqrt(x * x + z * z)
    local land = math.clamp(1 - distance / 820, 0, 1)
    land = land * land * (3 - 2 * land)
    local seabed = math.clamp((distance - 820) / 260, 0, 1)
    seabed = seabed * seabed * (3 - 2 * seabed)
    return 13 * land + 1.4 * math.sin(x / 400) * math.cos(z / 360) * land - 9 * seabed
end

local terrain = Instance.new("Terrain")
terrain.Name = "Ground"
terrain.VoxelSize = VoxelSize
terrain.MinHeight = -32
terrain.MaxHeight = 64
terrain.Parent = Workspace

-- **The island, as a heightmap, in one call.** One height per column, row
-- after row along +Z; the hill's tunnel below is what turns a few columns into
-- voxels.
--
-- Up to +HalfRegion INCLUSIVE: a lattice column is a vertex, and the ground
-- between two vertices is only drawn when both exist, so stopping one short
-- would leave a metre-wide slot along two edges between this and the boxes.
local heights: { number } = {}
local columns = 0
for z = -HalfRegion, HalfRegion, VoxelSize do
    columns = 0
    for x = -HalfRegion, HalfRegion, VoxelSize do
        table.insert(heights, heightAt(x, z))
        columns += 1
    end
end
terrain:WriteHeights(vector.create(-HalfRegion, 0, -HalfRegion), columns, heights, Grass)

-- **A hill with a tunnel through it**, north-east of the spawn and inside the
-- autopilot's 220 m walking circle, so a soak walks past it and never into it.
local HillX, HillZ = 130, -90
local hillBase = heightAt(HillX, HillZ)
-- Three overlapping raises rather than one: a single brush is a cone, and
-- a hill is broader than it is tall.
terrain:RaiseBall(vector.create(HillX, hillBase, HillZ), 48, 9)
terrain:RaiseBall(vector.create(HillX + 12, hillBase, HillZ + 8), 34, 6)
terrain:RaiseBall(vector.create(HillX - 14, hillBase, HillZ - 6), 30, 5)
-- The tunnel's floor a hand below the ground outside, so it is walked into
-- rather than climbed into, and its walls painted rock -- the brush that digs
-- a hole leaves the ground's own material on the walls.
local TunnelRadius = 3
for along = -40, 40, 1 do
    local x = HillX + along
    local z = HillZ + along * 0.35
    local centre = vector.create(x, heightAt(x, z) + TunnelRadius - 0.5, z)
    terrain:PaintBall(centre, TunnelRadius + 0.75, Rock)
    terrain:FillBall(centre, TunnelRadius, 0)
end

print(string.format("sculpted %d cells of ground", terrain.CellCount))
