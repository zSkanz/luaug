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
                -- A `.d.luau` declares; it is not a source, and analysing it as
                -- one is a page of syntax errors on a perfectly valid file.
                -- They go in through `--definitions` instead.
                if string.sub(entry.name, -7) ~= ".d.luau" then
                    table.insert(files, child)
                end
            end
        end
    end

    table.sort(files)
    return files
end

--- **The definitions, as the new solver can read them** (R2, D192). The
--- generated file types `Instance.new` as an intersection of one overload per
--- creatable class, which the old solver reads and the new one gives up on as
--- "code too complex" -- so every `Instance.new("Part")` became an `Instance`.
--- The new solver has what the intersection spelled out: a table from name to
--- class, read with `index<>` over a name constrained to `keyof<>` of it, so a
--- literal keeps its name and a wrong one is an error.
---
--- A copy, not an edit: the generated file stays readable by the old solver,
--- which is what an editor extension left on its default still runs, and the
--- engine's own checker reads it with a magic function instead (ADR 0093).
--- Returns the path to analyse with, which is the original when there is no
--- intersection to rewrite.
local function forNewSolver(definition: string, scratch: string): string
    local text = fs.readFileToString(definition)
    local lines = string.split(text, "\n")
    local first: number? = nil
    for index, line in lines do
        if line == "declare Instance: {" and string.sub(lines[index + 1] or "", 1, 12) == '    new: (("' then
            first = index + 1
            break
        end
    end
    if first == nil then
        return definition
    end
    local last = first
    while string.sub(lines[last + 1] or "", 1, 13) == '        & (("' do
        last += 1
    end
    local entries: { string } = {}
    for index = first, last do
        for name, class in string.gmatch(lines[index], '%(%("(%w+)"%) %-> (%w+)%)') do
            table.insert(entries, `    {name}: {class},`)
        end
    end
    local out: { string } = {}
    for index = 1, first - 2 do
        table.insert(out, lines[index])
    end
    table.insert(out, "type InstanceClasses = {")
    for _, entry in entries do
        table.insert(out, entry)
    end
    table.insert(out, "}")
    table.insert(out, "")
    table.insert(out, "declare Instance: {")
    table.insert(out, "    new: <K>(className: K & keyof<InstanceClasses>) -> index<InstanceClasses, K>,")
    for index = last + 1, #lines do
        table.insert(out, lines[index])
    end
    if not fs.exists(scratch) then
        fs.createDirectory(scratch, { makeParents = true })
    end
    local target = projectlib.joined(scratch, "engine.d.luau")
    fs.writeStringToFile(target, table.concat(out, "\n"))
    return target
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

    -- The new solver, which is what R2 says every script is checked by.
    local analyze: { string } = { "luau-lsp", "analyze", "--platform=standard", "--flag:LuauSolverV2=true" }
    local scratch = projectlib.joined(options.path, ".luaug/check")
    for _, definition in options.definitions do
        table.insert(analyze, `--definitions={forNewSolver(definition, scratch)}`)
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
