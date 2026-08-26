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

	**Two places hold property state and both are swept (D055).** Component
	pools are one; `scene::EngineState` in `world.h` is the other, and it is
	where a service with one instance per world keeps its knobs -- the fixed
	timestep, the streaming radii, the pointer lock, the master volume. That
	half was invisible to this lint until M8, which is exactly how
	`InputService.PointerLocked` sat stored and unread from M6 until a human
	asked for mouse look and found it (D049). A lint with a blind spot the size
	of a service is a lint that reports on the half nobody was worried about.

	**What counts as a reader.** Every `.cpp`/`.h` under `engine/` except the
	file that DECLARES the state being swept, plus two kinds excluded for their
	own reasons:

	  * the declaring header itself -- `components.h` for the component sweep,
	    `world.h` for the engine-state one -- because a declaration is not a use;
	  * anything under a `generated/` directory -- the descriptor tables name
	    every property whether or not anything acts on one;
	  * `native_accessors.cpp` -- these are the getters and setters, and
	    "readable from Luau" is precisely the state `Inert` describes.

	  * anything under a `tests/` directory. A test that sets a field and checks
	    it came back demonstrates the ACCESSOR, which is the one thing `Inert`
	    already means -- so counting it clears precisely the fields worth
	    reporting. `GuiObject.Rotation` was cleared for the whole of v1 partly by
	    the tests written to prove its drawing worked, and its drawing did not
	    exist. Excluding them dropped 106 of 383 files and surfaced no field
	    engine code genuinely reads.

	**How a field maps to a property.** By name: `cornerRadius` is
	`CornerRadius`. That is the convention every component in this repository
	follows, and a field that breaks it is caught here as an unmapped name rather
	than silently passing.

	**Two components that share a field name do NOT share a verdict** (S7.13).
	Eleven names in `components.h` are declared by more than one component --
	`enabled` by nine of them -- and until this pass a reader of any one cleared
	all of them. What replaces it is not the C++ parser this comment used to say
	it would take: a file only clears the owners it NAMES, by the pool accessor
	that is the sole way to reach a component, read out of `world.h` so a
	component added tomorrow is discriminated without touching this tool. A file
	that holds the name and names no owner is not evidence about any of them and
	is skipped.

	It paid for itself immediately. `InputBindingComponent::image` -- the example
	this comment used to give hypothetically -- turned out to be genuinely unread
	and is now argued for on the list below; `UIObjectComponent::rotation` turned
	out to be a documented property that nothing drew, cleared every run by
	`CFrame::rotation` in the editor's camera code.

	**What it still cannot do is separate two owners read in ONE file.**
	`render_world.cpp` reads a point light's `shadows` and a spot light's four
	lines apart, so both are cleared by that file whichever of them is really
	there. Attribution is per file because a file is the unit a lint can read
	without becoming a compiler; going finer is the parser, and it is still not
	worth buying.

	    lute tools/repo/inertcheck.luau
]]

local fs = require("@std/fs")
local path = require("@std/path")
local process = require("@std/process")

local ComponentsHeader = "engine/scene/include/luaug/scene/components.h"
local WorldHeader = "engine/scene/include/luaug/scene/world.h"
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

    -- The same datum as `displayName`, in a picture instead of a word, and here
    -- for the same argued reason. **It is also the find that paid for the
    -- attribution pass** (S7.13): this entry could not have been written before,
    -- because `ImageLabelComponent::image` cleared it and the sweep reported
    -- nothing to argue about.
    image = "a prompt glyph a game reads back; nothing in the engine draws a prompt",
}

--- Engine-state fields whose property is not just their name in PascalCase.
---
--- A service's knob is named for the service that owns it, and the field it
--- lands in is named for the struct that holds every service at once -- so
--- `StreamingService.LoadRadius` is `streamingLoadRadius` here. The mapping is
--- explicit rather than derived, because a rule that stripped a prefix would
--- also strip one from a field that meant it.
local EngineStateProperty: { [string]: string } = {
    requestedFixedTimestep = "FixedTimestep",
    streamingEnabled = "Enabled",
    streamingLoadRadius = "LoadRadius",
    streamingMinRadius = "MinRadius",
    streamingPauseOutsideLoadedArea = "PauseOutsideLoadedArea",
}

local function fail(message: string): never
    print(`inertcheck: ERROR: {message}`)
    return process.exit(1)
end

local function hasSuffix(text: string, suffix: string): boolean
    return #text >= #suffix and string.sub(text, -#suffix) == suffix
end

--- Every `.cpp`/`.h` under `engine/`, minus the kinds the header explains.
---
--- `declaring` is the header whose own declarations are being swept, and it is
--- a parameter rather than a constant because there are two of them now: a
--- component field's declaration lives in `components.h` and an engine-state
--- field's in `world.h`, and neither sweep may count its own declaration as a
--- reader (D055).
local function readerFiles(declaring: string): { string }
    local found: { string } = {}
    local pending: { string } = { EngineRoot }

    while #pending > 0 do
        local dir = table.remove(pending) :: string
        for _, entry in fs.listDirectory(dir) do
            local childPath = path.format(path.join(dir, entry.name))
            if entry.type == "dir" then
                -- **A test is not a reader** (S7.13). A test that sets a field
                -- and checks it came back demonstrates the accessor, which is
                -- the one thing `Inert` already means -- so counting it clears
                -- exactly the fields worth reporting. `GuiObject.Rotation` was
                -- cleared by the tests written to prove the drawing worked,
                -- which is as circular as it sounds.
                --
                -- It costs nothing to be strict here: dropping 106 of 383 files
                -- surfaced no field that engine code genuinely reads.
                if entry.name ~= "generated" and entry.name ~= "tests" then
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
            if hasSuffix(childPath, declaring) or hasSuffix(childPath, "native_accessors.cpp") then
                continue
            end
            table.insert(found, childPath)
        end
    end

    table.sort(found)
    return found
end

--- The field names of the structs in `header` whose names satisfy `wanted`, in
--- declaration order.
---
--- A hand-rolled scan rather than a parser: both files are one struct per class
--- with one field per line and no templates, which is a shape a hundred lines of
--- pattern matching reads correctly and a C++ parser would be absurd for.
--- Which structs declare a field of each name.
---
--- **The lint's one stated weakness, made addressable** (S7.13): eleven field
--- names in `components.h` are declared by more than one component -- `enabled`
--- by nine of them -- and a by-name sweep clears all of them when any one is
--- read. So `PointLightComponent::shadows` could be stored and unread forever
--- as long as somebody read the spot light's, which is precisely the shape of
--- defect this lint exists to catch.
local FieldOwners: { [string]: { string } } = {}

local function structFields(header: string, wanted: (name: string) -> boolean): { string }
    local source = fs.readFileToString(header)
    local fields: { string } = {}
    local seen: { [string]: boolean } = {}

    local owner: string? = nil
    local insideComponent = false
    for line in string.gmatch(source, "[^\n]*") do
        local structName = string.match(line, "^struct%s+([%a%d_]+)%s*$")
        if structName ~= nil then
            insideComponent = wanted(structName)
            owner = if insideComponent then structName else nil
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
        if owner ~= nil then
            local owners = FieldOwners[name]
            if owners == nil then
                owners = {}
                FieldOwners[name] = owners
            end
            table.insert(owners, owner)
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

--- What a source file must mention to be reading a given struct's fields.
---
--- **The discriminator is the accessor**, read out of `world.h` rather than
--- listed here, so a component added tomorrow is told apart from its siblings
--- without touching this tool. Every pool is reached exactly one way --
--- `world.pointLights()`, `world.engineState()` -- because the member is private
--- and the accessor is the only door.
---
--- Both shapes are taken: `ComponentPool<PartComponent>& parts()` and the bare
--- `EngineState& engineState()`. The second is not decoration -- the state
--- sweep's readers say `engineState()` and never say `EngineState`, so without
--- it three fields that `audio.cpp` and `services.cpp` demonstrably read are
--- reported unread.
local function poolAccessors(): { [string]: string }
    local source = fs.readFileToString(WorldHeader)
    local accessors: { [string]: string } = {}
    for component, accessor in string.gmatch(source, "ComponentPool<([%a%d_]+)>&%s+([%a%d_]+)%(%)") do
        accessors[component :: string] = accessor :: string
    end
    for component, accessor in string.gmatch(source, "%s([%a%d_]+)&%s+([%a%d_]+)%(%)%s*noexcept") do
        if accessors[component :: string] == nil then
            accessors[component :: string] = accessor :: string
        end
    end
    return accessors
end

--- Whether `source` is talking about `component` at all.
local function mentionsComponent(source: string, component: string, accessor: string?): boolean
    if string.find(source, component, 1, true) ~= nil then
        return true
    end
    return accessor ~= nil and string.find(source, `%f[%w]{accessor}%f[%W]`) ~= nil
end

--- One sweep: the fields declared in `header` by the structs `wanted` accepts,
--- against every engine source except that header itself.
---
--- Returns the fields nothing reads and whose property is not marked `Inert`,
--- and the number of sources it looked at -- which the summary prints, because
--- a sweep that silently looked at nothing would pass.
local function sweep(
    header: string,
    wanted: (name: string) -> boolean,
    inert: { [string]: boolean },
    propertyOf: (field: string) -> string
): ({ string }, number, number)
    local fields = structFields(header, wanted)
    if #fields == 0 then
        fail(`no fields found in {header} -- run this from the repository root`)
    end

    local sources: { string } = {}
    for _, file in readerFiles(string.match(header, "([^/]+)$") :: string) do
        table.insert(sources, fs.readFileToString(file))
    end

    local accessors = poolAccessors()

    local unread: { string } = {}
    for _, field in fields do
        if StorageOnly[field] ~= nil then
            continue
        end
        if inert[propertyOf(field)] then
            continue
        end

        -- **Who could this field belong to**, and a file only clears the owners
        -- it actually names. Several owners is the collision S7.13 is about;
        -- one owner is not the easy case it looks like, because a lone name is
        -- just as easily cleared by an unrelated token -- `displayName` was
        -- cleared for a milestone by `ContentTree::displayNameOf`, which is not
        -- a field of anything.
        local owners: { string } = FieldOwners[field] or {}
        local cleared: { [string]: boolean } = {}
        local clearedAll = false

        for _, source in sources do
            -- `%f[%w]` is a frontier pattern: it matches the boundary before a
            -- word character, so `image` does not match inside `imageColor`.
            -- Without it a field would be "read" by any longer field that
            -- contains its name.
            if string.find(source, `%f[%w]{field}%f[%W]`) == nil then
                continue
            end

            -- **A file that holds the name and names no owner is not evidence
            -- about any of them, and is skipped.** That is the whole of the
            -- narrowing, and it has to be this way round to be worth anything:
            -- clearing every owner instead would mean the first file with a
            -- local called `cframe` cleared the part's, the attachment's and the
            -- camera's, which is the by-name sweep again wearing a hat. Thirteen
            -- of these names hit exactly that on the first run.
            --
            -- What it costs is stated rather than hidden: a file that really
            -- does read a point light's field while naming neither
            -- `PointLightComponent` nor `pointLights` would be reported. The
            -- pool accessor is the only door to a component, so such a file
            -- would have to have been handed a reference from somewhere else --
            -- and the report says which component and which field, which is a
            -- minute to read and confirm, against a storage-only property
            -- surviving forever because a sibling class happened to use the same
            -- word.
            for _, owner in owners do
                if mentionsComponent(source, owner, accessors[owner]) then
                    cleared[owner] = true
                end
            end
        end

        if clearedAll then
            continue
        end

        if #owners == 0 then
            table.insert(unread, field)
            continue
        end
        for _, owner in owners do
            if not cleared[owner] then
                table.insert(unread, `{owner}::{field}`)
            end
        end
    end

    return unread, #fields, #sources
end

local inert = inertProperties()

local componentUnread, componentCount, componentSources = sweep(ComponentsHeader, function(name)
    return string.match(name, "Component$") ~= nil
end, inert, pascalCase)

-- **The second sweep, and D055 is the whole reason it exists.** A service with
-- one instance per world keeps its knobs in `EngineState` rather than in a
-- component pool, so half the properties in the API were never swept -- which
-- is how a pointer lock that nothing read survived from M6 to M8.
local stateUnread, stateCount, stateSources = sweep(
    WorldHeader,
    function(name)
        return name == "EngineState"
    end,
    inert,
    function(field)
        return EngineStateProperty[field] or pascalCase(field)
    end
)

local unread: { string } = {}
for _, field in componentUnread do
    table.insert(unread, field)
end
for _, field in stateUnread do
    table.insert(unread, field)
end

if #unread > 0 then
    for _, field in componentUnread do
        print(
            `inertcheck: component field {field} is stored and nothing reads it, and {pascalCase(field)} is not marked Inert`
        )
    end
    for _, field in stateUnread do
        local property = EngineStateProperty[field] or pascalCase(field)
        print(`inertcheck: engine state {field} is stored and nothing reads it, and {property} is not marked Inert`)
    end
    fail(
        `{#unread} field(s) stored and unread. Either something must act on the value, or its property carries `
            .. `Inert with the milestone that will (M4.5's marker, D030's lesson)`
    )
end

print(
    `inertcheck: ok -- {componentCount} component field(s) against {componentSources} source(s), `
        .. `{stateCount} engine-state field(s) against {stateSources}`
)
