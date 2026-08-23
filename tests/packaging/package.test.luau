--!strict
--[[
	The M8 packaging chain, end to end: scaffold, build, run, and read the
	identity back out of the artifact.

	Three of M8's scope items meet here and none of them can be checked any
	other way:

	  - `luaug build` produces a FOLDER that runs. Not "the command exited zero"
	    -- the executable inside it is launched, with no arguments, and has to
	    play the game beside it.
	  - **Application identity is asserted, not eyeballed.** The built `.exe` is
	    opened, its PE resource table is walked, and the icon it carries has to
	    be present and NOT the engine's default. An icon is the most
	    regression-prone thing in a build precisely because nothing fails when it
	    is wrong: the program runs, the window opens, and it wears the wrong
	    face.
	  - `luaug new` scaffolds a project that all of the above works on, which is
	    the roadmap's clean-machine chain minus the fresh clone.

	Windows only, because `luaug build --target win64` is. On any other platform
	the suite skips rather than failing: there is nothing here that a Linux
	runner could be doing wrong.
]]

local assert = require("@std/test/assert")
local fs = require("@std/fs")
local platform = require("@std/system/platform")
local process = require("@std/process")
local test = require("@std/test")

local build = require("../../tools/cli/commands/build")
local new = require("../../tools/cli/commands/new")
local projectlib = require("../../tools/cli/project")

-- Distinct from `branding/icon/luaug.ico` in every byte, which is the whole
-- point: the assertion at the end is that the artifact does NOT carry the
-- engine's icon, and a fixture that resembled it could make that pass by
-- accident.
local IconFixture = "tests/identity/game.ico"

local function repositoryRoot(): string
    local here = string.match(debug.info(1, "s"), "^(.*)[/\\][^/\\]+$") or "tests/packaging"
    return projectlib.joined(here, "..", "..")
end

local function removeTree(target: string)
    if not fs.exists(target) then
        return
    end
    -- `fs.remove` is for FILES: on Windows it refuses a directory outright, so
    -- a hand-rolled walk removes the leaves and then fails on the first branch.
    fs.removeDirectory(target, { recursive = true })
end

--- A scaffolded project with an icon of its own, under a directory this test
--- owns. `new.run` rather than a hand-copied template, because the scaffolder
--- is one of the things under test -- D045 was exactly a `luaug new` that could
--- not find its own template, and nothing noticed for two milestones.
local function scaffold(name: string): (string, string)
    local root = repositoryRoot()
    local parent = projectlib.joined(root, ".luaug-packaging-test")
    removeTree(parent)
    fs.createDirectory(parent, { makeParents = true })

    local created = new.run({ name = name, template = "starter", parent = parent })
    assert.eq(created, 0)

    local project = projectlib.joined(parent, name)
    fs.copy(projectlib.joined(root, IconFixture), projectlib.joined(project, "assets", "icon.ico"))

    local config = projectlib.joined(project, "luaug.toml")
    local text = fs.readFileToString(config)
    text = string.gsub(text, '# icon = "assets/icon.ico"', 'icon = "assets/icon.ico"', 1)
    fs.writeStringToFile(config, text)

    return parent, project
end

test.suite("packaging", function(suite)
    suite:case("a built folder runs, and carries the game's icon rather than the engine's", function()
        if not platform.win32 then
            -- Nothing to check and nothing broken: `--target win64` is the only
            -- target v1 builds, and `luaug build` refuses to produce it here.
            return
        end

        local root = repositoryRoot()
        local parent, project = scaffold("packagetest")

        -- **`--dev-host`, deliberately, and it is not a workaround.** What this
        -- suite tests is the packaging CHAIN -- scaffold, build, run, read the
        -- icon back -- on whatever host the gate just built, which is the dev
        -- one. That the packaged binary must be the `player` profile is a
        -- different claim with a different owner: the gate builds and links
        -- that profile on every run, and `luaug build` refuses it by default,
        -- which is the assertion below.
        -- **The refusal, first, because it is the one that protects a release**
        -- (D057). Without a player host and without the flag, `luaug build` has
        -- to fail rather than quietly package a binary carrying the debug
        -- overlay, the inspector and a REPL against its own VM.
        --
        -- Conditional on there being no player host, and it says which: on a
        -- machine where somebody has built `win-msvc-player` the command is
        -- SUPPOSED to succeed, and a test that failed there would be testing
        -- the machine. A silent skip would read as coverage this does not have.
        if projectlib.findPlayerEngine() == nil then
            local refused =
                build.run({ path = project, output = nil, target = "win64", force = false, devHost = false })
            assert.eq(refused, 1)
        else
            print("a player host exists on this machine, so the refusal path was not exercised")
        end

        local result = build.run({ path = project, output = nil, target = "win64", force = false, devHost = true })
        assert.eq(result, 0)

        local output = projectlib.joined(project, "dist", "win64")
        local executable = projectlib.joined(output, "packagetest.exe")

        -- The shape, before anything is run. Each of these is a thing a player
        -- needs and a build could plausibly forget: the binary, the engine's own
        -- content, the game, and the game's project file inside it.
        assert.that(fs.exists(executable), `no executable at {executable}`)
        assert.that(fs.exists(projectlib.joined(output, "content", "i18n", "en.json")), "no engine catalog")
        assert.that(fs.exists(projectlib.joined(output, "content", "shaders")), "no engine shaders")
        assert.that(fs.exists(projectlib.joined(output, "game", "luaug.toml")), "no project file in the game")
        assert.that(fs.exists(projectlib.joined(output, "game", "src", "scripts", "main.luau")), "no entry script")

        -- **No arguments.** That is the claim: a packaged game is a folder whose
        -- executable mounts the `game/` directory beside it, so double-clicking
        -- it plays. `--headless --frames --exit` is only how a test watches it
        -- do that without a window.
        local ran = process.run({ executable, "--headless", "--frames=30", "--exit" })
        local output_text = ran.stdout .. ran.stderr
        assert.that(ran.ok, `the packaged game exited non-zero:\n{output_text}`)
        -- The template's own first line, which proves the GAME ran rather than
        -- the engine merely starting.
        assert.that(
            string.find(output_text, "Hello from LuauG", 1, true) ~= nil,
            `the packaged game did not run its script:\n{output_text}`
        )

        -- The identity, read back out of the artifact rather than looked at.
        local iconpatch = projectlib.findTool("iconpatch")
        assert.that(iconpatch ~= nil, "iconpatch was not found; build it or set LUAUG_ICONPATCH")
        local verified = process.run({
            iconpatch :: string,
            "verify",
            executable,
            "--not",
            projectlib.joined(root, "branding", "icon", "luaug.ico"),
        })
        assert.that(verified.ok, `the artifact is still wearing the engine's icon:\n{verified.stdout}{verified.stderr}`)

        removeTree(parent)
    end)
end)
