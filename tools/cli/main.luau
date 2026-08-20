--!strict
--[[
	`luaug` — the CLI (roadmap M3, api-design.md §4).

	Lute scripts run by the pinned `lute`, not a `lute compile`d binary (M3
	brief, Decision 2). `lute compile` is host-only as far as anyone has
	documented, so a compiled CLI would have to be produced per tier before any
	gate could run, and a stale binary would silently gate a fresh tree.
	Packaging is an M8 release task, and this file is one step away from it.

	`scripts/luaug.ps1` and `scripts/luaug.sh` are the wrappers that make the
	documented command line exist.
]]

local process = require("@std/process")

local catalog = require("./catalog")
local check = require("./commands/check")
local dev = require("./commands/dev")
local fmt = require("./commands/fmt")
local new = require("./commands/new")
local test = require("./commands/test")
local version = require("./commands/version")

local tr = catalog.tr

-- Vendored trees and Lute's own typedefs. The second does not typecheck under
-- the pinned luau-lsp for upstream reasons (M0 finding 2), and gating on it
-- would fail for something that is not ours to fix.
local DefaultIgnore = { "third_party", ".lute", "luau_packages", ".luaug" }

--- `process.args` is not what its name suggests: it carries the script path as
--- its first entry and keeps the `--` separator that was meant to end Lute's
--- own options (U-60). Both are dropped here, so every command below sees the
--- arguments a user actually typed.
local function userArguments(raw: { string }): { string }
    local arguments = table.clone(raw)
    if arguments[1] ~= nil and string.sub(arguments[1], -5) == ".luau" then
        table.remove(arguments, 1)
    end
    if arguments[1] == "--" then
        table.remove(arguments, 1)
    end
    return arguments
end

type Parsed = {
    command: string,
    positional: { string },
    flags: { [string]: string },
}

local function parse(argv: { string }): Parsed
    local parsed: Parsed = { command = "", positional = {}, flags = {} }

    for index, argument in argv do
        if index == 1 then
            parsed.command = argument
            continue
        end
        if string.sub(argument, 1, 2) == "--" then
            local body = string.sub(argument, 3)
            local equals = string.find(body, "=", 1, true)
            if equals ~= nil then
                parsed.flags[string.sub(body, 1, equals - 1)] = string.sub(body, equals + 1)
            else
                parsed.flags[body] = "true"
            end
            continue
        end
        table.insert(parsed.positional, argument)
    end

    return parsed
end

local function firstPositional(parsed: Parsed, fallback: string): string
    return parsed.positional[1] or fallback
end

local function rejectUnknownFlags(parsed: Parsed, allowed: { string }): boolean
    local known: { [string]: boolean } = {}
    for _, name in allowed do
        known[name] = true
    end
    for name in parsed.flags do
        if not known[name] then
            -- Refused rather than ignored. A misspelt `--headles` that silently
            -- did nothing is the same class of failure as a gate that passes
            -- while doing nothing.
            print(tr("cli.err.unknown_option", { option = name, command = parsed.command }))
            return false
        end
    end
    return true
end

local function main(): number
    local parsed = parse(userArguments(process.args))

    if parsed.command == "" or parsed.flags.help ~= nil or parsed.command == "--help" then
        print(tr("cli.usage"))
        return if parsed.command == "" then 2 else 0
    end

    -- Both spellings `--help` gets, and for the same reason: `--version` is
    -- about the tool rather than about a command, so it is answered before
    -- dispatch and never reaches `rejectUnknownFlags`. There is deliberately
    -- no bare `version` subcommand, because there is no bare `help` one.
    if parsed.command == "--version" or parsed.flags.version ~= nil then
        return version.run()
    end

    if parsed.command == "dev" then
        if not rejectUnknownFlags(parsed, { "headless", "port" }) then
            return 2
        end
        return dev.run({
            path = firstPositional(parsed, "."),
            headless = parsed.flags.headless ~= nil,
            port = if parsed.flags.port ~= nil then tonumber(parsed.flags.port) else nil,
        })
    end

    if parsed.command == "test" then
        if not rejectUnknownFlags(parsed, { "junit", "report" }) then
            return 2
        end
        return test.run({
            path = firstPositional(parsed, "tests/conformance"),
            junit = parsed.flags.junit,
            report = parsed.flags.report,
        })
    end

    if parsed.command == "check" then
        if not rejectUnknownFlags(parsed, { "definitions" }) then
            return 2
        end
        local definitions: { string } = {}
        if parsed.flags.definitions ~= nil then
            for entry in string.gmatch(parsed.flags.definitions, "[^,]+") do
                table.insert(definitions, entry)
            end
        end
        return check.run({
            path = firstPositional(parsed, "."),
            definitions = definitions,
            ignore = DefaultIgnore,
        })
    end

    if parsed.command == "fmt" then
        if not rejectUnknownFlags(parsed, {}) then
            return 2
        end
        return fmt.run({ path = firstPositional(parsed, "."), ignore = DefaultIgnore })
    end

    if parsed.command == "new" then
        if not rejectUnknownFlags(parsed, { "template" }) then
            return 2
        end
        local name = parsed.positional[1]
        if name == nil then
            print(tr("cli.usage"))
            return 2
        end
        return new.run({
            name = name,
            template = parsed.flags.template or "starter",
            parent = new.defaultParent(),
        })
    end

    print(tr("cli.err.unknown_command", { command = parsed.command }))
    return 2
end

process.exit(main())
