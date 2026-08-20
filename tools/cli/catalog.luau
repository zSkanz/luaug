--!strict
--[[
	The CLI's message catalog (ADR 0019, rule R3).

	Named `catalog.luau` rather than `i18n.luau` because the catalog FILE lives
	at `i18n/en.json` beside it, and Luau's require refuses `../i18n` as
	ambiguous when a module and a directory share a name. The directory keeps
	the name api-design.md §6 gives it; the module takes the other one.

	Separate from the engine's `i18n/en.json` because the two ship separately
	and are translated separately (api-design.md §6). Keys are `cli.*`.

	The engine resolves keys in C++ against a catalog it loads at boot; this is
	the same idea with the same rule -- no user-facing string is written at a
	call site -- and about thirty lines, because a CLI has no plural forms and
	one locale.
]]

local fs = require("@std/fs")
local json = require("@std/json")
local process = require("@std/process")

local Catalog: { [string]: string }? = nil

--- Resolved relative to this file rather than to the working directory: the
--- CLI is run from whatever project a developer happens to be standing in.
local function catalogPath(): string
    local root = string.match(debug.info(1, "s"), "^(.*)[/\\][^/\\]+$")
    return `{root or "tools/cli"}/i18n/en.json`
end

local function load(): { [string]: string }
    if Catalog ~= nil then
        return Catalog
    end

    local path = catalogPath()
    if not fs.exists(path) then
        -- The one place a user-facing string may be hardcoded: if the catalog
        -- itself is missing there is, by definition, nothing to translate
        -- through. Kept to this single call site, exactly as the engine keeps
        -- its own to `main.cpp::reportCatalogFailure`.
        print(`luaug: cannot load the message catalog at {path}`)
        process.exit(3)
    end

    local decoded = json.deserialize(fs.readFileToString(path))
    local root = json.asObject(decoded)
    if root == nil then
        print(`luaug: the message catalog at {path} is not a JSON object`)
        process.exit(3)
    end

    local loaded: { [string]: string } = {}
    for key, value in root :: json.Object do
        -- `@std/json` tags decoded objects with a `newproxy()` sentinel key, so
        -- iteration yields a userdata alongside the real keys. `$`-prefixed
        -- entries are file metadata rather than messages.
        if typeof(key) == "string" and string.sub(key, 1, 1) ~= "$" and typeof(value) == "string" then
            loaded[key] = value
        end
    end

    Catalog = loaded
    return loaded
end

--- Resolves `key` and substitutes `{name}` placeholders from `params`.
---
--- A missing key echoes itself in brackets rather than raising. A CLI that
--- cannot print its own error message because the message was missing is worse
--- than one that prints the key -- and the i18n lint is what keeps it from
--- happening in the first place.
local function tr(key: string, params: { [string]: any }?): string
    local template = load()[key]
    if template == nil then
        return `[{key}]`
    end
    if params == nil then
        return template
    end
    return (
        string.gsub(template, "{(%w+)}", function(name: string): string
            local value = params[name]
            return if value == nil then `\{{name}}` else tostring(value)
        end)
    )
end

return {
    tr = tr,
}
