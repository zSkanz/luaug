--!strict
--[[
	`luaug check` — the one implementation of "is this tree clean".

	M3 brief, Decision 7: this is the implementation and
	`scripts/gates/luau-check.sh` calls it, rather than the two drifting until
	they disagree about whether the repository is green. Three checks, in the
	order that gives the most useful first failure: types, then formatting, then
	the i18n lint.
]]

local fs = require("@std/fs")
local process = require("@std/process")

local catalog = require("../catalog")
local projectlib = require("../project")

local tr = catalog.tr

export type Options = {
    path: string,
    -- Extra `.d.luau` files to load. The repository passes its generated engine
    -- definitions; a scaffolded project passes `.luaug/types/engine.d.luau`.
    definitions: { string },
    -- Directories to skip. Vendored trees and Lute's own typedefs, which do not
    -- typecheck under the pinned luau-lsp (M0 finding 2).
    ignore: { string },
}

local function collectLuau(root: string, ignore: { string }): { string }
    local files: { string } = {}
    local pending: { string } = { root }

    local function ignored(candidate: string): boolean
        for _, pattern in ignore do
            if string.find(candidate, pattern, 1, true) ~= nil then
                return true
            end
        end
        return false
    end

    while #pending > 0 do
        local dir = table.remove(pending) :: string
        for _, entry in fs.listDirectory(dir) do
            local child = projectlib.joined(dir, entry.name)
            if ignored(child) then
                continue
            end
            if entry.type == "dir" then
                table.insert(pending, child)
            elseif entry.type == "file" and string.sub(entry.name, -5) == ".luau" then
                table.insert(files, child)
            end
        end
    end

    table.sort(files)
    return files
end

local function toolMissing(tool: string): boolean
    local probe = process.run({ tool, "--version" }, { stdio = "none" })
    return not probe.ok
end

local function run(options: Options): number
    local files = collectLuau(options.path, options.ignore)
    if #files == 0 then
        -- Nothing to check is not the same as nothing wrong. A check that
        -- passes over an empty file list is the shape of gate this milestone
        -- keeps refusing to build (M2 Finding 19).
        print(tr("cli.check.err.analyze"))
        return 1
    end
    print(tr("cli.check.running", { count = #files }))

    for _, tool in { "luau-lsp", "stylua" } do
        if toolMissing(tool) then
            print(tr("cli.check.err.missing_tool", { tool = tool }))
            return 2
        end
    end

    local analyze: { string } = { "luau-lsp", "analyze", "--platform=standard" }
    for _, definition in options.definitions do
        table.insert(analyze, `--definitions={definition}`)
    end
    for _, pattern in options.ignore do
        table.insert(analyze, `--ignore=**/{pattern}/**`)
    end
    for _, file in files do
        table.insert(analyze, file)
    end

    local analyzed = process.run(analyze, { stdio = "inherit" })
    if not analyzed.ok then
        print(tr("cli.check.err.analyze"))
        return 1
    end

    local formatted = process.run({ "stylua", "--check", options.path }, { stdio = "inherit" })
    if not formatted.ok then
        print(tr("cli.check.err.stylua"))
        return 1
    end

    print(tr("cli.check.ok", { count = #files }))
    return 0
end

return {
    run = run,
    collectLuau = collectLuau,
}
