--!strict
--[[
	`luaug setup` -- write the typed stubs an editor resolves against (S5.19).

	**`.luaug/` is generated and gitignored**, which is right: it belongs to the
	engine version rather than to the project, and checking it in would be a copy
	of the engine's surface that goes stale in somebody's repository. What it
	also means is that a project CLONED from git has no types at all -- only the
	machine that ran `luaug new` ever had them -- so every `require("@luaug/…")`
	comes up unresolved in the editor, with no completion and no hover, while
	running perfectly. That is the worst shape for a beginner: the tooling says
	the line is wrong and the engine says it is fine.

	Two documents already named this command before it existed.
	`.vscode/settings.json` says "`luaug setup` regenerates it, and .luaug/types/,
	for whatever engine version is pinned", and the starter's own test says the
	editor resolves `@std` and `@luaug` "against the stubs `luaug setup`
	generates for the pinned engine". This is that command, and `luaug new` calls
	the same function rather than a second copy of it.

	**The modules themselves, not hand-written stubs.** They are `--!strict` Luau
	that exports its own types, so copying them copies the truth; a stub file
	would be a second declaration of one surface and would drift the first time
	either moved. It is the argument `engine.d.luau` already makes, applied to
	the modules beside it.

	**The engine's `@std/`, deliberately, and not Lute's.** A game script runs
	sandboxed (R4) and can reach `@std/net` and nothing else of that family;
	pointing the editor at Lute's typedefs would offer `fs`, `process` and `io`
	to a VM that has none of them -- the same mistake `settings.json` refuses for
	the Roblox API dump two comments higher up. The ANALYZER resolves `@std`
	separately through `.luaurc`, which is what makes a project's Lute-run tests
	work, and the starter's test says so in its own header.
]]

local fs = require("@std/fs")

local catalog = require("../catalog")
local projectlib = require("../project")

local tr = catalog.tr

export type Options = {
    -- The project to write into. Its `.luaug/types/` is replaced.
    target: string,
    -- The engine installation to copy from.
    root: string,
}

--- Copies a tree, creating what it needs. Files only; anything else is skipped.
local function copyTree(from: string, to: string)
    if not fs.exists(from) then
        return
    end
    fs.createDirectory(to, { makeParents = true })

    for _, entry in fs.listDirectory(from) do
        local source = projectlib.joined(from, entry.name)
        local target = projectlib.joined(to, entry.name)
        if entry.type == "dir" then
            copyTree(source, target)
        elseif entry.type == "file" then
            fs.writeStringToFile(target, fs.readFileToString(source))
        end
    end
end

--- Writes `.luaug/types/` for `options.target` from the engine at `options.root`.
---
--- Returns what it wrote, so a caller can say so rather than being silent about
--- work it did.
local function writeTypes(options: Options): { string }
    local types = projectlib.joined(options.target, ".luaug", "types")
    fs.createDirectory(types, { makeParents = true })

    local wrote: { string } = {}

    -- One file, and the hover documentation is inside it: the IDL's doc text is
    -- emitted as `---` comments above each declaration, so there is no second
    -- artifact to copy and none to keep in step.
    local dump = projectlib.joined(options.root, "runtime", "types", "engine.d.luau")
    if fs.exists(dump) then
        fs.writeStringToFile(projectlib.joined(types, "engine.d.luau"), fs.readFileToString(dump))
        table.insert(wrote, "engine.d.luau")
    end

    -- The two roots `.vscode/settings.json` has always aliased and nothing ever
    -- created.
    local modules = projectlib.joined(options.root, "runtime", "luaug")
    if fs.exists(modules) then
        copyTree(modules, projectlib.joined(types, "luaug"))
        table.insert(wrote, "luaug/")
    end
    local standard = projectlib.joined(options.root, "runtime", "std")
    if fs.exists(standard) then
        copyTree(standard, projectlib.joined(types, "std"))
        table.insert(wrote, "std/")
    end

    return wrote
end

local function run(options: Options): number
    if not fs.exists(projectlib.joined(options.target, "luaug.toml")) then
        -- Refused rather than writing `.luaug/` into whatever directory somebody
        -- happened to be standing in, which is the failure `luaug new` is
        -- written against and the same one applies here.
        print(tr("cli.setup.err.not_a_project"))
        return 1
    end

    local wrote = writeTypes(options)
    if #wrote == 0 then
        print(tr("cli.setup.err.nothing_to_write"))
        return 1
    end

    print(tr("cli.setup.wrote", { what = table.concat(wrote, ", ") }))
    return 0
end

return {
    run = run,
    writeTypes = writeTypes,
}
