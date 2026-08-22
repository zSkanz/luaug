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
--- **Found by walking up rather than by counting, and that is D045.** It
--- counted two directories up from its own source, which was right while this
--- command lived at `tools/cli/new.luau` and stopped being right the moment it
--- moved into `commands/`. Nothing said so: the command still ran and failed
--- inside `listDirectory` on a path with `../..` in the middle of it. A count
--- is a fact about a directory layout that no check holds still; looking for
--- the thing being looked for is not.
local function cliRoot(): string
    local here = string.match(debug.info(1, "s"), "^(.*)[/\\][^/\\]+$") or "tools/cli"
    local current = here
    for _ = 1, 8 do
        if fs.exists(projectlib.joined(current, "templates", "starter")) then
            return current
        end
        local parent = projectlib.joined(current, "..")
        if parent == current then
            break
        end
        current = parent
    end
    -- The old answer, so a failure names a path a reader recognises rather than
    -- an empty string turning every path below into a relative one.
    return projectlib.joined(here, "..", "..")
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
    copyTree(projectlib.joined(root, "templates", options.template), target, { ["{{name}}"] = options.name })

    -- `.luaug/` is generated and gitignored, which is why it is written here
    -- rather than kept in the template: it belongs to the engine version, not
    -- to the template.
    local types = projectlib.joined(target, ".luaug", "types")
    fs.createDirectory(types, { makeParents = true })
    -- One file, and the hover documentation is inside it: the IDL's doc text is
    -- emitted as `---` comments above each declaration, so there is no second
    -- artifact to copy and none to keep in step.
    fs.copy(projectlib.joined(root, "runtime", "types", "engine.d.luau"), projectlib.joined(types, "engine.d.luau"))

    print(tr("cli.new.created", { path = target, name = options.name }))
    return 0
end

return {
    run = run,
    defaultParent = function(): string
        return process.cwd()
    end,
}
