--!strict
--[[
	`luaug --version` (M4 carried debt).

	Driven through the real command line rather than by calling
	`commands/version.run` directly: the defect this replaces was a dispatch
	that did not exist, so a test that skips dispatch would have passed against
	the bug it is here to catch. It spawns the CLI exactly as
	`scripts/luaug.*` do and reads what a person would see.

	The version is then compared against the `project()` declaration in the
	superbuild root, because ADR 0031's requirement is not "prints something
	version-shaped" — it is that the number is the pinned one and cannot drift
	from it.
]]

local assert = require("@std/test/assert")
local fs = require("@std/fs")
local process = require("@std/process")
local test = require("@std/test")

local version = require("../commands/version")

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

test.suite("version", function(suite)
    suite:case("--version reports the declared version and the running Lute", function()
        local result = runCli("--version")
        assert.eq(result.code, 0, result.output)

        local declared = version.luaug()
        assert.that(declared.value ~= nil, `no project() version at {declared.source}`)

        local reported, lute = string.match(result.output, "^luaug ([%d%.]+) %(Lute ([^%)]+)%)%s*$")
        assert.that(reported ~= nil, `not the shape cli.version describes: {result.output}`)
        assert.eq(reported, declared.value)
        assert.that(lute ~= nil and #(lute :: string) > 0, "the Lute half must carry a version")
    end)

    suite:case("the declared version is the one CMake expands into build_info.h", function()
        local declared = version.luaug()
        assert.that(fs.exists(declared.source), `{declared.source} does not exist`)
        -- Guards the direction the pattern could silently fail in: a `project()`
        -- rewrite that still matches something would be caught by the shape,
        -- but reading the wrong project's version would not.
        assert.strContains(fs.readFileToString(declared.source), `VERSION {declared.value}`)
    end)

    suite:case("--version is answered wherever it appears, and does not invent a subcommand", function()
        -- The flag form, which reaches the CLI past a command name.
        local flagged = runCli("dev", "--version")
        assert.eq(flagged.code, 0, flagged.output)
        assert.strContains(flagged.output, "luaug ")

        -- `luaug version` is not a command, exactly as `luaug help` is not.
        local bare = runCli("version")
        assert.eq(bare.code, 2, bare.output)
        assert.strContains(bare.output, "Unknown command")
    end)
end)
