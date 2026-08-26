--!strict
--[[
	The documentation model: the IDL turned into the things a site needs that a
	definition file does not carry.

	Three of them, and each exists because a reference that lacks it is a
	reference somebody has to read twice:

	- **Ancestry and inherited members.** `docs/api/*.md` lists what a class
	  DECLARES and links to its base, which is right for a Markdown catalogue --
	  flattening turns one added member on `Instance` into a diff on every page.
	  A site has no such constraint and the opposite obligation: somebody looking
	  at `Part` wants to know it has `Anchored`, and being told to go and read
	  three more pages to find out is the failure mode of every generated
	  reference that has ever been generated. So the inherited set is computed
	  here and rendered collapsed, per base class, on the page that inherits it.

	- **A symbol table.** Every public name -- class, member, datatype,
	  constructor, enum, enum item, library function, global -- mapped to the
	  page and anchor it lives at. This is what makes a backticked identifier in
	  prose become a link without anybody authoring one, in the manual AND in the
	  doc strings the IDL already carries.

	- **Type rendering.** A `TypeName` is authored as the Luau type the user
	  sees: `"Instance?"`, `"{BasePart}"`, `"Enum.PartShape"`, `"Signal<Instance>"`.
	  Rendered as text it is dead; rendered here, every name inside it that the
	  engine declares becomes a link, punctuation and all.

	The model holds ROOT-RELATIVE paths (`reference/classes/Part.html`). Pages
	sit at varying depths, so each one prepends its own prefix -- which is why
	nothing here emits an `href` and everything returns a path.
]]

local Schema = require("../../schema")

export type SymbolKind = "class" | "datatype" | "enum" | "library" | "global" | "member" | "page"

export type Symbol = {
    Name: string,
    Kind: SymbolKind,
    --- Root-relative, anchor included: `reference/classes/Part.html#Shape`.
    Path: string,
    --- The owning page's title, for a search result that has to say where a
    --- member lives.
    Owner: string?,
    Summary: string?,
}

export type ClassInfo = {
    Class: Schema.Class,
    --- Root-first: `{ Instance, PVInstance, BasePart }` for `Part`.
    Ancestry: { Schema.Class },
    --- Direct subclasses, sorted.
    Children: { Schema.Class },
    Creatable: boolean,
    Service: boolean,
    Abstract: boolean,
}

local function orEmpty<T>(list: { T }?): { T }
    return list or {}
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

local function classPath(name: string): string
    return `reference/classes/{name}.html`
end

local function datatypePath(name: string): string
    return `reference/datatypes/{name}.html`
end

local function enumPath(name: string): string
    return `reference/enums/{name}.html`
end

local function libraryPath(name: string): string
    return `reference/libraries/{name}.html`
end

local GlobalsPath = "reference/globals.html"

--- The signature a method or constructor is displayed with.
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

export type Model = {
    Definition: Schema.Definition,
    Classes: { ClassInfo },
    ClassInfoByName: { [string]: ClassInfo },
    Datatypes: { Schema.Datatype },
    Enums: { Schema.Enum },
    Libraries: { Schema.Library },
    Globals: { Schema.Method },
    --- Every public name, for the search index.
    Symbols: { Symbol },
    --- Resolves an authored reference -- `Part`, `BasePart.Anchored`,
    --- `Instance:Destroy`, `Enum.PartShape.Ball`, `task.wait` -- to a
    --- root-relative path, or nil when the name is not one the engine declares.
    resolve: (string) -> string?,
    --- Whether a class name is one the engine declares, so a page can decide
    --- between a link and plain text without going through `resolve`.
    isClass: (string) -> boolean,
}

--- Splits a member reference into its owner and its member. `:` and `.` are
--- both accepted and mean the same thing here: the site links a name to where
--- it is documented, and `Instance:Destroy` and `Instance.Destroy` are the same
--- entry in the catalogue even though only one of them is callable Luau.
local function splitReference(text: string): (string?, string?)
    local owner, member = string.match(text, "^([%w_%.]+)[%.:]([%w_]+)$")
    if owner == nil or member == nil then
        return nil, nil
    end
    return owner, member
end

local function build(definition: Schema.Definition): Model
    local classes: { Schema.Class } = orEmpty(definition.Classes)
    local datatypes: { Schema.Datatype } = sortedCopy(orEmpty(definition.Datatypes), function(d: Schema.Datatype)
        return d.Name
    end)
    local enums: { Schema.Enum } = sortedCopy(orEmpty(definition.Enums), function(e: Schema.Enum)
        return e.Name
    end)
    local libraries: { Schema.Library } = sortedCopy(orEmpty(definition.Libraries), function(l: Schema.Library)
        return l.Name
    end)
    local globals: { Schema.Method } = sortedCopy(orEmpty(definition.Globals), function(g: Schema.Method)
        return g.Name
    end)

    local classByName: { [string]: Schema.Class } = {}
    for _, class in classes do
        classByName[class.Name] = class
    end

    local childrenOf: { [string]: { Schema.Class } } = {}
    for _, class in classes do
        local parent = class.Extends
        if parent ~= nil then
            local bucket = childrenOf[parent]
            if bucket == nil then
                bucket = {}
                childrenOf[parent] = bucket
            end
            table.insert(bucket, class)
        end
    end

    local infoByName: { [string]: ClassInfo } = {}
    local infos: { ClassInfo } = {}
    local sortedClasses: { Schema.Class } = sortedCopy(classes, function(c: Schema.Class)
        return c.Name
    end)
    for _, class in sortedClasses do
        local ancestry: { Schema.Class } = {}
        local cursor = class.Extends
        -- Bounded by the class count rather than by `while cursor ~= nil`: the
        -- schema does not forbid a cycle and a generator that hangs on one is
        -- harder to diagnose than one that stops.
        for _ = 1, #classes do
            if cursor == nil then
                break
            end
            local ancestor = classByName[cursor]
            if ancestor == nil then
                break
            end
            table.insert(ancestry, 1, ancestor)
            cursor = ancestor.Extends
        end

        local directChildren: { Schema.Class } = childrenOf[class.Name] or {}
        local info: ClassInfo = {
            Class = class,
            Ancestry = ancestry,
            Children = sortedCopy(directChildren, function(c: Schema.Class)
                return c.Name
            end),
            Creatable = not hasTag(class, "NotCreatable") and not hasTag(class, "Abstract"),
            Service = hasTag(class, "Service"),
            Abstract = hasTag(class, "Abstract"),
        }
        infoByName[class.Name] = info
        table.insert(infos, info)
    end

    -- The symbol table. `paths` is what `resolve` reads; `symbols` is the
    -- ordered list the search index is built from, and the two are filled
    -- together so a name can never be searchable and unlinkable.
    local paths: { [string]: string } = {}
    local symbols: { Symbol } = {}

    local function declare(name: string, kind: SymbolKind, path: string, owner: string?, summary: string?)
        if paths[name] == nil then
            paths[name] = path
        end
        table.insert(symbols, { Name = name, Kind = kind, Path = path, Owner = owner, Summary = summary })
    end

    for _, info in infos do
        local class = info.Class
        local page = classPath(class.Name)
        declare(class.Name, "class", page, nil, class.Doc)
        for _, property in orEmpty(class.Properties) do
            declare(`{class.Name}.{property.Name}`, "member", `{page}#{property.Name}`, class.Name, property.Doc)
        end
        for _, method in orEmpty(class.Methods) do
            declare(`{class.Name}.{method.Name}`, "member", `{page}#{method.Name}`, class.Name, method.Doc)
        end
        for _, event in orEmpty(class.Events) do
            declare(`{class.Name}.{event.Name}`, "member", `{page}#{event.Name}`, class.Name, event.Doc)
        end
    end

    for _, datatype in datatypes do
        local page = datatypePath(datatype.Name)
        declare(datatype.Name, "datatype", page, nil, datatype.Doc)
        for _, constructor in orEmpty(datatype.Constructors) do
            declare(
                `{datatype.Name}.{constructor.Name}`,
                "member",
                `{page}#{constructor.Name}`,
                datatype.Name,
                constructor.Doc
            )
        end
        for _, constant in orEmpty(datatype.Constants) do
            declare(`{datatype.Name}.{constant.Name}`, "member", `{page}#{constant.Name}`, datatype.Name, constant.Doc)
        end
        for _, property in orEmpty(datatype.Properties) do
            declare(`{datatype.Name}.{property.Name}`, "member", `{page}#{property.Name}`, datatype.Name, property.Doc)
        end
        for _, method in orEmpty(datatype.Methods) do
            declare(`{datatype.Name}.{method.Name}`, "member", `{page}#{method.Name}`, datatype.Name, method.Doc)
        end
    end

    for _, enumeration in enums do
        local page = enumPath(enumeration.Name)
        -- Declared under both spellings: prose says `Enum.PartShape`, while a
        -- TypeName in the IDL says the same thing and a heading says
        -- `PartShape`. All three have to reach the page.
        declare(`Enum.{enumeration.Name}`, "enum", page, nil, enumeration.Doc)
        declare(enumeration.Name, "enum", page, nil, enumeration.Doc)
        for _, item in enumeration.Items do
            declare(`Enum.{enumeration.Name}.{item.Name}`, "member", `{page}#{item.Name}`, enumeration.Name, item.Doc)
        end
    end

    for _, library in libraries do
        local page = libraryPath(library.Name)
        declare(library.Name, "library", page, nil, library.Doc)
        for _, method in library.Functions do
            declare(`{library.Name}.{method.Name}`, "member", `{page}#{method.Name}`, library.Name, method.Doc)
        end
    end

    for _, method in globals do
        declare(method.Name, "global", `{GlobalsPath}#{method.Name}`, nil, method.Doc)
    end

    local function resolve(text: string): string?
        local direct = paths[text]
        if direct ~= nil then
            return direct
        end

        -- `Instance:Destroy` and `part:Destroy()` both appear in prose. Strip a
        -- trailing call and normalise the separator before giving up.
        local stripped = string.gsub(text, "%(%s*%)$", "")
        local owner, member = splitReference(stripped)
        if owner ~= nil and member ~= nil then
            local qualified = paths[`{owner}.{member}`]
            if qualified ~= nil then
                return qualified
            end
        end

        return paths[stripped]
    end

    return {
        Definition = definition,
        Classes = infos,
        ClassInfoByName = infoByName,
        Datatypes = datatypes,
        Enums = enums,
        Libraries = libraries,
        Globals = globals,
        Symbols = symbols,
        resolve = resolve,
        isClass = function(name: string): boolean
            return classByName[name] ~= nil
        end,
    }
end

return {
    build = build,
    classPath = classPath,
    datatypePath = datatypePath,
    enumPath = enumPath,
    libraryPath = libraryPath,
    GlobalsPath = GlobalsPath,
    signature = signature,
    eventSignature = eventSignature,
    hasTag = hasTag,
    orEmpty = orEmpty,
    sortedCopy = sortedCopy,
}
