--!strict
--[[
	Puts the sculpted ground from `tools/sculpt-ground` into the flagship's
	scene, and takes out the boxes it replaces.

	The two steps, from the repository root:

	    luaug-host examples/10-open-world/tools/sculpt-ground --save-scene=<ground.scene.json>
	    lute examples/10-open-world/tools/merge_ground.luau <ground.scene.json>

	**What it changes, and nothing else.** The `Terrain` named `Ground` goes
	under `Workspace`, replacing one already there. The `Ground` boxes in
	`Scenery.Terrain` whose centres fall inside the sculpted square are
	removed. Everything else in the scene -- the character, whatever somebody
	placed in the editor -- is left exactly as it was, because the scene is the
	source and this is an edit to it, not a rebuild.
]]

local fs = require("@std/fs")
local json = require("@std/json")
local process = require("@std/process")

local ScenePath = "examples/10-open-world/content/scenes/main.scene.json"

--- Half the square the terrain covers. **It moves with `HalfRegion` in
--- `tools/sculpt-ground` and `TerrainRegion` in `generate_world.luau`.**
local TerrainRegion = 256

type Node = { class: string, name: string, properties: { [string]: any }, children: { Node }? }

-- Lute passes the script path as args[1]; user arguments start at 2.
local groundPath = process.args[2]
if groundPath == nil then
    error("usage: lute examples/10-open-world/tools/merge_ground.luau <ground.scene.json>")
end

local ground: any = json.deserialize(fs.readFileToString(groundPath))
local sculpted: Node? = nil
for _, child in ground.root.children do
    if child.class == "Terrain" and child.name == "Ground" then
        sculpted = child
    end
end
if sculpted == nil then
    error(`{groundPath} has no Terrain named "Ground" under Workspace`)
end

local scene: any = json.deserialize(fs.readFileToString(ScenePath))
local workspace: { Node } = scene.root.children

local placed = false
for index, child in workspace do
    if child.class == "Terrain" and child.name == "Ground" then
        workspace[index] = sculpted :: Node
        placed = true
    end
end
if not placed then
    table.insert(workspace, sculpted :: Node)
end

local removed = 0
for _, child in workspace do
    if child.name == "Scenery" and child.children then
        for _, group in child.children do
            if group.name == "Terrain" and group.children then
                local kept: { Node } = {}
                for _, box in group.children do
                    local cframe = box.properties.CFrame
                    local inside = box.name == "Ground"
                        and cframe ~= nil
                        and math.abs(cframe[1]) < TerrainRegion
                        and math.abs(cframe[3]) < TerrainRegion
                    if inside then
                        removed += 1
                    else
                        table.insert(kept, box)
                    end
                end
                group.children = kept
            end
        end
    end
end

fs.writeStringToFile(ScenePath, json.serialize(scene) .. "\n")
print(`sculpted ground {if placed then "replaced" else "added"}, {removed} box(es) under it removed -> {ScenePath}`)
