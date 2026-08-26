--!strict
--[[
	The documentation site generator (api-design.md §5, generated artifact 5;
	§8's "Docs site outline").

	Emits a complete, self-contained static site: a landing page, the manual
	authored under `docs/manual/**`, and a reference page for every class,
	datatype, enum and library in the IDL -- with search, a class hierarchy,
	inherited members, linked type names, and the code samples under
	`docs/samples/`.

	**Why the output is NOT checked in, when every other generated artifact
	here is.** A checked-in artifact is freshness-gated by regenerating it and
	diffing, which is the right shape for `runtime/types/engine.d.luau`: that
	file is consumed by the analyzer whether or not anybody regenerated it, so
	a stale copy is a lie the toolchain believes. A site is consumed by a
	browser after somebody built it, so a stale copy cannot exist -- the build
	IS the read. What checking it in would buy is a diff nobody reads across
	tens of thousands of generated lines, and what it would cost is that churn
	on every edit to a doc string.

	So the gate for this generator is a different one, and a stronger one: it
	RUNS, and it fails on a reference the engine cannot back. Every `api:` link
	in the manual, every code sample's file name, and every page the navigation
	declares is checked against the IDL and the filesystem, and the run exits
	non-zero listing what it could not resolve. A Markdown link cannot do that;
	this is what buying our own link syntax paid for.

	    lute api/generator/gen_site.luau [--out=DIR]

	With no `--out`, the site is written to `$LUAUG_BUILD_ROOT/docs-site`, and
	to `out/docs-site` when that variable is unset -- both outside the source
	tree (R14), and the second is gitignored.
]]

local fs = require("@std/fs")
local process = require("@std/process")

local Definition = require("../defs")
local Schema = require("../schema")
local manual = require("./site/manual")
local markdown = require("./site/markdown")
local model = require("./site/model")
local navigation = require("./site/nav")
local prose = require("./site/prose")
local reference = require("./site/reference")
local shell = require("./site/shell")

local ThemeDirectory = "api/generator/site/theme"
local SampleDirectory = "docs/samples"
local IconDirectory = "icons/default/class"
--- The header wordmark and the tab icon, each as a preference list rather than
--- a path. Branding is a human decision and the files under `branding/` are
--- renamed when it is remade; a generator that named one of them would break
--- the documentation every time the logo did.
local BrandLogoCandidates: { string } = {
    "branding/luaug-lockup-horizontal.png",
    "branding/luaug-logo.svg",
    "branding/luaug-logo-512.png",
}
local BrandMarkCandidates: { string } = {
    "branding/luaug-mark.svg",
    "branding/luaug-mark-512.png",
    "branding/icon/luaug-64.png",
}
local VersionSourcePath = "CMakeLists.txt"
local VersionPattern = "project%(LuauG%s+VERSION%s+([%w%.%-]+)"

local Errors: { string } = {}
local Written = 0

--- Filled by `copyBrand`, read by the page shell.
local BrandFiles: { [string]: string } = {}

local function fail(message: string)
    table.insert(Errors, message)
end

local function outputDirectory(): string
    for _, argument in process.args do
        local explicit = string.match(argument, "^%-%-out=(.+)$")
        if explicit ~= nil then
            return explicit
        end
    end
    local root = process.env["LUAUG_BUILD_ROOT"]
    if root ~= nil and root ~= "" then
        -- Written with a forward slash on every platform: Windows accepts it,
        -- and a backslash here would reach `fs.createDirectory` as part of a
        -- name rather than as a separator on anything else.
        local normalized = string.gsub(root, "\\", "/")
        return `{normalized}/docs-site`
    end
    return "out/docs-site"
end

local Output = outputDirectory()

local function write(path: string, body: string)
    local directory = string.match(`{Output}/{path}`, "^(.*)/[^/]*$")
    if directory ~= nil and not fs.exists(directory) then
        fs.createDirectory(directory, { makeParents = true })
    end
    local text = body
    if string.sub(text, -1) ~= "\n" then
        text ..= "\n"
    end
    fs.writeStringToFile(`{Output}/{path}`, text)
    Written += 1
end

--- Samples are `.luau` files named after the symbol they demonstrate:
--- `Part.luau` for the class, `Workspace.Raycast.luau` for the member. The name
--- is checked against the IDL, so a sample for a member that was renamed is a
--- failed run rather than a section that quietly stops appearing.
local function loadSamples(resolve: (string) -> string?): { [string]: string }
    local samples: { [string]: string } = {}
    if not fs.exists(SampleDirectory) then
        return samples
    end

    for _, entry in fs.listDirectory(SampleDirectory) do
        if entry.type ~= "file" then
            continue
        end
        local name = string.match(entry.name, "^(.+)%.luau$")
        if name == nil then
            continue
        end
        if resolve(name) == nil then
            fail(`{SampleDirectory}/{entry.name} names "{name}", which the API definition does not declare`)
            continue
        end

        local body = fs.readFileToString(`{SampleDirectory}/{entry.name}`)
        -- The `--!strict` line is what makes the file analyzable and is noise
        -- on a documentation page; the reader is told once, in the manual, that
        -- every file in a project carries one.
        body = string.gsub(body, "^%-%-!strict%s*\n", "", 1)
        samples[name] = (string.gsub(body, "%s+$", ""))
    end

    return samples
end

local function classIcons(): { [string]: boolean }
    local available: { [string]: boolean } = {}
    if not fs.exists(IconDirectory) then
        return available
    end
    for _, entry in fs.listDirectory(IconDirectory) do
        local name = string.match(entry.name, "^(.+)%.png$")
        if name ~= nil then
            available[name] = true
        end
    end
    return available
end

local function engineVersion(): string
    if not fs.exists(VersionSourcePath) then
        fail(`{VersionSourcePath} is missing, so the site has no engine version to display`)
        return "unknown"
    end
    local version = string.match(fs.readFileToString(VersionSourcePath), VersionPattern)
    if version == nil then
        fail(`no engine version in {VersionSourcePath} -- expected a project(LuauG VERSION ...) line`)
        return "unknown"
    end
    return `v{version}`
end

local Model = model.build(Definition)
local Version = engineVersion()
local Icons = classIcons()
local Samples = loadSamples(Model.resolve)

--- Every page the site will contain, so that a link into the manual can be
--- checked before a single file is written.
local ManualPages = manual.loadAll(Errors)
local KnownPages: { [string]: boolean } = {
    ["index.html"] = true,
    [reference.ReferencePath] = true,
    [reference.HierarchyPath] = true,
    [model.GlobalsPath] = true,
}
for _, section in navigation.Sections do
    KnownPages[navigation.sectionPath(section.Slug)] = true
    for _, page in section.Pages do
        KnownPages[navigation.pagePath(section.Slug, page.Slug)] = true
    end
end
for _, info in Model.Classes do
    KnownPages[model.classPath(info.Class.Name)] = true
end
for _, datatype in Model.Datatypes do
    KnownPages[model.datatypePath(datatype.Name)] = true
end
for _, enumeration in Model.Enums do
    KnownPages[model.enumPath(enumeration.Name)] = true
end
for _, library in Model.Libraries do
    KnownPages[model.libraryPath(library.Name)] = true
end

--- Resolves what a manual page may write in a link target. Four forms and no
--- more: an `api:` symbol, a `manual:` page, one of the named shortcuts, and an
--- absolute URL. Anything else is a mistake worth stopping for.
local function resolveLink(target: string): string?
    if string.match(target, "^https?://") ~= nil or string.sub(target, 1, 1) == "#" then
        return nil
    end

    local symbol = string.match(target, "^api:(.+)$")
    if symbol ~= nil then
        local path = Model.resolve(symbol)
        if path == nil then
            fail(`a manual page links to api:{symbol}, which the API definition does not declare`)
            return nil
        end
        return path
    end

    local page = string.match(target, "^manual:(.+)$")
    if page ~= nil then
        local anchor = ""
        local base = page
        local hash = string.find(page, "#", 1, true)
        if hash ~= nil then
            anchor = string.sub(page, hash)
            base = string.sub(page, 1, hash - 1)
        end
        local section, slug = string.match(base, "^([%w%-]+)/([%w%-]+)$")
        local path: string?
        if section ~= nil and slug ~= nil then
            path = navigation.pagePath(section, slug)
        else
            path = navigation.sectionPath(base)
        end
        if path == nil or not KnownPages[path] then
            fail(`a manual page links to manual:{page}, which is not a page the navigation declares`)
            return nil
        end
        return path .. anchor
    end

    local shortcut = navigation.Shortcuts[target]
    if shortcut ~= nil then
        return shortcut
    end

    fail(`a manual page links to "{target}", which is not an api:, a manual:, a shortcut or an absolute URL`)
    return nil
end

type Crumb = { Title: string, Path: string? }

--- The expected type has to be stated somewhere, and a table literal mixing an
--- entry with a `Path` and an entry without one infers from its first element.
local function crumbs(list: { Crumb }): { Crumb }
    return list
end

local function contextFor(path: string): reference.Context
    return {
        Model = Model,
        Prefix = shell.prefixFor(path),
        Samples = Samples,
        resolveLink = resolveLink,
    }
end

--- The sidebar. The manual comes first because somebody arriving at a
--- documentation site is far likelier to be learning than to be looking up an
--- argument list -- and the ones looking up an argument list use the search
--- box, which is why it has a keyboard shortcut.
local function buildNav(): { shell.NavItem }
    local items: { shell.NavItem } = {}

    table.insert(items, { Title = "Home", Path = "index.html" })

    for _, section in navigation.Sections do
        local children: { shell.NavItem } = {}
        for _, page in section.Pages do
            table.insert(children, { Title = page.Title, Path = navigation.pagePath(section.Slug, page.Slug) })
        end
        table.insert(items, {
            Title = section.Title,
            Path = navigation.sectionPath(section.Slug),
            Children = children,
        })
    end

    table.insert(items, { Title = "Reference" })
    table.insert(items, { Title = "Overview", Path = reference.ReferencePath })
    table.insert(items, { Title = "Class hierarchy", Path = reference.HierarchyPath })

    local services: { shell.NavItem } = {}
    local instances: { shell.NavItem } = {}
    for _, info in Model.Classes do
        local entry: shell.NavItem = {
            Title = info.Class.Name,
            Path = model.classPath(info.Class.Name),
            Icon = if Icons[info.Class.Name] then `assets/class/{info.Class.Name}.png` else nil,
        }
        if info.Service then
            table.insert(services, entry)
        else
            table.insert(instances, entry)
        end
    end
    table.insert(items, { Title = "Services", Children = services })
    table.insert(items, { Title = "Classes", Children = instances })

    local datatypes: { shell.NavItem } = {}
    for _, datatype in Model.Datatypes do
        table.insert(datatypes, { Title = datatype.Name, Path = model.datatypePath(datatype.Name) })
    end
    table.insert(items, { Title = "Datatypes", Children = datatypes })

    local enums: { shell.NavItem } = {}
    for _, enumeration in Model.Enums do
        table.insert(enums, { Title = enumeration.Name, Path = model.enumPath(enumeration.Name) })
    end
    table.insert(items, { Title = "Enums", Children = enums })

    local libraries: { shell.NavItem } = {}
    for _, library in Model.Libraries do
        table.insert(libraries, { Title = library.Name, Path = model.libraryPath(library.Name) })
    end
    table.insert(libraries, { Title = "Globals", Path = model.GlobalsPath })
    table.insert(items, { Title = "Libraries", Children = libraries })

    return items
end

local Nav = buildNav()

local function emit(spec: shell.PageSpec)
    local brand: shell.Brand = {
        Logo = BrandFiles["logo"] or "assets/logo.png",
        Mark = BrandFiles["mark"] or "assets/mark.png",
    }
    write(spec.Path, shell.renderPage(spec, Nav, Version, brand))
end

--- The reference landing page: what the four kinds of thing are, and the whole
--- class list in one table for somebody who would rather scan than search.
local function referenceIndex(): (string, { shell.Heading })
    local context = contextFor(reference.ReferencePath)
    local headings: { shell.Heading } = {}
    local out: { string } = {}

    table.insert(out, '<h1 class="page-title">API reference</h1>')
    table.insert(
        out,
        '<div class="lead"><p>Generated from the typed definition the engine itself is built from — the same source its C++ registration, its type definitions and its versioned API dump come from. If a member is here, the engine has it.</p></div>'
    )

    table.insert(
        out,
        `<div class="cards"><a class="card" href="{context.Prefix}{reference.HierarchyPath}"><h3>Class hierarchy</h3><p>Every class and what it inherits from, in one tree.</p></a>`
            .. `<a class="card" href="{context.Prefix}{model.GlobalsPath}"><h3>Globals</h3><p>What a script can name without requiring anything.</p></a></div>`
    )

    local function classTable(title: string, id: string, wanted: boolean): string
        headings[#headings + 1] = { Level = 2, Id = id, Text = title }
        local rows: { string } = {
            `<h2 id="{id}">{title}<a class="anchor" href="#{id}">#</a></h2>`,
            '<div class="table-scroll"><table><thead><tr><th>Class</th><th>Inherits</th><th>Summary</th></tr></thead><tbody>',
        }
        for _, info in Model.Classes do
            if info.Service ~= wanted then
                continue
            end
            local extends = info.Class.Extends
            local inherits = if extends ~= nil
                then `<a href="{context.Prefix}{model.classPath(extends)}"><code>{markdown.escape(extends)}</code></a>`
                else "—"
            local icon = if Icons[info.Class.Name]
                then `<img class="nav-icon" src="{context.Prefix}assets/class/{info.Class.Name}.png" alt=""> `
                else ""
            table.insert(
                rows,
                `<tr><td>{icon}<a href="{context.Prefix}{model.classPath(info.Class.Name)}"><code>{markdown.escape(
                    info.Class.Name
                )}</code></a></td><td>{inherits}</td><td>{markdown.escape(
                    markdown.summarize(prose.clean(info.Class.Doc))
                )}</td></tr>`
            )
        end
        table.insert(rows, "</tbody></table></div>")
        return table.concat(rows)
    end

    table.insert(out, classTable("Services", "services", true))
    table.insert(out, classTable("Classes", "classes", false))

    headings[#headings + 1] = { Level = 2, Id = "datatypes", Text = "Datatypes" }
    local datatypeRows: { string } = {
        '<h2 id="datatypes">Datatypes<a class="anchor" href="#datatypes">#</a></h2>',
        '<div class="table-scroll"><table><thead><tr><th>Datatype</th><th>Summary</th></tr></thead><tbody>',
    }
    for _, datatype in Model.Datatypes do
        table.insert(
            datatypeRows,
            `<tr><td><a href="{context.Prefix}{model.datatypePath(datatype.Name)}"><code>{markdown.escape(datatype.Name)}</code></a></td><td>{markdown.escape(
                markdown.summarize(prose.clean(datatype.Doc))
            )}</td></tr>`
        )
    end
    table.insert(datatypeRows, "</tbody></table></div>")
    table.insert(out, table.concat(datatypeRows))

    headings[#headings + 1] = { Level = 2, Id = "enums", Text = "Enums" }
    local enumRows: { string } = {
        '<h2 id="enums">Enums<a class="anchor" href="#enums">#</a></h2>',
        '<div class="table-scroll"><table><thead><tr><th>Enum</th><th>Items</th><th>Summary</th></tr></thead><tbody>',
    }
    for _, enumeration in Model.Enums do
        table.insert(
            enumRows,
            `<tr><td><a href="{context.Prefix}{model.enumPath(enumeration.Name)}"><code>Enum.{markdown.escape(
                enumeration.Name
            )}</code></a></td><td>{#enumeration.Items}</td><td>{markdown.escape(
                markdown.summarize(prose.clean(enumeration.Doc))
            )}</td></tr>`
        )
    end
    table.insert(enumRows, "</tbody></table></div>")
    table.insert(out, table.concat(enumRows))

    return table.concat(out, "\n"), headings
end

local function searchIndex(): string
    local entries: { string } = {}
    local function add(name: string, kind: string, path: string, owner: string?)
        local ownerField = if owner ~= nil then ',"o":"' .. owner .. '"' else ""
        table.insert(entries, '{"n":"' .. name .. '","k":"' .. kind .. '","p":"' .. path .. '"' .. ownerField .. "}")
    end

    for _, symbol in Model.Symbols do
        -- `Enum.PartShape` and `PartShape` are both declared so that prose
        -- resolves either spelling; the index carries the qualified one only,
        -- or every enum appears twice in a search for its name.
        if symbol.Kind == "enum" and string.sub(symbol.Name, 1, 5) ~= "Enum." then
            continue
        end
        add(symbol.Name, symbol.Kind, symbol.Path, symbol.Owner)
    end

    add("Home", "page", "index.html", nil)
    add("API reference", "page", reference.ReferencePath, nil)
    add("Class hierarchy", "page", reference.HierarchyPath, nil)
    for _, section in navigation.Sections do
        add(section.Title, "page", navigation.sectionPath(section.Slug), nil)
        for _, page in section.Pages do
            add(page.Title, "page", navigation.pagePath(section.Slug, page.Slug), section.Title)
        end
    end

    return `window.LuaugSearchIndex = [\n{table.concat(entries, ",\n")}\n];`
end

local function copyFile(source: string, destination: string)
    if not fs.exists(source) then
        fail(`{source} is missing and the site needs it`)
        return
    end
    local directory = string.match(destination, "^(.*)/[^/]*$")
    if directory ~= nil and not fs.exists(directory) then
        fs.createDirectory(directory, { makeParents = true })
    end
    fs.copy(source, destination)
    Written += 1
end

--- Every doc string in the definition files, checked against what a published
--- page may say. A sentence that names an internal milestone, a decision record
--- or a design-document section is one the mechanical filter cannot repair, and
--- publishing it would put the engine's own build history on a page written for
--- somebody who just wants to move a part.
local function auditProse()
    local function inspect(where: string, text: string)
        for _, problem in prose.check(prose.clean(text)) do
            fail(`{where} carries {problem}`)
        end
    end

    for _, info in Model.Classes do
        local class = info.Class
        inspect(class.Name, class.Doc)
        for _, property in model.orEmpty(class.Properties) do
            inspect(`{class.Name}.{property.Name}`, property.Doc)
        end
        for _, method in model.orEmpty(class.Methods) do
            inspect(`{class.Name}.{method.Name}`, method.Doc)
        end
        for _, event in model.orEmpty(class.Events) do
            inspect(`{class.Name}.{event.Name}`, event.Doc)
        end
    end

    for _, datatype in Model.Datatypes do
        inspect(datatype.Name, datatype.Doc)
        local groups: { { Schema.Method } } = { model.orEmpty(datatype.Constructors), model.orEmpty(datatype.Methods) }
        for _, group in groups do
            for _, method in group do
                inspect(`{datatype.Name}.{method.Name}`, method.Doc)
            end
        end
        local values: { { Schema.Property } } =
            { model.orEmpty(datatype.Constants), model.orEmpty(datatype.Properties) }
        for _, group in values do
            for _, property in group do
                inspect(`{datatype.Name}.{property.Name}`, property.Doc)
            end
        end
        for _, operator in model.orEmpty(datatype.Operators) do
            inspect(`{datatype.Name}.{operator.Name}`, operator.Doc)
        end
    end

    for _, enumeration in Model.Enums do
        inspect(`Enum.{enumeration.Name}`, enumeration.Doc)
        for _, item in enumeration.Items do
            if item.Doc ~= nil then
                inspect(`Enum.{enumeration.Name}.{item.Name}`, item.Doc)
            end
        end
    end

    for _, library in Model.Libraries do
        inspect(library.Name, library.Doc)
        for _, method in library.Functions do
            inspect(`{library.Name}.{method.Name}`, method.Doc)
        end
    end

    for _, method in Model.Globals do
        inspect(method.Name, method.Doc)
    end
end

--- Copies the first candidate that exists, keeping its own extension so the
--- page can point at it. Returns nothing: the href is recorded in `BrandFiles`
--- for the shell to read.
local function copyBrand(candidates: { string }, name: string, what: string)
    for _, source in candidates do
        if fs.exists(source) then
            local extension = string.match(source, "%.([%w]+)$") or "png"
            local destination = `assets/{name}.{extension}`
            copyFile(source, `{Output}/{destination}`)
            BrandFiles[name] = destination
            return
        end
    end
    fail(`no file under branding/ can serve as {what}`)
end

local function main(): number
    auditProse()

    -- Removed and rewritten rather than overwritten in place, for the reason
    -- `gen_reference.luau` gives: a class deleted from the IDL has to take its
    -- page with it, and a stale page is exactly the documentation that outlives
    -- the thing it describes.
    if fs.exists(Output) then
        fs.removeDirectory(Output, { recursive = true })
    end
    fs.createDirectory(Output, { makeParents = true })

    -- Before any page, because every page names them.
    copyBrand(BrandLogoCandidates, "logo", "the header wordmark")
    copyBrand(BrandMarkCandidates, "mark", "the tab icon")

    for _, info in Model.Classes do
        local path = model.classPath(info.Class.Name)
        local context = contextFor(path)
        local body, headings = reference.renderClassPage(context, info, Icons)
        emit({
            Path = path,
            Title = info.Class.Name,
            Summary = prose.clean(info.Class.Doc),
            Body = body,
            Headings = headings,
            Breadcrumbs = crumbs({
                { Title = "Reference", Path = reference.ReferencePath },
                { Title = if info.Service then "Services" else "Classes" },
                { Title = info.Class.Name },
            }),
        })
    end

    for _, datatype in Model.Datatypes do
        local path = model.datatypePath(datatype.Name)
        local context = contextFor(path)
        local body, headings = reference.renderDatatypePage(context, datatype)
        emit({
            Path = path,
            Title = datatype.Name,
            Summary = prose.clean(datatype.Doc),
            Body = body,
            Headings = headings,
            Breadcrumbs = crumbs({
                { Title = "Reference", Path = reference.ReferencePath },
                { Title = "Datatypes" },
                { Title = datatype.Name },
            }),
        })
    end

    for _, enumeration in Model.Enums do
        local path = model.enumPath(enumeration.Name)
        local context = contextFor(path)
        local body, headings = reference.renderEnumPage(context, enumeration)
        emit({
            Path = path,
            Title = `Enum.{enumeration.Name}`,
            Summary = prose.clean(enumeration.Doc),
            Body = body,
            Headings = headings,
            Breadcrumbs = crumbs({
                { Title = "Reference", Path = reference.ReferencePath },
                { Title = "Enums" },
                { Title = enumeration.Name },
            }),
        })
    end

    for _, library in Model.Libraries do
        local path = model.libraryPath(library.Name)
        local context = contextFor(path)
        local body, headings = reference.renderLibraryPage(context, library)
        emit({
            Path = path,
            Title = library.Name,
            Summary = prose.clean(library.Doc),
            Body = body,
            Headings = headings,
            Breadcrumbs = crumbs({
                { Title = "Reference", Path = reference.ReferencePath },
                { Title = "Libraries" },
                { Title = library.Name },
            }),
        })
    end

    do
        local context = contextFor(model.GlobalsPath)
        local body, headings = reference.renderGlobalsPage(context, Model.Globals)
        emit({
            Path = model.GlobalsPath,
            Title = "Globals",
            Body = body,
            Headings = headings,
            Breadcrumbs = crumbs({ { Title = "Reference", Path = reference.ReferencePath }, { Title = "Globals" } }),
        })
    end

    do
        local context = contextFor(reference.HierarchyPath)
        local body, headings = reference.renderHierarchyPage(context, Icons)
        emit({
            Path = reference.HierarchyPath,
            Title = "Class hierarchy",
            Body = body,
            Headings = headings,
            Breadcrumbs = crumbs({
                { Title = "Reference", Path = reference.ReferencePath },
                { Title = "Class hierarchy" },
            }),
        })
    end

    do
        local body, headings = referenceIndex()
        emit({
            Path = reference.ReferencePath,
            Title = "API reference",
            Body = body,
            Headings = headings,
            Breadcrumbs = crumbs({ { Title = "Reference" } }),
        })
    end

    for _, section in navigation.Sections do
        local path = navigation.sectionPath(section.Slug)
        local context = contextFor(path)
        local body, headings = manual.renderSectionIndex(section, context)
        emit({
            Path = path,
            Title = section.Title,
            Summary = section.Summary,
            Body = body,
            Headings = headings,
            Breadcrumbs = crumbs({ { Title = section.Title } }),
        })
    end

    for _, entry in ManualPages do
        local context = contextFor(entry.Path)
        local body, headings = manual.renderPage(entry, context)
        emit({
            Path = entry.Path,
            Title = entry.Page.Title,
            Summary = entry.Section.Summary,
            Body = body,
            Headings = headings,
            Breadcrumbs = crumbs({
                { Title = entry.Section.Title, Path = navigation.sectionPath(entry.Section.Slug) },
                { Title = entry.Page.Title },
            }),
        })
    end

    do
        local context = contextFor("index.html")
        emit({
            Path = "index.html",
            Title = "Documentation",
            Body = manual.renderLanding(context, Version),
            Headings = {},
            Wide = true,
        })
    end

    write("assets/search-index.js", searchIndex())
    copyFile(`{ThemeDirectory}/site.css`, `{Output}/assets/site.css`)
    copyFile(`{ThemeDirectory}/site.js`, `{Output}/assets/site.js`)

    for name in Icons do
        copyFile(`{IconDirectory}/{name}.png`, `{Output}/assets/class/{name}.png`)
    end

    if #Errors > 0 then
        print(`gen_site: {#Errors} unresolved reference(s).`)
        for _, message in Errors do
            print(`  {message}`)
        end
        return 1
    end

    print(`gen_site: wrote {Written} file(s) to {Output}/`)
    return 0
end

process.exit(main())
