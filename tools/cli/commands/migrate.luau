--!strict
--[[
	`luaug migrate materials` -- a project's scenes and stamps, from the
	material-as-instance design (ADR 0060) to the material-as-asset one (ADR
	0090).

	What it does to each version 1 `.scene.json` and `.stamp.json` under
	`content/`:

	  - every `Material` instance becomes a `.material.json` under
	    `content/materials/`, and leaves the tree;
	  - a stamp whose ROOT is a `Material` becomes a material asset of the same
	    name, and the stamp file goes; a placed instance of it with overrides
	    becomes a VARIANT of that asset;
	  - every part's `Material` reference becomes the asset's URN;
	  - every non-white `Color` and non-zero `Transparency` becomes an override
	    in `MaterialParameters`. On a part wearing a material the override is
	    what the part DREW -- the old tint multiplied the material -- and the
	    asset is made to declare the parameter rather than the tint being
	    dropped;
	  - and the file's version becomes 2.

	**Idempotent**: a version 2 file is left alone, so running it twice does
	nothing the second time. **The diff is meant to be read**: key order,
	number spellings and layout are kept as the file had them (`jsondoc`), so
	what changed is only what the migration changed.

	Scripts are NOT rewritten -- `part.Color = c` and `part.Transparency = t`
	need a person, because only a person knows which `.Color` is a part's. The
	command lists the lines that look like it.
]]

local fs = require("@std/fs")

local JsonDoc = require("../jsondoc")
local catalog = require("../catalog")
local projectlib = require("../project")

type Node = JsonDoc.Node

local tr = catalog.tr

local Migrate = {}

--- The classes that are parts: `BasePart` and everything under it.
local PartClasses: { [string]: boolean } = { Part = true, MeshPart = true, CharacterBody = true }

--- The fields a material file carries, in the order it writes them.
local Fields = {
    "Color",
    "Transparency",
    "ColorMap",
    "NormalMap",
    "MetallicRoughnessMap",
    "Emissive",
    "EmissiveMap",
    "Metalness",
    "Roughness",
    "NormalScale",
    "AlphaMode",
    "AlphaCutoff",
    "DoubleSided",
}

local Declarable = { "Color", "Transparency", "Emissive", "Metalness", "Roughness", "NormalScale", "AlphaCutoff" }

type Colour = { number }

--- A material as the migration knows it: what its file says, and the three
--- values the tint arithmetic needs, resolved through its parent.
type Asset = {
    path: string, -- content-relative
    parent: string?, -- a URN
    declared: { [string]: boolean },
    fields: { [string]: Node },
    colour: Colour,
    emissive: Colour,
    transparency: number,
}

export type Result = {
    -- Content-relative path -> the text to write there.
    writes: { [string]: string },
    -- Content-relative paths to delete.
    removes: { string },
    -- One line per thing done, for the command to print.
    notes: { string },
}

local function urnOf(path: string): string
    return "asset://" .. path
end

local function colourOf(node: Node?, fallback: Colour): Colour
    if node == nil or node.kind ~= "array" or #node.items < 3 then
        return fallback
    end
    local out: Colour = {}
    for index = 1, 3 do
        out[index] = JsonDoc.numberOf(node.items[index]) or 0
    end
    return out
end

local function isWhite(colour: Colour): boolean
    return colour[1] == 1 and colour[2] == 1 and colour[3] == 1
end

local function isBlack(colour: Colour): boolean
    return colour[1] == 0 and colour[2] == 0 and colour[3] == 0
end

-- **The shortest decimal that reads back as the same f32**, which is how the
-- engine's writer spells a material's floats -- so a file this writes is the
-- file the editor would save.
local function f32(value: number): number
    return (string.unpack("<f", string.pack("<f", value))) :: number
end

local function shortest(value: number): string
    local target = f32(value)
    for digits = 1, 9 do
        local text = string.format(`%.{digits}g`, target)
        if f32(tonumber(text) :: number) == target then
            return text
        end
    end
    return string.format("%.9g", target)
end

local function colourNode(colour: Colour): Node
    return JsonDoc.array({
        JsonDoc.number(colour[1]),
        JsonDoc.number(colour[2]),
        JsonDoc.number(colour[3]),
    })
end

--- ADR 0090's file: fixed key order, two-space indent, arrays on one line,
--- floats at their shortest. The same text `asset::writeMaterialAsset` makes.
local function writeAsset(asset: Asset): string
    local lines = {
        "{",
        '  "format": "luaug-material",',
        '  "version": 1,',
        `  "parent": {if asset.parent then '"' .. asset.parent .. '"' else '""'},`,
    }
    local declared = {}
    for _, name in Declarable do
        if asset.declared[name] then
            table.insert(declared, `"{name}"`)
        end
    end
    table.insert(lines, `  "instanceParameters": [{table.concat(declared, ", ")}],`)

    local written: { string } = {}
    for _, name in Fields do
        local node = asset.fields[name]
        if node == nil then
            continue
        end
        local text: string
        if node.kind == "array" then
            local parts = {}
            for _, item in node.items do
                table.insert(parts, shortest(JsonDoc.numberOf(item) or 0))
            end
            text = `[{table.concat(parts, ", ")}]`
        elseif node.kind == "number" then
            text = shortest(tonumber(node.raw) :: number)
        elseif node.kind == "bool" then
            text = if node.value then "true" else "false"
        elseif node.kind == "string" then
            text = JsonDoc.print(JsonDoc.string(node.value), { indent = 0 })
        else
            continue
        end
        table.insert(written, `    "{name}": {text}`)
    end
    if #written == 0 then
        table.insert(lines, '  "properties": {}')
    else
        table.insert(lines, '  "properties": {')
        table.insert(lines, table.concat(written, ",\n"))
        table.insert(lines, "  }")
    end
    table.insert(lines, "}")
    return table.concat(lines, "\n") .. "\n"
end

--- A name that is safe as a file name, from an instance name.
local function fileStem(name: string): string
    local stem = string.gsub(string.lower(name), "[^%w%-_]+", "-")
    return if stem == "" or stem == "-" then "material" else stem
end

--- The migration over one project's content, given as content-relative path
--- -> text. Pure, so a test gives it files and reads what comes back.
function Migrate.materials(files: { [string]: string }): Result
    local result: Result = { writes = {}, removes = {}, notes = {} }
    local assets: { [string]: Asset } = {} -- by URN
    local taken: { [string]: boolean } = {}
    for path in files do
        taken[path] = true
    end

    local parsed: { [string]: { root: Node, layout: JsonDoc.Layout } } = {}
    local ordered: { string } = {}
    for path, text in files do
        if string.sub(path, -11) == ".scene.json" or string.sub(path, -11) == ".stamp.json" then
            local ok, root, layout = pcall(JsonDoc.parse, text)
            if ok and root.kind == "object" and JsonDoc.stringOf(JsonDoc.get(root, "format")) == "luaug-scene" then
                parsed[path] = { root = root, layout = layout }
                table.insert(ordered, path)
            end
        end
    end
    -- Sorted, so two runs over one project write the same files in the same
    -- order and name them the same way.
    table.sort(ordered)

    local function uniquePath(stem: string): string
        local candidate = `materials/{stem}.material.json`
        local suffix = 2
        while taken[candidate] do
            candidate = `materials/{stem}-{suffix}.material.json`
            suffix += 1
        end
        taken[candidate] = true
        return candidate
    end

    -- A `Material` instance's properties as an asset.
    local function assetFrom(properties: Node?, stem: string, parent: Asset?): Asset
        local fields: { [string]: Node } = {}
        if properties ~= nil and properties.kind == "object" then
            for _, key in properties.keys do
                if table.find(Fields, key) ~= nil then
                    fields[key] = properties.values[key]
                end
            end
        end
        local asset: Asset = {
            path = uniquePath(stem),
            parent = if parent ~= nil then urnOf(parent.path) else nil,
            declared = {},
            fields = fields,
            colour = colourOf(fields.Color, if parent then parent.colour else { 1, 1, 1 }),
            emissive = colourOf(fields.Emissive, if parent then parent.emissive else { 0, 0, 0 }),
            transparency = JsonDoc.numberOf(fields.Transparency) or (if parent then parent.transparency else 0),
        }
        if parent == nil then
            -- A base writes every field (ADR 0090); the old class's defaults
            -- fill the ones the instance never set.
            local defaults: { [string]: Node } = {
                Color = colourNode({ 1, 1, 1 }),
                Transparency = JsonDoc.number(0),
                ColorMap = JsonDoc.string(""),
                NormalMap = JsonDoc.string(""),
                MetallicRoughnessMap = JsonDoc.string(""),
                Emissive = colourNode({ 0, 0, 0 }),
                EmissiveMap = JsonDoc.string(""),
                Metalness = JsonDoc.number(0),
                Roughness = JsonDoc.number(0.7),
                NormalScale = JsonDoc.number(1),
                AlphaMode = JsonDoc.string("Opaque"),
                AlphaCutoff = JsonDoc.number(0.5),
                DoubleSided = { kind = "bool", value = false } :: Node,
            }
            for name, node in defaults do
                if asset.fields[name] == nil then
                    asset.fields[name] = node
                end
            end
        end
        assets[urnOf(asset.path)] = asset
        return asset
    end

    -- --- Stamps whose root is a material ------------------------------------
    local materialStamps: { [string]: Asset } = {}
    for _, path in ordered do
        local file = parsed[path]
        local root = JsonDoc.get(file.root, "root")
        if
            string.sub(path, -11) == ".stamp.json"
            and JsonDoc.numberOf(JsonDoc.get(file.root, "version")) == 1
            and root ~= nil
            and JsonDoc.stringOf(JsonDoc.get(root, "class")) == "Material"
        then
            local stem = string.match(path, "([^/]+)%.stamp%.json$") or "material"
            local asset = assetFrom(JsonDoc.get(root, "properties"), fileStem(stem), nil)
            materialStamps[path] = asset
            table.insert(result.removes, path)
            table.insert(result.notes, tr("cli.migrate.stamp_to_material", { stamp = path, material = asset.path }))
        end
    end

    -- --- Each version 1 file --------------------------------------------------
    for _, path in ordered do
        local file = parsed[path]
        if materialStamps[path] ~= nil or JsonDoc.numberOf(JsonDoc.get(file.root, "version")) ~= 1 then
            continue
        end
        local root = JsonDoc.get(file.root, "root")
        if root == nil or root.kind ~= "object" then
            continue
        end

        -- Scene paths of every node, and the materials among them.
        local byPath: { [string]: Asset } = {}
        local changedHere = false

        local function walkMaterials(node: Node, prefix: string)
            if node.kind ~= "object" then
                return
            end
            local name = JsonDoc.stringOf(JsonDoc.get(node, "name")) or ""
            local here = if prefix == "" then name else prefix .. "." .. name
            local class = JsonDoc.stringOf(JsonDoc.get(node, "class"))
            local stamp = JsonDoc.stringOf(JsonDoc.get(node, "stamp"))
            if class == "Material" then
                local placedFrom = if stamp ~= nil then materialStamps[stamp] else nil
                local overrides = JsonDoc.get(node, "overrides")
                local own = if overrides ~= nil then JsonDoc.get(overrides, "") else nil
                if placedFrom ~= nil and own == nil then
                    -- A placed instance of a material stamp, unchanged: it IS
                    -- that asset.
                    byPath[here] = placedFrom
                elseif placedFrom ~= nil then
                    -- Placed and overridden: a variant, writing only what the
                    -- instance changed.
                    byPath[here] = assetFrom(own, fileStem(name), placedFrom)
                else
                    byPath[here] = assetFrom(JsonDoc.get(node, "properties"), fileStem(name), nil)
                end
                table.insert(
                    result.notes,
                    tr("cli.migrate.instance_to_material", { file = path, name = here, material = byPath[here].path })
                )
            end
            local children = JsonDoc.get(node, "children")
            if children ~= nil and children.kind == "array" then
                for _, child in children.items do
                    walkMaterials(child, here)
                end
            end
        end
        walkMaterials(root, "")

        -- A part's surface, converted in `properties` (a node's own, or one
        -- override entry of a placed stamp).
        local function convertPart(properties: Node, where: string)
            if properties.kind ~= "object" then
                return
            end
            local colourNodeIn = JsonDoc.get(properties, "Color")
            local transparencyNode = JsonDoc.get(properties, "Transparency")
            local materialNode = JsonDoc.get(properties, "Material")
            if colourNodeIn == nil and transparencyNode == nil and materialNode == nil then
                return
            end

            local wearing: Asset? = nil
            local reference = JsonDoc.stringOf(materialNode)
            if reference ~= nil then
                wearing = byPath[reference]
                if wearing == nil then
                    table.insert(
                        result.notes,
                        tr("cli.migrate.lost_reference", { file = path, name = where, path = reference })
                    )
                end
            end
            local tint = colourOf(colourNodeIn, { 1, 1, 1 })
            local fade = JsonDoc.numberOf(transparencyNode) or 0

            local parameters: { [string]: Node } = {}
            if wearing == nil then
                if not isWhite(tint) and colourNodeIn ~= nil then
                    -- The node as it was written: nothing about this value
                    -- changed, so nothing about its spelling does either.
                    parameters.Color = colourNodeIn
                end
                if fade ~= 0 and transparencyNode ~= nil then
                    parameters.Transparency = transparencyNode
                end
            else
                -- **What the part drew**, which is what the override has to
                -- be: the old tint MULTIPLIED the material, an override
                -- REPLACES it.
                if not isWhite(tint) then
                    local base = wearing.colour
                    parameters.Color = colourNode({ base[1] * tint[1], base[2] * tint[2], base[3] * tint[3] })
                    wearing.declared.Color = true
                    if not isBlack(wearing.emissive) then
                        local glow = wearing.emissive
                        parameters.Emissive = colourNode({ glow[1] * tint[1], glow[2] * tint[2], glow[3] * tint[3] })
                        wearing.declared.Emissive = true
                    end
                end
                if fade ~= 0 then
                    parameters.Transparency = JsonDoc.number(1 - (1 - wearing.transparency) * (1 - fade))
                    wearing.declared.Transparency = true
                end
            end

            -- Where the surface was, so a diff shows one key replaced rather
            -- than a key removed here and another added at the end.
            local at = #properties.keys + 1
            for index, key in properties.keys do
                if key == "Color" or key == "Transparency" or key == "Material" then
                    at = index
                    break
                end
            end
            JsonDoc.remove(properties, "Color")
            JsonDoc.remove(properties, "Transparency")
            JsonDoc.remove(properties, "Material")
            if wearing ~= nil then
                JsonDoc.insert(properties, at, "Material", JsonDoc.string(urnOf(wearing.path)))
                at += 1
            end
            local names = {}
            for name in parameters do
                table.insert(names, name)
            end
            if #names > 0 then
                table.sort(names)
                local object = JsonDoc.object()
                for _, name in names do
                    JsonDoc.set(object, name, parameters[name])
                end
                JsonDoc.insert(properties, at, "MaterialParameters", object)
            end
            changedHere = true
        end

        -- The class at a dotted path inside another file's tree, for a placed
        -- stamp's overrides.
        local function classInside(stampPath: string, inner: string): string?
            local stampFile = parsed[stampPath]
            local node = if stampFile ~= nil then JsonDoc.get(stampFile.root, "root") else nil
            if node == nil then
                return nil
            end
            if inner ~= "" then
                for step in string.gmatch(inner, "[^.]+") do
                    local children = JsonDoc.get(node :: Node, "children")
                    local found: Node? = nil
                    if children ~= nil and children.kind == "array" then
                        for _, child in children.items do
                            if JsonDoc.stringOf(JsonDoc.get(child, "name")) == step then
                                found = child
                                break
                            end
                        end
                    end
                    if found == nil then
                        return nil
                    end
                    node = found
                end
            end
            return JsonDoc.stringOf(JsonDoc.get(node :: Node, "class"))
        end

        local function walkParts(node: Node, prefix: string): boolean
            -- Answers whether the node itself is a material, which its parent
            -- then drops from its children.
            if node.kind ~= "object" then
                return false
            end
            local name = JsonDoc.stringOf(JsonDoc.get(node, "name")) or ""
            local here = if prefix == "" then name else prefix .. "." .. name
            local class = JsonDoc.stringOf(JsonDoc.get(node, "class"))
            if class == "Material" then
                return true
            end
            if class ~= nil and PartClasses[class] then
                local properties = JsonDoc.get(node, "properties")
                if properties ~= nil then
                    convertPart(properties, here)
                end
            end
            local stamp = JsonDoc.stringOf(JsonDoc.get(node, "stamp"))
            local overrides = JsonDoc.get(node, "overrides")
            if stamp ~= nil and overrides ~= nil and overrides.kind == "object" then
                for _, inner in overrides.keys do
                    local innerClass = classInside(stamp, inner)
                    if innerClass ~= nil and PartClasses[innerClass] then
                        convertPart(overrides.values[inner], if inner == "" then here else here .. "." .. inner)
                    end
                end
            end
            local children = JsonDoc.get(node, "children")
            if children ~= nil and children.kind == "array" then
                local kept: { Node } = {}
                for _, child in children.items do
                    if walkParts(child, here) then
                        changedHere = true
                    else
                        table.insert(kept, child)
                    end
                end
                children.items = kept
            end
            return false
        end
        walkParts(root, "")

        JsonDoc.set(file.root, "version", JsonDoc.number(2))
        result.writes[path] = JsonDoc.print(file.root, file.layout)
        table.insert(
            result.notes,
            tr(if changedHere then "cli.migrate.file_converted" else "cli.migrate.file_versioned", { file = path })
        )
    end

    -- The assets, written last: a part converted after its material was made
    -- may have made the material declare a parameter.
    local urns = {}
    for urn in assets do
        table.insert(urns, urn)
    end
    table.sort(urns)
    for _, urn in urns do
        local asset = assets[urn]
        result.writes[asset.path] = writeAsset(asset)
    end
    return result
end

--- Every line of a project's scripts that writes a `.Color` or a
--- `.Transparency`. Not all of them are a part's -- a light's, an emitter's
--- and a `Part2D`'s keep theirs -- which is why these are listed and not
--- rewritten.
function Migrate.scriptLines(root: string): { string }
    local out: { string } = {}
    for _, dir in projectlib.sourceDirectories(root) do
        for _, entry in fs.listDirectory(dir) do
            if entry.type == "file" and string.sub(entry.name, -5) == ".luau" then
                local file = projectlib.joined(dir, entry.name)
                local number = 0
                for line in string.gmatch(fs.readFileToString(file) .. "\n", "([^\n]*)\n") do
                    number += 1
                    if string.find(line, "%.Color%s*=") or string.find(line, "%.Transparency%s*=") then
                        table.insert(out, `{file}:{number}: {string.gsub(line, "^%s+", "")}`)
                    end
                end
            end
        end
    end
    return out
end

--- Every content-relative path under `content`, skipping dot-directories
--- (`.luaug/`, a VCS's own) the way the content browser does.
local function contentFiles(content: string): { [string]: string }
    local files: { [string]: string } = {}
    local function walk(dir: string, prefix: string)
        for _, entry in fs.listDirectory(dir) do
            if string.sub(entry.name, 1, 1) == "." then
                continue
            end
            local full = projectlib.joined(dir, entry.name)
            local relative = if prefix == "" then entry.name else prefix .. "/" .. entry.name
            if entry.type == "dir" then
                walk(full, relative)
            elseif entry.type == "file" and string.sub(entry.name, -5) == ".json" then
                files[relative] = fs.readFileToString(full)
            end
        end
    end
    if fs.exists(content) then
        walk(content, "")
    end
    return files
end

export type Options = {
    path: string,
    what: string?,
}

function Migrate.run(options: Options): number
    if options.what ~= "materials" then
        print(tr("cli.migrate.err.what", { what = options.what or "" }))
        return 2
    end
    local project = projectlib.load(options.path)
    if project.diagnostic ~= nil then
        print(project.diagnostic)
        return 2
    end
    if not projectlib.isProject(project) then
        print(tr("cli.err.not_a_project", { path = project.root }))
        return 2
    end

    local content = projectlib.joined(project.root, "content")
    local result = Migrate.materials(contentFiles(content))

    local written = {}
    for relative in result.writes do
        table.insert(written, relative)
    end
    table.sort(written)
    for _, relative in written do
        local full = projectlib.joined(content, relative)
        local folder = string.match(full, "^(.*)[/\\][^/\\]+$")
        if folder ~= nil then
            fs.createDirectory(folder, { makeParents = true })
        end
        fs.writeStringToFile(full, result.writes[relative])
    end
    for _, relative in result.removes do
        fs.remove(projectlib.joined(content, relative))
    end

    for _, note in result.notes do
        print(note)
    end
    if #result.notes == 0 then
        print(tr("cli.migrate.nothing", { path = project.root }))
    end

    local lines = Migrate.scriptLines(project.root)
    if #lines > 0 then
        print(tr("cli.migrate.scripts", { count = #lines }))
        for _, line in lines do
            print(tr("cli.migrate.script_line", { line = line }))
        end
    end
    return 0
end

return Migrate
