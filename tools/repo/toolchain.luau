--!strict
--[[
	Toolchains built from source and redistributed with the editor (ADR 0091).

	A row under `toolchains` in `third_party/manifest.json` is a tool this
	project builds itself, at a pinned commit, outside the repository -- and
	ships. DXC is the one there is: the compiler a user's surface shader is
	compiled with on the user's own machine. Its prebuilt releases carry
	Microsoft terms and so cannot be redistributed (ADR 0032); its source carries
	none, and a build of it signs its own DXIL (U-64).

	**Nothing an ordinary build does compiles LLVM.** The engine's own shaders
	keep the fetched prebuilt compiler (ADR 0032). This builds once per version
	and platform, into

		$LUAUG_BUILD_ROOT/toolchains/<name>-source/<version>/<platform>/
		  src/        the checkout, at the pinned commit and submodules
		  build/      CMake's tree
		  bin/        what ships: the compiler library and the command-line tool
		  licenses/   the licence texts the row names, shipped beside it
		  STAMP       the commit it was built from

	and `tools/repo/package.luau` copies `bin/` and `licenses/` into the editor
	folder. CI's `dxc-source` job runs the same command and caches the result by
	commit.

	**The pin is git's hashes.** The checkout must be the row's commit, and every
	submodule the row names must be at its SHA, or nothing is built: a tag can be
	moved upstream, a commit cannot.

	    lute tools/repo/toolchain.luau status
	    lute tools/repo/toolchain.luau build <name> [--jobs=N] [--platform=TAG]
	    lute tools/repo/toolchain.luau path <name>

	On Windows it needs the same Developer Shell the engine's presets need: `cl`,
	`cmake` and `ninja` on PATH.
]]

local fs = require("@std/fs")
local json = require("@std/json")
local platform = require("@std/system/platform")
local process = require("@std/process")

local ManifestPath = "third_party/manifest.json"

type Toolchain = {
    name: string,
    version: string,
    commit: string,
    upstream: string,
    submodules: { [string]: string },
    licenseFiles: { string },
}

local function fail(message: string): never
    print(`toolchain: {message}`)
    return process.exit(1)
end

local function joined(...: string): string
    return table.concat({ ... }, "/")
end

-- Set by `--platform=`, which CI passes: a runner knows its architecture, and
-- the environment variables below are not exported everywhere.
local PlatformOverride: string? = nil

local function platformTag(): string
    if PlatformOverride then
        return PlatformOverride :: string
    end
    local arch = if string.find(string.lower(process.env.PROCESSOR_ARCHITECTURE or process.env.HOSTTYPE or ""), "arm")
        then "arm64"
        else "x64"
    if platform.win32 then
        return `windows-{arch}`
    elseif platform.macos then
        return `macos-{arch}`
    end
    return `linux-{arch}`
end

local function readToolchains(): { Toolchain }
    if not fs.exists(ManifestPath) then
        fail(`{ManifestPath} not found -- run this from the repository root`)
    end
    local root = json.asObject(json.deserialize(fs.readFileToString(ManifestPath)))
    if not root then
        fail(`{ManifestPath} is not a JSON object`)
    end
    local rows = json.asArray((root :: json.Object).toolchains)
    local result: { Toolchain } = {}
    if not rows then
        return result
    end
    for index, entry in rows :: json.Array do
        local row = json.asObject(entry)
        if not row then
            fail(`{ManifestPath}: toolchain #{index} is not an object`)
        end
        local object = row :: json.Object
        local function text(key: string): string
            local value = object[key]
            if typeof(value) ~= "string" then
                fail(`{ManifestPath} toolchain #{index}: "{key}" must be a string`)
            end
            return value :: string
        end
        local submodules: { [string]: string } = {}
        local declared = json.asArray(object.submodules)
        if declared then
            for submoduleIndex, submoduleEntry in declared :: json.Array do
                local submodule = json.asObject(submoduleEntry)
                local path = if submodule then (submodule :: json.Object).path else nil
                local sha = if submodule then (submodule :: json.Object).commit else nil
                if typeof(path) ~= "string" or typeof(sha) ~= "string" or #(sha :: string) ~= 40 then
                    fail(`{ManifestPath} toolchain #{index} submodule #{submoduleIndex}: a path and a full SHA`)
                end
                submodules[path :: string] = sha :: string
            end
        end
        local licenseFiles: { string } = {}
        local files = json.asArray(object.licenseFiles)
        if files then
            for _, file in files :: json.Array do
                table.insert(licenseFiles, tostring(file))
            end
        end
        local commit = text("commit")
        if #commit ~= 40 or not string.match(commit, "^%x+$") then
            fail(`{ManifestPath} toolchain #{index}: "commit" must be a full SHA`)
        end
        table.insert(result, {
            name = text("name"),
            version = text("version"),
            commit = commit,
            upstream = text("upstream"),
            submodules = submodules,
            licenseFiles = licenseFiles,
        })
    end
    return result
end

local function find(name: string): Toolchain
    for _, toolchain in readToolchains() do
        if toolchain.name == name then
            return toolchain
        end
    end
    return fail(`no toolchain row named {name} in {ManifestPath}`)
end

local function home(toolchain: Toolchain): string
    local buildRoot = process.env.LUAUG_BUILD_ROOT
    if buildRoot == nil or buildRoot == "" then
        fail("LUAUG_BUILD_ROOT is not set (rule R14): run scripts/bootstrap first")
    end
    return joined(buildRoot :: string, "toolchains", `{toolchain.name}-source`, toolchain.version, platformTag())
end

local function run(argv: { string }, what: string, cwd: string?): string
    local result = process.run(argv, if cwd then { cwd = cwd } else nil)
    if not result.ok then
        -- Ninja and CMake say what went wrong on stdout, not stderr: the tail
        -- of both, so a failure in CI can be read without a rerun.
        local stdout = result.stdout
        if #stdout > 6000 then
            stdout = "..." .. string.sub(stdout, -6000)
        end
        fail(`{what} failed (exit {result.exitcode})\n{stdout}\n{result.stderr}`)
    end
    return result.stdout
end

--- What ships from DXC's build: the library a compile goes through and the
--- command-line tool, named the way this platform names them.
local function dxcOutputs(): { string }
    if platform.win32 then
        return { "bin/dxcompiler.dll", "bin/dxc.exe" }
    elseif platform.macos then
        return { "lib/libdxcompiler.dylib", "bin/dxc" }
    end
    return { "lib/libdxcompiler.so", "bin/dxc" }
end

local function installed(toolchain: Toolchain): boolean
    local stamp = joined(home(toolchain), "STAMP")
    return fs.exists(stamp) and string.find(fs.readFileToString(stamp), toolchain.commit, 1, true) ~= nil
end

local function checkout(toolchain: Toolchain, source: string)
    if not fs.exists(source) then
        run({
            "git",
            "clone",
            "--quiet",
            "--depth",
            "1",
            "--branch",
            toolchain.version,
            "--recurse-submodules",
            "--shallow-submodules",
            toolchain.upstream,
            source,
        }, `cloning {toolchain.name} {toolchain.version}`)
    end
    -- **The hashes, every one**: the tag the clone followed could have moved.
    local head = string.gsub(run({ "git", "rev-parse", "HEAD" }, "reading the checkout's commit", source), "%s+", "")
    if head ~= toolchain.commit then
        fail(`{source} is at {head}, and the manifest pins {toolchain.commit}. Delete it and build again.`)
    end
    local status = run({ "git", "submodule", "status" }, "reading the submodules", source)
    for submodule, sha in toolchain.submodules do
        local found = string.match(status, `%s?[-+ ]?(%x+) {string.gsub(submodule, "%-", "%%-")}`)
        if found ~= sha then
            fail(`{submodule} is at {found or "nothing"}, and the manifest pins {sha}`)
        end
    end
end

local function buildDxc(toolchain: Toolchain, jobs: string)
    local root = home(toolchain)
    local source = joined(root, "src")
    local build = joined(root, "build")
    fs.createDirectory(root, { makeParents = true })
    checkout(toolchain, source)

    print(`toolchain: building {toolchain.name} {toolchain.version} into {root}`)
    local started = os.clock()
    local configure = {
        "cmake",
        "-S",
        source,
        "-B",
        build,
        "-G",
        "Ninja",
        "-C",
        joined(source, "cmake", "caches", "PredefinedParams.cmake"),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DLLVM_ENABLE_ASSERTIONS=OFF",
        "-DHLSL_INCLUDE_TESTS=OFF",
        "-DLLVM_INCLUDE_TESTS=OFF",
        "-DCLANG_INCLUDE_TESTS=OFF",
        "-DSPIRV_BUILD_TESTS=OFF",
        "-DHLSL_BUILD_DXILCONV=OFF",
    }
    -- The LLVM inside DXC specialises `std::is_nothrow_constructible`, which
    -- Apple's newer libc++ refuses as an error; the diagnostic is turned off
    -- for that platform alone.
    if platform.macos then
        table.insert(configure, "-DCMAKE_CXX_FLAGS=-Wno-invalid-specialization")
    end
    run(configure, "configuring DXC (on Windows, from a Developer Shell)")
    run({ "ninja", "-C", build, `-j{jobs}`, "dxc" }, "building DXC")

    local bin = joined(root, "bin")
    fs.createDirectory(bin, { makeParents = true })
    for _, output in dxcOutputs() do
        local from = joined(build, output)
        if not fs.exists(from) then
            fail(`the build finished without {output}`)
        end
        local name = string.match(output, "[^/]+$") :: string
        fs.copy(from, joined(bin, name))
    end
    local licenses = joined(root, "licenses")
    fs.createDirectory(licenses, { makeParents = true })
    for _, file in toolchain.licenseFiles do
        local from = joined(source, file)
        if not fs.exists(from) then
            fail(`the manifest names licence file {file}, and the checkout has none`)
        end
        fs.copy(from, joined(licenses, file))
    end
    fs.writeStringToFile(joined(root, "STAMP"), `{toolchain.commit}\n`)
    print(`toolchain: {toolchain.name} built in {math.floor(os.clock() - started)} s of CPU on this thread`)
end

local function main(): number
    local positional: { string } = {}
    local jobs = "8"
    for index, argument in process.args do
        local value = string.match(argument, "^%-%-jobs=(%d+)$")
        local chosen = string.match(argument, "^%-%-platform=([%w%-]+)$")
        if value then
            jobs = value
        elseif chosen then
            PlatformOverride = chosen
        elseif not (index == 1 and string.sub(argument, -5) == ".luau") and string.sub(argument, 1, 2) ~= "--" then
            table.insert(positional, argument)
        end
    end
    local command = positional[1]
    local name = positional[2]

    if command == "status" then
        for _, toolchain in readToolchains() do
            local state = if installed(toolchain) then "built" else "not built"
            print(
                `{toolchain.name} {toolchain.version} ({string.sub(toolchain.commit, 1, 12)}): {state} -- {home(
                    toolchain
                )}`
            )
        end
        return 0
    elseif command == "path" and name then
        local toolchain = find(name)
        if not installed(toolchain) then
            print(`toolchain: {name} is not built here; run: lute tools/repo/toolchain.luau build {name}`)
            return 1
        end
        print(home(toolchain))
        return 0
    elseif command == "build" and name then
        local toolchain = find(name)
        if installed(toolchain) then
            print(`toolchain: {name} {toolchain.version} is already built at {home(toolchain)}`)
            return 0
        end
        if name ~= "dxc" then
            fail(`no build recipe for {name}`)
        end
        buildDxc(toolchain, jobs)
        return 0
    end
    print("usage: lute tools/repo/toolchain.luau status | build <name> [--jobs=N] | path <name>")
    return 2
end

return process.exit(main())
