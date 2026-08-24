--!strict
--[[
	The editor's distribution folder (ADR 0054).

	**The product is a folder**, exactly as it is for a packaged game (ADR 0045)
	and for the same reason: the host resolves its content directory beside its
	own executable, so a directory is a working installation and a copy of one is
	another working installation. `scripts/package.ps1` builds the profiles and
	compresses what this writes.

	The layout, and every part of it is something a command reaches for:

		LuauG-<version>-<platform>/
		  luaug.cmd  luaug.ps1  luaug.sh   the CLI, as a person types it
		  lute                             the pinned interpreter that runs it
		  luaug-host                       the editor: profile `editor`
		  content/                         what that host resolves beside itself
		  player/luaug-host                what `luaug build` packages
		  player/content/                  and what THAT host resolves
		  assetc  iconpatch                the tools `build` and `build-assets` run
		  tools/cli/                       the CLI itself
		  templates/                       what `luaug new` copies
		  runtime/types/engine.d.luau      what it copies into a new project
		  VERSION                          the stamp `--version` reads
		  LICENSE  NOTICE  THIRD_PARTY_NOTICES.md

	**Two hosts rather than one, and that is D057 rather than caution.** What a
	person edits with carries the debug overlay, the inspector and a REPL against
	its own VM; what their players receive must not. A package with one binary
	would make `luaug build` ship the editor inside somebody's game.

	**It packages for the platform it runs on and cannot do otherwise**, because
	the binaries it copies are named the way that platform names them. There is
	no target flag and no cross build to get wrong.

	    lute tools/repo/package.luau [--out=DIR] [--build-root=DIR]
	                                 [--editor-preset=NAME] [--player-preset=NAME]
]]

local fs = require("@std/fs")
local path = require("@std/path")
local platform = require("@std/system/platform")
local process = require("@std/process")

local Failures: { string } = {}

local function fail(message: string)
    table.insert(Failures, message)
end

local function joined(...: string): string
    return path.format(path.join(...))
end

local function executableName(base: string): string
    return if platform.win32 then `{base}.exe` else base
end

--- The platform tag in the folder's name. Not derived from a target, because
--- there is no target: it is the machine this is running on.
local function platformTag(): string
    if platform.win32 then
        return "win64"
    end
    return if platform.macos then "macos" else "linux"
end

local function parseFlags(): { [string]: string }
    local flags: { [string]: string } = {}
    for index, argument in process.args do
        if index == 1 and string.sub(argument, -5) == ".luau" then
            continue
        end
        if string.sub(argument, 1, 2) ~= "--" then
            continue
        end
        local body = string.sub(argument, 3)
        local equals = string.find(body, "=", 1, true)
        if equals ~= nil then
            flags[string.sub(body, 1, equals - 1)] = string.sub(body, equals + 1)
        else
            flags[body] = "true"
        end
    end
    return flags
end

--- The version, from the one declaration ADR 0031 makes authoritative. The
--- stamp this writes into the folder is derived from it here, at the moment the
--- binary built from it is copied, rather than typed anywhere.
local function declaredVersion(): string?
    if not fs.exists("CMakeLists.txt") then
        return nil
    end
    return string.match(fs.readFileToString("CMakeLists.txt"), "project%s*%(%s*LuauG%s+VERSION%s+([%d%.]+)")
end

--- A version as comparable numbers. `nil` for anything that is not `X.Y.Z`,
--- which is what keeps a tag like `milestone/e1` out of the comparison below.
local function ordered(text: string): { number }?
    local major, minor, patch = string.match(text, "^(%d+)%.(%d+)%.(%d+)$")
    if major == nil then
        return nil
    end
    return { tonumber(major) :: number, tonumber(minor) :: number, tonumber(patch) :: number }
end

local function newer(left: { number }, right: { number }): boolean
    for index = 1, 3 do
        if left[index] ~= right[index] then
            return left[index] > right[index]
        end
    end
    return false
end

--- The newest `vX.Y.Z` tag in this repository, or nothing.
---
--- **D086 is why this is here.** `v1.0.0` was tagged and released while
--- `project(LuauG VERSION ...)` still said 0.0.1, and everything downstream
--- reported 0.0.1 faithfully -- the generated header, `luaug --version`, the api
--- dump, the engine's boot line. ADR 0031's derivation was never the problem;
--- what had no owner was the declaration. Checked HERE because packaging is the
--- moment the number stops being internal: it becomes the name of a folder
--- somebody downloads.
local function newestReleaseTag(): { number }?
    local listed = process.run({ "git", "tag", "--list", "v*" })
    if not listed.ok then
        return nil
    end
    local newest: { number }? = nil
    for line in string.gmatch(listed.stdout, "[^\r\n]+") do
        local parsed = ordered((string.gsub(line, "^v", "")))
        if parsed ~= nil and (newest == nil or newer(parsed, newest)) then
            newest = parsed
        end
    end
    return newest
end

local function copyTree(from: string, to: string, skip: { [string]: boolean }?)
    fs.createDirectory(to, { makeParents = true })
    for _, entry in fs.listDirectory(from) do
        if skip ~= nil and skip[entry.name] then
            continue
        end
        local source = joined(from, entry.name)
        local target = joined(to, entry.name)
        if entry.type == "dir" then
            copyTree(source, target, skip)
        elseif entry.type == "file" then
            -- `fs.copy` rather than a text round trip: this carries a font, an
            -- interpreter and three executables, and reading them as strings
            -- would corrupt all five.
            fs.copy(source, target)
        end
    end
end

local function removeTree(target: string)
    if fs.exists(target) then
        fs.removeDirectory(target, { recursive = true })
    end
end

--- One file into the folder, refusing rather than skipping when it is not
--- there. Every caller below is copying something a command will reach for, so
--- a missing one is an installation that fails later and further away.
local function copyFile(from: string, to: string, what: string): boolean
    if not fs.exists(from) then
        fail(`{what}: not found at {from}`)
        return false
    end
    fs.createDirectory(path.dirname(to), { makeParents = true })
    fs.copy(from, to)
    return true
end

local function copyRequiredTree(from: string, to: string, what: string, skip: { [string]: boolean }?): boolean
    if not fs.exists(from) then
        fail(`{what}: not found at {from}`)
        return false
    end
    copyTree(from, to, skip)
    return true
end

--- A host binary and the content directory it resolves beside itself. The pair
--- is what makes a folder runnable, so they are copied together or not at all.
local function copyHost(buildDirectory: string, target: string, what: string)
    local name = executableName("luaug-host")
    if not copyFile(joined(buildDirectory, name), joined(target, name), what) then
        return
    end
    copyRequiredTree(joined(buildDirectory, "content"), joined(target, "content"), `{what} content`)
end

--- The wrappers, written here rather than copied from `scripts/`, because those
--- resolve the repository from their own location and this layout has no
--- `scripts` directory to be inside. Same one line of work, a different tree.
local Wrappers: { { name: string, text: string } } = {
    {
        name = "luaug.cmd",
        text = "@echo off\r\n"
            .. "REM `luaug`, as a person types it. See tools/repo/package.luau for the layout.\r\n"
            .. 'setlocal\r\n"%~dp0lute.exe" run "%~dp0tools\\cli\\main.luau" -- %*\r\n'
            .. "exit /b %ERRORLEVEL%\r\n",
    },
    {
        name = "luaug.ps1",
        text = "# `luaug`, as a person types it. See tools/repo/package.luau for the layout.\n"
            .. "& (Join-Path $PSScriptRoot 'lute.exe') run (Join-Path $PSScriptRoot 'tools/cli/main.luau') -- @args\n"
            .. "exit $LASTEXITCODE\n",
    },
    {
        name = "luaug.sh",
        text = "#!/usr/bin/env bash\n"
            .. "# `luaug`, as a person types it. See tools/repo/package.luau for the layout.\n"
            .. 'here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"\n'
            .. 'exec "$here/lute" run "$here/tools/cli/main.luau" -- "$@"\n',
    },
}

local function run(): number
    if not fs.exists("CMakeLists.txt") or not fs.exists("third_party/manifest.json") then
        print("package: run this from the repository root")
        return 2
    end

    local flags = parseFlags()
    local version = declaredVersion()
    if version == nil then
        print("package: no project(LuauG VERSION ...) declaration in CMakeLists.txt")
        return 2
    end

    local declared = ordered(version)
    local released = newestReleaseTag()
    if declared ~= nil and released ~= nil and newer(released, declared) then
        print(
            `package: CMakeLists.txt declares {version}, and this repository has already released `
                .. `v{released[1]}.{released[2]}.{released[3]}. Bump the declaration (D086).`
        )
        return 2
    end

    local buildRoot = flags["build-root"] or process.env.LUAUG_BUILD_ROOT
    if buildRoot == nil or buildRoot == "" then
        print("package: LUAUG_BUILD_ROOT is not set and no --build-root was given (rule R14)")
        return 2
    end

    local editorPreset = flags["editor-preset"] or (if platform.win32 then "win-msvc-editor" else "linux-clang-editor")
    local playerPreset = flags["player-preset"] or (if platform.win32 then "win-msvc-player" else "linux-clang-player")

    local name = `LuauG-{version}-{platformTag()}`
    local out = flags.out or joined(buildRoot, "package")
    local target = joined(out, name)

    removeTree(target)
    fs.createDirectory(target, { makeParents = true })

    -- The editor, which is what the folder is for.
    copyHost(joined(buildRoot, editorPreset, "engine", "app"), target, "the editor host")
    -- The player, under its own directory with its own content, so that
    -- `luaug build` can find a binary that carries no overlay (D057).
    copyHost(joined(buildRoot, playerPreset, "engine", "app"), joined(target, "player"), "the player host")

    -- The two tools the CLI shells out to. `assetc` keeps a content directory of
    -- its own when the build produced one.
    for _, tool in { "assetc", "iconpatch" } do
        local from = joined(buildRoot, editorPreset, "tools", tool, executableName(tool))
        if copyFile(from, joined(target, executableName(tool)), tool) then
            local content = joined(buildRoot, editorPreset, "tools", tool, "content")
            if fs.exists(content) then
                copyTree(content, joined(target, "content"))
            end
        end
    end

    -- The interpreter that runs the CLI, taken from the one running this: a
    -- `lute` found on PATH instead would be whichever one happened to be first,
    -- and the pinned version is the whole point of pinning it.
    local lute = tostring(process.execPath())
    local luteName = executableName("lute")
    if copyFile(lute, joined(target, luteName), "the pinned Lute") then
        -- Verified rather than assumed, because the thing that could go wrong
        -- here is silent: a package manager's shim copies as a file, runs on the
        -- machine that has the real binary behind it, and does nothing anywhere
        -- else.
        local probe = process.run({ joined(target, luteName), "--version" })
        if not probe.ok then
            fail(`the copied Lute does not run: {joined(target, luteName)}`)
        end
    end

    -- The CLI, its template, and the definitions `luaug new` puts in a project.
    -- `tests/` is left out: it drives the CLI through Lute's test runner, which
    -- an installation has no reason to carry.
    copyRequiredTree("tools/cli", joined(target, "tools", "cli"), "the CLI", { tests = true })
    copyRequiredTree("templates", joined(target, "templates"), "the project template")
    copyRequiredTree("runtime/types", joined(target, "runtime", "types"), "the engine definitions")

    -- The attributions this repository already generates and already audits.
    for _, file in { "LICENSE", "NOTICE", "THIRD_PARTY_NOTICES.md" } do
        copyFile(file, joined(target, file), file)
    end

    for _, wrapper in Wrappers do
        fs.writeStringToFile(joined(target, wrapper.name), wrapper.text)
    end

    fs.writeStringToFile(
        joined(target, "VERSION"),
        `{version}\n`
            .. "\nWritten by tools/repo/package.luau from the project(LuauG VERSION ...)"
            .. "\ndeclaration in CMakeLists.txt, at the moment the binaries built from it"
            .. "\nwere copied. `luaug --version` reads the first line.\n"
    )

    if #Failures > 0 then
        -- The folder is left where it is. Half a package on disk with a list of
        -- what is missing beside it is easier to act on than a clean directory
        -- and a sentence, and nothing downstream reads this without being told.
        print(`package: {#Failures} thing(s) missing, and the folder is incomplete:`)
        for _, message in Failures do
            print(`  {message}`)
        end
        return 1
    end

    print(`package: wrote {target}`)
    return 0
end

return process.exit(run())
