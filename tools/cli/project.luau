--!strict
--[[
	Finding the project and finding the engine.

	Both are "look in the obvious places and say so when they are not there".
	The alternative -- guessing -- produces the failure this milestone keeps
	designing against: a command that runs, reports success, and did nothing.
]]

local fs = require("@std/fs")
local path = require("@std/path")
local platform = require("@std/system/platform")
local process = require("@std/process")

local toml = require("./toml")

export type Project = {
    root: string,
    -- Parsed `luaug.toml`, or an empty table when the project has none. A
    -- project without one is legal -- `src/scripts` is what makes a directory a
    -- project -- and every default below is what api-design.md §4 documents.
    config: toml.Table,
    diagnostic: string?,
}

local DefaultDevPort = 4560

local function joined(...: string): string
    return path.format(path.join(...))
end

--- Walks up from `start` looking for a `luaug.toml` or a `src/scripts`. Returns
--- `start` itself when neither is found, so the caller reports "not a project"
--- against the directory the user actually named.
local function findRoot(start: string): string
    local current = path.format(path.resolve(start))
    for _ = 1, 32 do
        if fs.exists(joined(current, "luaug.toml")) or fs.exists(joined(current, "src", "scripts")) then
            return current
        end
        local parent = path.dirname(current)
        if parent == current or parent == "" then
            break
        end
        current = parent
    end
    return path.format(path.resolve(start))
end

local function load(start: string): Project
    local root = findRoot(start)
    local configPath = joined(root, "luaug.toml")

    if not fs.exists(configPath) then
        return { root = root, config = {} }
    end

    local parsed = toml.parse(fs.readFileToString(configPath))
    if not parsed.ok then
        return {
            root = root,
            config = {},
            diagnostic = `{configPath}:{parsed.line}: {parsed.diagnostic}`,
        }
    end
    return { root = root, config = parsed.table }
end

--- Whether this directory is something the engine can mount at all: a project
--- file, or the entry-script tree that makes one.
local function isProject(project: Project): boolean
    return fs.exists(joined(project.root, "luaug.toml")) or fs.exists(joined(project.root, "src", "scripts"))
end

local function devPort(project: Project): number
    local dev = project.config.dev
    if typeof(dev) == "table" and typeof(dev.port) == "number" then
        return dev.port
    end
    return DefaultDevPort
end

local function executableName(base: string): string
    return if platform.win32 then `{base}.exe` else base
end

--- Every directory `luaug-host` could reasonably be, in the order worth trying.
---
--- `LUAUG_HOST` first, because a developer who set it means it. Then the build
--- tree, since inside this repository that is where it always is (R14 puts it
--- outside the source tree, so the path comes from `LUAUG_BUILD_ROOT`). Then
--- beside this CLI, which is the shape an installed release has.
local function engineCandidates(): { string }
    local name = executableName("luaug-host")
    local candidates: { string } = {}

    local explicit = process.env.LUAUG_HOST
    if explicit ~= nil and explicit ~= "" then
        table.insert(candidates, explicit)
    end

    local buildRoot = process.env.LUAUG_BUILD_ROOT
    if buildRoot ~= nil and buildRoot ~= "" then
        for _, preset in { "win-msvc-dev", "linux-clang-dev", "macos-clang-dev", "win-msvc-debug" } do
            table.insert(candidates, joined(buildRoot, preset, "engine", "app", name))
        end
    end

    table.insert(candidates, joined(path.format(process.cwd()), name))
    return candidates
end

local function findEngine(): string?
    for _, candidate in engineCandidates() do
        if fs.exists(candidate) then
            return candidate
        end
    end
    return nil
end

--- The same search, for a host TOOL rather than for the engine: `assetc` and
--- anything else `tools/` builds. Kept as one function because "where did the
--- build put it" has exactly one answer and two copies of it would drift the
--- first time a preset was renamed.
local function findTool(base: string): string?
    local name = executableName(base)

    local explicit = process.env[`LUAUG_{string.upper(base)}`]
    if explicit ~= nil and explicit ~= "" and fs.exists(explicit) then
        return explicit
    end

    local buildRoot = process.env.LUAUG_BUILD_ROOT
    if buildRoot ~= nil and buildRoot ~= "" then
        for _, preset in { "win-msvc-dev", "linux-clang-dev", "macos-clang-dev", "win-msvc-debug" } do
            local candidate = joined(buildRoot, preset, "tools", base, name)
            if fs.exists(candidate) then
                return candidate
            end
        end
    end

    local beside = joined(path.format(process.cwd()), name)
    if fs.exists(beside) then
        return beside
    end
    return nil
end

--- Every directory under `root` that holds Luau the engine mounts or requires,
--- deepest last. One watch per directory, because `fs.watch` reports the
--- top-level entry rather than the changed file and inotify may not report a
--- subdirectory's contents at all (U-58).
local function sourceDirectories(root: string): { string }
    local found: { string } = {}
    local pending: { string } = {}

    for _, name in { "src", "tests" } do
        local candidate = joined(root, name)
        if fs.exists(candidate) then
            table.insert(pending, candidate)
        end
    end

    while #pending > 0 do
        local dir = table.remove(pending) :: string
        table.insert(found, dir)
        for _, entry in fs.listDirectory(dir) do
            if entry.type == "dir" then
                table.insert(pending, joined(dir, entry.name))
            end
        end
    end

    table.sort(found)
    return found
end

--- Every `.luau` file under the project's source directories, with its content
--- hash, sorted by path. This is what a save is compared against: `fs.watch`
--- says only that *something* under a directory changed (U-58), so the watcher
--- rescans and diffs rather than trusting the event.
local function sourceSnapshot(root: string): { [string]: string }
    local snapshot: { [string]: string } = {}
    for _, dir in sourceDirectories(root) do
        for _, entry in fs.listDirectory(dir) do
            if entry.type == "file" and string.sub(entry.name, -5) == ".luau" then
                local file = joined(dir, entry.name)
                -- The content itself rather than a digest: a project's scripts
                -- are kilobytes, and comparing them directly cannot collide.
                snapshot[file] = fs.readFileToString(file)
            end
        end
    end
    return snapshot
end

local function changedFiles(before: { [string]: string }, after: { [string]: string }): { string }
    local changed: { string } = {}
    for file, contents in after do
        if before[file] ~= contents then
            table.insert(changed, file)
        end
    end
    for file in before do
        if after[file] == nil then
            table.insert(changed, file)
        end
    end
    table.sort(changed)
    return changed
end

return {
    load = load,
    isProject = isProject,
    devPort = devPort,
    findEngine = findEngine,
    findTool = findTool,
    executableName = executableName,
    sourceDirectories = sourceDirectories,
    sourceSnapshot = sourceSnapshot,
    changedFiles = changedFiles,
    joined = joined,
}
