--!strict
--[[
	The manual's information architecture.

	**The section list is the site's table of contents and it is authored, not
	discovered.** A directory scan would put the pages in whatever order the
	filesystem returns them, and the order of a manual is most of what a manual
	is -- somebody reads "Your first world" before "Streaming a large world"
	because this file says so. It is the same argument `api/defs/init.luau`
	makes for listing its definition files by hand: adding a page is meant to be
	a visible edit in a file people review.

	**Shortcuts exist so that a page does not have to know how deep it sits.**
	A manual page links to another with `manual:section/page`, to a member with
	`api:Class.Member`, and to the few fixed destinations below by name. All
	three are resolved at generation time and a target that does not exist fails
	the run, so a page cannot go quietly stale against an engine that renamed
	something.
]]

export type Page = {
    --- File name without extension, under the section's directory, in both
    --- `docs/manual/` and the emitted site.
    Slug: string,
    Title: string,
}

export type Section = {
    Slug: string,
    Title: string,
    --- One line, shown on the section index and the landing page.
    Summary: string,
    Pages: { Page },
}

local Sections: { Section } = {
    {
        Slug = "get-started",
        Title = "Get started",
        Summary = "Install the toolchain, make a project, and get a world on the screen.",
        Pages = {
            { Slug = "what-is-luaug", Title = "What LuauG is" },
            { Slug = "install", Title = "Install and toolchain" },
            { Slug = "first-world", Title = "Your first world" },
            { Slug = "project-anatomy", Title = "Anatomy of a project" },
            { Slug = "cli", Title = "The luaug CLI" },
            { Slug = "editor", Title = "The visual editor" },
        },
    },
    {
        Slug = "concepts",
        Title = "Core concepts",
        Summary = "The model every script is written against: the tree, the frame, signals and time.",
        Pages = {
            { Slug = "instance-tree", Title = "The Instance tree" },
            { Slug = "services", Title = "Services" },
            { Slug = "properties", Title = "Properties, attributes and tags" },
            { Slug = "signals", Title = "Signals and connections" },
            { Slug = "task", Title = "task, and asking for time" },
            { Slug = "frame", Title = "The frame, phase by phase" },
            { Slug = "determinism", Title = "Determinism" },
            { Slug = "scripts", Title = "Scripts, modules and requires" },
            { Slug = "datatypes", Title = "Datatypes and units" },
            { Slug = "sandbox", Title = "What a script may do" },
        },
    },
    {
        Slug = "world",
        Title = "Building a world",
        Summary = "Parts, transforms, meshes, prefabs, and the scene the game starts in.",
        Pages = {
            { Slug = "parts", Title = "Parts and solids" },
            { Slug = "transforms", Title = "Transforms, pivots and units" },
            { Slug = "meshes", Title = "Meshes and models" },
            { Slug = "prefabs", Title = "Prefabs" },
            { Slug = "scenes", Title = "Scenes: the world as data" },
        },
    },
    {
        Slug = "rendering",
        Title = "Rendering and lighting",
        Summary = "The camera, the sun, the sky, the lights and the post chain.",
        Pages = {
            { Slug = "camera", Title = "The camera" },
            { Slug = "lighting", Title = "Lighting and the sky" },
            { Slug = "shadows", Title = "Shadows" },
            { Slug = "lights", Title = "Point and spot lights" },
            { Slug = "post", Title = "The post chain" },
            { Slug = "quality", Title = "Graphics quality settings" },
        },
    },
    {
        Slug = "physics",
        Title = "Physics",
        Summary = "Rigid bodies, collisions, queries, joints, and a character that walks.",
        Pages = {
            { Slug = "bodies", Title = "Rigid bodies" },
            { Slug = "collisions", Title = "Collisions and contact" },
            { Slug = "queries", Title = "Raycasts and queries" },
            { Slug = "joints", Title = "Welds and constraints" },
            { Slug = "character", Title = "CharacterBody" },
        },
    },
    {
        Slug = "input",
        Title = "Input",
        Summary = "The Input Action System: what the player did, not which key they pressed.",
        Pages = {
            { Slug = "actions", Title = "Actions, bindings and contexts" },
            { Slug = "rebinding", Title = "Rebinding and prompts" },
            { Slug = "raw", Title = "Raw input, and when to use it" },
        },
    },
    {
        Slug = "ui",
        Title = "User interface",
        Summary = "The screen tree, the layout arithmetic, text, images and interaction.",
        Pages = {
            { Slug = "tree", Title = "The UI tree" },
            { Slug = "layout", Title = "Layout with UDim2" },
            { Slug = "text-and-images", Title = "Text and images" },
            { Slug = "interaction", Title = "Buttons and interaction" },
        },
    },
    {
        Slug = "audio",
        Title = "Audio",
        Summary = "Playing a sound, placing it in the world, and mixing what comes out.",
        Pages = {
            { Slug = "sounds", Title = "Sounds" },
            { Slug = "mixing", Title = "Groups, mixing and the listener" },
        },
    },
    {
        Slug = "animation",
        Title = "Animation",
        Summary = "Tweening a property, and playing a skinned mesh's clips.",
        Pages = {
            { Slug = "tweens", Title = "Tweens" },
            { Slug = "skeletal", Title = "Skeletal animation" },
        },
    },
    {
        Slug = "assets",
        Title = "Assets and streaming",
        Summary = "Where content comes from, and how a world larger than memory is played.",
        Pages = {
            { Slug = "content", Title = "Content and asset URNs" },
            { Slug = "pipeline", Title = "The asset pipeline" },
            { Slug = "streaming", Title = "Streaming a large world" },
            { Slug = "floating-origin", Title = "Floating origin" },
        },
    },
    {
        Slug = "guides",
        Title = "Guides",
        Summary = "Hot reload, testing, localization, the debug overlay, and shipping.",
        Pages = {
            { Slug = "hot-reload", Title = "Hot reload" },
            { Slug = "testing", Title = "Testing" },
            { Slug = "i18n", Title = "Localization" },
            { Slug = "debug-overlay", Title = "The debug overlay" },
            { Slug = "shipping", Title = "Shipping a game" },
            { Slug = "backend", Title = "Talking to a backend" },
        },
    },
    {
        Slug = "why",
        Title = "Why it works this way",
        Summary = "The decisions a person will trip over, each with the reasoning that settled it.",
        Pages = {
            { Slug = "deferred-signals", Title = "Signals are deferred-only" },
            { Slug = "fixed-tick", Title = "The tick is fixed" },
            { Slug = "vector", Title = "Vector3 is the Luau vector" },
            { Slug = "children-are-not-members", Title = "Children are not members" },
            { Slug = "world-is-data", Title = "The world is data, scripts are behaviour" },
            { Slug = "casing", Title = "Objects are PascalCase, namespaces are camelCase" },
            { Slug = "graphics-settings", Title = "Graphics settings belong to the player" },
            { Slug = "input-model", Title = "There is one input model" },
        },
    },
    {
        Slug = "roblox",
        Title = "Coming from Roblox",
        Summary = "What transfers, what is spelled differently, and what is deliberately absent.",
        Pages = {
            { Slug = "migration", Title = "The migration guide" },
            { Slug = "divergences", Title = "Every deliberate divergence" },
            { Slug = "not-here", Title = "What is not here" },
        },
    },
}

--- Root-relative page path for a manual page.
local function pagePath(section: string, slug: string): string
    return `manual/{section}/{slug}.html`
end

local function sectionPath(section: string): string
    return `manual/{section}/index.html`
end

--- Targets a manual page may link to with `[text](target)`, beyond another
--- manual page and an `api:` reference. Keeps a hand-authored page from having
--- to know how deep in the tree it sits.
--- Every key carries a colon for the same reason `api:` and `manual:` do: the
--- documentation gate resolves a Markdown link target as a path unless it holds
--- one, so a bare `reference` would send the gate looking for a file of that
--- name beside the page.
local Shortcuts: { [string]: string } = {
    ["site:reference"] = "reference/index.html",
    ["site:hierarchy"] = "reference/hierarchy.html",
    ["site:globals"] = "reference/globals.html",
    ["site:home"] = "index.html",
}

return {
    Sections = Sections,
    Shortcuts = Shortcuts,
    pagePath = pagePath,
    sectionPath = sectionPath,
}
