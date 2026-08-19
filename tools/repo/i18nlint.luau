--!strict
--[[
	i18n lint (ADR 0019, rule R3).

	M0 scope -- the "stub" the roadmap asks for: every key named by a
	LUAUG_TR("...") in engine sources must exist in `i18n/en.json`.

	That check is what makes the four-byte TextKey design safe. A TextKey
	carries only a hash, so the catalog doubles as the hash -> name registry;
	if a key were missing from the catalog the engine could not name it, and a
	message would degrade to `[i18n:missing:xxxxxxxx]` at runtime. This turns
	that into a build failure instead.

	M3 extends this to the second half of R3 -- rejecting hardcoded
	user-facing string literals in C++ and CLI Luau (roadmap M3 gate). That is
	deliberately NOT attempted here: it needs the API/doc-key generator to know
	which strings are user-facing.

	    lute tools/repo/i18nlint.luau
]]

local fs = require("@std/fs")
local json = require("@std/json")
local path = require("@std/path")
local process = require("@std/process")

local CATALOG_PATH = "i18n/en.json"
local SCAN_ROOTS = { "engine" }
local SCAN_SUFFIXES = { ".cpp", ".h" }

-- Test sources are skipped on purpose. They exercise the catalog machinery
-- against local fixture catalogs using deliberately synthetic keys ("greeting",
-- "nope.not.here"); those are not engine messages and must not be shipped in
-- en.json. Tests that assert on REAL keys load the real catalog and check the
-- resolved message, which is the stronger check anyway.
local SKIP_DIRECTORIES = { tests = true }

local function fail(message: string): never
    print(`i18nlint: ERROR: {message}`)
    return process.exit(1)
end

local function hasSuffix(text: string, suffix: string): boolean
    return #text >= #suffix and string.sub(text, -#suffix) == suffix
end

local function catalogKeys(): { [string]: boolean }
    if not fs.exists(CATALOG_PATH) then
        fail(`{CATALOG_PATH} not found — run this from the repository root`)
    end

    local decoded = json.deserialize(fs.readFileToString(CATALOG_PATH))
    local root = json.asObject(decoded)
    if not root then
        fail(`{CATALOG_PATH} is not a JSON object`)
    end

    local keys: { [string]: boolean } = {}
    for key in root :: json.Object do
        -- @std/json tags decoded objects with a `newproxy()` sentinel key, so
        -- iteration yields a userdata alongside the real keys.
        -- `$`-prefixed entries are file metadata, not messages.
        if typeof(key) == "string" and string.sub(key, 1, 1) ~= "$" then
            keys[key] = true
        end
    end
    return keys
end

type Usage = {
    key: string,
    file: string,
}

local function collectUsages(): { Usage }
    local usages: { Usage } = {}

    -- Deliberately not fs.walk: its iterator performs a yielding libuv call
    -- internally, and driving it from a generic `for` fails with "attempt to
    -- yield across metamethod/C-call boundary" on Lute 1.0.0. listDirectory
    -- returns a plain array, so an explicit traversal has no such boundary.
    local pending: { string } = {}
    for _, root in SCAN_ROOTS do
        if fs.exists(root) then
            table.insert(pending, root)
        end
    end

    while #pending > 0 do
        local dir = table.remove(pending) :: string

        for _, entry in fs.listDirectory(dir) do
            local childPath = path.format(path.join(dir, entry.name))

            if entry.type == "dir" then
                if not SKIP_DIRECTORIES[entry.name] then
                    table.insert(pending, childPath)
                end
                continue
            end
            if entry.type ~= "file" then
                continue
            end

            local matched = false
            for _, suffix in SCAN_SUFFIXES do
                if hasSuffix(childPath, suffix) then
                    matched = true
                    break
                end
            end
            if not matched then
                continue
            end

            local source = fs.readFileToString(childPath)
            -- Matches the macro's only supported form: a string literal, so
            -- the key is always statically visible to this lint.
            for key in string.gmatch(source, 'LUAUG_TR%(%s*"([^"]*)"%s*%)') do
                table.insert(usages, { key = key, file = childPath })
            end
        end
    end

    return usages
end

local function main()
    local keys = catalogKeys()
    local usages = collectUsages()

    local missing: { Usage } = {}
    local used: { [string]: boolean } = {}

    for _, usage in usages do
        used[usage.key] = true
        if not keys[usage.key] then
            table.insert(missing, usage)
        end
    end

    for _, usage in missing do
        print(`i18nlint: {usage.file}: key "{usage.key}" is not in {CATALOG_PATH}`)
    end

    -- Unused catalog entries are reported but do not fail: a key may be
    -- referenced from Luau or staged ahead of the code that raises it.
    local unused = 0
    for key in keys do
        if not used[key] then
            unused += 1
        end
    end

    local total = 0
    for _ in keys do
        total += 1
    end

    print(`i18nlint: {#usages} references, {total} catalog keys, {unused} unreferenced`)

    if #missing > 0 then
        fail(`{#missing} key(s) referenced but missing from {CATALOG_PATH}`)
    end
end

main()
