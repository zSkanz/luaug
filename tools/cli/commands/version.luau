--!strict
--[[
	`luaug --version` — advertised by `--help` since M3, answered here (M4
	carried debt).

	ADR 0031's rule is that a version is DERIVED from its one source of truth,
	never typed into a file where the two can drift: on the C++ side CMake
	expands `PROJECT_VERSION` into a generated `build_info.h` at configure
	time. The CLI is Lute scripts with no build step in front of them, so it
	cannot read that header; it reads the declaration the header is expanded
	FROM — `project(LuauG VERSION ...)` in the superbuild root. Same authority,
	no second copy. A literal in this file is precisely the failure ADR 0031
	exists to prevent, so there is none, and an unreadable declaration is an
	error rather than a guess.

	Lute's version is asked of the Lute that is running us. Nothing exposes it
	in-process: `_VERSION` is the string "Luau", and no module in the installed
	1.0.0 typedefs mentions a version at all (checked against the artifact, not
	against Lute's documentation). `--version` on `process.execPath()` is the
	only answer that is about the interpreter actually in use rather than about
	whatever `lute` happens to be first on PATH.
]]

local fs = require("@std/fs")
local pathlib = require("@std/path")
local process = require("@std/process")

local catalog = require("../catalog")

local tr = catalog.tr

--- A version and where it was read from. The source travels with the value so
--- a failure can name the file or executable that did not answer, which is the
--- difference between "no version" and a diagnosis.
export type Reading = {
    value: string?,
    source: string,
}

--- Resolved from this file rather than from the working directory: `luaug` is
--- run from whatever project a developer happens to be standing in, while the
--- `scripts/luaug.*` wrappers hand Lute an absolute path to `main.luau`, so
--- `debug.info` yields an absolute path here too. Normalised because this path
--- is also what a failure names, and three `..` segments in a diagnostic make
--- the reader do the engine's arithmetic.
local function cmakeListsPath(): string
    local here = string.match(debug.info(1, "s"), "^(.*)[/\\][^/\\]+$")
    return tostring(pathlib.normalize(`{here or "tools/cli/commands"}/../../../CMakeLists.txt`))
end

--- The LuauG version, read from the `project()` declaration CMake expands into
--- `LUAUG_VERSION_STRING`. `nil` when this is not a source tree.
local function luaug(): Reading
    local path = cmakeListsPath()
    if not fs.exists(path) then
        return { value = nil, source = path }
    end
    local declared = string.match(fs.readFileToString(path), "project%s*%(%s*LuauG%s+VERSION%s+([%d%.]+)")
    return { value = declared :: string?, source = path }
end

--- The version of the Lute interpreter running this script.
local function lute(): Reading
    local executable = tostring(process.execPath())
    local probe = process.run({ executable, "--version" })
    if not probe.ok then
        return { value = nil, source = executable }
    end
    -- Lute 1.0.0 answers with the bare version and a trailing newline. Only
    -- the first line is taken so a future Lute that adds a banner cannot fold
    -- a paragraph into a one-line `cli.version`.
    local reported = string.match(string.match(probe.stdout, "^[^\r\n]*") or "", "^%s*(.-)%s*$")
    if reported == nil or reported == "" then
        return { value = nil, source = executable }
    end
    return { value = reported :: string, source = executable }
end

local function run(): number
    local engine = luaug()
    if engine.value == nil then
        print(tr("cli.err.no_version", { path = engine.source }))
        return 3
    end

    local interpreter = lute()
    if interpreter.value == nil then
        print(tr("cli.err.no_lute_version", { path = interpreter.source }))
        return 3
    end

    print(tr("cli.version", { version = engine.value, lute = interpreter.value }))
    return 0
end

return {
    run = run,
    luaug = luaug,
    lute = lute,
}
