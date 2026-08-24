--!strict
--[[
	`luaug edit` (E1).

	Driven through the real command line rather than by calling
	`commands/edit.run` directly, for the reason `version.test.luau` states: the
	defect that convention exists for was a dispatch that did not exist, and a
	test that skips dispatch would pass against exactly that bug. Every case
	here spawns the CLI the way `scripts/luaug.*` do.

	None of them opens a window. An editor session is a windowed session by
	definition, so what is provable without a GUI is the two halves either side
	of it: the refusals, which return before the engine is ever looked for, and
	`engineArguments`, which is the whole of the command line the host receives.
]]

local assert = require("@std/test/assert")
local process = require("@std/process")
local test = require("@std/test")

local edit = require("../commands/edit")

--- The test file's own location, so this works from any working directory the
--- runner is invoked in — the same reason `catalog.luau` resolves its catalog
--- this way.
local function testsDirectory(): string
    return string.match(debug.info(1, "s"), "^(.*)[/\\][^/\\]+$") or "tools/cli/tests"
end

local function runCli(...: string): { code: number, output: string }
    local arguments: { string } = { tostring(process.execPath()), "run", `{testsDirectory()}/../main.luau`, "--" }
    for _, argument in { ... } do
        table.insert(arguments, argument)
    end
    local result = process.run(arguments)
    return { code = result.exitcode, output = result.stdout .. result.stderr }
end

test.suite("edit", function(suite)
    suite:case("the command list advertises edit", function()
        local listed = runCli("--help")
        assert.eq(listed.code, 0, listed.output)
        assert.strContains(listed.output, "edit [path]")

        -- `--help` past a command name is answered before dispatch, so this is
        -- also what `luaug edit --help` prints.
        local perCommand = runCli("edit", "--help")
        assert.eq(perCommand.code, 0, perCommand.output)
        assert.strContains(perCommand.output, "edit [path]")
    end)

    suite:case("an unknown option is refused rather than ignored", function()
        -- `--headless` deliberately: it is `dev`'s flag and the one a developer
        -- is likeliest to carry over, and an editor with no window is not an
        -- editor. Silently ignoring it would start a windowed session while the
        -- user believed they had asked for none.
        local headless = runCli("edit", "--headless")
        assert.eq(headless.code, 2, headless.output)
        assert.strContains(headless.output, "Unknown option")

        local typo = runCli("edit", "--eddit")
        assert.eq(typo.code, 2, typo.output)
        assert.strContains(typo.output, "Unknown option")
    end)

    suite:case("a path that is not a project is refused with a sentence, not a traceback", function()
        -- This directory: `project.findRoot` walks up from it and finds neither
        -- a `luaug.toml` nor a `src/scripts` anywhere above, because the engine
        -- source tree is not itself a game project. The refusal therefore
        -- happens before the engine is looked for, which is what keeps this
        -- case from ever opening a window.
        local refused = runCli("edit", testsDirectory())
        assert.eq(refused.code, 2, refused.output)
        assert.strContains(refused.output, "is not a LuauG project")
        assert.strNotContains(refused.output, "stack traceback")
    end)

    suite:case("nothing typed asks the host for the project browser", function()
        -- **The one place `luaug edit` does NOT refuse** (ADR 0055). A path
        -- that is not a project is still a refusal -- the case above -- because
        -- somebody who names a directory means that one, and answering a typo
        -- with a project list answers a different question. Nothing typed is
        -- the other question, and it has an answer now.
        assert.eq(edit.engineArguments("/build/luaug-host", nil), {
            "/build/luaug-host",
            "--launcher",
        })
    end)

    suite:case("the engine command line is the project and --edit", function()
        -- The narrow seam this file's header describes. `--edit` is a boolean
        -- on an ordinary windowed session, not a mode that replaces the loop
        -- the way `--replay=DIR` and `--bench=DIR` do, so it carries no value
        -- and nothing else is passed with it.
        assert.eq(edit.engineArguments("/build/luaug-host", "/games/obby"), {
            "/build/luaug-host",
            "/games/obby",
            "--edit",
        })
    end)
end)
