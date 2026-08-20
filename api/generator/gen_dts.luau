--!strict
--[[
	Type-definition generator (api-design.md §5).

	Emits `runtime/types/engine.d.luau` from the IDL: one `declare extern type`
	per class and datatype, the `Enum` namespace, and the world globals. The
	analyzer and luau-lsp read that file, which is what lets every example and
	every conformance spec be `--!strict` (R2, ADR 0018) instead of exempted --
	and an exemption is the kind of thing that spreads.

	The output is CHECKED IN and diff-checked, the same arrangement
	architecture.md §1 already specifies for `runtime/types/`. That keeps Lute
	off the critical path of a C++ build while still making drift a build
	failure rather than a discovery.

	Declaration syntax is grounded in the vendored parser rather than in memory:
	`third_party/luau/Ast/src/Parser.cpp:1832-1872` requires `with` after
	`extern type`, :1948-1961 accepts a `read`/`write` access prefix on a
	property, and :1730 requires `self` as an UNANNOTATED first parameter of a
	declared method.

	    lute api/generator/gen_dts.luau
]]

local fs = require("@std/fs")

local Definition = require("../defs")
local Schema = require("../schema")

local OutputPath = "runtime/types/engine.d.luau"

--- Enum types follow the ecosystem's spelling: the enum `PartShape` produces
--- the type `EnumPartShape`, which is what a user writes in an annotation and
--- what luau-lsp users already expect. The IDL spells the same thing
--- `Enum.PartShape`, because that is what the *value* path looks like.
local function enumTypeName(enumName: string): string
    return `Enum{enumName}`
end

local function translateType(typeName: string): string
    local enumName = string.match(typeName, "^Enum%.([%w_]+)$")
    if enumName ~= nil then
        return enumTypeName(enumName)
    end
    local optionalEnum = string.match(typeName, "^Enum%.([%w_]+)%?$")
    if optionalEnum ~= nil then
        return `{enumTypeName(optionalEnum)}?`
    end
    -- A COMPOSITE type may mention an enum inside itself: M5's
    -- `RaycastParams.new` takes a table with a `FilterType` field. Left
    -- untranslated, `Enum.RaycastFilterType` reaches the definitions verbatim,
    -- where `Enum` is a value rather than a namespace of types -- and luau-lsp
    -- answers by refusing to read the whole file, so every global in the
    -- repository becomes unknown at once. That is what it did.
    --
    -- Substituted rather than matched whole, and only where the name is not
    -- already part of a longer identifier.
    local rewritten = string.gsub(typeName, "Enum%.([%w_]+)", function(name: string): string
        return enumTypeName(name)
    end)
    return rewritten
end

local function parameterList(parameters: { Schema.Parameter }): string
    local parts: { string } = {}
    for _, parameter in parameters do
        local rendered = translateType(parameter.Type)
        -- An optional parameter is marked in the IDL by carrying a default, and
        -- in a type by the `?`. Only add one if the author did not already.
        if parameter.Default ~= nil and string.sub(rendered, -1) ~= "?" then
            rendered ..= "?"
        end
        -- A variadic carries no name in a type: `...vector`, never
        -- `rest: ...vector`; and neither does a generic type pack, `T...`. The
        -- IDL names both anyway, because the name is real documentation and the
        -- docs generator wants it -- but naming one here is a syntax error that
        -- takes the whole definitions file down with it.
        if string.sub(rendered, 1, 3) == "..." or string.sub(rendered, -3) == "..." then
            table.insert(parts, rendered)
        else
            table.insert(parts, `{parameter.Name}: {rendered}`)
        end
    end
    return table.concat(parts, ", ")
end

--- The same list for a `declare function`, where the syntax is not the same.
--- A declaration insists every parameter be annotated and spells a variadic
--- `...: any`, while a function *type* spells the identical thing `...any`.
--- One character, two productions in the grammar, and the definitions file
--- fails to load entirely if the wrong one is emitted -- which is how this was
--- found.
local function declarationParameterList(parameters: { Schema.Parameter }): string
    local parts: { string } = {}
    for _, parameter in parameters do
        local rendered = translateType(parameter.Type)
        if parameter.Default ~= nil and string.sub(rendered, -1) ~= "?" then
            rendered ..= "?"
        end
        if string.sub(rendered, 1, 3) == "..." then
            table.insert(parts, `...: {string.sub(rendered, 4)}`)
        else
            table.insert(parts, `{parameter.Name}: {rendered}`)
        end
    end
    return table.concat(parts, ", ")
end

local function returnList(returns: { string }): string
    if #returns == 0 then
        return "()"
    end
    if #returns == 1 then
        return translateType(returns[1])
    end
    local parts: { string } = {}
    for _, entry in returns do
        table.insert(parts, translateType(entry))
    end
    return `({table.concat(parts, ", ")})`
end

--- `list or {}` types as `{T} | {}`, which the new solver declines to iterate.
--- A generic that returns `{T}` collapses it at one call site instead of at
--- every loop.
local function orEmpty<T>(list: { T }?): { T }
    return list or {}
end

local function functionType(method: Schema.Method): string
    local rendered = `({parameterList(method.Parameters)}) -> {returnList(method.Returns)}`
    -- `Signal.new` returns `Signal<T...>`, whose pack has to be bound
    -- *somewhere*. It is bound on the function itself, which is also what makes
    -- `local s: Signal<string> = Signal.new()` infer.
    if string.find(rendered, "T...", 1, true) ~= nil then
        rendered = `<T...>{rendered}`
    end
    return rendered
end

--- `game:GetService("RunService")` has to come back as a `RunService` and not
--- as an `Instance`, or every call site needs an `:IsA` narrowing to reach the
--- members it just asked for. The overload set is *generative* -- one per
--- Service-tagged class -- so it is emitted from the class table rather than
--- listed in a definition file that would drift every time a service ships.
--- The specific overloads precede the general one, because resolution takes
--- the first that matches.
local SingletonMethod = "GetService"

--- The IDL's doc text, wrapped as `---` comment lines above the declaration.
---
--- Carried in the definitions file itself rather than in the separate
--- documentation JSON api-design.md §5 also lists. That format is an LSP-only
--- setting -- `luau-lsp analyze` has no flag for it, so nothing in the gate can
--- prove a generated one is even shaped right, and MASTER_PROMPT §9 forbids
--- shipping a guess. A `---` comment is plain Luau, the freshness gate already
--- covers this file, and there is no second artifact to keep in step.
local function emitDoc(lines: { string }, indent: string, doc: string?)
    if doc == nil or doc == "" then
        return
    end

    -- Wrapped at a width that keeps the generated file inside stylua's column
    -- budget once the indent is added.
    local width = 100 - #indent
    local line = ""
    for word in string.gmatch(doc, "%S+") do
        if line == "" then
            line = word
        elseif #line + #word + 1 <= width then
            line ..= ` {word}`
        else
            table.insert(lines, `{indent}--- {line}`)
            line = word
        end
    end
    if line ~= "" then
        table.insert(lines, `{indent}--- {line}`)
    end
end

local function emitMembers(
    lines: { string },
    properties: { Schema.Property }?,
    methods: { Schema.Method }?,
    events: { Schema.Event }?,
    singletonNames: { string }?
)
    for _, property in orEmpty(properties) do
        local access = if property.ReadOnly then "read " else ""
        emitDoc(lines, "    ", property.Doc)
        table.insert(lines, `    {access}{property.Name}: {translateType(property.Type)}`)
    end
    for _, event in orEmpty(events) do
        local parts: { string } = {}
        for _, parameter in event.Parameters do
            table.insert(parts, translateType(parameter.Type))
        end
        -- Events are read-only: a script connects to a signal, it never
        -- replaces one. A signal with no payload is `Signal<()>` and not
        -- `Signal<>` -- an empty type pack has a spelling, and the analyzer
        -- rejects the other one.
        local pack = if #parts == 0 then "()" else table.concat(parts, ", ")
        emitDoc(lines, "    ", event.Doc)
        table.insert(lines, `    read {event.Name}: Signal<{pack}>`)
    end
    for _, method in orEmpty(methods) do
        if method.Name == SingletonMethod and singletonNames ~= nil then
            for _, serviceName in singletonNames do
                table.insert(lines, `    function {method.Name}(self, name: "{serviceName}"): {serviceName}`)
            end
        end
        local parameters = parameterList(method.Parameters)
        local separator = if parameters == "" then "" else ", "
        emitDoc(lines, "    ", method.Doc)
        table.insert(lines, `    function {method.Name}(self{separator}{parameters}): {returnList(method.Returns)}`)
    end
end

--- Classes are emitted parents-first, because `extends` has to name a type the
--- file has already declared.
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

local Lines: { string } = {
    "-- GENERATED by api/generator/gen_dts.luau from api/defs/*.api.luau.",
    "--",
    "-- Do not edit. CI regenerates this file and fails on a diff, so a hand edit",
    -- Deliberately ASCII-only, section signs included: this file is regenerated
    -- and byte-compared, so an encoding difference between two machines would
    -- read as a spurious diff rather than as the toolchain problem it is.
    "-- is a change that survives exactly until the next build (api-design.md section 5).",
    "--",
    "-- StyLua cannot parse `declare`, which is a luau-lsp extension rather than",
    "-- Luau syntax; `.styluaignore` excludes `*.d.luau` for that reason.",
    "",
    "export type Content = string",
    "",
    -- `Signal` and `Connection` are hand-shaped here rather than emitted from
    -- their IDL entries, for a reason the pin forces: an `extern type` cannot
    -- take type parameters (Parser.cpp:1701 carries upstream's TODO for it), so
    -- `Signal<T...>` is not expressible as one. A generic type alias is, and it
    -- keeps the payload types on every event -- which is the entire value of
    -- declaring events at all. Their `Constructors` still come from the IDL
    -- below, so `Signal.new` stays generated.
    "export type Connection = {",
    "    Connected: boolean,",
    "    Disconnect: (self: Connection) -> (),",
    "}",
    "",
    -- A variadic return needs its own parentheses (`-> (T...)`), and a type
    -- pack cannot follow a named parameter in an argument list -- `Fire` takes
    -- `...any` for that reason, which loses the payload types on the *firing*
    -- side while keeping them on the *handling* side, where they are what
    -- anyone actually writes against.
    "export type Signal<T...> = {",
    "    Connect: (self: Signal<T...>, handler: (T...) -> ()) -> Connection,",
    "    Once: (self: Signal<T...>, handler: (T...) -> ()) -> Connection,",
    "    Wait: (self: Signal<T...>) -> (T...),",
    "    Fire: (self: Signal<T...>, ...any) -> (),",
    "    Destroy: (self: Signal<T...>) -> (),",
    "}",
    "",
}

--- Emitted by hand above; the extern-type pass skips them.
local HandWritten: { [string]: boolean } = { Signal = true, Connection = true }

local Classes: { Schema.Class } = sortedByHierarchy(orEmpty(Definition.Classes))
local Datatypes: { Schema.Datatype } = orEmpty(Definition.Datatypes)
local Enums: { Schema.Enum } = orEmpty(Definition.Enums)
local Libraries: { Schema.Library } = orEmpty(Definition.Libraries)
local Globals: { Schema.Method } = orEmpty(Definition.Globals)

-- EnumItem first: every generated enum type extends it.
table.insert(Lines, "declare extern type EnumItem with")
table.insert(Lines, "    read Name: string")
table.insert(Lines, "    read Value: number")
table.insert(Lines, "    read EnumType: any")
table.insert(Lines, "end")
table.insert(Lines, "")

for _, enum in Enums do
    table.insert(Lines, `declare extern type {enumTypeName(enum.Name)} extends EnumItem with`)
    table.insert(Lines, "end")
    table.insert(Lines, "")
end

for _, datatype in Datatypes do
    if not HandWritten[datatype.Name] then
        table.insert(Lines, `declare extern type {datatype.Name} with`)
        emitMembers(Lines, datatype.Properties, datatype.Methods, nil)
        -- Metamethods after the ordinary members, and by repeated declaration
        -- for an overloaded one: an `extern type` takes several `function`
        -- lines with the same name and reads them as an overload set, which is
        -- the only way `CFrame * CFrame` and `CFrame * vector` can both be
        -- declared.
        for _, operator in orEmpty(datatype.Operators) do
            local parameters = parameterList(operator.Parameters)
            local separator = if parameters == "" then "" else ", "
            table.insert(
                Lines,
                `    function {operator.Name}(self{separator}{parameters}): {returnList(operator.Returns)}`
            )
        end
        table.insert(Lines, "end")
        table.insert(Lines, "")
    end
end

local ServiceNames: { string } = {}
for _, class in Classes do
    for _, tag in orEmpty(class.Tags) do
        if tag == "Service" then
            table.insert(ServiceNames, class.Name)
        end
    end
end

for _, class in Classes do
    local header = if class.Extends ~= nil
        then `declare extern type {class.Name} extends {class.Extends} with`
        else `declare extern type {class.Name} with`
    table.insert(Lines, header)
    emitMembers(Lines, class.Properties, class.Methods, class.Events, ServiceNames)
    table.insert(Lines, "end")
    table.insert(Lines, "")
end

-- The `Enum` global. Each enum is an anonymous table rather than a named type:
-- nothing annotates the enum *container*, only its items, and a named type for
-- it would only be a name to get wrong.
if #Enums > 0 then
    table.insert(Lines, "declare Enum: {")
    for _, enum in Enums do
        local itemType = enumTypeName(enum.Name)
        table.insert(Lines, `    {enum.Name}: \{`)
        for _, item in enum.Items do
            table.insert(Lines, `        {item.Name}: {itemType},`)
        end
        table.insert(Lines, `        GetEnumItems: (self: any) -> \{ {itemType} },`)
        table.insert(Lines, "    },")
    end
    table.insert(Lines, "}")
    table.insert(Lines, "")
end

-- Datatype namespaces: the constructors and constants reached off the type
-- name, which are camelCase (ADR 0034).
for _, datatype in Datatypes do
    local entries: { string } = {}

    -- Two constructors with one name are an overload set, and a table type
    -- cannot carry the same key twice: written naively the second silently
    -- shadows the first and `CFrame.new(position)` quietly stops existing.
    -- Overloads are an intersection of function types, which is the same form
    -- `Instance.new`'s string singletons use below.
    local order: { string } = {}
    local byName: { [string]: { string } } = {}
    for _, constructor in orEmpty(datatype.Constructors) do
        local overloads = byName[constructor.Name]
        if overloads == nil then
            overloads = {}
            byName[constructor.Name] = overloads
            table.insert(order, constructor.Name)
        end
        table.insert(overloads, `({functionType(constructor)})`)
    end
    for _, name in order do
        local overloads = byName[name]
        local rendered = if #overloads == 1 then overloads[1] else table.concat(overloads, " & ")
        table.insert(entries, `    {name}: {rendered},`)
    end
    for _, constant in orEmpty(datatype.Constants) do
        table.insert(entries, `    {constant.Name}: {translateType(constant.Type)},`)
    end
    if #entries > 0 then
        table.insert(Lines, `declare {datatype.Name}: \{`)
        for _, entry in entries do
            table.insert(Lines, entry)
        end
        table.insert(Lines, "}")
        table.insert(Lines, "")
    end
end

-- Global libraries. `task` is a table of functions reached by name and never
-- held, so it is a `declare <name>: { … }` like a datatype namespace rather than
-- an `extern type` -- nothing has its type.
for _, library in Libraries do
    table.insert(Lines, `declare {library.Name}: \{`)
    for _, fn in library.Functions do
        table.insert(Lines, `    {fn.Name}: ({functionType(fn)}),`)
    end
    table.insert(Lines, "}")
    table.insert(Lines, "")
end

-- Free globals the engine adds. `print` is Luau's own and is deliberately not
-- here: the analyzer already knows it, and declaring it again would make this
-- file a second source of truth for something the pin fixes.
for _, global in Globals do
    table.insert(
        Lines,
        `declare function {global.Name}({declarationParameterList(global.Parameters)}): {returnList(global.Returns)}`
    )
end
if #Globals > 0 then
    table.insert(Lines, "")
end

--- `Instance.new` is typed by string singleton, so `Instance.new("Part")` is a
--- `Part` and not an `Instance`. Overloads are an intersection of function
--- types, which is how a declaration file spells them.
local Creatable: { string } = {}
for _, class in Classes do
    local creatable = true
    for _, tag in orEmpty(class.Tags) do
        if tag == "Abstract" or tag == "Service" or tag == "NotCreatable" then
            creatable = false
        end
    end
    if creatable then
        table.insert(Creatable, class.Name)
    end
end

if #Creatable > 0 then
    local overloads: { string } = {}
    for _, name in Creatable do
        table.insert(overloads, `(("{name}") -> {name})`)
    end
    table.insert(Lines, "declare Instance: {")
    table.insert(Lines, `    new: {table.concat(overloads, "\n        & ")},`)
    table.insert(Lines, "}")
    table.insert(Lines, "")
end

-- The world globals. `game` and `workspace` are the DataModel and the Workspace
-- service; `script` is the running Script instance (api-design.md §3).
local function declareGlobalIfPresent(globalName: string, className: string)
    for _, class in Classes do
        if class.Name == className then
            table.insert(Lines, `declare {globalName}: {className}`)
            return
        end
    end
end

declareGlobalIfPresent("game", "DataModel")
declareGlobalIfPresent("workspace", "Workspace")
declareGlobalIfPresent("script", "Script")
table.insert(Lines, "")

if not fs.exists("runtime/types") then
    fs.createDirectory("runtime/types", { makeParents = true })
end
fs.writeStringToFile(OutputPath, table.concat(Lines, "\n"))

print(
    `gen_dts: wrote {OutputPath} -- {#Classes} class(es), {#Datatypes} datatype(s), {#Enums} enum(s), `
        .. `{#Libraries} librar(ies), {#Globals} global(s), {#Creatable} creatable`
)
