--!strict
--[[
	Writes the flagship's world as chunk sources, which `luaug build-assets`
	compiles into `.lchunk` payloads.

	Generated rather than committed, for the reason `examples/05-streaming`
	established at M7: four and a half kilometres of terrain is megabytes of
	binary nobody can diff, and the roadmap's deliverable line says "no giant
	binary assets in the repo". What is committed is this file.

	**Two things here are different from `05-streaming`'s generator, and both
	are because this world is WALKED rather than flown over.**

	  1. **The ground is terraced finely enough to walk up.** A chunk's floor is
	     8 by 8 flat tiles instead of 2 by 2, and the height field's amplitudes
	     are chosen so that the step between neighbouring tiles never exceeds the
	     character's `AutoStepHeight`. A fly-cam does not care; a character walks
	     into a ledge and stops.

	  2. **Colours come from a small palette.** A part's colour is part of its
	     material, so two boxes that differ only in tint are two draw calls
	     (M7.5's instanced path groups by mesh AND material). Six ground shades
	     and four prop tints let a resident ring of a thousand parts collapse
	     into a handful of instanced draws, and it costs nothing to author.
	     The horde scene learned this the expensive way.

	Run from the repository root:

	    lute examples/10-open-world/tools/generate_world.luau
]]

local fs = require("@std/fs")

local ChunkSize = 256
-- 17 by 17 chunks: 4.35 km on a side, which is a world a character cannot walk
-- across in one session and the engine never holds more than a fraction of.
local Radius = 8
-- 8 by 8 tiles per chunk, so a tile is 32 m.
local TilesPerChunk = 8

--- A deterministic value hash, so the world is the same on every machine and in
--- every run. `math.random` would be neither: it is seeded per process and its
--- sequence is the implementation's.
local function hash(x: number, z: number, salt: number): number
    local h = (x * 374761393 + z * 668265263 + salt * 2246822519) % 4294967296
    h = bit32.bxor(h, bit32.rshift(h, 13))
    h = (h * 1274126177) % 4294967296
    return bit32.bxor(h, bit32.rshift(h, 16)) / 4294967296
end

--- Rolling hills, and the amplitudes are a WALKABILITY budget rather than a
--- taste. The steepest this field gets is 8/1200 + 3/500 ≈ 0.013 m per metre,
--- so across a 32 m tile the tallest step between neighbours is about 0.41 m --
--- inside the 0.6 m `AutoStepHeight` the character is given, with room for the
--- diagonal case.
local function heightAt(x: number, z: number): number
    return 8 * math.sin(x / 1200) * math.cos(z / 1000) + 3 * math.sin((x + z) / 500)
end

--- Six shades, quantised. See the header: a colour is part of a material, and a
--- material is what the instanced path batches by.
---
--- **These are ALBEDOS and the renderer is physically based**, which is the
--- thing to get right rather than the hue. Grass reflects 10 to 20 per cent of
--- what lands on it; the first version of this file used 0.30 to 0.44, and
--- under a sky bright enough to light a field the result was a pale mint plain
--- with no contrast anywhere. A too-bright albedo cannot be fixed by exposure:
--- lowering it darkens the sky by the same amount and the picture stays flat.
local GroundPalette = {
    { 0.13, 0.18, 0.10 },
    { 0.16, 0.21, 0.11 },
    { 0.11, 0.15, 0.09 },
    { 0.18, 0.22, 0.12 },
    { 0.14, 0.17, 0.10 },
    { 0.10, 0.14, 0.09 },
}

--- Four tints, and the pairing matters as much as the values: 1 and 2 are
--- foliage, 3 is bark, 4 is stone. A trunk tinted from the same table as a
--- crown is a lamp post with a bush on it, which is what the first render of
--- this world looked like.
local PropPalette = {
    { 0.08, 0.14, 0.06 },
    { 0.11, 0.18, 0.08 },
    { 0.12, 0.08, 0.05 },
    { 0.26, 0.25, 0.23 },
}
local BarkTint = 3
local StoneTint = 4

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
): string
    return `\{"kind":"part","name":"{name}","shape":{shape},"anchored":true,`
        .. `"position":[{px},{py},{pz}],"size":[{sx},{sy},{sz}],`
        .. `"color":[{color[1]},{color[2]},{color[3]}]\}`
end

local function buildChunk(cx: number, cz: number): string
    local instances: { string } = {}
    local minY, maxY = math.huge, -math.huge

    local originX = cx * ChunkSize
    local originZ = cz * ChunkSize
    local tile = ChunkSize / TilesPerChunk

    for tx = 0, TilesPerChunk - 1 do
        for tz = 0, TilesPerChunk - 1 do
            local px = originX + tile * (tx + 0.5)
            local pz = originZ + tile * (tz + 0.5)
            -- The box's TOP is the terrain height, which is what makes the
            -- surface the character stands on the surface this function
            -- describes. A centre at `heightAt` would sink every tile by half
            -- its thickness and turn the whole field into a staircase of holes.
            local top = heightAt(px, pz)
            local thickness = 6
            local py = top - thickness / 2
            minY = math.min(minY, py - thickness)
            maxY = math.max(maxY, top)

            local shade = GroundPalette[1 + math.floor(hash(cx * 8 + tx, cz * 8 + tz, 1) * #GroundPalette)]
            table.insert(instances, part("Ground", 0, px, py, pz, tile, thickness, tile, shade))
        end
    end

    -- Trees and boulders, seeded from the cell's own coordinates so a chunk
    -- looks the same however many times it streams in and out. A chunk whose
    -- contents changed on reload would make eviction visible, which is exactly
    -- what streaming exists to hide.
    local count = 10 + math.floor(hash(cx, cz, 2) * 14)
    for index = 1, count do
        local px = originX + hash(cx, cz, index * 7) * ChunkSize
        local pz = originZ + hash(cx, cz, index * 13) * ChunkSize
        local ground = heightAt(px, pz)
        local isTree = hash(cx, cz, index * 29) > 0.35

        if isTree then
            local trunk = 4 + hash(cx, cz, index * 17) * 5
            table.insert(
                instances,
                part("Trunk", 0, px, ground + trunk / 2, pz, 0.9, trunk, 0.9, PropPalette[BarkTint])
            )
            local crown = 3.5 + hash(cx, cz, index * 31) * 3
            local foliage = PropPalette[1 + math.floor(hash(cx, cz, index * 23) * 2)]
            table.insert(
                instances,
                part("Crown", 1, px, ground + trunk + crown * 0.3, pz, crown, crown, crown, foliage)
            )
            maxY = math.max(maxY, ground + trunk + crown)
        else
            local size = 1.5 + hash(cx, cz, index * 19) * 3.5
            table.insert(
                instances,
                part("Boulder", 1, px, ground + size * 0.35, pz, size, size, size, PropPalette[StoneTint])
            )
            maxY = math.max(maxY, ground + size)
        end
        minY = math.min(minY, ground - 8)
    end

    -- A landmark every second chunk -- half a kilometre apart, so two or three
    -- are in view at once and one of them is always ahead. Without them a
    -- procedural world is a treadmill: correct, streamed, and impossible to
    -- navigate, because every hilltop looks like every other.
    --
    -- Eight courses of 5 m rather than six, because the fog swallows the bottom
    -- of a distant one and what is left has to still read as a tower.
    if cx % 2 == 0 and cz % 2 == 0 then
        local px = originX + ChunkSize / 2
        local pz = originZ + ChunkSize / 2
        local ground = heightAt(px, pz)
        for level = 0, 7 do
            local width = 9 - level
            table.insert(
                instances,
                part("Tower", 0, px, ground + 2.5 + level * 5, pz, width, 5, width, PropPalette[StoneTint])
            )
        end
        maxY = math.max(maxY, ground + 45)
    end

    return `\{"format":"luaug-chunk-source","chunkSize":{ChunkSize},"x":{cx},"z":{cz},"layer":0,`
        .. `"minY":{minY},"maxY":{maxY},"instances":[{table.concat(instances, ",")}]\}\n`
end

local root = "examples/10-open-world/content/world"
if not fs.exists(root) then
    fs.createDirectory(root, { makeParents = true })
end

local written = 0
for cz = -Radius, Radius do
    for cx = -Radius, Radius do
        fs.writeStringToFile(`{root}/cell_{cx}_{cz}.chunk.json`, buildChunk(cx, cz))
        written += 1
    end
end

print(`generate_world: wrote {written} chunk source(s) to {root}`)
