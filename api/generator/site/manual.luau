--!strict
--[[
	The manual: `docs/manual/**.md` rendered into the site, plus the section
	index pages and the landing page.

	**The manual is authored Markdown and the reference is generated, and that
	split is deliberate.** A catalogue of members can be generated because the
	IDL knows every member; a paragraph explaining why an obby's checkpoint is a
	tag rather than a class cannot be, because nothing in the IDL knows what an
	obby is. So the two halves of a documentation site come from two places, and
	this module is where the authored half is read.

	What the manual may NOT do is invent a link. A page reaches the reference
	through `api:Part.Shape`, which resolves against the IDL at generation time
	and fails the run if the symbol does not exist -- so a manual page cannot
	quietly go stale against an engine that renamed a member.
]]

local fs = require("@std/fs")

local markdown = require("./markdown")
local navigation = require("./nav")
local reference = require("./reference")
local shell = require("./shell")

export type Loaded = {
    Section: navigation.Section,
    Page: navigation.Page,
    --- Root-relative output path.
    Path: string,
    Source: string,
    --- Raw Markdown body with the leading `# Title` heading removed, because
    --- the title is rendered from the navigation entry rather than from the
    --- file -- one authority for what a page is called.
    Body: string,
}

local ManualRoot = "docs/manual"

local function sourcePath(section: navigation.Section, page: navigation.Page): string
    return `{ManualRoot}/{section.Slug}/{page.Slug}.md`
end

--- Reads every page the navigation declares. A declared page with no file is an
--- error rather than a silent omission: the navigation is the table of
--- contents, and a table of contents with a dead row is worse than a short one.
local function loadAll(errors: { string }): { Loaded }
    local loaded: { Loaded } = {}
    for _, section in navigation.Sections do
        for _, page in section.Pages do
            local path = sourcePath(section, page)
            if not fs.exists(path) then
                table.insert(errors, `{path} is declared in the navigation and does not exist`)
                continue
            end
            local text = fs.readFileToString(path)
            local body = string.gsub(text, "^#%s+[^\n]*\n", "", 1)
            table.insert(loaded, {
                Section = section,
                Page = page,
                Path = navigation.pagePath(section.Slug, page.Slug),
                Source = path,
                Body = body,
            })
        end
    end
    return loaded
end

local function renderPage(entry: Loaded, context: reference.Context): (string, { shell.Heading })
    local headings: { shell.Heading } = {}
    local options = reference.markdownOptions(context)
    options.headings = headings

    local out: { string } = {}
    table.insert(out, `<h1 class="page-title">{markdown.escape(entry.Page.Title)}</h1>`)
    table.insert(out, markdown.render(entry.Body, options))
    return table.concat(out, "\n"), headings
end

--- A section's index: the one-line summary and a card per page. Somebody who
--- opens "Physics" wants to see the five things it covers, not to be redirected
--- to the first of them.
local function renderSectionIndex(section: navigation.Section, context: reference.Context): (string, { shell.Heading })
    local out: { string } = {}
    table.insert(out, `<h1 class="page-title">{markdown.escape(section.Title)}</h1>`)
    table.insert(out, `<div class="lead"><p>{markdown.escape(section.Summary)}</p></div>`)

    local cards: { string } = {}
    for _, page in section.Pages do
        table.insert(
            cards,
            `<a class="card" href="{context.Prefix}{navigation.pagePath(section.Slug, page.Slug)}"><h3>{markdown.escape(
                page.Title
            )}</h3></a>`
        )
    end
    table.insert(out, `<div class="cards">{table.concat(cards)}</div>`)

    return table.concat(out, "\n"), {}
end

local function renderLanding(context: reference.Context, version: string): string
    local out: { string } = {}
    table.insert(out, '<div class="hero">')
    table.insert(out, "<h1>LuauG documentation</h1>")
    table.insert(
        out,
        "<p>A standalone game engine scripted in Luau. An <code>Instance</code> tree, services, deferred signals and <code>task</code> — the developer experience you already know — over a C++ core with a data-oriented ECS, a deterministic fixed tick, Jolt physics and a renderer that carries cascaded shadows, clustered lights and image-based lighting.</p>"
    )
    table.insert(out, '<div class="hero-actions">')
    table.insert(
        out,
        `<a class="button" href="{context.Prefix}{navigation.pagePath("get-started", "first-world")}">Your first world</a>`
    )
    table.insert(
        out,
        `<a class="button button-ghost" href="{context.Prefix}{reference.ReferencePath}">API reference</a>`
    )
    -- The third action is the migration section's own first page, named by the
    -- navigation rather than here: a hero button that hard-codes a section's
    -- title is a second place to edit when the section is renamed.
    local migration = navigation.Sections[#navigation.Sections]
    local firstPage = migration.Pages[1]
    if firstPage ~= nil then
        table.insert(
            out,
            `<a class="button button-ghost" href="{context.Prefix}{navigation.pagePath(migration.Slug, firstPage.Slug)}">{markdown.escape(
                migration.Title
            )}</a>`
        )
    end
    table.insert(out, "</div></div>")

    table.insert(out, '<div class="landing">')
    local cards: { string } = {}
    for _, section in navigation.Sections do
        table.insert(
            cards,
            `<a class="card" href="{context.Prefix}{navigation.sectionPath(section.Slug)}"><h3>{markdown.escape(
                section.Title
            )}</h3><p>{markdown.escape(section.Summary)}</p></a>`
        )
    end
    table.insert(out, `<div class="cards">{table.concat(cards)}</div>`)

    local counts = context.Model
    table.insert(
        out,
        `<p class="hint">The reference covers {#counts.Classes} classes, {#counts.Datatypes} datatypes, {#counts.Enums} enumerations and the <code>task</code> library, generated from the engine's own API definition at version {markdown.escape(
            version
        )}. If a member is on these pages, the engine has it.</p>`
    )
    table.insert(out, "</div>")

    return table.concat(out, "\n")
end

return {
    ManualRoot = ManualRoot,
    loadAll = loadAll,
    renderPage = renderPage,
    renderSectionIndex = renderSectionIndex,
    renderLanding = renderLanding,
}
