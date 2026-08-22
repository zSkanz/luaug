--!strict
--[[
	`luaug edit` — the same host, with the editor UI in front of the world.

	Not a mode that hijacks the loop the way `--replay`, `--bench` and
	`--two-worlds` do: `--edit` is a boolean on an otherwise ordinary windowed
	session, so this command is `dev` minus the dev server. It resolves the
	project, finds the engine, and hands both to `luaug-host`.

	There is no `--headless`: an editor with no window is not an editor, and
	there is no `--port` because nothing here dials in. `engineArguments` is
	exported for the same reason `version.luaug` is — it is the narrow seam a
	test can prove the argv through without opening a window.
]]

local process = require("@std/process")

local catalog = require("../catalog")
local projectlib = require("../project")

local tr = catalog.tr

export type Options = {
    path: string,
}

--- The engine command line for an editor session. Pure, and the only place the
--- `--edit` flag is spelled.
local function engineArguments(engine: string, root: string): { string }
    return { engine, root, "--edit" }
end

local function run(options: Options): number
    local project = projectlib.load(options.path)
    if project.diagnostic ~= nil then
        print(project.diagnostic)
        return 2
    end
    if not projectlib.isProject(project) then
        print(tr("cli.err.not_a_project", { path = project.root }))
        return 2
    end

    -- 3 rather than the 2 `dev` returns for the same condition: a missing
    -- engine is the environment being wrong, not the command line, and
    -- `version.run` already separates the two that way.
    local engine = projectlib.findEngine()
    if engine == nil then
        print(tr("cli.err.no_engine"))
        return 3
    end

    print(tr("cli.edit.opening", { path = project.root }))

    local result = process.run(engineArguments(engine, project.root), { stdio = "inherit" })

    print(tr("cli.edit.closed", { code = result.exitcode }))
    return if result.ok then 0 else 1
end

return {
    run = run,
    engineArguments = engineArguments,
}
