--!strict
--[[
	C++ reflection-table generator (architecture.md §4, ADR 0028).

	Emits `engine/scene/generated/class_descriptors.gen.{h,cpp}` from the IDL:
	the `PropertyDesc`/`MethodDesc`/`EventDesc` arrays `ClassRegistry` holds
	spans into, one `ClassDescriptor` per class, and the single
	`registerClasses` entry point that installs them parents-first.

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
	- Doc keys. `docKey` is emitted empty, because nothing in the engine
	  formats one yet; the docs pipeline is `gen_docs` and the docs JSON, which
	  docs/roadmap.md places in M3. The doc text stays in the IDL until then.
	- Anything about components. `Class.Storage` and `Property.Native` are
	  *names* of hand-written functions, so this generator never learns what a
	  component is -- moving a property's storage is a change in one C++ file.

	Only classes whose `Module` is `scene` land in this file, and the count of
	the ones skipped is printed -- a class silently dropped is a surface that
	goes missing without anybody being told.

	    lute api/generator/gen_cpp.luau
]]

local fs = require("@std/fs")
local process = require("@std/process")

local Definition = require("../defs")
local Schema = require("../schema")

--- The engine module this file registers. A definition naming another module
--- belongs in that module's generated file, which arrives with the milestone
--- that gives the class meaning.
local TargetModule = "scene"

local OutputDirectory = "engine/scene/generated"
local HeaderPath = `{OutputDirectory}/class_descriptors.gen.h`
local SourcePath = `{OutputDirectory}/class_descriptors.gen.cpp`

--- `scene::ValueType`, keyed by the Luau type the IDL writes. Instance-ish and
--- `Enum.X` types are resolved below rather than listed, because both are open
--- sets that grow every time a class or an enum ships.
local ScalarValueTypes: { [string]: string } = {
    ["string"] = "String",
    ["number"] = "Number",
    ["boolean"] = "Bool",
    ["vector"] = "Vector3",
    ["CFrame"] = "CFrame",
    ["Color3"] = "Color3",
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
for _, class in AllClasses do
    ClassNames[class.Name] = true
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

local function flagsExpression(class: Schema.Class, errors: { string }): string
    local parts: { string } = {}
    for _, tag in orEmpty(class.Tags) do
        local flag = ClassFlagNames[tag]
        if flag == nil then
            table.insert(errors, `{class.Name}: tag "{tag}" has no ClassFlags equivalent`)
            continue
        end
        table.insert(parts, flag)
    end
    if #parts == 0 then
        return "ClassFlags::None"
    end
    return table.concat(parts, " | ")
end

local Errors: { string } = {}

local Ordered: { Schema.Class } = sortedByHierarchy(AllClasses)
local Emitted: { Schema.Class } = {}
local EmittedNames: { [string]: boolean } = {}
local Skipped = 0

for _, class in Ordered do
    if class.Module == TargetModule then
        table.insert(Emitted, class)
        EmittedNames[class.Name] = true
    else
        Skipped += 1
    end
end

for _, class in Emitted do
    local parent = class.Extends
    if parent ~= nil and not EmittedNames[parent] then
        -- A cross-module super cannot be named here: the parent's `ClassId` is
        -- a local in the file that registers it, and registration order across
        -- two modules is a run-time question this generator cannot answer.
        table.insert(Errors, `{class.Name}: extends "{parent}", which module "{TargetModule}" does not register`)
    end
end

--- Event slots are dense per class with the inherited ones first, the same
--- numbering `ClassRegistry::propertySlot` gives properties: a class keeps its
--- super's numbering and appends its own after it, so `ChildAdded` is slot 0 on
--- every class that inherits it and a fire is an array offset.
local ClassByName: { [string]: Schema.Class } = {}
for _, class in Emitted do
    ClassByName[class.Name] = class
end

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

--- The header declares exactly what the source references, so the two are
--- collected together while the tables are emitted rather than walked twice.
type Declaration = {
    Class: string,
    Lines: { string },
}

local Declarations: { Declaration } = {}
local Unbacked = 0
local PropertyCount = 0
local MethodCount = 0
local EventCount = 0
local GetterCount = 0
local SetterCount = 0
local StorageCount = 0

local Source: { string } = {}
local function emit(line: string)
    table.insert(Source, line)
end

local FileBanner: { string } = {
    "// GENERATED by api/generator/gen_cpp.luau from api/defs/*.api.luau.",
    "//",
    "// Do not edit. The Luau gate regenerates this file and fails on a diff, so a",
    -- Deliberately ASCII-only, section signs included: this file is regenerated
    -- and byte-compared, so an encoding difference between two machines would
    -- read as a spurious diff rather than as the toolchain problem it is.
    "// hand edit is a change that survives exactly until the next build",
    "// (architecture.md section 4).",
}

for _, line in FileBanner do
    emit(line)
end
emit("//")
emit("// Every `docKey` below is empty on purpose. The doc text lives in the IDL and")
emit("// nothing in the engine formats a doc key yet: turning it into a catalog entry")
emit("// and a hover string is the M3 documentation pipeline (gen_docs and the docs")
emit("// JSON, docs/roadmap.md). An empty key here is that decision, not an omission.")
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
emit("namespace luaug::scene::generated")
emit("{")
emit("")
emit("// The descriptor arrays have static storage duration because")
emit("// `ClassDescriptor` holds `std::span`s into them and the registry outlives")
emit("// this call -- a local array would leave every descriptor pointing at a dead")
emit("// frame. They are ASSIGNED on each call rather than initialised on the first,")
emit("// so a second registry built over a second `AtomTable` gets ids from its own")
emit("// table instead of inheriting the first one's alphabet. The two registries")
emit("// still share this storage and so cannot be live at once, which is exactly")
emit("// why `scene_fixture.h` owns per-instance arrays instead of calling in here.")
emit("//")
emit("// A `ClassId` is bound to a name only where a later class extends that class;")
emit("// binding one nothing reads would be an unused local, and warnings are errors")
emit("// on `engine/` (architecture.md section 8).")
emit("void registerClasses(ClassRegistry& classes, core::AtomTable& atoms)")
emit("{")

local Parents: { [string]: boolean } = {}
for _, class in Emitted do
    local parent = class.Extends
    if parent ~= nil then
        Parents[parent] = true
    end
end

local function classIdVariable(className: string): string
    return `{lowerFirst(className)}Class`
end

for index, class in Emitted do
    if index > 1 then
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
        emit(`    static std::array<PropertyDesc, {#properties}> {arrayName};`)
        emit(`    {arrayName} = \{\{`)
        for _, property in properties do
            PropertyCount += 1
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
                Unbacked += 1
            else
                GetterCount += 1
                getter = `native::get{upperFirst(base)}`
                table.insert(declared, `Value get{upperFirst(base)}(const World& world, core::InstanceId id);`)
                -- A read-only property gets a getter and a null setter, and no
                -- setter is DECLARED for it: declaring one asks somebody to
                -- write a function that must never be called.
                if property.ReadOnly ~= true then
                    SetterCount += 1
                    setter = `native::set{upperFirst(base)}`
                    table.insert(
                        declared,
                        `bool set{upperFirst(base)}(World& world, core::InstanceId id, const Value& value);`
                    )
                end
            end

            local errorKey = InvalidSetKeys[valueType]
            if errorKey == nil then
                table.insert(Errors, `{class.Name}.{property.Name}: no invalid-set key for ValueType::{valueType}`)
                errorKey = "scene.err.unknown_property"
            end

            emit("        PropertyDesc{")
            emit(`            .name = atoms.intern("{property.Name}"),`)
            emit(`            .type = ValueType::{valueType},`)
            emit(`            .threadSafety = ThreadSafety::{property.ThreadSafety or "Unsafe"},`)
            emit(`            .readOnly = {if property.ReadOnly == true then "true" else "false"},`)
            emit("            .docKey = {},")
            emit(`            .errKeyOnInvalidSet = LUAUG_TR("{errorKey}"),`)
            emit(`            .get = {getter},`)
            emit(`            .set = {setter},`)
            emit("        },")
        end
        emit("    }};")
    end

    if #methods > 0 then
        local arrayName = `{prefix}Methods`
        emit(`    static std::array<MethodDesc, {#methods}> {arrayName};`)
        emit(`    {arrayName} = \{\{`)
        for _, method in methods do
            MethodCount += 1
            emit("        MethodDesc{")
            emit(`            .name = atoms.intern("{method.Name}"),`)
            emit(`            .yields = {if method.Yields == true then "true" else "false"},`)
            emit(`            .threadSafety = ThreadSafety::{method.ThreadSafety or "Unsafe"},`)
            emit("            .docKey = {},")
            emit("        },")
        end
        emit("    }};")
    end

    if #events > 0 then
        local arrayName = `{prefix}Events`
        local slot = inheritedEventCount(class)
        emit(`    static std::array<EventDesc, {#events}> {arrayName};`)
        emit(`    {arrayName} = \{\{`)
        for _, event in events do
            EventCount += 1
            emit("        EventDesc{")
            emit(`            .name = atoms.intern("{event.Name}"),`)
            emit(`            .slot = {slot},`)
            emit("            .docKey = {},")
            emit("        },")
            slot += 1
        end
        emit("    }};")
    end

    local descriptor = `{prefix}Desc`
    emit(`    ClassDescriptor {descriptor};`)
    emit(`    {descriptor}.name = atoms.intern("{class.Name}");`)
    if class.Extends ~= nil then
        emit(`    {descriptor}.super = {classIdVariable(class.Extends)};`)
    end
    emit(`    {descriptor}.flags = {flagsExpression(class, Errors)};`)
    emit(`    {descriptor}.defaultName = atoms.intern("{class.DefaultName or class.Name}");`)
    emit(`    {descriptor}.docKey = \{};`)
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
        StorageCount += 1
        emit(`    {descriptor}.attachComponents = native::attach{upperFirst(storage)}Components;`)
        emit(`    {descriptor}.detachComponents = native::detach{upperFirst(storage)}Components;`)
        table.insert(declared, `void attach{upperFirst(storage)}Components(World& world, core::InstanceId id);`)
        table.insert(declared, `void detach{upperFirst(storage)}Components(World& world, core::InstanceId id);`)
    end

    if Parents[class.Name] then
        emit(`    const ClassId {classIdVariable(class.Name)} = classes.registerClass({descriptor});`)
    else
        emit(`    classes.registerClass({descriptor});`)
    end

    if #declared > 0 then
        table.insert(Declarations, { Class = class.Name, Lines = declared })
    end
end

emit("}")
emit("")
emit("} // namespace luaug::scene::generated")
emit("")

local Header: { string } = {}
local function emitHeader(line: string)
    table.insert(Header, line)
end

for _, line in FileBanner do
    emitHeader(line)
end
emitHeader("//")
emitHeader("// The functions below are DECLARED here and implemented by hand in")
emitHeader("// engine/scene/src/native_accessors.cpp (architecture.md section 4: the")
emitHeader("// generated tables point at native functions, and those functions are the")
emitHeader("// only hand-written half). Declaring every one the definitions reference is")
emitHeader("// what makes a missing implementation a link error rather than a null")
emitHeader("// function pointer that reads as nil the first time a script touches it.")
emitHeader("#pragma once")
emitHeader("")
emitHeader(`#include "luaug/core/id.h"`)
emitHeader(`#include "luaug/core/name_atom.h"`)
emitHeader(`#include "luaug/scene/class_registry.h"`)
emitHeader(`#include "luaug/scene/value.h"`)
emitHeader("")
emitHeader("namespace luaug::scene")
emitHeader("{")
emitHeader("")
emitHeader("class World;")
emitHeader("")
emitHeader("namespace generated")
emitHeader("{")
emitHeader("")
emitHeader("// Registers every class this module owns, parents first -- the order")
emitHeader("// `ClassRegistry::registerClass` requires. Interns each name into `atoms`,")
emitHeader("// so the table handed in is the one the descriptors speak.")
emitHeader("void registerClasses(ClassRegistry& classes, core::AtomTable& atoms);")
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

for _, declaration in Declarations do
    emitHeader("")
    emitHeader(`// {declaration.Class}`)
    for _, line in declaration.Lines do
        emitHeader(line)
    end
end

emitHeader("")
emitHeader("} // namespace native")
emitHeader("")
emitHeader("} // namespace luaug::scene")
emitHeader("")

if #Errors > 0 then
    for _, message in Errors do
        print(`gen_cpp: {message}`)
    end
    print(`gen_cpp: {#Errors} error(s) -- nothing written`)
    process.exit(1)
end

if not fs.exists(OutputDirectory) then
    fs.createDirectory(OutputDirectory, { makeParents = true })
end
fs.writeStringToFile(HeaderPath, table.concat(Header, "\n"))
fs.writeStringToFile(SourcePath, table.concat(Source, "\n"))

print(`gen_cpp: wrote {HeaderPath} and {SourcePath}`)
print(
    `gen_cpp: {#Emitted} class(es) in module "{TargetModule}", {Skipped} skipped, `
        .. `{PropertyCount} propert(ies), {MethodCount} method(s), {EventCount} event(s)`
)
print(
    `gen_cpp: {GetterCount + SetterCount + StorageCount * 2} native function(s) declared -- `
        .. `{GetterCount} getter(s), {SetterCount} setter(s), {StorageCount} storage hook pair(s)`
)
print(`gen_cpp: {Unbacked} unbacked propert(ies)`)
