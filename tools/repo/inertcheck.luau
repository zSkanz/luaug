--!strict
--[[
	Stored-and-unread lint (M6, D030).

	**Every component field has a reader, or the property that stores it is
	marked `Inert`.**

	`Inert` arrived at M4.5 so that "declared, stored, read back, and nothing
	acts on it" would be a visible state rather than a discovery: the inspector
	shows it, the api-dump carries it, and the property's own doc names the
	milestone that will make it real. Two milestones later `UICorner.CornerRadius`
	was born into exactly that state and was not marked, and a person found it by
	looking at a square button. The marker still depended on somebody
	remembering, which is the one thing it exists not to depend on.

	So this is the mechanical half. It reads the component structs, counts who
	mentions each field, and requires that a field nothing reads corresponds to a
	property somebody deliberately marked.

	**What counts as a reader.** Every `.cpp`/`.h` under `engine/` except three
	kinds of file, each excluded for a reason:

	  * `components.h` itself -- the declaration is not a use;
	  * anything under a `generated/` directory -- the descriptor tables name
	    every property whether or not anything acts on one;
	  * `native_accessors.cpp` -- these are the getters and setters, and
	    "readable from Luau" is precisely the state `Inert` describes.

	Tests are NOT excluded, deliberately. A field only a test reads is a field
	only a test uses, and that is worth knowing -- but it is not a false
	positive, because the test is a real reader and the lint is about the
	engine's own honesty rather than about coverage.

	**How a field maps to a property.** By name: `cornerRadius` is
	`CornerRadius`. That is the convention every component in this repository
	follows, and a field that breaks it is caught here as an unmapped name rather
	than silently passing.

    **Two components that share a field name share a verdict**, and that is the
    one weakness worth stating: `InputBindingComponent::image` and
    `ImageLabelComponent::image` are one name to this lint, so a reader of either
    clears both. Distinguishing them means matching `->field` against a type,
    which is a C++ parser rather than a lint. The names are listed in
    `components.h` and a collision is visible there; when one bites, that is the
    moment to pay for the parser.

	    lute tools/repo/inertcheck.luau
]]

local fs = require("@std/fs")
local path = require("@std/path")
local process = require("@std/process")

local ComponentsHeader = "engine/scene/include/luaug/scene/components.h"
local ApiDump = "api/api-dump.json"
local EngineRoot = "engine"

--- Fields that are storage rather than surface: nothing in the IDL declares
--- them, so there is no property to mark and no reader to demand. Each entry is
--- a decision somebody made in this file.
local StorageOnly: { [string]: string } = {
    -- Layout's own dirty flag, written by every layout-affecting setter and read
    -- by the solver. It is not a property and never will be.
    layoutDirty = "a layout bookkeeping flag, not a property",

    -- **The engine never acts on this and is not meant to.** It is data a GAME
    -- reads back to draw its own prompt: `GetPreferredBinding` hands over the
    -- binding and the game's rebinding screen reads the name off it. That is a
    -- third state beside "has a reader" and "is Inert", and it is a narrow one --
    -- an entry here says the absence of an engine reader is the DESIGN, and it
    -- has to be argued in the property's own doc as well as here.
    displayName = "prompt text a game reads back; the engine deliberately never acts on it",
}

local function fail(message: string): never
    print(`inertcheck: ERROR: {message}`)
    return process.exit(1)
end

local function hasSuffix(text: string, suffix: string): boolean
    return #text >= #suffix and string.sub(text, -#suffix) == suffix
end

--- Every `.cpp`/`.h` under `engine/`, minus the three kinds the header explains.
local function readerFiles(): { string }
    local found: { string } = {}
    local pending: { string } = { EngineRoot }

    while #pending > 0 do
        local dir = table.remove(pending) :: string
        for _, entry in fs.listDirectory(dir) do
            local childPath = path.format(path.join(dir, entry.name))
            if entry.type == "dir" then
                if entry.name ~= "generated" then
                    table.insert(pending, childPath)
                end
                continue
            end
            if entry.type ~= "file" then
                continue
            end
            if not hasSuffix(childPath, ".cpp") and not hasSuffix(childPath, ".h") then
                continue
            end
            if hasSuffix(childPath, "components.h") or hasSuffix(childPath, "native_accessors.cpp") then
                continue
            end
            table.insert(found, childPath)
        end
    end

    table.sort(found)
    return found
end

--- The field names of every `*Component` struct, in declaration order.
---
--- A hand-rolled scan rather than a parser: the file is one struct per class
--- with one field per line and no templates, which is a shape a hundred lines of
--- pattern matching reads correctly and a C++ parser would be absurd for.
local function componentFields(): { string }
    local source = fs.readFileToString(ComponentsHeader)
    local fields: { string } = {}
    local seen: { [string]: boolean } = {}

    local insideComponent = false
    for line in string.gmatch(source, "[^\n]*") do
        local structName = string.match(line, "^struct%s+([%a%d_]+)%s*$")
        if structName ~= nil then
            insideComponent = string.match(structName, "Component$") ~= nil
            continue
        end
        if string.match(line, "^};") ~= nil then
            insideComponent = false
            continue
        end
        if not insideComponent then
            continue
        end

        -- `type name = value;`, `type name{...};`, `type name;` and
        -- `type name[4]{...};`. The type may be qualified and may carry `::`.
        local name = string.match(line, "^%s+[%a%d_:<>, ]+%s+([%a%d_]+)%s*[=;{%[]")
        if name == nil then
            continue
        end
        if seen[name] then
            continue
        end
        seen[name] = true
        table.insert(fields, name)
    end

    return fields
end

--- Every property the api-dump marks `Inert`, by name.
---
--- Read from the dump rather than from the definition files because the dump is
--- the generated, checked-in answer -- and because it is one flat file where the
--- definitions are five.
local function inertProperties(): { [string]: boolean }
    local source = fs.readFileToString(ApiDump)
    local inert: { [string]: boolean } = {}

    -- The dump is JSON with one property per object; a property's `"Name"`
    -- precedes its `"Inert": true` within the same object, so the nearest
    -- preceding name is the one the flag belongs to. Matched textually because
    -- the alternative is walking the whole document to answer one question.
    local searchFrom = 1
    while true do
        local at = string.find(source, '"Inert": true', searchFrom, true)
        if at == nil then
            break
        end
        local before = string.sub(source, 1, at)
        local name = nil
        local nameFrom = 1
        while true do
            local nameAt, nameEnd, captured = string.find(before, '"Name": "([%a%d_]+)"', nameFrom)
            if nameAt == nil then
                break
            end
            name = captured
            nameFrom = (nameEnd :: number) + 1
        end
        if name ~= nil then
            inert[name] = true
        end
        searchFrom = at + 1
    end

    return inert
end

local function pascalCase(field: string): string
    return string.upper(string.sub(field, 1, 1)) .. string.sub(field, 2)
end

local fields = componentFields()
if #fields == 0 then
    fail(`no component fields found in {ComponentsHeader} -- run this from the repository root`)
end

local sources: { string } = {}
for _, file in readerFiles() do
    table.insert(sources, fs.readFileToString(file))
end

local inert = inertProperties()
local unread: { string } = {}

for _, field in fields do
    if StorageOnly[field] ~= nil then
        continue
    end

    local readers = 0
    for _, source in sources do
        -- `%f[%w]` is a frontier pattern: it matches the boundary before a word
        -- character, so `image` does not match inside `imageColor`. Without it a
        -- field would be "read" by any longer field that contains its name.
        if string.find(source, `%f[%w]{field}%f[%W]`) ~= nil then
            readers += 1
            break
        end
    end

    if readers == 0 and not inert[pascalCase(field)] then
        table.insert(unread, field)
    end
end

if #unread > 0 then
    for _, field in unread do
        print(`inertcheck: {field} is stored and nothing reads it, and {pascalCase(field)} is not marked Inert`)
    end
    fail(
        `{#unread} field(s) stored and unread. Either something must act on the value, or its property carries `
            .. `Inert with the milestone that will (M4.5's marker, D030's lesson)`
    )
end

print(`inertcheck: ok -- {#fields} component field(s), {#sources} source file(s) swept`)
