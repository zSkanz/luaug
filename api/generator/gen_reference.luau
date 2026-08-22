--!strict
--[[
	API reference generator (api-design.md §5, generated artifact 5; roadmap M8).

	Emits `docs/api/*.md` from the IDL: one page per class, plus a page each for
	the datatypes, the enums and the libraries, plus an index.

	**Why a generated reference at all, when `api-design.md` exists.**
	`api-design.md` is the DESIGN: it argues, it compares, it says why a thing is
	shaped the way it is, and it is written for somebody deciding whether this
	engine's model suits them. A reference answers a different question -- "what
	are the arguments to `Workspace:Raycast`" -- and answering it out of prose
	means the prose has to list every member, which is how a design document
	turns into a stale catalogue. Generating it from the IDL means the catalogue
	cannot be stale: it is the same source the C++ registration, the type
	definitions and the api-dump come from.

	**What is in it and what is not.** Every declared member with its type, its
	default, its thread safety and its doc prose. NOT inherited members: a class
	page lists what it DECLARES and links to its base, which is the same rule the
	api-dump follows and for the same reason -- flattening turns one added member
	on `Instance` into a diff on every page in the directory.

	The output is CHECKED IN and freshness-gated like every other generated
	artifact, so a member added to the IDL without regenerating is a red gate
	rather than a reference that quietly disagrees with the engine.

	    lute api/generator/gen_reference.luau
]]

local fs = require("@std/fs")
local process = require("@std/process")

local Definition = require("../defs")
local Schema = require("../schema")

local OutputDirectory = "docs/api"

local function orEmpty<T>(list: { T }?): { T }
    return list or {}
end

--- Markdown escaping is deliberately absent: doc prose in the IDL is authored as
--- markdown already -- it carries `**bold**`, backticked identifiers and section
--- references -- and escaping it would render those literally. What IS handled
--- is the table cell, because a doc string containing a pipe would end the
--- column early and silently drop the rest of the sentence.
local function cell(text: string): string
    local out = string.gsub(text, "|", "\\|")
    out = string.gsub(out, "\n\n", "<br><br>")
    out = string.gsub(out, "\n", " ")
    return out
end

local function anchorFor(name: string): string
    return `{string.lower(name)}.md`
end

local function signature(method: Schema.Method): string
    local parts: { string } = {}
    for _, parameter in method.Parameters do
        local text = `{parameter.Name}: {parameter.Type}`
        if parameter.Default ~= nil then
            text ..= ` = {parameter.Default}`
        end
        table.insert(parts, text)
    end

    local returns = ""
    if #method.Returns == 1 then
        returns = `: {method.Returns[1]}`
    elseif #method.Returns > 1 then
        returns = `: ({table.concat(method.Returns, ", ")})`
    end

    return `({table.concat(parts, ", ")}){returns}`
end

local function eventSignature(event: Schema.Event): string
    local parts: { string } = {}
    for _, parameter in event.Parameters do
        table.insert(parts, `{parameter.Name}: {parameter.Type}`)
    end
    return `({table.concat(parts, ", ")})`
end

local function hasTag(class: Schema.Class, tag: Schema.ClassTag): boolean
    for _, candidate in orEmpty(class.Tags) do
        if candidate == tag then
            return true
        end
    end
    return false
end

local function sortedCopy<T>(list: { T }, key: (T) -> string): { T }
    local copy = table.clone(list)
    table.sort(copy, function(a, b)
        return key(a) < key(b)
    end)
    return copy
end

local function emitProperties(lines: { string }, properties: { Schema.Property }, heading: string)
    if #properties == 0 then
        return
    end
    table.insert(lines, `## {heading}`)
    table.insert(lines, "")
    table.insert(lines, "| Name | Type | Default | Access | Description |")
    table.insert(lines, "|---|---|---|---|---|")
    for _, property in sortedCopy(properties, function(p)
        return p.Name
    end) do
        local access = if property.ReadOnly then "read-only" else "read/write"
        if property.Inert then
            -- **The one tag a reference must never omit.** `Inert` means the
            -- engine stores the value, reports it back faithfully, and acts on
            -- none of it -- so a page that listed it like any other property
            -- would be documenting a feature that does not exist.
            access ..= ", **inert**"
        end
        table.insert(
            lines,
            `| \`{property.Name}\` | \`{property.Type}\` | {if property.Default then `\`{property.Default}\`` else "—"} `
                .. `| {access} | {cell(property.Doc or "")} |`
        )
    end
    table.insert(lines, "")
end

local function emitMethods(lines: { string }, methods: { Schema.Method }, heading: string)
    if #methods == 0 then
        return
    end
    table.insert(lines, `## {heading}`)
    table.insert(lines, "")
    for _, method in sortedCopy(methods, function(m)
        return m.Name
    end) do
        table.insert(lines, `### \`{method.Name}{signature(method)}\``)
        table.insert(lines, "")
        if method.Yields then
            table.insert(lines, "**Yields.** The calling thread parks until it completes.")
            table.insert(lines, "")
        end
        table.insert(lines, method.Doc)
        table.insert(lines, "")
    end
end

local function emitEvents(lines: { string }, events: { Schema.Event })
    if #events == 0 then
        return
    end
    table.insert(lines, "## Events")
    table.insert(lines, "")
    table.insert(lines, "Every signal here is **deferred** (ADR 0015): a handler runs at the next")
    table.insert(lines, "drain point, never inside the call that fired it.")
    table.insert(lines, "")
    for _, event in sortedCopy(events, function(e)
        return e.Name
    end) do
        table.insert(lines, `### \`{event.Name}{eventSignature(event)}\``)
        table.insert(lines, "")
        table.insert(lines, event.Doc)
        table.insert(lines, "")
    end
end

local Written = 0

local function write(name: string, lines: { string })
    local body = table.concat(lines, "\n")
    if string.sub(body, -1) ~= "\n" then
        body ..= "\n"
    end
    fs.writeStringToFile(`{OutputDirectory}/{name}`, body)
    Written += 1
end

local Header = {
    "<!-- GENERATED by api/generator/gen_reference.luau from api/defs/*.api.luau. Do not edit. -->",
    "",
}

local function classPage(class: Schema.Class)
    local lines = table.clone(Header)
    table.insert(lines, `# {class.Name}`)
    table.insert(lines, "")

    local facts: { string } = {}
    if class.Extends ~= nil then
        table.insert(facts, `Inherits [\`{class.Extends}\`]({anchorFor(class.Extends)})`)
    end
    if hasTag(class, "Service") then
        table.insert(facts, `A **service**: reached with \`game:GetService("{class.Name}")\``)
    end
    if hasTag(class, "NotCreatable") then
        table.insert(facts, "**Not creatable**: `Instance.new` refuses it")
    elseif class.Extends ~= nil then
        table.insert(facts, `Created with \`Instance.new("{class.Name}")\``)
    end
    if hasTag(class, "Abstract") then
        table.insert(facts, "**Abstract**: it exists to be inherited from")
    end
    if hasTag(class, "DevOnly") then
        table.insert(facts, "**Development builds only**: absent from a shipped game")
    end
    for _, fact in facts do
        table.insert(lines, `- {fact}`)
    end
    table.insert(lines, "")
    table.insert(lines, class.Doc)
    table.insert(lines, "")
    table.insert(lines, "**Members below are the ones this class DECLARES.** Everything its base")
    table.insert(lines, "offers is on the base's page, which is what keeps one added member on")
    table.insert(lines, "`Instance` from being a change to every page here.")
    table.insert(lines, "")

    emitProperties(lines, orEmpty(class.Properties), "Properties")
    emitMethods(lines, orEmpty(class.Methods), "Methods")
    emitEvents(lines, orEmpty(class.Events))

    write(anchorFor(class.Name), lines)
end

local function datatypesPage(datatypes: { Schema.Datatype })
    local lines = table.clone(Header)
    table.insert(lines, "# Datatypes")
    table.insert(lines, "")
    table.insert(lines, "Value types rather than Instances. **Constructors and constants are reached")
    table.insert(lines, "through the namespace and are camelCase** (`CFrame.new`, `CFrame.identity`);")
    table.insert(lines, "**members reached through a value are PascalCase** (`v.Magnitude`,")
    table.insert(lines, "`cf:Inverse()`). That split is ADR 0034 and it is the whole naming rule.")
    table.insert(lines, "")

    for _, datatype in sortedCopy(datatypes, function(d)
        return d.Name
    end) do
        table.insert(lines, `## {datatype.Name}`)
        table.insert(lines, "")
        table.insert(lines, datatype.Doc)
        table.insert(lines, "")
        emitProperties(lines, orEmpty(datatype.Constants), `{datatype.Name} — constants`)
        emitMethods(lines, orEmpty(datatype.Constructors), `{datatype.Name} — constructors`)
        emitProperties(lines, orEmpty(datatype.Properties), `{datatype.Name} — properties`)
        emitMethods(lines, orEmpty(datatype.Methods), `{datatype.Name} — methods`)

        local operators = orEmpty(datatype.Operators)
        if #operators > 0 then
            table.insert(lines, `## {datatype.Name} — operators`)
            table.insert(lines, "")
            table.insert(lines, "| Operator | Signature | Description |")
            table.insert(lines, "|---|---|---|")
            for _, operator in operators do
                table.insert(lines, `| \`{operator.Name}\` | \`{signature(operator :: any)}\` | {cell(operator.Doc)} |`)
            end
            table.insert(lines, "")
        end
    end

    write("datatypes.md", lines)
end

local function enumsPage(enums: { Schema.Enum })
    local lines = table.clone(Header)
    table.insert(lines, "# Enums")
    table.insert(lines, "")
    table.insert(lines, "Reached as `Enum.<Name>.<Item>`. Every enum-typed property is **total**:")
    table.insert(lines, "there is no nil member of an enumeration, so a value meaning `unset` is")
    table.insert(lines, "declared as an item rather than left to a nil (ADR 0039).")
    table.insert(lines, "")

    for _, enumeration in sortedCopy(enums, function(e)
        return e.Name
    end) do
        table.insert(lines, `## Enum.{enumeration.Name}`)
        table.insert(lines, "")
        table.insert(lines, enumeration.Doc)
        table.insert(lines, "")
        table.insert(lines, "| Item | Value | Description |")
        table.insert(lines, "|---|---|---|")
        for _, item in enumeration.Items do
            table.insert(lines, `| \`{item.Name}\` | {item.Value} | {cell(item.Doc or "")} |`)
        end
        table.insert(lines, "")
    end

    write("enums.md", lines)
end

local function librariesPage(libraries: { Schema.Library }, globals: { Schema.Method })
    local lines = table.clone(Header)
    table.insert(lines, "# Libraries and globals")
    table.insert(lines, "")
    table.insert(lines, "Global tables reached by name. Their functions are camelCase, because a")
    table.insert(lines, "library is a namespace and not an object (ADR 0034).")
    table.insert(lines, "")

    for _, library in sortedCopy(libraries, function(l)
        return l.Name
    end) do
        table.insert(lines, `## {library.Name}`)
        table.insert(lines, "")
        table.insert(lines, library.Doc)
        table.insert(lines, "")
        emitMethods(lines, library.Functions, `{library.Name} — functions`)
    end

    if #globals > 0 then
        table.insert(lines, "## Free functions")
        table.insert(lines, "")
        emitMethods(lines, globals, "Globals")
    end

    write("libraries.md", lines)
end

local function indexPage(classes: { Schema.Class })
    local lines = table.clone(Header)
    table.insert(lines, "# LuauG API reference")
    table.insert(lines, "")
    table.insert(lines, "Generated from the typed IDL in [`../../api/defs/`](../../api/defs/), which")
    table.insert(lines, "is the same source the engine's C++ registration, the `.d.luau` type")
    table.insert(lines, "definitions and the versioned api-dump come from. If a member is here, the")
    table.insert(lines, "engine has it.")
    table.insert(lines, "")
    table.insert(lines, "**This is the catalogue, not the design.**")
    table.insert(lines, "[`../api-design.md`](../api-design.md) is where the reasoning lives —")
    table.insert(lines, "why signals are deferred, why children are not members, what a `Content`")
    table.insert(lines, "URN is — and [`../coming-from-roblox.md`](../coming-from-roblox.md) is the")
    table.insert(lines, "guided tour.")
    table.insert(lines, "")
    table.insert(lines, "- [Datatypes](datatypes.md) — `CFrame`, `Color3`, `UDim2`, `Signal`, and the rest")
    table.insert(lines, "- [Enums](enums.md)")
    table.insert(lines, "- [Libraries and globals](libraries.md) — `task`, and what the global table carries")
    table.insert(lines, "")

    local services: { Schema.Class } = {}
    local instances: { Schema.Class } = {}
    for _, class in classes do
        if hasTag(class, "Service") then
            table.insert(services, class)
        else
            table.insert(instances, class)
        end
    end

    local function classTable(heading: string, list: { Schema.Class })
        table.insert(lines, `## {heading}`)
        table.insert(lines, "")
        table.insert(lines, "| Class | Inherits | Summary |")
        table.insert(lines, "|---|---|---|")
        for _, class in sortedCopy(list, function(c)
            return c.Name
        end) do
            -- The first sentence, because an index is a list of what to open
            -- rather than a place to read. The rest is on the page.
            local summary = string.match(class.Doc, "^(.-%.)%s") or class.Doc
            table.insert(
                lines,
                `| [\`{class.Name}\`]({anchorFor(class.Name)}) `
                    .. `| {if class.Extends then `[\`{class.Extends}\`]({anchorFor(class.Extends)})` else "—"} `
                    .. `| {cell(summary)} |`
            )
        end
        table.insert(lines, "")
    end

    classTable("Services", services)
    classTable("Instances", instances)

    write("README.md", lines)
end

local function main(): number
    local classes = orEmpty(Definition.Classes)

    -- Removed and rewritten rather than overwritten in place: a class deleted
    -- from the IDL has to take its page with it, and a stale page is exactly the
    -- kind of documentation that outlives the thing it describes.
    if fs.exists(OutputDirectory) then
        fs.removeDirectory(OutputDirectory, { recursive = true })
    end
    fs.createDirectory(OutputDirectory, { makeParents = true })

    for _, class in classes do
        classPage(class)
    end
    datatypesPage(orEmpty(Definition.Datatypes))
    enumsPage(orEmpty(Definition.Enums))
    librariesPage(orEmpty(Definition.Libraries), orEmpty(Definition.Globals))
    indexPage(classes)

    print(`gen_reference: wrote {Written} page(s) to {OutputDirectory}/`)
    return 0
end

process.exit(main())
