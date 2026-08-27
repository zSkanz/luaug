--!strict
--[[
	The wire schema's lint (N1, ADR 0069).

	**Three lists of the same simulation state, and nothing compared them.**

	`api/wire/state.wire.luau` says what a replica is told. `api/defs/*.api.luau`
	says what a world has. `engine/scene/src/world_hash.cpp` says what a world IS,
	for determinism. A field in one and not the others is not a cosmetic drift: it
	is a replica confidently wrong about the state that decides the next tick, and
	the symptom is a divergence somebody debugs for a week.

	So this compares them, in the three directions that can go wrong:

	  1. **Every class the API declares is decided about.** Replicated or
	     excluded, by name. Absence is not an answer -- a class nobody thought
	     about and a class deliberately left out look identical in a schema.
	  2. **Every component-sourced wire field names a pool that exists.** A field
	     reading `parts` when the pool is `partComponents` is a generator that
	     emits code nobody can compile, discovered at the wrong end.
	  3. **Ids are permanent.** Live and retired alike, within a class, and for
	     channels and messages globally. This is `Schema.validate`'s half and it
	     runs from here so there is one command to run.

	It does NOT try to prove that every hashed field is replicated. It should not:
	`Terrain` is hashed and deliberately not replicated, and a lint that demanded
	the two lists match exactly would be a lint that argued with a decision. What
	it demands is that the difference is *stated*.
]]

local fs = require("@std/fs")
local process = require("@std/process")

local Defs = require("../../api/defs")
local Wire = require("../../api/wire")

local Schema = require("../../api/wire/schema")

--- Named once, because `X or {}` is a union the analyser will not iterate and
--- naming the element type at each use would be the same sentence four times.
local ByTag: { Schema.ExcludedTag } = Wire.Definition.ExcludedByTag or {}
local Common: { Schema.Field } = Wire.Definition.Common or {}
local ApiSchema = require("../../api/schema")
local ApiClasses: { ApiSchema.Class } = Defs.Classes or {}

local WorldHeader = "engine/scene/include/luaug/scene/world.h"

local function fail(message: string): never
    print(`wirecheck: ERROR: {message}`)
    return process.exit(1)
end

--- The component pool names, read from the macro list that defines them rather
--- than from a copy here -- which is the whole point of this file.
local function poolNames(): { [string]: boolean }
    local ok, text = pcall(function()
        return fs.readFileToString(WorldHeader)
    end)
    if not ok or type(text) ~= "string" then
        fail(`could not read {WorldHeader}`)
    end

    local names: { [string]: boolean } = {}
    for pool in string.gmatch(text :: string, "X%(%w+Component,%s*(%w+)%)") do
        names[pool] = true
    end
    if next(names) == nil then
        fail(`found no component pools in {WorldHeader}; the macro list's shape must have changed`)
    end
    return names
end

--- Tags the schema excluded wholesale, as a set.
local function excludedTags(): { [string]: boolean }
    local tags: { [string]: boolean } = {}
    for _, entry in ByTag do
        tags[entry.Tag] = true
    end
    return tags
end

--- Classes that need no decision of their own.
---
--- **Abstract is derived rather than enumerated**: an abstract class has no
--- instances, so there is nothing to replicate and nothing to exclude. And a
--- tag the schema excluded wholesale carries its own decision -- written once
--- in the schema rather than repeated per class here, which is the difference
--- between a rule and a list.
local function needsDecision(class: ApiSchema.Class, tagged: { [string]: boolean }): boolean
    local tags: { ApiSchema.ClassTag } = class.Tags or {}
    for _, tag in tags do
        if tag == "Abstract" or tagged[tag] then
            return false
        end
    end
    return true
end

local problems: { string } = {}

local function report(message: string)
    table.insert(problems, message)
end

-- 1. The schema's own rules: permanent ids, named pools, documented fields.
for _, problem in Wire.validate(Wire.Definition) do
    report(problem)
end

-- 2. Component-sourced fields name a pool that exists.
local pools = poolNames()
for _, class in Wire.classes() do
    for _, field in class.Fields do
        if field.Source == "Component" and field.Component ~= nil then
            if not pools[field.Component] then
                report(`{class.Name}.{field.Name}: no component pool named "{field.Component}"`)
            end
        end
    end
end

-- 3. Every concrete class the API declares is replicated or excluded, by name.
local decided: { [string]: string } = {}
for _, class in Wire.classes() do
    decided[class.Name] = "replicated"
end
for _, excluded in Wire.excluded() do
    if decided[excluded.Name] ~= nil then
        report(`{excluded.Name}: both replicated and excluded`)
    end
    decided[excluded.Name] = "excluded"
    if excluded.Reason == "" then
        report(`{excluded.Name}: an exclusion has to say why`)
    end
end

--- **A class whose ancestor is decided is decided.** `Part` inherits
--- `BasePart`'s fields, so requiring a row for every leaf would be requiring the
--- schema to repeat the class hierarchy -- which is the second list this file
--- exists to prevent.
local superOf: { [string]: string } = {}
for _, class in ApiClasses do
    if class.Extends ~= nil then
        superOf[class.Name] = class.Extends
    end
end

local function decidedThrough(name: string): boolean
    local at: string? = name
    local guard = 0
    while at ~= nil and guard < 64 do
        if decided[at] ~= nil then
            return true
        end
        at = superOf[at]
        guard += 1
    end
    return false
end

local tagged = excludedTags()
for _, entry in ByTag do
    if entry.Reason == "" then
        report(`tag {entry.Tag}: an exclusion has to say why`)
    end
end

local undecided: { string } = {}
for _, class in ApiClasses do
    if not needsDecision(class, tagged) then
        continue
    end
    if not decidedThrough(class.Name) then
        table.insert(undecided, class.Name)
    end
end

table.sort(undecided)
for _, name in undecided do
    report(
        `{name}: neither replicated nor excluded. Every concrete class is a decision -- `
            .. `add it to Classes with its fields, or to Excluded with the reason it is not on the wire`
    )
end

if #problems > 0 then
    table.sort(problems)
    for _, problem in problems do
        print(`wirecheck: {problem}`)
    end
    fail(`{#problems} problem(s) in the wire schema`)
end

local fieldCount = #Common
for _, class in Wire.classes() do
    fieldCount += #class.Fields
end

print(
    `wirecheck: ok -- protocol v{Wire.ProtocolVersion}, {#Wire.classes()} replicated class(es) `
        .. `carrying {fieldCount} field(s), {#Wire.excluded()} excluded by name, `
        .. `{#Wire.channels()} channel(s), {#Wire.messages()} message(s)`
)
