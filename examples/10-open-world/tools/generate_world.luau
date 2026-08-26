--!strict
--[[
	Seeds the flagship's island INTO `content/scenes/main.scene.json`, under
	`Workspace.Scenery`.

	**The world is authored, not generated at build time** (ADR 0053). It used to
	be written as `.chunk.json` sources that `assetc` compiled and the streaming
	system read -- a world the editor had never seen and could not have saved.
	Now the terrain is ordinary instances in the scene, and the engine's
	partitioner turns them into streamed cells when the game runs.

	So this file is a SEED rather than a build step. Run it once to fill an empty
	island; after that the scene is the source and the editor is what edits it.
	Running it again replaces `Scenery` and leaves everything else alone.

	**What makes it work is `Model.StreamingMode`.** A tile is `Nonatomic`, the
	default: each part goes into the cell its own position falls in, and nothing
	has to know a cell exists. A tree and a tower are `Atomic`, because a trunk
	that arrives without its crown is a post.

	**The tiles are 16 m, and that is the walkability budget rather than detail.**
	A slope is paid for in step height: the taller the island, the bigger the jump
	between neighbouring tiles, and the character has `AutoStepHeight = 0.6`.
	`worstStep` measures the largest step the field actually produces and refuses
	to write a world the character cannot walk.

	Run from the repository root:

	    lute examples/10-open-world/tools/generate_world.luau
]]

local fs = require("@std/fs")
local json = require("@std/json")

local ScenePath = "examples/10-open-world/content/scenes/main.scene.json"

--- Half the island's world, in metres. 2 km on a side, which is three times the
--- `LoadRadius` the scene sets -- so a walk still loads ahead of you and evicts
--- behind you, which is the thing this example exists to demonstrate.
local HalfWorld = 1024
--- One ground tile. See the header: this is the step budget, not the detail.
local Tile = 16
--- The grid the props and the landmarks are laid out on, and the same size the
--- partitioner's cells are, so a chunk's worth of props comes from one draw.
local Cell = 256

--- Sea level, and the one number the island is described against: land is above
--- it, the seabed is below it, and the water surface sits on it.
local SeaLevel = 0
--- Where the land ends. Beyond this the field is under water all the way out.
local ShoreRadius = 820
--- How high the middle stands above the sea.
local PeakHeight = 13
--- The sand band, just above the water line. Below it a tile is seabed and grows
--- nothing; above it the island is grass.
local BeachTop = 1.6
--- What the character in the scene is given. The world is checked against it
--- before it is written.
local AutoStepHeight = 0.6

--- A deterministic value hash, so the island is the same on every machine and in
--- every run. `math.random` would be neither: it is seeded per process and its
--- sequence is the implementation's.
local function hash(x: number, z: number, salt: number): number
    local h = (x * 374761393 + z * 668265263 + salt * 2246822519) % 4294967296
    h = bit32.bxor(h, bit32.rshift(h, 13))
    h = (h * 1274126177) % 4294967296
    return bit32.bxor(h, bit32.rshift(h, 16)) / 4294967296
end

--- Smooth at both ends, which keeps the shore from being a cliff and the summit
--- from being a spike. Its steepest point is the middle of the band, and that is
--- where the step budget is spent.
local function smoothstep(t: number): number
    local c = math.clamp(t, 0, 1)
    return c * c * (3 - 2 * c)
end

--- The island. A radial profile plus a gentle roll, and the roll is weak on
--- purpose: the two gradients ADD, and their sum is what the character steps
--- over.
---
--- **This is repeated in `src/scripts/init.luau`** and the two have to move
--- together -- along with the spawn and the lantern posts, which stand on it.
local function heightAt(x: number, z: number): number
    local distance = math.sqrt(x * x + z * z)
    local land = smoothstep(1 - distance / ShoreRadius)
    local roll = 1.4 * math.sin(x / 400) * math.cos(z / 360)
    local seabed = -9 * smoothstep((distance - ShoreRadius) / 260)
    return SeaLevel + PeakHeight * land + roll * land + seabed
end

--- The largest step between neighbouring tiles anywhere, sampled on the grid the
--- world is actually built on. Both axes AND the diagonal, because a character
--- walking north-east crosses 22.6 m rather than 16.
local function worstStep(): number
    local worst = 0
    local at = -HalfWorld
    while at < HalfWorld do
        local other = -HalfWorld
        while other < HalfWorld do
            local here = heightAt(at, other)
            -- **Only where a character can stand.** The seabed drops fast on
            -- purpose -- that is what makes the offshore cells coarse -- and it
            -- is under water, where nobody walks. Measuring it would fail a
            -- budget that is about walking.
            if here >= SeaLevel then
                worst = math.max(worst, math.abs(heightAt(at + Tile, other) - here))
                worst = math.max(worst, math.abs(heightAt(at, other + Tile) - here))
                worst = math.max(worst, math.abs(heightAt(at + Tile, other + Tile) - here))
            end
            other += Tile
        end
        at += Tile
    end
    return worst
end

--- Ground shades, quantised. A colour is part of a material and a material is
--- what the instanced path batches by, so a handful of shades collapses a
--- resident ring of thousands into a few draws.
---
--- **These are ALBEDOS and the renderer is physically based.** Grass reflects 10
--- to 20 per cent of what lands on it; sand reflects rather more, which is why
--- the beach is the brightest thing in the world at noon and reads as sand.
local GrassPalette = {
    { 0.13, 0.18, 0.1 },
    { 0.16, 0.21, 0.11 },
    { 0.11, 0.15, 0.09 },
    { 0.18, 0.22, 0.12 },
}
local SandPalette = {
    { 0.42, 0.36, 0.25 },
    { 0.46, 0.4, 0.28 },
    { 0.39, 0.33, 0.23 },
}
local SeabedPalette = {
    { 0.14, 0.13, 0.11 },
    { 0.11, 0.11, 0.1 },
}
--- Opaque, and that is a rendering decision rather than a look. A transparent
--- part is excluded from the instanced path, so a translucent sea would turn
--- every water tile into a draw call of its own -- thousands of them, for a
--- surface nobody looks through. Depth is carried by the fog and by the shore.
local WaterPalette = {
    { 0.05, 0.13, 0.2 },
    { 0.04, 0.11, 0.18 },
}
--- Foliage, foliage, bark, stone. The pairing matters as much as the values: a
--- trunk tinted from the crown's table is a lamp post with a bush on it.
local PropPalette = {
    { 0.08, 0.14, 0.06 },
    { 0.11, 0.18, 0.08 },
    { 0.12, 0.08, 0.05 },
    { 0.26, 0.25, 0.23 },
}
local BarkTint = 3
local StoneTint = 4

type Node = { class: string, name: string, properties: { [string]: any }, children: { Node }? }

local function pick(palette: { { number } }, x: number, z: number, salt: number): { number }
    return palette[1 + math.floor(hash(x, z, salt) * #palette)]
end

--- One part, as a scene node. `CFrame` rather than `Position` because that is
--- what the scene writer emits and what a round trip through the editor keeps.
local function part(
    name: string,
    shape: number,
    px: number,
    py: number,
    pz: number,
    sx: number,
    sy: number,
    sz: number,
    color: { number }
): Node
    return {
        class = "Part",
        name = name,
        properties = {
            Shape = shape,
            Anchored = true,
            CFrame = { px, py, pz, 1, 0, 0, 0, 1, 0, 0, 0, 1 },
            Size = { sx, sy, sz },
            Color = color,
        },
    }
end

local function folder(name: string, children: { Node }): Node
    return { class = "Folder", name = name, properties = {}, children = children }
end

local step = worstStep()
print(`worst step between neighbouring tiles: {string.format("%.2f", step)} m of {AutoStepHeight} allowed`)
if step > AutoStepHeight then
    error(
        `the island is not walkable: the worst step is {string.format("%.2f", step)} m against an `
            .. `AutoStepHeight of {AutoStepHeight}. Lower PeakHeight, widen ShoreRadius, or halve Tile.`
    )
end

local terrain: { Node } = {}
local sea: { Node } = {}
local props: { Node } = {}
local towers: { Node } = {}

--- **Offshore is coarse, and that is the whole performance story.** The sea is
--- flat and the seabed under it is never seen, so describing either at the
--- land's 16 m costs thousands of parts for no picture. At `CoarseTile` the
--- water and the bed together are a few hundred, and the seam is under water.
---
--- The land keeps its fine grid because that is where the character walks and
--- where the step budget applies.
local CoarseTile = 64

--- Is any part of this coarse cell close enough to the water line to need the
--- fine grid? Sampled at the corners and the centre rather than assumed, so the
--- shore keeps its resolution wherever it actually runs.
local function nearShore(x: number, z: number, size: number): boolean
    local samples = { { 0, 0 }, { size, 0 }, { 0, size }, { size, size }, { size / 2, size / 2 } }
    for _, at in samples do
        if heightAt(x + at[1], z + at[2]) > SeaLevel - 3 then
            return true
        end
    end
    return false
end

local tx = -HalfWorld
while tx < HalfWorld do
    local tz = -HalfWorld
    while tz < HalfWorld do
        local fine = nearShore(tx, tz, CoarseTile)
        local size = if fine then Tile else CoarseTile
        local sx = tx
        while sx < tx + CoarseTile do
            local sz = tz
            while sz < tz + CoarseTile do
                local px = sx + size / 2
                local pz = sz + size / 2
                -- The box's TOP is the terrain height, which is what makes the
                -- surface the character stands on the surface this function
                -- describes. A centre at `heightAt` would sink every tile by
                -- half its thickness and turn the field into a staircase.
                local top = heightAt(px, pz)
                local thickness = 6
                local palette = if top < SeaLevel
                    then SeabedPalette
                    elseif top < BeachTop then SandPalette
                    else GrassPalette
                table.insert(
                    terrain,
                    part("Ground", 0, px, top - thickness / 2, pz, size, thickness, size, pick(palette, sx, sz, 1))
                )

                -- **The sea is a surface, not a volume**: one flat tile at sea
                -- level wherever the ground is below it. The bed is still under
                -- it, so a shore reads as ground going under water rather than
                -- as water stopping at a line.
                if top < SeaLevel - 0.15 then
                    local shade = pick(WaterPalette, sx, sz, 5)
                    table.insert(sea, part("Water", 0, px, SeaLevel - 0.25, pz, size, 0.5, size, shade))
                end
                sz += size
            end
            sx += size
        end
        tz += CoarseTile
    end
    tx += CoarseTile
end

-- Props on land only. Nothing grows on the beach or below it: that band is what
-- separates the island from the sea, and a tree standing in it erases it.
local cx = -HalfWorld
while cx < HalfWorld do
    local cz = -HalfWorld
    while cz < HalfWorld do
        local count = 8 + math.floor(hash(cx, cz, 2) * 12)
        for index = 1, count do
            local px = cx + hash(cx, cz, index * 7) * Cell
            local pz = cz + hash(cx, cz, index * 13) * Cell
            local ground = heightAt(px, pz)
            if ground >= BeachTop then
                if hash(cx, cz, index * 29) > 0.35 then
                    local trunk = 4 + hash(cx, cz, index * 17) * 5
                    local crown = 3.5 + hash(cx, cz, index * 31) * 3
                    local foliage = PropPalette[1 + math.floor(hash(cx, cz, index * 23) * 2)]
                    -- **Atomic**: a trunk that arrives without its crown is a
                    -- post, and the two are three metres apart.
                    table.insert(props, {
                        class = "Model",
                        name = "Tree",
                        properties = { StreamingMode = "Enum.StreamingMode.Atomic" },
                        children = {
                            part("Trunk", 0, px, ground + trunk / 2, pz, 0.9, trunk, 0.9, PropPalette[BarkTint]),
                            part("Crown", 1, px, ground + trunk + crown * 0.3, pz, crown, crown, crown, foliage),
                        },
                    })
                else
                    local size = 1.5 + hash(cx, cz, index * 19) * 3.5
                    table.insert(
                        props,
                        part("Boulder", 1, px, ground + size * 0.35, pz, size, size, size, PropPalette[StoneTint])
                    )
                end
            end
        end

        -- A landmark every second cell, and only on land. Without them a
        -- procedural world is a treadmill: correct, streamed, and impossible to
        -- navigate, because every hilltop looks like every other.
        if (cx / Cell) % 2 == 0 and (cz / Cell) % 2 == 0 then
            local px = cx + Cell / 2
            local pz = cz + Cell / 2
            local ground = heightAt(px, pz)
            if ground >= BeachTop then
                local courses: { Node } = {}
                for level = 0, 7 do
                    local width = 9 - level
                    table.insert(
                        courses,
                        part("Course", 0, px, ground + 2.5 + level * 5, pz, width, 5, width, PropPalette[StoneTint])
                    )
                end
                -- Atomic for the same reason a tree is: eight courses arriving
                -- one at a time is a tower being built while you watch.
                table.insert(towers, {
                    class = "Model",
                    name = "Tower",
                    properties = { StreamingMode = "Enum.StreamingMode.Atomic" },
                    children = courses,
                })
            end
        end
        cz += Cell
    end
    cx += Cell
end

local scene: any = json.deserialize(fs.readFileToString(ScenePath))
local replaced = false
for _, child in scene.root.children do
    if child.name == "Scenery" then
        child.children = {
            folder("Terrain", terrain),
            folder("Sea", sea),
            folder("Props", props),
            folder("Towers", towers),
        }
        replaced = true
    end
end
if not replaced then
    error(`{ScenePath} has no "Scenery" folder under Workspace to fill`)
end

fs.writeStringToFile(ScenePath, json.serialize(scene) .. "\n")
print(`{#terrain} ground, {#sea} water, {#props} props, {#towers} towers -> {ScenePath}`)
