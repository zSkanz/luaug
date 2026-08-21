--!strict
--[[
	Writes this example's world as chunk sources, which `luaug build-assets`
	compiles into `.lchunk` payloads.

	The world is GENERATED rather than committed (M7 brief, Decision 8): a
	sixteen-square-kilometre terrain is megabytes of binary nobody can diff, and
	the roadmap's own deliverable line says "no giant binary assets in the repo".
	What is committed is this file, which is forty lines and readable.

	It is also the pipeline's first honest caller. A generator that wrote
	`.lchunk` directly would be a second implementation of the format; writing
	the SOURCE and letting `assetc` compile it means the example exercises the
	same path a game would.

	Run from the repository root:

	    lute examples/05-streaming/tools/generate_world.luau
]]

local fs = require("@std/fs")

local ChunkSize = 256
local Radius = 8
local Seed = 0x5EED_1234

--- A deterministic value hash, so the world is the same on every machine and in
--- every run. `math.random` would be neither: it is seeded per process and its
--- sequence is the implementation's.
local function hash(x: number, z: number, salt: number): number
    local h = (x * 374761393 + z * 668265263 + salt * 2246822519) % 4294967296
    h = bit32.bxor(h, bit32.rshift(h, 13))
    h = (h * 1274126177) % 4294967296
    return bit32.bxor(h, bit32.rshift(h, 16)) / 4294967296
end

--- Rolling hills. Smooth enough that a fly-cam sees a landscape rather than
--- noise, and cheap enough that a chunk is a few dozen boxes.
local function heightAt(x: number, z: number): number
    return 12 * math.sin(x / 900) * math.cos(z / 700) + 5 * math.sin((x + z) / 320)
end

local function buildChunk(cx: number, cz: number): string
    local instances: { string } = {}
    local minY, maxY = math.huge, -math.huge

    local originX = cx * ChunkSize
    local originZ = cz * ChunkSize

    -- The ground: four tiles per chunk rather than one, so a chunk arriving is
    -- visibly several objects and the materialisation budget has something to
    -- spend.
    for tx = 0, 1 do
        for tz = 0, 1 do
            local size = ChunkSize / 2
            local px = originX + size * (tx + 0.5)
            local pz = originZ + size * (tz + 0.5)
            local py = heightAt(px, pz)
            minY = math.min(minY, py - 2)
            maxY = math.max(maxY, py + 2)

            local shade = 0.28 + 0.12 * hash(cx * 2 + tx, cz * 2 + tz, 1)
            table.insert(
                instances,
                `\{"kind":"part","name":"Ground","anchored":true,"position":[{px},{py},{pz}],`
                    .. `"size":[{size},2,{size}],"color":[{shade * 0.7},{shade},{shade * 0.5}]\}`
            )
        end
    end

    -- Scenery, seeded from the cell's own coordinates so a chunk looks the same
    -- however many times it streams in and out. A chunk whose contents changed
    -- on reload would make eviction visible, which is exactly what streaming is
    -- supposed to hide.
    local count = 6 + math.floor(hash(cx, cz, 2) * 10)
    for i = 1, count do
        local px = originX + hash(cx, cz, i * 7) * ChunkSize
        local pz = originZ + hash(cx, cz, i * 13) * ChunkSize
        local height = 3 + hash(cx, cz, i * 17) * 9
        local py = heightAt(px, pz) + height / 2
        minY = math.min(minY, py - height)
        maxY = math.max(maxY, py + height)

        local tint = 0.35 + 0.45 * hash(cx, cz, i * 23)
        local shape = if hash(cx, cz, i * 29) > 0.7 then 1 else 0
        table.insert(
            instances,
            `\{"kind":"part","name":"Rock","shape":{shape},"anchored":true,`
                .. `"position":[{px},{py},{pz}],"size":[{2 + tint * 4},{height},{2 + tint * 3}],`
                .. `"color":[{tint},{tint * 0.9},{tint * 0.85}]\}`
        )
    end

    return `\{"format":"luaug-chunk-source","chunkSize":{ChunkSize},"x":{cx},"z":{cz},"layer":0,`
        .. `"minY":{minY},"maxY":{maxY},"instances":[{table.concat(instances, ",")}]\}\n`
end

local root = "examples/05-streaming/content/world"
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

print(`generate_world: wrote {written} chunk source(s) to {root} (seed {Seed})`)
