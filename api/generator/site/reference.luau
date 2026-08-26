--!strict
--[[
	The reference pages: one per class, datatype, enum and library, plus the
	globals page, the class hierarchy and the reference index.

	**What this renders that `docs/api/*.md` does not**, and why each one is
	worth the code:

	- **Inherited members**, collapsed per ancestor. A Markdown catalogue is
	  right to omit them; a page somebody lands on from a search is not.
	- **Every type name is a link.** `{BasePart}`, `Enum.PartShape`,
	  `Signal<Instance>` -- the punctuation stays and the names become
	  navigation.
	- **The facts a member carries besides its type**: read-only, transient,
	  yielding, thread safety, the numeric domain an `ErrorKey` names, and the
	  one marker that matters most -- `Inert`, which means the engine stores the
	  value, reports it back faithfully, and does not act on it. A reference that
	  omits that is a reference that lies by omission, and every one of M4's
	  unbacked-behaviour defects was found by a person watching nothing happen.
	- **Code samples**, from `docs/samples/`. The IDL has no field for one
	  (`api/schema.luau`: `Doc` is a single string that doubles as an i18n key),
	  so a sample is a real `.luau` file named after the symbol it demonstrates.
	  That is not a workaround: it means `luaug check` type-checks every sample
	  in the documentation against `runtime/types/engine.d.luau`, so a sample
	  cannot drift from the engine without the gate going red.
]]

local markdown = require("./markdown")
local model = require("./model")

local Schema = require("../../schema")
local prose = require("./prose")
local shell = require("./shell")

export type Context = {
    Model: model.Model,
    --- Root-relative path of the page being rendered, for `../` arithmetic.
    Prefix: string,
    --- Body text for a symbol, keyed `Owner.Member` or `Owner`.
    Samples: { [string]: string },
    --- Resolves an authored `[text](target)` link.
    resolveLink: (string) -> string?,
}

local ReferencePath = "reference/index.html"
local HierarchyPath = "reference/hierarchy.html"

--- Names that appear inside a `TypeName` and are not types the engine declares:
--- Luau's own primitives, plus the generic parameter packs the IDL writes.
local Primitives: { [string]: boolean } = {
    ["any"] = true,
    ["boolean"] = true,
    ["nil"] = true,
    ["number"] = true,
    ["string"] = true,
    ["thread"] = true,
    ["buffer"] = true,
    ["T"] = true,
    ["U"] = true,
}

local function heading(headings: { shell.Heading }, level: number, text: string, id: string)
    table.insert(headings, { Level = level, Id = id, Text = text })
end

local function href(context: Context, path: string): string
    return context.Prefix .. path
end

--- Renders a `TypeName` with every declared name inside it linked, keeping the
--- punctuation that makes it readable Luau.
local function renderType(context: Context, typeName: string): string
    local out: { string } = {}
    local index = 1
    while index <= #typeName do
        local start, finish = string.find(typeName, "[%a_][%w_]*[%.%w_]*", index)
        if start == nil or finish == nil then
            table.insert(out, markdown.escape(string.sub(typeName, index)))
            break
        end
        if start > index then
            table.insert(out, markdown.escape(string.sub(typeName, index, start - 1)))
        end

        local token = string.sub(typeName, start, finish)
        local path = if Primitives[token] then nil else context.Model.resolve(token)
        if path ~= nil then
            table.insert(out, `<a href="{href(context, path)}">{markdown.escape(token)}</a>`)
        else
            table.insert(out, markdown.escape(token))
        end
        index = finish + 1
    end
    return table.concat(out)
end

local function markdownOptions(context: Context): markdown.RenderOptions
    return {
        resolveSymbol = function(text: string): string?
            local path = context.Model.resolve(text)
            return if path ~= nil then href(context, path) else nil
        end,
        resolveLink = function(target: string): string?
            local resolved = context.resolveLink(target)
            return if resolved ~= nil then href(context, resolved) else nil
        end,
    }
end

--- Doc prose, published.
---
--- The definition files are the engine's own specification and their prose
--- cites the design document and the decision records. `prose.clean` removes
--- those citations; nothing else about the sentence changes, because the
--- sentence is already the one worth reading.
local function renderDoc(context: Context, text: string): string
    return markdown.prose(prose.clean(text), markdownOptions(context))
end

local function badge(text: string, kind: string): string
    return `<span class="badge badge-{kind}">{markdown.escape(text)}</span>`
end

--- The numeric domain an `ErrorKey` names, spelled out. The key is shared by
--- shape rather than minted per property, so one sentence covers every property
--- that carries it.
local ErrorDomains: { [string]: string } = {
    ["number_at_least_zero"] = "Accepts a number of zero or more.",
    ["number_above_zero"] = "Accepts a number greater than zero.",
    ["number_positive"] = "Accepts a positive number.",
    ["number_zero_to_one"] = "Accepts a number from 0 to 1.",
    ["number_cone_degrees"] = "Accepts an angle in degrees, greater than zero and at most 180.",
    ["number_slope_degrees"] = "Accepts a slope in degrees, from 0 to 90.",
    ["number_latitude_degrees"] = "Accepts a latitude in degrees, from -90 to 90.",
    ["number_exposure_stops"] = "Accepts an exposure in stops.",
    ["number_fixed_timestep"] = "Accepts a fixed timestep in seconds, within the range the scheduler supports.",
}

local function propertyFacts(property: Schema.Property): string
    local facts: { string } = {}
    table.insert(facts, if property.ReadOnly then badge("read-only", "readonly") else badge("read/write", "rw"))
    if property.Inert then
        table.insert(facts, badge("stored, not yet acted on", "inert"))
    end
    if property.Transient then
        table.insert(facts, badge("not serialized", "transient"))
    end
    if property.ThreadSafety ~= nil then
        table.insert(facts, badge(property.ThreadSafety, "thread"))
    end

    local notes: { string } = {}
    if property.Default ~= nil then
        table.insert(notes, `<span class="fact">Default <code>{markdown.escape(property.Default)}</code></span>`)
    end
    local errorKey = property.ErrorKey
    if errorKey ~= nil then
        local domain = ErrorDomains[errorKey]
        if domain ~= nil then
            table.insert(notes, `<span class="fact">{markdown.escape(domain)}</span>`)
        end
    end

    local badges = `<div class="badges">{table.concat(facts, " ")}</div>`
    local factLine = if #notes > 0 then `<div class="facts">{table.concat(notes, " ")}</div>` else ""
    return badges .. factLine
end

local function sampleFor(context: Context, key: string): string
    local body = context.Samples[key]
    if body == nil then
        return ""
    end
    return `<div class="sample"><p class="sample-title">Example</p><pre><code class="language-luau">{markdown.escape(
        body
    )}</code></pre></div>`
end

local function memberSection(
    context: Context,
    headings: { shell.Heading },
    owner: string,
    name: string,
    signatureHtml: string,
    factsHtml: string,
    doc: string
): string
    heading(headings, 3, name, name)
    local out: { string } = {}
    table.insert(out, `<section class="member" id="{markdown.escape(name)}">`)
    table.insert(
        out,
        `<h3 class="member-name">{markdown.escape(name)}<a class="anchor" href="#{markdown.escape(name)}" aria-label="Link to {markdown.escape(
            name
        )}">#</a></h3>`
    )
    table.insert(out, `<p class="signature"><code>{signatureHtml}</code></p>`)
    table.insert(out, factsHtml)
    table.insert(out, `<div class="doc">{renderDoc(context, doc)}</div>`)
    table.insert(out, sampleFor(context, `{owner}.{name}`))
    table.insert(out, "</section>")
    return table.concat(out)
end

--- The compact list of member names at the top of a page, so a reader who knows
--- what they are looking for never scrolls to find it.
local function memberIndex(names: { { Kind: string, Names: { string } } }): string
    local columns: { string } = {}
    for _, group in names do
        if #group.Names == 0 then
            continue
        end
        local links: { string } = {}
        for _, name in group.Names do
            table.insert(links, `<li><a href="#{markdown.escape(name)}"><code>{markdown.escape(name)}</code></a></li>`)
        end
        table.insert(
            columns,
            `<div class="index-column"><p class="index-title">{markdown.escape(group.Kind)}</p><ul>{table.concat(links)}</ul></div>`
        )
    end
    if #columns == 0 then
        return ""
    end
    return `<div class="member-index">{table.concat(columns)}</div>`
end

local function classIcon(context: Context, name: string, available: { [string]: boolean }): string
    if not available[name] then
        return ""
    end
    return `<img class="title-icon" src="{href(context, `assets/class/{name}.png`)}" alt="">`
end

local function renderClassPage(
    context: Context,
    info: model.ClassInfo,
    icons: { [string]: boolean }
): (string, { shell.Heading })
    local class = info.Class
    local headings: { shell.Heading } = {}
    local out: { string } = {}

    table.insert(
        out,
        `<h1 class="page-title">{classIcon(context, class.Name, icons)}{markdown.escape(class.Name)}</h1>`
    )

    local badges: { string } = {}
    if info.Service then
        table.insert(badges, badge("Service", "service"))
    end
    if info.Abstract then
        table.insert(badges, badge("Abstract", "abstract"))
    end
    if info.Creatable then
        table.insert(badges, badge("Creatable", "creatable"))
    elseif not info.Abstract then
        table.insert(badges, badge("Not creatable", "notcreatable"))
    end
    if model.hasTag(class, "DevOnly") then
        table.insert(badges, badge("Development builds only", "devonly"))
    end
    table.insert(badges, badge(class.Module, "module"))
    table.insert(out, `<div class="badges badges-title">{table.concat(badges, " ")}</div>`)

    if #info.Ancestry > 0 then
        local chain: { string } = {}
        for _, ancestor in info.Ancestry do
            table.insert(
                chain,
                `<a href="{href(context, model.classPath(ancestor.Name))}">{markdown.escape(ancestor.Name)}</a>`
            )
        end
        table.insert(chain, `<span class="current">{markdown.escape(class.Name)}</span>`)
        table.insert(out, `<p class="ancestry">{table.concat(chain, '<span class="sep">&rsaquo;</span>')}</p>`)
    end

    table.insert(out, `<div class="lead">{renderDoc(context, class.Doc)}</div>`)

    if info.Creatable then
        local defaultName = class.DefaultName or class.Name
        local note = if defaultName ~= class.Name
            then ` The new instance is named <code>{markdown.escape(defaultName)}</code>.`
            else ""
        table.insert(
            out,
            `<div class="callout callout-how"><p>Created with <code>Instance.new("{markdown.escape(class.Name)}")</code>.{note}</p></div>`
        )
    elseif info.Service then
        table.insert(
            out,
            `<div class="callout callout-how"><p>Reached with <code>game:GetService("{markdown.escape(class.Name)}")</code>. There is exactly one, and it is never created or destroyed by a script.</p></div>`
        )
    elseif info.Abstract then
        table.insert(
            out,
            `<div class="callout callout-note"><p>Abstract: nothing constructs a <code>{markdown.escape(class.Name)}</code> directly. It exists so that everything below it shares one set of members.</p></div>`
        )
    end

    table.insert(out, sampleFor(context, class.Name))

    if #info.Children > 0 then
        local links: { string } = {}
        for _, child in info.Children do
            table.insert(
                links,
                `<a class="chip" href="{href(context, model.classPath(child.Name))}">{markdown.escape(child.Name)}</a>`
            )
        end
        table.insert(
            out,
            `<div class="subclasses"><p class="index-title">Inherited by</p><div class="chips">{table.concat(links)}</div></div>`
        )
    end

    local properties = model.sortedCopy(model.orEmpty(class.Properties), function(p: Schema.Property)
        return p.Name
    end)
    local methods = model.sortedCopy(model.orEmpty(class.Methods), function(m: Schema.Method)
        return m.Name
    end)
    local events = model.sortedCopy(model.orEmpty(class.Events), function(e: Schema.Event)
        return e.Name
    end)

    local propertyNames: { string } = {}
    for _, property in properties do
        table.insert(propertyNames, property.Name)
    end
    local methodNames: { string } = {}
    for _, method in methods do
        table.insert(methodNames, method.Name)
    end
    local eventNames: { string } = {}
    for _, event in events do
        table.insert(eventNames, event.Name)
    end

    table.insert(
        out,
        memberIndex({
            { Kind = "Properties", Names = propertyNames },
            { Kind = "Methods", Names = methodNames },
            { Kind = "Events", Names = eventNames },
        })
    )

    if #properties > 0 then
        heading(headings, 2, "Properties", "properties")
        table.insert(out, '<h2 id="properties">Properties<a class="anchor" href="#properties">#</a></h2>')
        for _, property in properties do
            local signatureHtml = `{markdown.escape(property.Name)}: {renderType(context, property.Type)}`
            table.insert(
                out,
                memberSection(
                    context,
                    headings,
                    class.Name,
                    property.Name,
                    signatureHtml,
                    propertyFacts(property),
                    property.Doc
                )
            )
        end
    end

    if #methods > 0 then
        heading(headings, 2, "Methods", "methods")
        table.insert(out, '<h2 id="methods">Methods<a class="anchor" href="#methods">#</a></h2>')
        for _, method in methods do
            local facts: { string } = {}
            if method.Yields then
                table.insert(facts, badge("yields", "yields"))
            end
            if method.ThreadSafety ~= nil then
                table.insert(facts, badge(method.ThreadSafety, "thread"))
            end
            local factsHtml = if #facts > 0 then `<div class="badges">{table.concat(facts, " ")}</div>` else ""
            local signatureHtml = `{markdown.escape(class.Name)}:{markdown.escape(method.Name)}{renderType(
                context,
                model.signature(method)
            )}`
            table.insert(
                out,
                memberSection(context, headings, class.Name, method.Name, signatureHtml, factsHtml, method.Doc)
            )
        end
    end

    if #events > 0 then
        heading(headings, 2, "Events", "events")
        table.insert(out, '<h2 id="events">Events<a class="anchor" href="#events">#</a></h2>')
        for _, event in events do
            local signatureHtml = `{markdown.escape(class.Name)}.{markdown.escape(event.Name)}{renderType(
                context,
                model.eventSignature(event)
            )}`
            local facts = '<div class="badges">'
                .. badge("deferred", "deferred")
                .. " "
                .. badge("Signal", "signal")
                .. "</div>"
            table.insert(out, memberSection(context, headings, class.Name, event.Name, signatureHtml, facts, event.Doc))
        end
    end

    -- Inherited members, per ancestor, innermost first: that is the order
    -- somebody looks in, because the nearest base is the likeliest owner.
    local inherited: { string } = {}
    for index = #info.Ancestry, 1, -1 do
        local ancestor = info.Ancestry[index]
        local names: { string } = {}
        local ancestorPath = model.classPath(ancestor.Name)
        for _, property in model.orEmpty(ancestor.Properties) do
            table.insert(
                names,
                `<a href="{href(context, `{ancestorPath}#{property.Name}`)}"><code>{markdown.escape(property.Name)}</code></a>`
            )
        end
        for _, method in model.orEmpty(ancestor.Methods) do
            table.insert(
                names,
                `<a href="{href(context, `{ancestorPath}#{method.Name}`)}"><code>{markdown.escape(method.Name)}</code></a>`
            )
        end
        for _, event in model.orEmpty(ancestor.Events) do
            table.insert(
                names,
                `<a href="{href(context, `{ancestorPath}#{event.Name}`)}"><code>{markdown.escape(event.Name)}</code></a>`
            )
        end
        if #names > 0 then
            table.sort(names)
            table.insert(
                inherited,
                `<details class="inherited"><summary>From <a href="{href(context, ancestorPath)}">{markdown.escape(
                    ancestor.Name
                )}</a> <span class="count">{#names}</span></summary><div class="chips">{table.concat(
                    names
                )}</div></details>`
            )
        end
    end

    if #inherited > 0 then
        heading(headings, 2, "Inherited members", "inherited-members")
        table.insert(
            out,
            '<h2 id="inherited-members">Inherited members<a class="anchor" href="#inherited-members">#</a></h2>'
        )
        table.insert(
            out,
            `<p class="hint">These are declared by a base class and are available on every <code>{markdown.escape(
                class.Name
            )}</code>.</p>`
        )
        table.insert(out, table.concat(inherited))
    end

    return table.concat(out, "\n"), headings
end

local function renderDatatypePage(context: Context, datatype: Schema.Datatype): (string, { shell.Heading })
    local headings: { shell.Heading } = {}
    local out: { string } = {}

    table.insert(out, `<h1 class="page-title">{markdown.escape(datatype.Name)}</h1>`)
    table.insert(out, `<div class="badges badges-title">{badge("Datatype", "datatype")}</div>`)
    table.insert(out, `<div class="lead">{renderDoc(context, datatype.Doc)}</div>`)
    table.insert(out, sampleFor(context, datatype.Name))

    local constructors = model.orEmpty(datatype.Constructors)
    local constants = model.orEmpty(datatype.Constants)
    local properties = model.orEmpty(datatype.Properties)
    local methods = model.orEmpty(datatype.Methods)
    local operators = model.orEmpty(datatype.Operators)

    local function names<T>(list: { T }, key: (T) -> string): { string }
        local result: { string } = {}
        for _, entry in list do
            table.insert(result, key(entry))
        end
        return result
    end

    table.insert(
        out,
        memberIndex({
            {
                Kind = "Constructors",
                Names = names(constructors, function(m: Schema.Method)
                    return m.Name
                end),
            },
            {
                Kind = "Constants",
                Names = names(constants, function(p: Schema.Property)
                    return p.Name
                end),
            },
            {
                Kind = "Properties",
                Names = names(properties, function(p: Schema.Property)
                    return p.Name
                end),
            },
            {
                Kind = "Methods",
                Names = names(methods, function(m: Schema.Method)
                    return m.Name
                end),
            },
        })
    )

    if #constructors > 0 then
        heading(headings, 2, "Constructors", "constructors")
        table.insert(out, '<h2 id="constructors">Constructors<a class="anchor" href="#constructors">#</a></h2>')
        table.insert(
            out,
            '<p class="hint">Reached through the namespace, and therefore camelCase — <code>CFrame.new</code>, <code>Color3.fromRGB</code>.</p>'
        )
        for _, constructor in constructors do
            local signatureHtml = `{markdown.escape(datatype.Name)}.{markdown.escape(constructor.Name)}{renderType(
                context,
                model.signature(constructor)
            )}`
            table.insert(
                out,
                memberSection(context, headings, datatype.Name, constructor.Name, signatureHtml, "", constructor.Doc)
            )
        end
    end

    if #constants > 0 then
        heading(headings, 2, "Constants", "constants")
        table.insert(out, '<h2 id="constants">Constants<a class="anchor" href="#constants">#</a></h2>')
        for _, constant in constants do
            local signatureHtml = `{markdown.escape(datatype.Name)}.{markdown.escape(constant.Name)}: {renderType(
                context,
                constant.Type
            )}`
            table.insert(
                out,
                memberSection(context, headings, datatype.Name, constant.Name, signatureHtml, "", constant.Doc)
            )
        end
    end

    if #properties > 0 then
        heading(headings, 2, "Properties", "properties")
        table.insert(out, '<h2 id="properties">Properties<a class="anchor" href="#properties">#</a></h2>')
        table.insert(
            out,
            '<p class="hint">Reached through a value, and therefore PascalCase — <code>v.Magnitude</code>.</p>'
        )
        for _, property in properties do
            local signatureHtml = `{markdown.escape(property.Name)}: {renderType(context, property.Type)}`
            table.insert(
                out,
                memberSection(
                    context,
                    headings,
                    datatype.Name,
                    property.Name,
                    signatureHtml,
                    propertyFacts(property),
                    property.Doc
                )
            )
        end
    end

    if #methods > 0 then
        heading(headings, 2, "Methods", "methods")
        table.insert(out, '<h2 id="methods">Methods<a class="anchor" href="#methods">#</a></h2>')
        for _, method in methods do
            local signatureHtml = `{markdown.escape(method.Name)}{renderType(context, model.signature(method))}`
            table.insert(
                out,
                memberSection(context, headings, datatype.Name, method.Name, signatureHtml, "", method.Doc)
            )
        end
    end

    if #operators > 0 then
        heading(headings, 2, "Operators", "operators")
        table.insert(out, '<h2 id="operators">Operators<a class="anchor" href="#operators">#</a></h2>')
        local rows: { string } = {
            '<div class="table-scroll"><table><thead><tr><th>Operator</th><th>Result</th><th>Description</th></tr></thead><tbody>',
        }
        for _, operator in operators do
            local parameters: { string } = {}
            for _, parameter in operator.Parameters do
                table.insert(parameters, renderType(context, parameter.Type))
            end
            local returns: { string } = {}
            for _, returned in operator.Returns do
                table.insert(returns, renderType(context, returned))
            end
            table.insert(
                rows,
                `<tr><td><code>{markdown.escape(operator.Name)}({table.concat(parameters, ", ")})</code></td><td><code>{table.concat(
                    returns,
                    ", "
                )}</code></td><td>{renderDoc(context, operator.Doc)}</td></tr>`
            )
        end
        table.insert(rows, "</tbody></table></div>")
        table.insert(out, table.concat(rows))
    end

    return table.concat(out, "\n"), headings
end

local function renderEnumPage(context: Context, enumeration: Schema.Enum): (string, { shell.Heading })
    local headings: { shell.Heading } = {}
    local out: { string } = {}

    table.insert(out, `<h1 class="page-title">Enum.{markdown.escape(enumeration.Name)}</h1>`)
    table.insert(
        out,
        `<div class="badges badges-title">{badge("Enum", "enum")}{" "}{badge(`{#enumeration.Items} items`, "count")}</div>`
    )
    table.insert(out, `<div class="lead">{renderDoc(context, enumeration.Doc)}</div>`)
    table.insert(
        out,
        '<div class="callout callout-note"><p>Every enum-typed property is <strong>total</strong>: there is no nil member of an enumeration, so a value meaning <em>unset</em> is declared as an item rather than left to a nil.</p></div>'
    )
    table.insert(out, sampleFor(context, `Enum.{enumeration.Name}`))

    heading(headings, 2, "Items", "items")
    table.insert(out, '<h2 id="items">Items<a class="anchor" href="#items">#</a></h2>')

    local rows: { string } = {
        '<div class="table-scroll"><table class="enum-table"><thead><tr><th>Item</th><th>Value</th><th>Description</th></tr></thead><tbody>',
    }
    for _, item in enumeration.Items do
        local doc = if item.Doc ~= nil then renderDoc(context, item.Doc) else ""
        table.insert(
            rows,
            `<tr id="{markdown.escape(item.Name)}"><td><code>Enum.{markdown.escape(enumeration.Name)}.{markdown.escape(
                item.Name
            )}</code></td><td>{item.Value}</td><td>{doc}</td></tr>`
        )
    end
    table.insert(rows, "</tbody></table></div>")
    table.insert(out, table.concat(rows))

    return table.concat(out, "\n"), headings
end

local function renderLibraryPage(context: Context, library: Schema.Library): (string, { shell.Heading })
    local headings: { shell.Heading } = {}
    local out: { string } = {}

    table.insert(out, `<h1 class="page-title">{markdown.escape(library.Name)}</h1>`)
    table.insert(out, `<div class="badges badges-title">{badge("Library", "library")}</div>`)
    table.insert(out, `<div class="lead">{renderDoc(context, library.Doc)}</div>`)
    table.insert(out, '<p class="hint">A library is a namespace and not an object, so its functions are camelCase.</p>')
    table.insert(out, sampleFor(context, library.Name))

    local functionNames: { string } = {}
    for _, method in library.Functions do
        table.insert(functionNames, method.Name)
    end
    table.insert(out, memberIndex({ { Kind = "Functions", Names = functionNames } }))

    heading(headings, 2, "Functions", "functions")
    table.insert(out, '<h2 id="functions">Functions<a class="anchor" href="#functions">#</a></h2>')
    for _, method in library.Functions do
        local facts = if method.Yields then `<div class="badges">{badge("yields", "yields")}</div>` else ""
        local signatureHtml = `{markdown.escape(library.Name)}.{markdown.escape(method.Name)}{renderType(
            context,
            model.signature(method)
        )}`
        table.insert(out, memberSection(context, headings, library.Name, method.Name, signatureHtml, facts, method.Doc))
    end

    return table.concat(out, "\n"), headings
end

local function renderGlobalsPage(context: Context, globals: { Schema.Method }): (string, { shell.Heading })
    local headings: { shell.Heading } = {}
    local out: { string } = {}

    table.insert(out, '<h1 class="page-title">Globals</h1>')
    table.insert(
        out,
        '<div class="lead"><p>Free functions the engine adds to the global table. Everything else a script can name without requiring it is either a world global (<code>game</code>, <code>workspace</code>, <code>script</code>), a datatype, <code>Enum</code>, or one of Luau\'s own builtins.</p></div>'
    )

    heading(headings, 2, "Functions", "functions")
    table.insert(out, '<h2 id="functions">Functions<a class="anchor" href="#functions">#</a></h2>')
    for _, method in globals do
        local signatureHtml = `{markdown.escape(method.Name)}{renderType(context, model.signature(method))}`
        table.insert(out, memberSection(context, headings, "", method.Name, signatureHtml, "", method.Doc))
    end

    return table.concat(out, "\n"), headings
end

--- The class tree, drawn as a nested list. This is the page that answers "what
--- IS there", which no per-class page can.
local function renderHierarchyPage(context: Context, icons: { [string]: boolean }): (string, { shell.Heading })
    local headings: { shell.Heading } = {}
    local out: { string } = {}

    table.insert(out, '<h1 class="page-title">Class hierarchy</h1>')
    table.insert(
        out,
        `<div class="lead"><p>Every class the engine declares, and what it inherits from. There are {#context.Model.Classes} of them and the tree is four levels deep — a class hierarchy is a thing to read in one screen, not a thing to explore.</p></div>`
    )

    local function renderNode(name: string): string
        local info = context.Model.ClassInfoByName[name]
        if info == nil then
            return ""
        end
        local badges: { string } = {}
        if info.Service then
            table.insert(badges, badge("Service", "service"))
        end
        if info.Abstract then
            table.insert(badges, badge("Abstract", "abstract"))
        end
        local children: { string } = {}
        for _, child in info.Children do
            table.insert(children, renderNode(child.Name))
        end
        local childrenHtml = if #children > 0 then `<ul>{table.concat(children)}</ul>` else ""
        return `<li>{classIcon(context, name, icons)}<a href="{href(context, model.classPath(name))}"><code>{markdown.escape(
            name
        )}</code></a> {table.concat(badges, " ")}<span class="tree-doc">{markdown.escape(
            markdown.summarize(prose.clean(info.Class.Doc))
        )}</span>{childrenHtml}</li>`
    end

    heading(headings, 2, "The tree", "the-tree")
    table.insert(out, '<h2 id="the-tree">The tree<a class="anchor" href="#the-tree">#</a></h2>')
    table.insert(out, `<ul class="tree">{renderNode("Instance")}</ul>`)

    return table.concat(out, "\n"), headings
end

return {
    ReferencePath = ReferencePath,
    HierarchyPath = HierarchyPath,
    renderType = renderType,
    renderDoc = renderDoc,
    markdownOptions = markdownOptions,
    badge = badge,
    renderClassPage = renderClassPage,
    renderDatatypePage = renderDatatypePage,
    renderEnumPage = renderEnumPage,
    renderLibraryPage = renderLibraryPage,
    renderGlobalsPage = renderGlobalsPage,
    renderHierarchyPage = renderHierarchyPage,
}
