--!strict
--[[
	The page chrome every page in the site is poured into: the header, the
	navigation tree, the article, the on-this-page rail, and the footer.

	**Everything is relative and nothing is absolute.** A page at
	`reference/classes/Part.html` reaches the stylesheet at `../../assets/site.css`,
	and a page at `index.html` reaches it at `assets/site.css`. The site
	therefore opens from a file:// URL, from a subdirectory of a web server, and
	from GitHub Pages, with no configuration and no base tag -- which matters
	because the first way anybody will look at this is by double-clicking the
	index.

	The navigation tree is rendered in full on every page rather than fetched,
	for the same reason: there is no server, and a fetch from file:// is blocked
	by every browser's origin rules. It costs a few kilobytes a page and it is
	what makes the site work when it is a folder on somebody's disk.
]]

local markdown = require("./markdown")

export type NavItem = {
    Title: string,
    --- Root-relative page path, or nil for a heading that groups without
    --- being a page of its own.
    Path: string?,
    --- Root-relative image path shown before the title.
    Icon: string?,
    Children: { NavItem }?,
    --- Groups start collapsed unless the active page is inside them; a group
    --- marked open starts expanded regardless.
    Open: boolean?,
}

export type Heading = { Level: number, Id: string, Text: string }

export type PageSpec = {
    --- Root-relative, e.g. `reference/classes/Part.html`.
    Path: string,
    Title: string,
    --- The `<title>` suffix and the search index's description.
    Summary: string?,
    --- Rendered HTML for the article body.
    Body: string,
    --- Populates the on-this-page rail; empty means the rail is omitted.
    Headings: { Heading },
    --- Breadcrumb above the title, innermost last.
    Breadcrumbs: { { Title: string, Path: string? } }?,
    --- Suppresses the sidebar and the rail, for the landing page.
    Wide: boolean?,
}

local SiteName = "LuauG"

--- How many `../` a page needs to reach the site root.
local function prefixFor(path: string): string
    local depth = 0
    for _ in string.gmatch(path, "/") do
        depth += 1
    end
    return string.rep("../", depth)
end

--- Whether `candidate` is the page being rendered, or an ancestor group of it.
--- Drives both the highlight and which groups start open, which is the whole of
--- what a static sidebar can do about "where am I".
local function containsPath(item: NavItem, active: string): boolean
    if item.Path == active then
        return true
    end
    local children: { NavItem } = item.Children or {}
    for _, child in children do
        if containsPath(child, active) then
            return true
        end
    end
    return false
end

local function renderNavItems(items: { NavItem }, active: string, prefix: string, depth: number): string
    local out: { string } = { `<ul class="nav-list nav-depth-{depth}">` }
    for _, item in items do
        local children = item.Children
        local isActive = item.Path ~= nil and item.Path == active
        local inside = containsPath(item, active)

        local label: { string } = {}
        if item.Icon ~= nil then
            table.insert(label, `<img class="nav-icon" src="{prefix}{markdown.escape(item.Icon)}" alt="">`)
        end
        table.insert(label, `<span>{markdown.escape(item.Title)}</span>`)
        local labelHtml = table.concat(label)

        if children ~= nil and #children > 0 then
            local open = if inside or item.Open == true then " open" else ""
            table.insert(out, `<li class="nav-group">`)
            table.insert(out, `<details{open}><summary>{labelHtml}</summary>`)
            if item.Path ~= nil then
                local current = if isActive then ' aria-current="page"' else ""
                local overview = `<ul class="nav-list nav-depth-{depth + 1}"><li><a class="nav-link{if isActive
                    then " active"
                    else ""}" href="{prefix}{markdown.escape(item.Path)}"{current}>Overview</a></li></ul>`
                table.insert(out, overview)
            end
            table.insert(out, renderNavItems(children, active, prefix, depth + 1))
            table.insert(out, "</details></li>")
        elseif item.Path ~= nil then
            local current = if isActive then ' aria-current="page"' else ""
            local class = if isActive then "nav-link active" else "nav-link"
            table.insert(
                out,
                `<li><a class="{class}" href="{prefix}{markdown.escape(item.Path)}"{current}>{labelHtml}</a></li>`
            )
        else
            table.insert(out, `<li class="nav-heading">{labelHtml}</li>`)
        end
    end
    table.insert(out, "</ul>")
    return table.concat(out)
end

local function renderToc(headings: { Heading }): string
    local usable: { Heading } = {}
    for _, heading in headings do
        -- h1 is the page title and h4 and below are detail; a rail that lists
        -- every one of them is a second copy of the page.
        if heading.Level >= 2 and heading.Level <= 3 then
            table.insert(usable, heading)
        end
    end
    if #usable < 2 then
        return ""
    end

    local out: { string } = { '<nav class="toc" aria-label="On this page"><p class="toc-title">On this page</p><ul>' }
    for _, heading in usable do
        table.insert(
            out,
            `<li class="toc-l{heading.Level}"><a href="#{markdown.escape(heading.Id)}">{markdown.escape(heading.Text)}</a></li>`
        )
    end
    table.insert(out, "</ul></nav>")
    return table.concat(out)
end

--- The banner every emitted file carries, in the form the other generators use.
--- A file that says where it came from is a file nobody edits twice.
local Banner =
    "<!-- GENERATED by api/generator/gen_site.luau from api/defs/*.api.luau and docs/manual/**. Do not edit. -->"

export type Brand = { Logo: string, Mark: string }

local function renderPage(spec: PageSpec, nav: { NavItem }, version: string, brand: Brand): string
    local prefix = prefixFor(spec.Path)
    local title = if spec.Path == "index.html" then `{SiteName} — Documentation` else `{spec.Title} · {SiteName}`
    local description = spec.Summary or `The {SiteName} engine documentation.`

    local breadcrumbs: { string } = {}
    local crumbs: { { Title: string, Path: string? } } = spec.Breadcrumbs or {}
    for _, crumb in crumbs do
        if crumb.Path ~= nil then
            table.insert(
                breadcrumbs,
                `<a href="{prefix}{markdown.escape(crumb.Path)}">{markdown.escape(crumb.Title)}</a>`
            )
        else
            table.insert(breadcrumbs, `<span>{markdown.escape(crumb.Title)}</span>`)
        end
    end
    local breadcrumbHtml = if #breadcrumbs > 0
        then `<nav class="breadcrumbs" aria-label="Breadcrumb">{table.concat(breadcrumbs, '<span class="sep">/</span>')}</nav>`
        else ""

    local toc = if spec.Wide == true then "" else renderToc(spec.Headings)
    local sidebar = if spec.Wide == true
        then ""
        else `<aside class="sidebar" id="sidebar"><nav class="nav" aria-label="Documentation">{renderNavItems(
            nav,
            spec.Path,
            prefix,
            0
        )}</nav></aside>`

    local layoutClass = if spec.Wide == true then "layout layout-wide" else "layout"
    local tocHtml = if toc ~= "" then `<div class="rail">{toc}</div>` else ""

    local lines: { string } = {
        "<!DOCTYPE html>",
        '<html lang="en" data-theme="auto">',
        "<head>",
        Banner,
        '<meta charset="utf-8">',
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        `<title>{markdown.escape(title)}</title>`,
        `<meta name="description" content="{markdown.escape(markdown.summarize(description))}">`,
        `<link rel="icon" href="{prefix}{brand.Mark}">`,
        `<link rel="stylesheet" href="{prefix}assets/site.css">`,
        '<script>window.LuaugDocs = { root: "' .. prefix .. '" };</script>',
        "</head>",
        '<body class="doc-body">',
        '<a class="skip-link" href="#content">Skip to content</a>',
        '<header class="topbar">',
        '<button class="menu-toggle" id="menu-toggle" aria-label="Toggle navigation" aria-expanded="false">☰</button>',
        `<a class="brand" href="{prefix}index.html"><img src="{prefix}{brand.Logo}" alt="{SiteName}"></a>`,
        `<span class="version" title="Engine version">{markdown.escape(version)}</span>`,
        '<div class="search" role="search">',
        '<input type="search" id="search-input" placeholder="Search the documentation" autocomplete="off" spellcheck="false" aria-label="Search the documentation">',
        '<div class="search-results" id="search-results" hidden></div>',
        "</div>",
        '<button class="theme-toggle" id="theme-toggle" aria-label="Switch between light and dark">◐</button>',
        "</header>",
        `<div class="{layoutClass}">`,
        sidebar,
        '<main class="content" id="content">',
        '<article class="article">',
        breadcrumbHtml,
        spec.Body,
        "</article>",
        '<footer class="footer">',
        `<p>{SiteName} {markdown.escape(version)} — Apache-2.0. This page is generated from the engine's own API definition; if a member is here, the engine has it.</p>`,
        "</footer>",
        "</main>",
        tocHtml,
        "</div>",
        `<script src="{prefix}assets/search-index.js"></script>`,
        `<script src="{prefix}assets/site.js"></script>`,
        "</body>",
        "</html>",
    }

    return table.concat(lines, "\n")
end

return {
    Banner = Banner,
    SiteName = SiteName,
    prefixFor = prefixFor,
    renderPage = renderPage,
    renderNavItems = renderNavItems,
}
