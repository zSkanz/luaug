--!strict
--[[
	C++ reflection-table generator (architecture.md §4, ADR 0028).

	Emits `engine/<module>/generated/class_descriptors.gen.{h,cpp}` from the
	IDL: the `PropertyDesc`/`MethodDesc`/`EventDesc` arrays `ClassRegistry`
	holds spans into, one `ClassDescriptor` per class, and the
	`registerClasses` entry point that installs them parents-first -- plus, for
	`scene` alone, the `EnumDescriptor` tables and `registerEnums`, because an
	enum item's numeric value is what a property write validates and what a
	snapshot stores, so the list a script reads and the list the engine checks
	against must be one list.

	One file per module, not one file: architecture.md §2 rule 3 says `scene`
	never includes a higher module, so `render`, `input` and `ui` register
	their classes INTO scene's registry from their own translation units
	(`render::registerSceneTypes(scene::ClassRegistry&)`). `Class.Module` in
	the IDL picks which file a descriptor lands in, and `Modules` below is the
	declared order those files' `registerClasses` must run in.

	This is what makes architecture.md §4's "all descriptors are generated" a
	fact rather than an intention. `engine/scene/tests/scene_fixture.cpp` is the
	hand-built version of the same shape, and it exists so the tests exercise
	the paths these tables drive -- not as a second source of truth.

	The output is CHECKED IN and diff-checked by `scripts/gates/luau-check.sh`,
	the same arrangement `runtime/types/engine.d.luau` already has. That keeps
	Lute off the critical path of a C++ build while still making drift a build
	failure rather than a discovery.

	What this generator does NOT emit, deliberately:

	- The native functions themselves. It declares them; `native_accessors.cpp`
	  implements them. Declaring every one the definitions reference is the
	  point: a missing implementation is then a link error rather than a null
	  function pointer that reads as `nil` at runtime.
	- Doc KEYS. The prose itself is emitted, verbatim and in English, into
	  `.doc` on every descriptor -- the property grid needs a tooltip and a
	  `TextKey` is a hash with nothing on the other end of it. R3 governs what a
	  PLAYER reads; this text is the API reference, read by whoever is building
	  a game, and `gen_dts` and `gen_reference` already emit the same sentences
	  verbatim from the same field. Routing this third copy through the catalog
	  would put every paragraph of the reference into `i18n/en.json`, which
	  every shipped game loads at boot, to be resolved by a panel that shipping
	  builds do not draw. `class_registry.h` carries the same note where the
	  field is declared.
	- Anything about components. `Class.Storage` and `Property.Native` are
	  *names* of hand-written functions, so this generator never learns what a
	  component is -- moving a property's storage is a change in one C++ file.

	A class naming a module `Modules` does not list is an error rather than a
	skip: a surface that goes missing without anybody being told is the failure
	mode this file is meant not to have.

	    lute api/generator/gen_cpp.luau
]]

local fs = require("@std/fs")
local process = require("@std/process")

local Definition = require("../defs")
local Schema = require("../schema")

--- An engine module that registers classes into scene's registry.
type Module = {
    Name: string,
    --- architecture.md §2's layer. It decides which parents this module's
    --- classes may extend: a class registered by a module at an
    --- equal-or-higher layer is a layering violation, not an inheritance.
    Layer: number,
}

--- Every module that owns a generated descriptor file, in the order their
--- `registerClasses` must run -- a subclass needs its parent's `ClassId`, so a
--- module comes after every module it inherits from, and layers are
--- non-decreasing down this list. `engine/app/src/world_host.cpp` calls them in
--- this order.
---
--- Declared as an array rather than collected from the definitions, because
--- R10 forbids a container's iteration order from deciding what a generated
--- file contains. Adding a module is a line here and a call there.
---
--- `Layer` is architecture.md §2's; `tools/repo/checklayers.luau` carries the
--- same numbers for `#include` edges and enforces them there.
local Modules: { Module } = {
    { Name = "scene", Layer = 3 },
    { Name = "render", Layer = 4 },
    { Name = "audio", Layer = 4 },
    { Name = "input", Layer = 4 },
    { Name = "ui", Layer = 5 },
}

--- `scene` owns the class and the enum registry; every higher module registers
--- INTO them (architecture.md §2, rule 3). That makes it the one module whose
--- file spells the registry types unqualified, and the only one that registers
--- the enums -- they are global, and a global table with two owners is two
--- numbering schemes.
local RegistryModule = "scene"

--- `scene::ValueType`, keyed by the Luau type the IDL writes. Instance-ish and
--- `Enum.X` types are resolved below rather than listed, because both are open
--- sets that grow every time a class or an enum ships.
local ScalarValueTypes: { [string]: string } = {
    ["string"] = "String",
    -- `Content` is a type ALIAS of `string` in v1 (api-design.md §2.3), so it
    -- carries a distinct name in the type definitions and no distinct storage:
    -- a Content property accepts and returns a string, because that is what one
    -- is. §2.3 reserves it to become opaque later, and this line is where it
    -- would stop being an alias -- a `ValueType::Content` enumerator, a `Value`
    -- alternative and its own invalid-set key. Adding those now would make the
    -- engine disagree with its own documentation about what a Content is.
    ["Content"] = "String",
    ["number"] = "Number",
    ["boolean"] = "Bool",
    ["vector"] = "Vector3",
    ["CFrame"] = "CFrame",
    ["Color3"] = "Color3",
    ["Vector2"] = "Vector2",
    ["UDim"] = "UDim",
    ["UDim2"] = "UDim2",
    ["Rect"] = "Rect",
}

--- `PropertyDesc::errKeyOnInvalidSet`, keyed by `ValueType`. One key per type
--- rather than a single "invalid value", because a rejection that cannot name
--- what it wanted is a rejection the caller has to guess at.
local InvalidSetKeys: { [string]: string } = {
    Bool = "scene.err.expected_boolean",
    Number = "scene.err.expected_number",
    String = "scene.err.expected_string",
    Vector3 = "scene.err.expected_vector",
    CFrame = "scene.err.expected_cframe",
    Color3 = "scene.err.expected_color3",
    Instance = "scene.err.expected_instance",
    EnumItem = "scene.err.expected_enum_item",
    Vector2 = "scene.err.expected_vector2",
    UDim = "scene.err.expected_udim",
    UDim2 = "scene.err.expected_udim2",
    Rect = "scene.err.expected_rect",
}

--- `Schema.ClassTag` -> `scene::ClassFlags`. The two lists are the same set
--- spelled twice, so a tag with no flag is a generator error rather than a tag
--- that quietly does nothing.
local ClassFlagNames: { [string]: string } = {
    Service = "ClassFlags::Service",
    Abstract = "ClassFlags::Abstract",
    NotCreatable = "ClassFlags::NotCreatable",
    DevOnly = "ClassFlags::DevOnly",
}

--- `list or {}` types as `{T} | {}`, which the new solver declines to iterate.
--- A generic that returns `{T}` collapses it at one call site instead of at
--- every loop. Same reason as in `gen_dts.luau`.
local function orEmpty<T>(list: { T }?): { T }
    return list or {}
end

local function upperFirst(text: string): string
    return string.upper(string.sub(text, 1, 1)) .. string.sub(text, 2)
end

local function lowerFirst(text: string): string
    return string.lower(string.sub(text, 1, 1)) .. string.sub(text, 2)
end

--- Classes are emitted parents-first, because `ClassRegistry::registerClass`
--- refuses a class whose super is not registered yet, and because the generated
--- code names the parent's `ClassId` as a local. Same walk as `gen_dts.luau`,
--- where the constraint is that `extends` has to name a declared type.
local function sortedByHierarchy(classes: { Schema.Class }): { Schema.Class }
    local byName: { [string]: Schema.Class } = {}
    for _, class in classes do
        byName[class.Name] = class
    end

    local emitted: { [string]: boolean } = {}
    local ordered: { Schema.Class } = {}

    local function visit(class: Schema.Class)
        if emitted[class.Name] then
            return
        end
        emitted[class.Name] = true
        local parent = class.Extends
        if parent ~= nil and byName[parent] ~= nil then
            visit(byName[parent])
        end
        table.insert(ordered, class)
    end

    for _, class in classes do
        visit(class)
    end
    return ordered
end

--- Every class name in the IDL, whatever module it belongs to: a `scene` class
--- may perfectly well declare a property typed as a class another module
--- registers, and that property is still an `Instance` reference.
local AllClasses: { Schema.Class } = orEmpty(Definition.Classes)
local ClassNames: { [string]: boolean } = {}
--- Keyed by class name across every module, because an inherited event slot is
--- counted through the whole ancestry and a chain may cross a module boundary
--- (`MeshPart` extends `BasePart`, which `scene` registers).
local ClassByName: { [string]: Schema.Class } = {}
for _, class in AllClasses do
    ClassNames[class.Name] = true
    ClassByName[class.Name] = class
end

--- `nil` for a type this generator cannot express, which is reported and
--- refused rather than defaulted to `Nil` -- a property silently typed as
--- nothing accepts nothing, and does it at runtime.
local function valueTypeOf(typeName: string): string?
    local bare = string.match(typeName, "^(.-)%?$") or typeName
    local scalar = ScalarValueTypes[bare]
    if scalar ~= nil then
        return scalar
    end
    if string.match(bare, "^Enum%.[%w_]+$") ~= nil then
        return "EnumItem"
    end
    if bare == "Instance" or ClassNames[bare] then
        return "Instance"
    end
    return nil
end

--- The enum an `Enum.X` type names, or `nil` for any other type. The same
--- pattern `valueTypeOf` matches, kept separate because one answers *which*
--- `ValueType` and this one answers *which enum* -- and `PropertyDesc` now
--- records both.
local function enumNameOf(typeName: string): string?
    local bare = string.match(typeName, "^(.-)%?$") or typeName
    return string.match(bare, "^Enum%.([%w_]+)$")
end

--- A C++ string literal for arbitrary IDL prose.
---
--- Anything outside printable ASCII goes out as a three-digit OCTAL escape
--- rather than as itself. These files are byte-compared by the Luau gate and
--- compiled by three toolchains, and a raw UTF-8 byte inside a narrow literal
--- means whatever the compiler's source charset says it means -- MSVC's is the
--- system code page unless somebody remembered a flag, and the IDL's prose
--- carries both a section sign and a pi. Three digits and not a hex escape,
--- because a hex escape has no length limit and swallows the character after it.
---
--- A digit following an octal escape SPLITS the literal into two adjacent ones.
--- The escape is already three digits long and cannot absorb a fourth, but MSVC
--- warns about the shape regardless (C4125) and warnings are errors on
--- `engine/`, so the gap is what keeps the generated file compiling.
local function cppString(text: string): string
    local out: { string } = { '"' }
    local afterOctal = false
    for index = 1, #text do
        local byte = string.byte(text, index) :: number
        local c = string.sub(text, index, index)
        if afterOctal and byte >= 0x30 and byte <= 0x39 then
            table.insert(out, '" "')
        end
        afterOctal = false
        if c == "\\" then
            table.insert(out, "\\\\")
        elseif c == '"' then
            table.insert(out, '\\"')
        elseif byte >= 0x20 and byte < 0x7F then
            table.insert(out, c)
        else
            table.insert(out, string.format("\\%03o", byte))
            afterOctal = true
        end
    end
    table.insert(out, '"')
    return table.concat(out)
end

--- The base name of the hand-written accessor pair. `Native` is an override,
--- not a requirement: the M2 surface was scoped so that every declared property
--- is one the engine implements, so the name is derived as `<Class><Property>`
--- in lower camel and the definitions carry nothing.
---
--- `Native = ""` is the one way the IDL can say "declared, not yet backed". It
--- produces null accessors and is counted, so a surface that ships ahead of its
--- implementation is a number in this generator's output rather than a
--- discovery made by reading `nil` off a property.
local function nativeBase(class: Schema.Class, property: Schema.Property): string?
    local override = property.Native
    if override ~= nil then
        return if override == "" then nil else override
    end
    return lowerFirst(class.Name .. property.Name)
end

local function classIdVariable(className: string): string
    return `{lowerFirst(className)}Class`
end

--- Event slots are dense per class with the inherited ones first, the same
--- numbering `ClassRegistry::propertySlot` gives properties: a class keeps its
--- super's numbering and appends its own after it, so `ChildAdded` is slot 0 on
--- every class that inherits it and a fire is an array offset.
local function inheritedEventCount(class: Schema.Class): number
    local total = 0
    local parent = class.Extends
    while parent ~= nil do
        local parentClass = ClassByName[parent]
        if parentClass == nil then
            break
        end
        total += #orEmpty(parentClass.Events)
        parent = parentClass.Extends
    end
    return total
end

local Errors: { string } = {}

local ModuleLayers: { [string]: number } = {}
for index, target in Modules do
    if ModuleLayers[target.Name] ~= nil then
        table.insert(Errors, `Modules lists "{target.Name}" twice`)
    end
    ModuleLayers[target.Name] = target.Layer
    if index > 1 and target.Layer < Modules[index - 1].Layer then
        -- Emission order IS registration order, and a class cannot extend one
        -- registered later. Layers going backwards down this list would let a
        -- parent be looked up before it exists.
        table.insert(
            Errors,
            `Modules: "{target.Name}" (L{target.Layer}) follows "{Modules[index - 1].Name}" `
                .. `(L{Modules[index - 1].Layer}); the list is registration order and layers may not decrease`
        )
    end
end
if ModuleLayers[RegistryModule] == nil then
    table.insert(Errors, `Modules does not list "{RegistryModule}", which owns the registries`)
end

for _, class in AllClasses do
    if ModuleLayers[class.Module] == nil then
        table.insert(
            Errors,
            `{class.Name}: module "{class.Module}" has no generated file -- add it to Modules in gen_cpp.luau`
        )
    end
end

local Enums: { Schema.Enum } = orEmpty(Definition.Enums)
--- Every declared enum, so that a property typed `Enum.Something` that no enum
--- declares is a generator error rather than a `PropertyDesc` naming a domain
--- the registry will never resolve. `check.luau` cannot catch it: the type is a
--- string, and a misspelling is still a well-formed one.
local EnumNames: { [string]: boolean } = {}
for _, enumeration in Enums do
    EnumNames[enumeration.Name] = true
end
local Ordered: { Schema.Class } = sortedByHierarchy(AllClasses)

--- One `{h, cpp}` pair, held until every module has generated cleanly. Nothing
--- is written while an error is outstanding: a half-written set of files is a
--- build that fails somewhere other than where the mistake is.
type Output = {
    Path: string,
    Lines: { string },
}

local Outputs: { Output } = {}
local EnumItemCount = 0

local function generate(target: Module)
    --- Inside `luaug::scene` the registry types are unqualified; every other
    --- module names them through `scene::`.
    local scope = if target.Name == RegistryModule then "" else "scene::"
    local ownsRegistries = target.Name == RegistryModule

    local function flagsExpression(class: Schema.Class): string
        local parts: { string } = {}
        for _, tag in orEmpty(class.Tags) do
            local flag = ClassFlagNames[tag]
            if flag == nil then
                table.insert(Errors, `{class.Name}: tag "{tag}" has no ClassFlags equivalent`)
                continue
            end
            table.insert(parts, scope .. flag)
        end
        if #parts == 0 then
            return `{scope}ClassFlags::None`
        end
        return table.concat(parts, " | ")
    end

    local emitted: { Schema.Class } = {}
    local emittedNames: { [string]: boolean } = {}
    for _, class in Ordered do
        if class.Module == target.Name then
            table.insert(emitted, class)
            emittedNames[class.Name] = true
        end
    end

    --- A parent this module does not register, in the order its subclasses
    --- appear -- so the emitted lookups are ordered by the definitions rather
    --- than by a set's iteration.
    local externalParents: { string } = {}
    local externalSeen: { [string]: boolean } = {}

    for _, class in emitted do
        local parent = class.Extends
        if parent == nil or emittedNames[parent] then
            continue
        end
        local parentClass = ClassByName[parent]
        if parentClass == nil then
            table.insert(Errors, `{class.Name}: extends "{parent}", which no module declares`)
            continue
        end
        local parentLayer = ModuleLayers[parentClass.Module]
        if parentLayer == nil then
            -- Already reported against the parent's own definition.
            continue
        end
        if parentLayer >= target.Layer then
            -- architecture.md §2: a module sees only strictly lower layers. An
            -- equal-or-higher parent would need this module's registration to
            -- run after one that cannot be linked from here at all.
            table.insert(
                Errors,
                `{class.Name}: extends "{parent}", registered by module "{parentClass.Module}" `
                    .. `(L{parentLayer}), which is not below "{target.Name}" (L{target.Layer}) -- `
                    .. `architecture.md section 2 allows only strictly lower layers`
            )
            continue
        end
        if not externalSeen[parent] then
            externalSeen[parent] = true
            table.insert(externalParents, parent)
        end
    end

    --- The header declares exactly what the source references, so the two are
    --- collected together while the tables are emitted rather than walked twice.
    type Declaration = {
        Class: string,
        Lines: { string },
    }

    local declarations: { Declaration } = {}
    local unbacked = 0
    local propertyCount = 0
    local methodCount = 0
    local eventCount = 0
    local getterCount = 0
    local setterCount = 0
    local storageCount = 0

    local source: { string } = {}
    local function emit(line: string)
        table.insert(source, line)
    end

    local fileBanner: { string } = {
        "// GENERATED by api/generator/gen_cpp.luau from api/defs/*.api.luau.",
        "//",
        "// Do not edit. The Luau gate regenerates this file and fails on a diff, so a",
        -- Deliberately ASCII-only, section signs included: this file is regenerated
        -- and byte-compared, so an encoding difference between two machines would
        -- read as a spurious diff rather than as the toolchain problem it is.
        "// hand edit is a change that survives exactly until the next build",
        "// (architecture.md section 4).",
    }

    for _, line in fileBanner do
        emit(line)
    end
    emit("//")
    emit("// Every `.doc` below is the IDL's own English prose, not a catalog key: the")
    emit("// audience for a reflection tooltip is whoever is building a game, and a")
    emit("// `TextKey` is a hash with nothing on the other end of it unless the catalog is")
    emit("// loaded. See `class_registry.h` for the whole of that reasoning.")
    emit("//")
    emit("// Anything outside printable ASCII is written as a three-digit octal escape, so")
    emit("// these files stay pure ASCII whatever a compiler believes their source charset")
    emit("// to be, and the bytes reaching the program are the IDL's own UTF-8.")
    emit("")
    emit(`#include "class_descriptors.gen.h"`)
    emit("")
    emit("#include <array>")
    emit("")
    emit(`#include "luaug/core/name_atom.h"`)
    emit(`#include "luaug/core/text_key.h"`)
    emit(`#include "luaug/scene/class_registry.h"`)
    emit(`#include "luaug/scene/value.h"`)
    emit("")
    emit(`namespace luaug::{target.Name}::generated`)
    emit("{")
    emit("")
    emit("// The descriptor arrays have static storage duration because")
    emit("// `ClassDescriptor` holds `std::span`s into them and the registry outlives")
    emit("// this call -- a local array would leave every descriptor pointing at a dead")
    emit("// frame. They are ASSIGNED on each call rather than initialised on the first,")
    emit("// so a second registry built over a second `AtomTable` gets ids from its own")
    emit("// table instead of inheriting the first one's alphabet. The two registries")
    if ownsRegistries then
        emit("// still share this storage and so cannot be live at once, which is exactly")
        emit("// why `scene_fixture.h` owns per-instance arrays instead of calling in here.")
    else
        emit("// still share this storage and so cannot be live at once.")
    end
    emit("//")
    emit("// A `ClassId` is bound to a name only where a later class extends that class;")
    emit("// binding one nothing reads would be an unused local, and warnings are errors")
    emit("// on `engine/` (architecture.md section 8).")
    if #emitted > 0 then
        emit(`void registerClasses({scope}ClassRegistry& classes, core::AtomTable& atoms)`)
    else
        -- Unnamed, because `-Wunused-parameter` is an error on `engine/` and a
        -- module whose classes have not shipped yet still has to compile.
        emit(`void registerClasses({scope}ClassRegistry&, core::AtomTable&)`)
    end
    emit("{")

    if #externalParents > 0 then
        emit("    // Registered by a lower module (architecture.md section 2, rule 3), so")
        emit("    // the id is a lookup rather than a local: the module that owns a class is")
        emit("    // the module that holds its `ClassId`. That module's `registerClasses`")
        emit("    // must already have run -- engine/app/src/world_host.cpp orders them.")
        for _, parent in externalParents do
            emit(`    const {scope}ClassId {classIdVariable(parent)} = classes.findId(atoms.intern("{parent}"));`)
        end
    end

    local parents: { [string]: boolean } = {}
    for _, class in emitted do
        local parent = class.Extends
        if parent ~= nil then
            parents[parent] = true
        end
    end

    for index, class in emitted do
        if index > 1 or #externalParents > 0 then
            emit("")
        end
        emit(`    // --- {class.Name} ---`)

        local prefix = lowerFirst(class.Name)
        local properties = orEmpty(class.Properties)
        local methods = orEmpty(class.Methods)
        local events = orEmpty(class.Events)
        local declared: { string } = {}

        if #properties > 0 then
            local arrayName = `{prefix}Properties`
            emit(`    static std::array<{scope}PropertyDesc, {#properties}> {arrayName};`)
            emit(`    {arrayName} = \{\{`)
            for _, property in properties do
                propertyCount += 1
                -- Bound to a non-optional local rather than narrowed in place: the
                -- new solver keeps `string?` across the assignment in the `nil`
                -- branch, and every use below wants a `string`.
                local resolved = valueTypeOf(property.Type)
                local valueType = "Nil"
                if resolved == nil then
                    table.insert(
                        Errors,
                        `{class.Name}.{property.Name}: type "{property.Type}" has no scene::ValueType equivalent`
                    )
                else
                    valueType = resolved
                end

                local base = nativeBase(class, property)
                local getter = "nullptr"
                local setter = "nullptr"
                if base == nil then
                    unbacked += 1
                else
                    getterCount += 1
                    getter = `native::get{upperFirst(base)}`
                    table.insert(
                        declared,
                        `{scope}Value get{upperFirst(base)}(const {scope}World& world, core::InstanceId id);`
                    )
                    -- A read-only property gets a getter and a null setter, and no
                    -- setter is DECLARED for it: declaring one asks somebody to
                    -- write a function that must never be called.
                    if property.ReadOnly ~= true then
                        setterCount += 1
                        setter = `native::set{upperFirst(base)}`
                        table.insert(
                            declared,
                            `bool set{upperFirst(base)}({scope}World& world, core::InstanceId id, `
                                .. `const {scope}Value& value);`
                        )
                    end
                end

                -- The property's own key wins over the one derived from its
                -- type (D021). A ranged property has no way to report which of
                -- its conditions a write failed -- `set` answers with a bool --
                -- so the key states the whole domain, which is true of a
                -- wrong-typed value as much as of an out-of-range one.
                local errorKey = property.ErrorKey
                if errorKey == nil then
                    errorKey = InvalidSetKeys[valueType]
                end
                if errorKey == nil then
                    table.insert(Errors, `{class.Name}.{property.Name}: no invalid-set key for ValueType::{valueType}`)
                    errorKey = "scene.err.unknown_property"
                end

                -- The enum's NAME and not its `EnumId`: the id is assigned by
                -- `EnumRegistry::registerEnum` in registration order, and these
                -- tables are `static` and shared by every registry a process
                -- builds. `class_registry.h` states the rest of the reasoning.
                local enumName = enumNameOf(property.Type)
                if enumName ~= nil and not EnumNames[enumName] then
                    table.insert(Errors, `{class.Name}.{property.Name}: no enum named "{enumName}" is declared`)
                    enumName = nil
                elseif enumName == nil and valueType == "EnumItem" then
                    table.insert(
                        Errors,
                        `{class.Name}.{property.Name}: type "{property.Type}" is an EnumItem with no enum name`
                    )
                end

                emit(`        {scope}PropertyDesc\{`)
                emit(`            .name = atoms.intern("{property.Name}"),`)
                emit(`            .type = {scope}ValueType::{valueType},`)
                if enumName ~= nil then
                    emit(`            .enumName = atoms.intern("{enumName}"),`)
                end
                if property.ContentKind ~= nil then
                    -- The same shape and the same argument as `enumName`: which
                    -- files a `Content` may name is a fact about the CLASS, and
                    -- the value -- a string -- can never answer it.
                    emit(`            .contentKind = atoms.intern("{property.ContentKind}"),`)
                end
                if property.Code == true then
                    -- Luau text rather than a value, so the grid summarises it
                    -- instead of drawing it. A fact about the class, for the
                    -- reason `ContentKind` is one.
                    emit("            .code = true,")
                end
                if property.Observed == true then
                    -- The engine watches this one, so its writes reach the
                    -- change queue with or without a subscriber. Emitted only
                    -- when true: the default is the quiet path and every other
                    -- property in the IDL wants it.
                    emit("            .observed = true,")
                end
                emit(`            .threadSafety = {scope}ThreadSafety::{property.ThreadSafety or "Unsafe"},`)
                emit(`            .readOnly = {if property.ReadOnly == true then "true" else "false"},`)
                emit(`            .inert = {if property.Inert == true then "true" else "false"},`)
                emit(`            .doc = {cppString(property.Doc)},`)
                emit(`            .errKeyOnInvalidSet = LUAUG_TR("{errorKey}"),`)
                emit(`            .get = {getter},`)
                emit(`            .set = {setter},`)
                emit("        },")
            end
            emit("    }};")
        end

        if #methods > 0 then
            local arrayName = `{prefix}Methods`
            emit(`    static std::array<{scope}MethodDesc, {#methods}> {arrayName};`)
            emit(`    {arrayName} = \{\{`)
            for _, method in methods do
                methodCount += 1
                emit(`        {scope}MethodDesc\{`)
                emit(`            .name = atoms.intern("{method.Name}"),`)
                emit(`            .yields = {if method.Yields == true then "true" else "false"},`)
                emit(`            .threadSafety = {scope}ThreadSafety::{method.ThreadSafety or "Unsafe"},`)
                emit(`            .doc = {cppString(method.Doc)},`)
                emit("        },")
            end
            emit("    }};")
        end

        if #events > 0 then
            local arrayName = `{prefix}Events`
            local slot = inheritedEventCount(class)
            emit(`    static std::array<{scope}EventDesc, {#events}> {arrayName};`)
            emit(`    {arrayName} = \{\{`)
            for _, event in events do
                eventCount += 1
                emit(`        {scope}EventDesc\{`)
                emit(`            .name = atoms.intern("{event.Name}"),`)
                emit(`            .slot = {slot},`)
                emit(`            .doc = {cppString(event.Doc)},`)
                emit("        },")
                slot += 1
            end
            emit("    }};")
        end

        local descriptor = `{prefix}Desc`
        emit(`    {scope}ClassDescriptor {descriptor};`)
        emit(`    {descriptor}.name = atoms.intern("{class.Name}");`)
        if class.Extends ~= nil then
            emit(`    {descriptor}.super = {classIdVariable(class.Extends)};`)
        end
        emit(`    {descriptor}.flags = {flagsExpression(class)};`)
        emit(`    {descriptor}.defaultName = atoms.intern("{class.DefaultName or class.Name}");`)
        emit(`    {descriptor}.doc = {cppString(class.Doc)};`)
        if #properties > 0 then
            emit(`    {descriptor}.properties = {prefix}Properties;`)
        end
        if #methods > 0 then
            emit(`    {descriptor}.methods = {prefix}Methods;`)
        end
        if #events > 0 then
            emit(`    {descriptor}.events = {prefix}Events;`)
        end

        -- Only a class that DECLARES storage carries a hook. `World::create` walks
        -- the ancestry root-first and calls every one it finds, so repeating a
        -- parent's name on a child would attach the same component twice.
        local storage = class.Storage
        if storage ~= nil then
            storageCount += 1
            emit(`    {descriptor}.attachComponents = native::attach{upperFirst(storage)}Components;`)
            emit(`    {descriptor}.detachComponents = native::detach{upperFirst(storage)}Components;`)
            table.insert(
                declared,
                `void attach{upperFirst(storage)}Components({scope}World& world, core::InstanceId id);`
            )
            table.insert(
                declared,
                `void detach{upperFirst(storage)}Components({scope}World& world, core::InstanceId id);`
            )
        end

        if parents[class.Name] then
            emit(`    const {scope}ClassId {classIdVariable(class.Name)} = classes.registerClass({descriptor});`)
        else
            emit(`    classes.registerClass({descriptor});`)
        end

        if #declared > 0 then
            table.insert(declarations, { Class = class.Name, Lines = declared })
        end
    end

    emit("}")

    --- Enums are registered in declaration order, which fixes the `EnumId` each
    --- one gets: the ids are emitted as header constants so that a hand-written
    --- accessor validating an enum property names the same number this file
    --- assigns. `enums.api.luau` already treats item values as contract; the id
    --- is the other half of the pair a `Value` stores.
    if ownsRegistries then
        emit("")
        emit("// Registered in declaration order, so an enum's `EnumId` is its position in")
        emit("// `enums.api.luau` and the constants in the header stay true. Items keep")
        emit("// declaration order too -- that is `GetEnumItems`'s documented order, and R10")
        emit("// forbids a container from being the thing that decides it.")
        if #Enums > 0 then
            emit("void registerEnums(EnumRegistry& enums, core::AtomTable& atoms)")
        else
            emit("void registerEnums(EnumRegistry&, core::AtomTable&)")
        end
        emit("{")

        for index, enumeration in Enums do
            if index > 1 then
                emit("")
            end
            local prefix = lowerFirst(enumeration.Name)
            local arrayName = `{prefix}Items`
            emit(`    // --- {enumeration.Name} ---`)
            emit(`    static std::array<EnumItemDesc, {#enumeration.Items}> {arrayName};`)
            emit(`    {arrayName} = \{\{`)
            for _, item in enumeration.Items do
                EnumItemCount += 1
                emit("        EnumItemDesc{")
                emit(`            .name = atoms.intern("{item.Name}"),`)
                emit(`            .value = {item.Value},`)
                emit("            .docKey = {},")
                emit("        },")
            end
            emit("    }};")

            local descriptor = `{prefix}Desc`
            emit(`    EnumDescriptor {descriptor};`)
            emit(`    {descriptor}.name = atoms.intern("{enumeration.Name}");`)
            emit(`    {descriptor}.docKey = \{};`)
            emit(`    {descriptor}.items = {arrayName};`)
            emit(`    enums.registerEnum({descriptor});`)
        end

        emit("}")
    end

    emit("")
    emit(`\} // namespace luaug::{target.Name}::generated`)
    emit("")

    local header: { string } = {}
    local function emitHeader(line: string)
        table.insert(header, line)
    end

    for _, line in fileBanner do
        emitHeader(line)
    end
    emitHeader("//")
    emitHeader("// The functions below are DECLARED here and implemented by hand in")
    emitHeader(`// engine/{target.Name}/src/native_accessors.cpp (architecture.md section 4: the`)
    emitHeader("// generated tables point at native functions, and those functions are the")
    emitHeader("// only hand-written half). Declaring every one the definitions reference is")
    emitHeader("// what makes a missing implementation a link error rather than a null")
    emitHeader("// function pointer that reads as nil the first time a script touches it.")
    emitHeader("#pragma once")
    emitHeader("")
    emitHeader(`#include "luaug/core/id.h"`)
    emitHeader(`#include "luaug/core/name_atom.h"`)
    emitHeader(`#include "luaug/scene/class_registry.h"`)
    -- Unconditional: every module's header now names `EnumId` for the id
    -- constants below, whether or not it owns the registry.
    emitHeader(`#include "luaug/scene/enum_registry.h"`)
    emitHeader(`#include "luaug/scene/value.h"`)
    emitHeader("")
    emitHeader("namespace luaug::scene")
    emitHeader("{")
    emitHeader("")
    emitHeader("class World;")
    emitHeader("")
    if not ownsRegistries then
        -- The accessors below take scene's `World` and `Value`; only the
        -- forward declaration belongs in scene's namespace, and the rest of
        -- this file is this module's.
        emitHeader("} // namespace luaug::scene")
        emitHeader("")
        emitHeader(`namespace luaug::{target.Name}`)
        emitHeader("{")
        emitHeader("")
    end
    emitHeader("namespace generated")
    emitHeader("{")
    emitHeader("")
    emitHeader("// Registers every class this module owns, parents first -- the order")
    emitHeader("// `ClassRegistry::registerClass` requires. Interns each name into `atoms`,")
    emitHeader("// so the table handed in is the one the descriptors speak.")
    emitHeader(`void registerClasses({scope}ClassRegistry& classes, core::AtomTable& atoms);`)
    if ownsRegistries then
        emitHeader("")
        emitHeader("// Registers every enum in the IDL, in declaration order.")
        emitHeader("void registerEnums(EnumRegistry& enums, core::AtomTable& atoms);")
    end

    -- The id constants go in EVERY module's header, not just the one that owns
    -- the registry. Any module may declare a property of an enum type -- M5's
    -- `MeshPart.CollisionFidelity` is the first that is not scene's -- and its
    -- accessor has to name the same number a stored `EnumValue` carries. The
    -- alternative is a hash probe per property write, or a cross-module include
    -- of another module's generated header.
    emitHeader("")
    emitHeader("// The id `registerEnums` assigns each enum, so that a hand-written accessor")
    emitHeader("// validating an enum property names the same number a stored `EnumValue`")
    emitHeader("// carries. A registry lookup by name would answer the same question and")
    emitHeader("// would do it with a hash probe on a property write.")

    for index, enumeration in Enums do
        emitHeader(`inline constexpr {scope}EnumId {enumeration.Name}EnumId = {index};`)
    end

    emitHeader("")
    emitHeader("} // namespace generated")
    emitHeader("")
    emitHeader("// One accessor pair per property -- `get<Class><Property>` and")
    emitHeader("// `set<Class><Property>`, or the property's `Native` override. A read-only")
    emitHeader("// property has a getter and no setter, because a setter nothing may call is a")
    emitHeader("// function nobody should be asked to write.")
    emitHeader("//")
    emitHeader("// A class that declares its own component storage also has an attach/detach")
    emitHeader("// pair, named from its `Storage`. `World::create` calls them root-first down")
    emitHeader("// the ancestry, so only the declaring class carries one.")
    emitHeader("namespace native")
    emitHeader("{")

    for _, declaration in declarations do
        emitHeader("")
        emitHeader(`// {declaration.Class}`)
        for _, line in declaration.Lines do
            emitHeader(line)
        end
    end

    emitHeader("")
    emitHeader("} // namespace native")
    emitHeader("")
    emitHeader(`\} // namespace luaug::{target.Name}`)
    emitHeader("")

    local directory = `engine/{target.Name}/generated`
    table.insert(Outputs, { Path = `{directory}/class_descriptors.gen.h`, Lines = header })
    table.insert(Outputs, { Path = `{directory}/class_descriptors.gen.cpp`, Lines = source })

    print(
        `gen_cpp: module "{target.Name}": {#emitted} class(es), {propertyCount} propert(ies), `
            .. `{methodCount} method(s), {eventCount} event(s), {#externalParents} inherited across modules`
    )
    print(
        `gen_cpp: module "{target.Name}": {getterCount + setterCount + storageCount * 2} native function(s) `
            .. `declared -- {getterCount} getter(s), {setterCount} setter(s), {storageCount} storage hook pair(s), `
            .. `{unbacked} unbacked propert(ies)`
    )
end

for _, target in Modules do
    generate(target)
end

if #Errors > 0 then
    for _, message in Errors do
        print(`gen_cpp: {message}`)
    end
    print(`gen_cpp: {#Errors} error(s) -- nothing written`)
    process.exit(1)
end

for _, output in Outputs do
    local directory = string.match(output.Path, "^(.*)/[^/]*$")
    if directory ~= nil and not fs.exists(directory) then
        fs.createDirectory(directory, { makeParents = true })
    end
    fs.writeStringToFile(output.Path, table.concat(output.Lines, "\n"))
    print(`gen_cpp: wrote {output.Path}`)
end

print(`gen_cpp: {#Enums} enum(s), {EnumItemCount} item(s) -- registered by module "{RegistryModule}"`)
