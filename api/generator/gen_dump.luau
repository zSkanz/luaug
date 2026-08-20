--!strict
--[[
	API dump generator (api-design.md §5, generated artifact 4).

	Emits `api/api-dump.json` from the IDL: the versioned, machine-readable
	record of the public surface. §5 states what it is for -- "CI diffs it to
	force changelog entries and catch accidental API breaks" -- and that purpose
	is what decides the contents. Anything a user cannot observe is left out, so
	that every line of a diff is a question somebody has to answer.

	What is deliberately NOT in the dump:

	- Doc prose. It rides in `runtime/types/engine.d.luau`, which is
	  freshness-gated too, and it changes for typos and rewordings. A dump that
	  carried it would bury a removed method under a reflowed paragraph, which
	  is the one thing this file exists to prevent.
	- `Property.Native`, `Class.Storage`, `Class.Module`. These name
	  hand-written C++ and the module that registers the class -- backend facts
	  no script can observe (R17). Renaming a C++ accessor is not an API change
	  and must not read as one.
	- Inherited members. A class records what it DECLARES; `Extends` is how the
	  rest is reachable. Flattening would turn one added member on `Instance`
	  into a diff on every class in the file.

	Ordering is by name everywhere, never by declaration order, and every sort
	carries a tiebreaker that makes it a total order (R10). Formatting is one
	value per line, so a single added property is a handful of added lines
	rather than a reflowed file.

	The output is CHECKED IN and diff-checked by `scripts/gates/luau-check.sh`,
	the same arrangement `runtime/types/engine.d.luau` and the generated
	descriptors already have.

	    lute api/generator/gen_dump.luau
]]

local fs = require("@std/fs")
local process = require("@std/process")

local Definition = require("../defs")
local Schema = require("../schema")

local OutputPath = "api/api-dump.json"

--- The engine version is read from the one file that defines it rather than
--- copied into a second place that would drift. §1.4 makes the dump a versioned
--- artifact of each engine release and has the CLI warn when a runtime and a
--- dump disagree, which only means something if the number here is the number
--- the build carries.
local VersionSourcePath = "CMakeLists.txt"
local VersionPattern = "project%(LuauG%s+VERSION%s+([%w%.%-]+)"

--- Two spaces, one value per line. JSON's usual indent rather than the four
--- spaces our Luau uses, because what reads this file is JSON tooling and
--- `git diff`, not StyLua.
local Indent = "  "

--- A byte no identifier can contain, separating the parts of a sort key. It
--- keeps `new` sorting before `newFromMatrix` rather than interleaving with it,
--- which a printable separator would not.
local KeySeparator = "\0"

type Field = {
    Key: string,
    Value: Node,
}

type Node =
    {
        Kind: "Scalar",
        Text: string,
    }
    | {
        Kind: "Array",
        Items: { Node },
    }
    | {
        Kind: "Object",
        Fields: { Field },
    }

local Errors: { string } = {}

--- Counted while the tree is built rather than derived afterwards: a member
--- this generator silently drops is a surface that goes missing without anybody
--- being told, which is why `gen_cpp` prints its skipped count too.
local MemberCount = 0

--- `list or {}` types as `{T} | {}`, which the new solver declines to iterate.
--- Same helper, same reason, as in `gen_dts.luau` and `gen_cpp.luau`.
local function orEmpty<T>(list: { T }?): { T }
    return list or {}
end

local Escapes: { [string]: string } = {
    ['"'] = '\\"',
    ["\\"] = "\\\\",
    ["\b"] = "\\b",
    ["\f"] = "\\f",
    ["\n"] = "\\n",
    ["\r"] = "\\r",
    ["\t"] = "\\t",
}

--- Only what JSON requires: the quote, the backslash, and the C0 controls. A
--- default literal arrives from the IDL with its quotes intact, so the first of
--- those is not theoretical.
---
--- Bytes above 0x7F pass through instead of becoming `\uXXXX`. The definition
--- files are UTF-8 and this output is byte-compared, so passing them through is
--- what keeps the file a function of its input; escaping them would need a
--- UTF-8 decoder whose only job is to undo itself.
local function escape(text: string): string
    local escaped = string.gsub(text, '[%c"\\]', function(character: string): string
        return Escapes[character] or string.format("\\u%04x", string.byte(character))
    end)
    return escaped
end

local function stringNode(text: string): Node
    return { Kind = "Scalar", Text = `"{escape(text)}"` }
end

local function booleanNode(value: boolean): Node
    return { Kind = "Scalar", Text = if value then "true" else "false" }
end

local function arrayNode(items: { Node }): Node
    return { Kind = "Array", Items = items }
end

local function objectNode(fields: { Field }): Node
    return { Kind = "Object", Fields = fields }
end

local function field(key: string, value: Node): Field
    return { Key = key, Value = value }
end

--- A field the IDL does not declare is omitted rather than written as `null`.
--- Absence and an explicit null say the same thing about a surface, and one
--- spelling keeps "this parameter gained a default" to a single added line.
local function appendOptional(fields: { Field }, key: string, value: string?)
    if value ~= nil then
        table.insert(fields, field(key, stringNode(value)))
    end
end

--- `false` and absent are likewise the same statement about a flag, so only
--- `true` is written and turning a flag on is one added line.
local function appendFlag(fields: { Field }, key: string, value: boolean?)
    if value == true then
        table.insert(fields, field(key, booleanNode(true)))
    end
end

--- An array of scalars is written on one line -- `"Tags": ["Service"]`,
--- `"Returns": ["number"]`. Every such array here is a tag list or a return
--- list, both of which are short and are read as a unit; spreading three words
--- over five lines would cost the reviewer the shape of the thing without
--- buying a finer diff, since changing one entry of a two-entry list is one
--- line either way.
local function isScalarList(items: { Node }): boolean
    for _, item in items do
        if item.Kind ~= "Scalar" then
            return false
        end
    end
    return true
end

--- Renders `node` beginning on its own line: `prefix` is whatever precedes the
--- value (a key, or nothing inside an array) and `suffix` is the separating
--- comma. An empty container collapses to `[]` or `{}`, so a class with no
--- events costs one line instead of three.
local function render(lines: { string }, node: Node, indent: string, prefix: string, suffix: string): ()
    if node.Kind == "Scalar" then
        table.insert(lines, indent .. prefix .. node.Text .. suffix)
    elseif node.Kind == "Array" then
        local items = node.Items
        if #items == 0 then
            table.insert(lines, indent .. prefix .. "[]" .. suffix)
            return
        end
        if isScalarList(items) then
            local texts: { string } = {}
            for _, item in items do
                -- Narrowed by `isScalarList`, which the solver cannot carry
                -- across the call, so the field is read through a cast.
                table.insert(texts, (item :: { Kind: "Scalar", Text: string }).Text)
            end
            table.insert(lines, `{indent}{prefix}[{table.concat(texts, ", ")}]{suffix}`)
            return
        end
        table.insert(lines, indent .. prefix .. "[")
        for index, item in items do
            render(lines, item, indent .. Indent, "", if index == #items then "" else ",")
        end
        table.insert(lines, indent .. "]" .. suffix)
    else
        local fields = node.Fields
        if #fields == 0 then
            table.insert(lines, indent .. prefix .. "{}" .. suffix)
            return
        end
        table.insert(lines, indent .. prefix .. "{")
        for index, entry in fields do
            local key = `"{escape(entry.Key)}": `
            render(lines, entry.Value, indent .. Indent, key, if index == #fields then "" else ",")
        end
        table.insert(lines, indent .. "}" .. suffix)
    end
end

--- Sorts by name so that the file's order is a property of the IDL's *content*
--- rather than of its authoring order: moving a class between definition files
--- then produces no diff at all, and adding one produces a diff in exactly one
--- place. Declaration order would let either edit reshuffle the file, and a
--- reshuffled file is a diff nobody reads.
---
--- The declaration index is the last tiebreaker because `table.sort` is a
--- quicksort and is not stable: two entries comparing equal could otherwise
--- come out in either order, on two platforms or on two runs (R10). Sorting on
--- `<` is itself safe, because Luau compares strings with `memcmp`
--- (`third_party/luau/VM/src/lvmutils.cpp:287-308`) and not with the locale's
--- collation, so the order is the same everywhere.
local function sortedByKey<T>(items: { T }, keyOf: (T) -> string): { T }
    local decorated: { { Key: string, Index: number, Item: T } } = {}
    for index, item in items do
        table.insert(decorated, { Key = keyOf(item), Index = index, Item = item })
    end
    table.sort(decorated, function(left, right): boolean
        if left.Key ~= right.Key then
            return left.Key < right.Key
        end
        return left.Index < right.Index
    end)

    local ordered: { T } = {}
    for _, entry in decorated do
        table.insert(ordered, entry.Item)
    end
    return ordered
end

local function memberArray<T>(items: { T }?, keyOf: (T) -> string, nodeOf: (T) -> Node): Node
    local nodes: { Node } = {}
    for _, item in sortedByKey(orEmpty(items), keyOf) do
        table.insert(nodes, nodeOf(item))
    end
    return arrayNode(nodes)
end

--- Overloads share a name -- `CFrame.new` is several functions -- so a name
--- alone is not a sort key. The signature completes it, and completes it with
--- the part that actually distinguishes the overloads.
local function signatureKey(name: string, parameters: { Schema.Parameter }, returns: { Schema.TypeName }): string
    local types: { string } = {}
    for _, parameter in parameters do
        table.insert(types, parameter.Type)
    end
    return `{name}{KeySeparator}{table.concat(types, ",")}{KeySeparator}{table.concat(returns, ",")}`
end

local function parameterNode(parameter: Schema.Parameter): Node
    local fields: { Field } = {
        field("Name", stringNode(parameter.Name)),
        field("Type", stringNode(parameter.Type)),
    }
    appendOptional(fields, "Default", parameter.Default)
    return objectNode(fields)
end

--- Parameters and returns keep their declaration order. It is the CALL order,
--- which is surface rather than presentation: sorting it would destroy the one
--- thing the list means.
local function parametersNode(parameters: { Schema.Parameter }): Node
    local nodes: { Node } = {}
    for _, parameter in parameters do
        table.insert(nodes, parameterNode(parameter))
    end
    return arrayNode(nodes)
end

local function returnsNode(returns: { Schema.TypeName }): Node
    local nodes: { Node } = {}
    for _, entry in returns do
        table.insert(nodes, stringNode(entry))
    end
    return arrayNode(nodes)
end

local function propertyKey(property: Schema.Property): string
    return property.Name
end

local function propertyNode(property: Schema.Property): Node
    MemberCount += 1
    local fields: { Field } = {
        field("Name", stringNode(property.Name)),
        field("Type", stringNode(property.Type)),
    }
    appendFlag(fields, "ReadOnly", property.ReadOnly)
    appendOptional(fields, "Default", property.Default)
    appendOptional(fields, "ThreadSafety", property.ThreadSafety)
    appendFlag(fields, "Transient", property.Transient)
    return objectNode(fields)
end

local function methodKey(method: Schema.Method): string
    return signatureKey(method.Name, method.Parameters, method.Returns)
end

local function methodNode(method: Schema.Method): Node
    MemberCount += 1
    local fields: { Field } = {
        field("Name", stringNode(method.Name)),
        field("Parameters", parametersNode(method.Parameters)),
        field("Returns", returnsNode(method.Returns)),
    }
    appendFlag(fields, "Yields", method.Yields)
    appendOptional(fields, "ThreadSafety", method.ThreadSafety)
    return objectNode(fields)
end

local function eventKey(event: Schema.Event): string
    return event.Name
end

local function eventNode(event: Schema.Event): Node
    MemberCount += 1
    return objectNode({
        field("Name", stringNode(event.Name)),
        field("Parameters", parametersNode(event.Parameters)),
    })
end

local function operatorKey(operator: Schema.Operator): string
    return signatureKey(operator.Name, operator.Parameters, operator.Returns)
end

local function operatorNode(operator: Schema.Operator): Node
    MemberCount += 1
    return objectNode({
        field("Name", stringNode(operator.Name)),
        field("Parameters", parametersNode(operator.Parameters)),
        field("Returns", returnsNode(operator.Returns)),
    })
end

local function enumItemNode(enumName: string, item: Schema.EnumItem): Node
    MemberCount += 1
    -- Written as an integer literal rather than through `tostring`, so a value
    -- cannot appear as `3` in one run and `3.0` in another. A non-integral
    -- value is a definition error rather than something to round: enum values
    -- are serialized and compared for equality.
    local text
    if item.Value % 1 == 0 then
        text = string.format("%d", item.Value)
    else
        table.insert(Errors, `Enum.{enumName}.{item.Name}: enum values are integers, and {item.Value} is not`)
        text = tostring(item.Value)
    end
    return objectNode({
        field("Name", stringNode(item.Name)),
        field("Value", { Kind = "Scalar", Text = text } :: Node),
    })
end

--- Tags are a set, and the IDL writes them in whatever order reads well. Sorted
--- here so the dump records the set rather than the spelling. Equal strings are
--- indistinguishable, so this is the one sort with nothing to be unstable
--- about.
local function tagsNode(tags: { Schema.ClassTag }?): Node
    local names: { string } = {}
    for _, tag in orEmpty(tags) do
        table.insert(names, tag)
    end
    table.sort(names)

    local nodes: { Node } = {}
    for _, name in names do
        table.insert(nodes, stringNode(name))
    end
    return arrayNode(nodes)
end

local function classNode(class: Schema.Class): Node
    local fields: { Field } = { field("Name", stringNode(class.Name)) }
    appendOptional(fields, "Extends", class.Extends)
    appendOptional(fields, "DefaultName", class.DefaultName)
    table.insert(fields, field("Tags", tagsNode(class.Tags)))
    table.insert(fields, field("Properties", memberArray(class.Properties, propertyKey, propertyNode)))
    table.insert(fields, field("Methods", memberArray(class.Methods, methodKey, methodNode)))
    table.insert(fields, field("Events", memberArray(class.Events, eventKey, eventNode)))
    return objectNode(fields)
end

local function datatypeNode(datatype: Schema.Datatype): Node
    return objectNode({
        field("Name", stringNode(datatype.Name)),
        field("Constructors", memberArray(datatype.Constructors, methodKey, methodNode)),
        field("Constants", memberArray(datatype.Constants, propertyKey, propertyNode)),
        field("Properties", memberArray(datatype.Properties, propertyKey, propertyNode)),
        field("Methods", memberArray(datatype.Methods, methodKey, methodNode)),
        field("Operators", memberArray(datatype.Operators, operatorKey, operatorNode)),
    })
end

local function enumNode(enumeration: Schema.Enum): Node
    local items = memberArray(enumeration.Items, function(item: Schema.EnumItem): string
        return item.Name
    end, function(item: Schema.EnumItem): Node
        return enumItemNode(enumeration.Name, item)
    end)

    return objectNode({
        field("Name", stringNode(enumeration.Name)),
        field("Items", items),
    })
end

local function libraryNode(library: Schema.Library): Node
    return objectNode({
        field("Name", stringNode(library.Name)),
        field("Functions", memberArray(library.Functions, methodKey, methodNode)),
    })
end

local Classes: { Schema.Class } = orEmpty(Definition.Classes)
local Datatypes: { Schema.Datatype } = orEmpty(Definition.Datatypes)
local Enums: { Schema.Enum } = orEmpty(Definition.Enums)
local Libraries: { Schema.Library } = orEmpty(Definition.Libraries)
local Globals: { Schema.Method } = orEmpty(Definition.Globals)

local Version: string? = nil
if fs.exists(VersionSourcePath) then
    Version = string.match(fs.readFileToString(VersionSourcePath), VersionPattern)
end
if Version == nil then
    -- Refused rather than defaulted. A dump that invents its own version is a
    -- dump §1.4's mismatch warning cannot be built on.
    table.insert(Errors, `no engine version in {VersionSourcePath} -- expected a project(LuauG VERSION ...) line`)
end

local Root = objectNode({
    field("Version", stringNode(Version or "")),
    field(
        "Classes",
        memberArray(Classes, function(class: Schema.Class): string
            return class.Name
        end, classNode)
    ),
    field(
        "Datatypes",
        memberArray(Datatypes, function(datatype: Schema.Datatype): string
            return datatype.Name
        end, datatypeNode)
    ),
    field(
        "Enums",
        memberArray(Enums, function(enumeration: Schema.Enum): string
            return enumeration.Name
        end, enumNode)
    ),
    field("Globals", memberArray(Globals, methodKey, methodNode)),
    field(
        "Libraries",
        memberArray(Libraries, function(library: Schema.Library): string
            return library.Name
        end, libraryNode)
    ),
})

local Lines: { string } = {}
render(Lines, Root, "", "", "")
-- The trailing empty entry becomes the final newline. Without one, every diff
-- of this file ends in a "\ No newline at end of file" marker.
table.insert(Lines, "")

if #Errors > 0 then
    for _, message in Errors do
        print(`gen_dump: {message}`)
    end
    print(`gen_dump: {#Errors} error(s) -- nothing written`)
    process.exit(1)
end

fs.writeStringToFile(OutputPath, table.concat(Lines, "\n"))

print(`gen_dump: wrote {OutputPath} -- engine {Version}`)
print(
    `gen_dump: {#Classes} class(es), {#Datatypes} datatype(s), {#Enums} enum(s), `
        .. `{#Libraries} librar(ies), {#Globals} global(s), {MemberCount} member(s)`
)
