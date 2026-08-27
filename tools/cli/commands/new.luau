--!strict
--[[
	`luaug new` — scaffold a project from a template.

	The promise this command makes is api-design.md §4's: run it, open the
	folder in VS Code, and you have completion, hover documentation and strict
	analysis with nothing else installed. That means the type definitions and
	the editor settings are part of scaffolding, not a follow-up step somebody
	has to be told about.
]]

local fs = require("@std/fs")
local process = require("@std/process")

local catalog = require("../catalog")
local projectlib = require("../project")
local setup = require("./setup")

local tr = catalog.tr

export type Options = {
    name: string,
    template: string,
    -- Where to create it; the working directory by default.
    parent: string,
}

local Templates = { starter = true }

--- Where this CLI's own files live, so the template and the engine definitions
--- are found relative to the installation rather than to the caller's cwd.
---
--- **This used to be a walk of its own, and it was the walk the rest of the CLI
--- should have been doing** (ADR 0054). D045 taught it here — a count two
--- directories up was right while this command lived one level higher and
--- stopped being right the moment it moved, and nothing said so — while the
--- engine search and `--version` kept counting and kept looking in the working
--- directory. `projectlib.installRoot` is that same walk, asked once.
local function cliRoot(): string?
    return projectlib.installRoot()
end

local function copyTree(from: string, to: string, substitutions: { [string]: string })
    fs.createDirectory(to, { makeParents = true })

    for _, entry in fs.listDirectory(from) do
        local source = projectlib.joined(from, entry.name)
        local target = projectlib.joined(to, entry.name)

        if entry.type == "dir" then
            copyTree(source, target, substitutions)
            continue
        end
        if entry.type ~= "file" then
            continue
        end

        -- Substitution is deliberately text-wide and deliberately absent from
        -- `.luau` files: a template's scripts are analysed by CI exactly as
        -- written (templates/README.md), so a placeholder in one would be a
        -- syntax error in the repository rather than a nicety in the output.
        local contents = fs.readFileToString(source)
        for placeholder, value in substitutions do
            contents = string.gsub(contents, placeholder, value)
        end
        fs.writeStringToFile(target, contents)
    end
end

local function run(options: Options): number
    if not Templates[options.template] then
        print(tr("cli.new.err.unknown_template", { name = options.template }))
        return 2
    end
    if string.match(options.name, "^[%w_%-]+$") == nil then
        print(tr("cli.new.err.bad_name", { name = options.name }))
        return 2
    end

    local target = projectlib.joined(options.parent, options.name)
    if fs.exists(target) then
        print(tr("cli.new.err.exists", { path = target }))
        return 2
    end

    local root = cliRoot()
    if root == nil then
        -- Named rather than guessed. A relative path from here would scaffold a
        -- project out of whatever happened to be beside the working directory,
        -- which is the failure this whole command is written against.
        print(tr("cli.new.err.no_install"))
        return 1
    end
    copyTree(projectlib.joined(root, "templates", options.template), target, { ["{{name}}"] = options.name })

    -- `.luaug/` is generated and gitignored, which is why it is written here
    -- rather than kept in the template: it belongs to the engine version, not
    -- to the template.
    --
    -- **`luaug setup` writes it, and this asks that** rather than carrying its
    -- own copy (S5.19). The same directory has to be writable a second time --
    -- `.luaug/` is gitignored, so a project cloned from git has none of it --
    -- and two functions writing one layout would disagree the first time either
    -- learned about a new module.
    setup.writeTypes({ target = target, root = root })

    print(tr("cli.new.created", { path = target, name = options.name }))
    return 0
end

return {
    run = run,
    defaultParent = function(): string
        return process.cwd()
    end,
}
