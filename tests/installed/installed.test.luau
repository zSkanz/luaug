--!strict
--[[
	The installed shape, driven as a person would drive it (ADR 0054).

	**This is the one test that runs the CLI from somewhere other than this
	repository**, which is the whole reason it exists: every path the CLI
	resolves had a comment claiming it worked in an installation, two of them did
	not, and nothing here could have noticed because nothing here had ever left
	the source tree.

	So it runs the packaged `luaug` with an environment that has no
	`LUAUG_BUILD_ROOT` and no `LUAUG_HOST` in it. If the folder cannot find its
	own engine, its own template and its own version, there is nowhere else for
	it to look.

	**It does not open the editor.** The ImGui shell cannot render headlessly and
	SDL does not accept injected input, which is the limit E1 recorded and it has
	not moved. What can be driven is everything around it: the version, the
	scaffold, and a game built by the player host the package carries.

	Run after `scripts/package.ps1` has written a folder; it skips, saying so,
	when there is none.
]]

local assert = require("@std/test/assert")
local fs = require("@std/fs")
local platform = require("@std/system/platform")
local process = require("@std/process")
local test = require("@std/test")

local projectlib = require("../../tools/cli/project")
local version = require("../../tools/cli/commands/version")

local function repositoryRoot(): string
    local here = string.match(debug.info(1, "s"), "^(.*)[/\\][^/\\]+$") or "tests/installed"
    return projectlib.joined(here, "..", "..")
end

--- Where `tools/repo/package.luau` writes by default. Derived the same way it
--- derives it, so the two cannot disagree about the name.
local function packageDirectory(): string?
    local buildRoot = process.env.LUAUG_BUILD_ROOT
    if buildRoot == nil or buildRoot == "" then
        return nil
    end
    local declared = version.luaug().value
    if declared == nil then
        return nil
    end
    local tag = if platform.win32 then "win64" else (if platform.macos then "macos" else "linux")
    local candidate = projectlib.joined(buildRoot, "package", `LuauG-{declared}-{tag}`)
    return if fs.exists(candidate) then candidate else nil
end

--- What a fresh machine has, and nothing this repository put there. Built by
--- naming what a shell needs rather than by filtering what happens to be set:
--- a filter is a list of things to remember, and the one somebody forgets is
--- the one that makes the test pass for the wrong reason.
local function bareEnvironment(): { [string]: string }
    local bare: { [string]: string } = {}
    for _, name in { "PATH", "SystemRoot", "COMSPEC", "TEMP", "TMP", "HOME", "USERPROFILE" } do
        local value = process.env[name]
        if value ~= nil and value ~= "" then
            bare[name] = value
        end
    end
    return bare
end

local function removeTree(target: string)
    if fs.exists(target) then
        fs.removeDirectory(target, { recursive = true })
    end
end

test.suite("installed", function(suite)
    suite:case("a packaged folder answers for itself, with no build root anywhere", function()
        local installed = packageDirectory()
        if installed == nil then
            print("no packaged folder to test -- run scripts/package.ps1 first")
            return
        end

        local wrapper = projectlib.joined(installed, if platform.win32 then "luaug.cmd" else "luaug.sh")
        assert.that(fs.exists(wrapper), `no wrapper at {wrapper}`)

        local environment = bareEnvironment()
        local scratch = projectlib.joined(repositoryRoot(), ".luaug-installed-test")
        removeTree(scratch)
        fs.createDirectory(scratch, { makeParents = true })

        local function luaug(...: string): { code: number, output: string }
            local arguments: { string } = { wrapper }
            for _, argument in { ... } do
                table.insert(arguments, argument)
            end
            -- From the scratch directory, which is the case that never worked:
            -- the old search ended at the working directory, so it found a host
            -- only when somebody happened to be standing in the installation.
            local result = process.run(arguments, { cwd = scratch, env = environment })
            return { code = result.exitcode, output = result.stdout .. result.stderr }
        end

        -- The version, from the stamp rather than from a `CMakeLists.txt` that
        -- is not in the folder.
        local reported = luaug("--version")
        assert.eq(reported.code, 0, reported.output)
        local declared = version.luaug().value
        assert.that(
            declared ~= nil and string.find(reported.output, declared :: string, 1, true) ~= nil,
            `--version did not report {tostring(declared)}: {reported.output}`
        )

        -- The scaffold, from the template the folder carries.
        local created = luaug("new", "installedtest")
        assert.eq(created.code, 0, created.output)
        local project = projectlib.joined(scratch, "installedtest")
        assert.that(fs.exists(projectlib.joined(project, "src", "scripts", "main.luau")), "no entry script")
        assert.that(
            fs.exists(projectlib.joined(project, ".luaug", "types", "engine.d.luau")),
            "no engine definitions in the scaffolded project"
        )

        if not platform.win32 then
            print("`luaug build --target win64` is Windows-only, so the build half was not exercised")
            removeTree(scratch)
            return
        end

        -- And a game, built by the `player` host the folder carries under
        -- `player/` — which is the reason it carries two hosts at all (D057).
        local built = luaug("build", project)
        assert.eq(built.code, 0, built.output)
        local artifact = projectlib.joined(project, "dist", "win64", "installedtest.exe")
        assert.that(fs.exists(artifact), `no artifact at {artifact}:\n{built.output}`)

        local ran = process.run({ artifact, "--headless", "--frames=30", "--exit" })
        local text = ran.stdout .. ran.stderr
        assert.that(ran.ok, `the game built by the installed CLI exited non-zero:\n{text}`)
        assert.that(
            string.find(text, "Hello from LuauG", 1, true) ~= nil,
            `the game did not run its own script:\n{text}`
        )

        removeTree(scratch)
    end)
end)
