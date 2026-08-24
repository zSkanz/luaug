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
    -- Nothing when the person typed `luaug edit` and nothing else. That is not
    -- the same as ".": it means "open something", and what answers it is the
    -- project browser (ADR 0055).
    path: string?,
}

--- The engine command line for an editor session. Pure, and the only place the
--- `--edit` flag is spelled.
---
--- With no project, the host's own launcher argv -- which is what `luaug edit`
--- typed on its own now produces (ADR 0055). One function rather than two,
--- because the two differ by exactly this and a test proves both through it.
local function engineArguments(engine: string, root: string?): { string }
    if root == nil then
        return { engine, "--launcher" }
    end
    return { engine, root, "--edit" }
end

local function run(options: Options): number
    -- **Nothing typed opens the browser**, and a path typed still has to be a
    -- project. The difference matters: somebody who names a directory means
    -- that one, and answering a typo with a project list would be answering a
    -- different question.
    local root: string? = nil
    if options.path ~= nil then
        local project = projectlib.load(options.path)
        if project.diagnostic ~= nil then
            print(project.diagnostic)
            return 2
        end
        if not projectlib.isProject(project) then
            print(tr("cli.err.not_a_project", { path = project.root }))
            return 2
        end
        root = project.root
    end

    -- 3 rather than the 2 `dev` returns for the same condition: a missing
    -- engine is the environment being wrong, not the command line, and
    -- `version.run` already separates the two that way.
    local engine = projectlib.findEngine()
    if engine == nil then
        print(tr("cli.err.no_engine"))
        return 3
    end

    -- **Asked of the binary, not assumed from where it was found** (D087). A
    -- host built without the debug UI has no ImGui linked into it at all, so
    -- `--edit` gives you a window with the world rendered into an offscreen
    -- texture and nothing on screen but the backdrop clear. It runs, it exits
    -- zero, and it is black -- which is the failure this whole command is
    -- written against, arriving through the one thing nothing checked.
    if not projectlib.hostHasDebugUi(engine) then
        print(tr("cli.edit.err.no_editor_host", { path = engine }))
        return 3
    end

    if root ~= nil then
        print(tr("cli.edit.opening", { path = root }))
    end

    local result = process.run(engineArguments(engine, root), { stdio = "inherit" })

    -- **Closing the window is the ordinary way to leave an editor**, and on
    -- Windows a terminated process reports a negative code rather than zero. A
    -- message that read "exited with code -1" for the commonest thing a person
    -- does made a normal close look like a failure, which is a defect in the
    -- message and not in the engine.
    if result.ok or result.exitcode < 0 then
        print(tr("cli.edit.closed"))
        return 0
    end

    print(tr("cli.edit.failed", { code = result.exitcode }))
    return 1
end

return {
    run = run,
    engineArguments = engineArguments,
}
