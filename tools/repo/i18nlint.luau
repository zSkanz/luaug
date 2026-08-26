--!strict
--[[
	i18n lint (ADR 0019, rule R3).

	Two halves, and M3 is where the second one lands.

	**Every key is in a catalog.** A `TextKey` carries only a hash, so the
	catalog doubles as the hash -> name registry; a key missing from it would
	make the engine unable to name its own message and degrade it to
	`[i18n:missing:xxxxxxxx]` at runtime. This turns that into a build failure.
	`LUAUG_TR` is the sanctioned spelling of a key but not the only one that
	reaches a person -- the dev protocol puts the key NAME on the wire because
	the peer is what resolves it -- so this half sweeps key-SHAPED literals
	wherever they are written, not the macro alone (D139).

	**No user-facing string is written at a call site.** R3's other half, and
	the M3 gate's fourth item. It is a SINK lint rather than a literal lint (M3
	brief, Decision 8): a general "no string literals" rule would flag every
	key, path and format specifier in the engine and would be suppressed
	everywhere within a week. Instead the sinks a user-facing string can reach
	are named, and a raw literal in one is the failure.

	    lute tools/repo/i18nlint.luau
]]

local fs = require("@std/fs")
local json = require("@std/json")
local path = require("@std/path")
local process = require("@std/process")

local EngineCatalogPath = "i18n/en.json"
local CliCatalogPath = "tools/cli/i18n/en.json"

local EngineRoots = { "engine" }
local EngineSuffixes = { ".cpp", ".h" }
local CliRoot = "tools/cli"

-- Test sources are skipped on purpose. They exercise the catalog machinery
-- against local fixture catalogs using deliberately synthetic keys ("greeting",
-- "nope.not.here"); those are not engine messages and must not be shipped in
-- en.json. Tests that assert on REAL keys load the real catalog and check the
-- resolved message, which is the stronger check anyway.
local SkipDirectories = { tests = true }

--- Where a literal is legal, by `file:function`, each for a reason that is
--- written at the call site as well as here. Spelled as an explicit list so a
--- fourth entry is a decision somebody makes in this file.
local EngineExemptions: { [string]: string } = {
    -- If the catalog itself failed to load there is, by definition, nothing to
    -- translate through.
    ["engine/app/src/main.cpp:reportCatalogFailure"] = "no catalog has loaded yet",
}

--- The same idea on the CLI side. `print` writes to a person and must carry a
--- resolved message; machine output goes through `output.emit`, which is a
--- different function precisely so this lint can tell them apart.
local CliSinks = { "print" }

local CliExemptFiles: { [string]: boolean } = {
    -- The catalog loader's own failure path: same reason as the engine's.
    ["tools/cli/catalog.luau"] = true,
    -- Machine output by definition; see its own header.
    ["tools/cli/output.luau"] = true,
}

type Finding = {
    file: string,
    line: number,
    detail: string,
}

local function fail(message: string): never
    print(`i18nlint: ERROR: {message}`)
    return process.exit(1)
end

local function hasSuffix(text: string, suffix: string): boolean
    return #text >= #suffix and string.sub(text, -#suffix) == suffix
end

local function catalogKeys(catalogPath: string): { [string]: boolean }
    if not fs.exists(catalogPath) then
        fail(`{catalogPath} not found — run this from the repository root`)
    end

    local decoded = json.deserialize(fs.readFileToString(catalogPath))
    local root = json.asObject(decoded)
    if not root then
        fail(`{catalogPath} is not a JSON object`)
    end

    local keys: { [string]: boolean } = {}
    for key in root :: json.Object do
        -- `@std/json` tags decoded objects with a `newproxy()` sentinel key, so
        -- iteration yields a userdata alongside the real keys. `$`-prefixed
        -- entries are file metadata, not messages.
        if typeof(key) == "string" and string.sub(key, 1, 1) ~= "$" then
            keys[key] = true
        end
    end
    return keys
end

--- Every file under `root` whose name ends in one of `suffixes`.
---
--- Deliberately not `fs.walk`: its iterator performs a yielding libuv call
--- internally, and driving it from a generic `for` fails with "attempt to yield
--- across metamethod/C-call boundary" on Lute 1.0.0. `listDirectory` returns a
--- plain array, so an explicit traversal has no such boundary.
local function collectFiles(root: string, suffixes: { string }): { string }
    local found: { string } = {}
    local pending: { string } = {}
    if fs.exists(root) then
        table.insert(pending, root)
    end

    while #pending > 0 do
        local dir = table.remove(pending) :: string
        for _, entry in fs.listDirectory(dir) do
            local childPath = path.format(path.join(dir, entry.name))
            if entry.type == "dir" then
                if not SkipDirectories[entry.name] then
                    table.insert(pending, childPath)
                end
                continue
            end
            if entry.type ~= "file" then
                continue
            end
            for _, suffix in suffixes do
                if hasSuffix(childPath, suffix) then
                    table.insert(found, childPath)
                    break
                end
            end
        end
    end

    table.sort(found)
    return found
end

local function lineOf(source: string, offset: number): number
    local line = 1
    for _ in string.gmatch(string.sub(source, 1, offset), "\n") do
        line += 1
    end
    return line
end

--- The index just past the `)` matching the `(` at `open`, or nil.
local function matchingParen(source: string, open: number): number?
    local depth = 0
    local index = open
    local inString = false
    local inChar = false

    while index <= #source do
        local c = string.sub(source, index, index)
        if inString then
            if c == "\\" then
                index += 1
            elseif c == '"' then
                inString = false
            end
        elseif inChar then
            if c == "\\" then
                index += 1
            elseif c == "'" then
                inChar = false
            end
        elseif c == '"' then
            inString = true
        elseif c == "'" then
            inChar = true
        elseif c == "(" then
            depth += 1
        elseif c == ")" then
            depth -= 1
            if depth == 0 then
                return index
            end
        end
        index += 1
    end
    return nil
end

--- The C++ half.
---
--- The message argument of a sink must be a `LUAUG_TR` key. Only that argument
--- is checked, and only at its own nesting level: `makeError`'s third parameter
--- is `detail`, which `error.h` documents as developer context that is never
--- localised, and a literal inside a nested call -- `parse(config, ".luaurc")`
--- -- is an argument to that call rather than a message.
---
--- What it deliberately does NOT check: the VALUES interpolated into a message.
--- Those are data -- a path, a class name, a count, an OS error number -- and
--- are as untranslatable as the `{path}` they fill; `error.h` says the same of
--- `detail`. A first version tried to follow one level of indirection into
--- helpers that raise, and every finding it produced was a parameter value:
--- `checkRotationOrder(L, 2, "CFrame:ToEuler")` and `compareTraces(a, b, "run
--- 1", "run 2")` are both data by design. A helper that hides a whole SENTENCE
--- behind itself is a review matter rather than a lint one.
local SinkMessageArgument: { [string]: number } = {
    -- `log(level, key, args)` and `logText(level, text)`.
    log = 2,
    logText = 2,
    -- `makeError(key, args, detail)`.
    makeError = 1,
    -- `raise(L, key, args)`.
    raise = 2,
}

--- Splits a call's argument list at commas that are not inside a nested
--- construct. Returns each argument with the offset it started at.
local function topLevelArguments(source: string, open: number, close: number): { { text: string, at: number } }
    local arguments: { { text: string, at: number } } = {}
    local depth = 0
    local index = open + 1
    local startAt = index
    local inString = false
    local inChar = false

    while index < close do
        local c = string.sub(source, index, index)
        if inString then
            if c == "\\" then
                index += 1
            elseif c == '"' then
                inString = false
            end
        elseif inChar then
            if c == "\\" then
                index += 1
            elseif c == "'" then
                inChar = false
            end
        elseif c == '"' then
            inString = true
        elseif c == "'" then
            inChar = true
        elseif c == "(" or c == "{" or c == "[" then
            depth += 1
        elseif c == ")" or c == "}" or c == "]" then
            depth -= 1
        elseif c == "," and depth == 0 then
            table.insert(arguments, { text = string.sub(source, startAt, index - 1), at = startAt })
            startAt = index + 1
        end
        index += 1
    end

    if startAt < close then
        table.insert(arguments, { text = string.sub(source, startAt, close - 1), at = startAt })
    end
    return arguments
end

--- Whether `text` contains a string literal at its own nesting level.
local function hasTopLevelLiteral(text: string): boolean
    local depth = 0
    local index = 1
    while index <= #text do
        local c = string.sub(text, index, index)
        if c == "(" or c == "{" or c == "[" then
            depth += 1
        elseif c == ")" or c == "}" or c == "]" then
            depth -= 1
        elseif c == "'" then
            -- A character literal; skip it, escape included.
            index += if string.sub(text, index + 1, index + 1) == "\\" then 3 else 2
        elseif c == '"' then
            if depth == 0 then
                return true
            end
            -- Skip the literal.
            local scan = index + 1
            while scan <= #text do
                local inner = string.sub(text, scan, scan)
                if inner == "\\" then
                    scan += 1
                elseif inner == '"' then
                    break
                end
                scan += 1
            end
            index = scan
        end
        index += 1
    end
    return false
end

--- Calls to `name`, however it is qualified.
---
--- The excluded characters are `%w_` and `.` only. `:` must NOT be among them:
--- every sink in this engine is spelled `core::makeError`, and excluding the
--- colon meant the pattern matched nothing at all -- a lint that reported zero
--- findings because it was looking for a spelling nobody uses. `sinkCalls`
--- below exists so that the next version of that mistake fails loudly.
local SinkCallsSeen = { count = 0 }

local function eachCall(source: string, name: string, visit: (open: number, close: number, at: number) -> ())
    local index = 1
    while true do
        local start, finish = string.find(source, `[^%w_.]{name}%s*%(`, index)
        if start == nil then
            break
        end
        index = (finish :: number) + 1

        local open = string.find(source, "(", start :: number, true) :: number
        local close = matchingParen(source, open)
        if close ~= nil then
            SinkCallsSeen.count += 1
            visit(open, close, start :: number)
        end
    end
end

local function lintEngineFile(file: string, findings: { Finding })
    local source = fs.readFileToString(file)

    -- The enclosing function name, for the exemption list and for wrapper
    -- detection. The last definition seen above the offset, which is enough
    -- because every case here is a free function at file scope.
    local function enclosingFunction(at: number): string
        local name = ""
        -- `{` is not a magic character, so it is spelled plainly: `%{` is a
        -- pattern the linter rejects even though the matcher tolerates it.
        for start, candidate in string.gmatch(source, "()\n[%w_:<>&%*%s%[%]]-([%w_]+)%b()%s*\n?%s*{") do
            -- `gmatch` is typed as yielding optionals; a position capture
            -- and a `+` capture both always produce a value.
            if (start :: any) :: number > at then
                break
            end
            name = (candidate :: any) :: string
        end
        return name
    end

    local wrappers: { [string]: boolean } = {}

    for sink, position in SinkMessageArgument do
        eachCall(source, sink, function(open, close, at)
            local enclosing = enclosingFunction(at)
            if enclosing ~= "" then
                wrappers[enclosing] = true
            end
            if EngineExemptions[`{file}:{enclosing}`] ~= nil then
                return
            end

            local arguments = topLevelArguments(source, open, close)
            local message = arguments[position]
            if message ~= nil and hasTopLevelLiteral(message.text) then
                table.insert(findings, {
                    file = file,
                    line = lineOf(source, message.at),
                    detail = `{sink}(...) takes a raw string where a LUAUG_TR key belongs`,
                })
            end
        end)
    end
end

--- The CLI half: `print` may not carry a string literal. Machine output goes
--- through `output.emit` instead, which is a different function so that this
--- lint can tell a message to a person from a line of TAP.
local function lintCliFile(file: string, findings: { Finding })
    -- `listDirectory` joins with the platform separator, so an exemption keyed
    -- on `/` would silently stop matching on Windows -- which is the machine
    -- this gate is usually run from.
    if CliExemptFiles[(string.gsub(file, "\\", "/"))] then
        return
    end

    local source = fs.readFileToString(file)

    for _, sink in CliSinks do
        eachCall(source, sink, function(open, close, at)
            -- A `tr(...)` call is the resolved form. Removed WHOLE rather than
            -- by its key alone, because its parameters legitimately carry
            -- literals that are not prose -- a `", "` separator, a `"%.1f"`
            -- format -- and flagging those would push the message back to the
            -- call site to avoid the lint.
            local body = string.sub(source, open + 1, close - 1)
            local scan = 1
            while true do
                local trAt = string.find(body, "tr%s*%(", scan)
                if trAt == nil then
                    break
                end
                local trOpen = string.find(body, "(", trAt, true) :: number
                local trClose = matchingParen(body, trOpen)
                if trClose == nil then
                    break
                end
                body = string.sub(body, 1, trAt - 1) .. string.sub(body, trClose + 1)
                scan = trAt
            end

            if string.find(body, '"') ~= nil or string.find(body, "`") ~= nil then
                table.insert(findings, {
                    file = file,
                    line = lineOf(source, at),
                    detail = "print(...) carries a raw string; use tr(key) for a message, output.emit for machine text",
                })
            end
        end)
    end
end

--- Every string literal in `source`, each with the offset of its opening quote.
---
--- Comment-aware, and it has to be. This engine's comments are prose, prose has
--- apostrophes, and a scanner that read `the overlay's` as the start of a
--- character literal would run to the next apostrophe several paragraphs down
--- and be out of step with the file for the rest of it. Having to know where
--- the comments are answers the other question too: a key named in a comment is
--- documentation rather than something the program emits, so this sweep does
--- not look there.
---
--- Raw string literals are skipped whole. The only one in non-test engine code
--- is `R"({"op":")"` -- a JSON fragment with its own quotes in it -- and a
--- scanner that walked into it would come out the far side mismatched.
local function stringLiterals(source: string): { { text: string, at: number } }
    local literals: { { text: string, at: number } } = {}
    local size = #source
    local index = 1

    while index <= size do
        local c = string.sub(source, index, index)
        local ahead = string.sub(source, index + 1, index + 1)

        if c == "/" and ahead == "/" then
            local newline = string.find(source, "\n", index + 2, true)
            index = if newline == nil then size + 1 else newline + 1
        elseif c == "/" and ahead == "*" then
            local close = string.find(source, "*/", index + 2, true)
            index = if close == nil then size + 1 else close + 2
        elseif c == "'" and string.match(string.sub(source, index - 1, index - 1), "%x") == nil then
            -- A quote after a hex digit is C++'s DIGIT SEPARATOR, not a
            -- character literal: this engine writes `1'000'000'000.0` and
            -- `0x5741'4D50u`, and reading the first of those as an open quote
            -- put the scanner out of step from `frame_scheduler.cpp:32` to the
            -- end of the file -- silently, because a blind sweep reports a
            -- clean tree. Nothing here writes a prefixed literal (`u8'a'`,
            -- `L'a'`), which is the only other thing that puts a quote after
            -- an identifier character.
            index += 1
            while index <= size do
                local ch = string.sub(source, index, index)
                if ch == "\\" then
                    index += 2
                    continue
                end
                index += 1
                if ch == "'" then
                    break
                end
            end
        elseif c == "R" and ahead == '"' and string.match(string.sub(source, index - 1, index - 1), "[%w_]") == nil then
            -- `R"delim(` ... `)delim"`, delimiter included so the rare non-empty
            -- one closes where C++ says it closes.
            local open = string.find(source, "(", index + 2, true)
            if open == nil then
                index += 2
                continue
            end
            local delimiter = string.sub(source, index + 2, open - 1)
            local close = string.find(source, `){delimiter}"`, open + 1, true)
            index = if close == nil then size + 1 else close + #delimiter + 2
        elseif c == '"' then
            local scan = index + 1
            while scan <= size do
                local ch = string.sub(source, scan, scan)
                if ch == "\\" then
                    scan += 2
                    continue
                end
                if ch == '"' then
                    break
                end
                scan += 1
            end
            table.insert(literals, { text = string.sub(source, index + 1, scan - 1), at = index })
            index = scan + 1
        else
            index += 1
        end
    end

    return literals
end

--- The segment that makes a dotted literal a message key rather than a path.
local KeySpine: { [string]: boolean } = { err = true, warn = true, info = true }

--- Whether `text` is spelled the way this catalog spells a message key.
---
--- The test is `err`, `warn` or `info` as an INTERIOR dotted segment, every
--- segment lower-case and snake_case. That is the catalog's own spine: all but
--- a handful of the engine's keys carry it, and no literal in the engine that
--- is NOT a key is spelled that way.
---
--- Deliberately narrower than "dotted and lower-case", which is the rule a
--- first reading suggests and which would be useless here. The engine writes
--- `luaug.toml`, `init.luau`, `xxhash.h`, `graphics.bloom`, `project.name`,
--- `window.title` and `environment.brdf` -- file names, TOML paths and asset
--- ids -- and every one of them is that shape. A gate that cried about those
--- would be suppressed within the week, which is the same argument the M3
--- brief's Decision 8 makes about literals in general. The lower-case rule
--- earns its place the same way: `services.cpp` tells a caller it wanted
--- `Enum.KeyCode.Virtual1..Virtual4`, and a class or enum name is nobody's
--- catalog key.
---
--- The handful it gives up are the keys with no severity segment -- the boot
--- banner, the usage text, the window title. Every one of them is written
--- through `LUAUG_TR`, where `collectKeyUses` already has it, and widening the
--- shape to reach them would cost the precision that makes this sweep quiet
--- enough to keep.
local function looksLikeMessageKey(text: string): boolean
    local segments: { string } = {}
    for segment in string.gmatch(text, "[^.]+") do
        if string.match(segment, "^[a-z][a-z0-9_]*$") == nil then
            return false
        end
        table.insert(segments, segment)
    end

    -- Rejoining catches an empty segment, which `[^.]+` would otherwise drop
    -- and let `a..b.err.c` pass as four well-formed segments.
    if #segments < 3 or table.concat(segments, ".") ~= text then
        return false
    end

    for position = 2, #segments - 1 do
        if KeySpine[segments[position]] then
            return true
        end
    end
    return false
end

--- Guards the sweep the way `SinkCallsSeen` guards the other one: a shape test
--- that stopped matching would report a clean tree forever.
local KeyLiteralsSeen = { count = 0 }

--- Key-shaped literals written outside `LUAUG_TR`.
---
--- This is the shape that got `dev.err.not_implemented` past this gate from M3
--- until D139: the dev protocol answers a reserved verb with the key on a JSON
--- writer, and a field on a wire message is neither a macro use nor an argument
--- to a named sink. Being outside the macro is legal there and is not what is
--- reported -- naming a key that no catalog has is.
local function collectRawKeyLiterals(files: { string }): { { key: string, file: string, line: number } }
    local uses: { { key: string, file: string, line: number } } = {}
    for _, file in files do
        local source = fs.readFileToString(file)
        for _, literal in stringLiterals(source) do
            if not looksLikeMessageKey(literal.text) then
                continue
            end
            KeyLiteralsSeen.count += 1

            -- The macro's own argument is `collectKeyUses`'s to report;
            -- counting it here as well would name the same key twice.
            local before = string.sub(source, math.max(1, literal.at - 24), literal.at - 1)
            if string.find(before, "LUAUG_TR%s*%(%s*$") ~= nil then
                continue
            end

            table.insert(uses, { key = literal.text, file = file, line = lineOf(source, literal.at) })
        end
    end
    return uses
end

--- Every `LUAUG_TR("…")` in the engine, and every `tr("…")` in the CLI.
local function collectKeyUses(files: { string }, pattern: string): { { key: string, file: string } }
    local uses: { { key: string, file: string } } = {}
    for _, file in files do
        local source = fs.readFileToString(file)
        for key in string.gmatch(source, pattern) do
            table.insert(uses, { key = key, file = file })
        end
    end
    return uses
end

local function reportMissing(
    uses: { { key: string, file: string } },
    keys: { [string]: boolean },
    catalog: string
): number
    local missing = 0
    for _, use in uses do
        if not keys[use.key] then
            print(`i18nlint: {use.file}: key "{use.key}" is not in {catalog}`)
            missing += 1
        end
    end
    return missing
end

--- The same failure, said differently because the reader has to be told why a
--- plain string literal is being held to the catalog at all.
local function reportMissingRawKeys(
    uses: { { key: string, file: string, line: number } },
    keys: { [string]: boolean },
    catalog: string
): number
    local missing = 0
    for _, use in uses do
        if not keys[use.key] then
            print(`i18nlint: {use.file}:{use.line}: "{use.key}" is spelled like a message key and is not in {catalog}`)
            missing += 1
        end
    end
    return missing
end

local function main()
    local engineFiles: { string } = {}
    for _, root in EngineRoots do
        for _, file in collectFiles(root, EngineSuffixes) do
            table.insert(engineFiles, file)
        end
    end
    local cliFiles = collectFiles(CliRoot, { ".luau" })

    local engineKeys = catalogKeys(EngineCatalogPath)
    local cliKeys = catalogKeys(CliCatalogPath)

    local engineUses = collectKeyUses(engineFiles, 'LUAUG_TR%(%s*"([^"]*)"%s*%)')
    local cliUses = collectKeyUses(cliFiles, 'tr%(%s*"([^"]*)"')
    local engineRawKeys = collectRawKeyLiterals(engineFiles)

    local missing = reportMissing(engineUses, engineKeys, EngineCatalogPath)
        + reportMissing(cliUses, cliKeys, CliCatalogPath)
        + reportMissingRawKeys(engineRawKeys, engineKeys, EngineCatalogPath)

    local findings: { Finding } = {}
    for _, file in engineFiles do
        lintEngineFile(file, findings)
    end
    for _, file in cliFiles do
        lintCliFile(file, findings)
    end

    for _, finding in findings do
        print(`i18nlint: {finding.file}:{finding.line}: {finding.detail} (R3)`)
    end

    -- A key on the wire is a reference like any other, so it counts here or the
    -- unreferenced tally would name an entry that something does use.
    local used: { [string]: boolean } = {}
    for _, use in engineUses do
        used[use.key] = true
    end
    for _, use in cliUses do
        used[use.key] = true
    end
    for _, use in engineRawKeys do
        used[use.key] = true
    end

    local unused = 0
    local total = 0
    for key in engineKeys do
        total += 1
        if not used[key] then
            unused += 1
        end
    end
    for key in cliKeys do
        total += 1
        if not used[key] then
            unused += 1
        end
    end

    print(
        `i18nlint: {#engineUses + #cliUses + #engineRawKeys} references, {total} catalog keys, {unused} unreferenced, `
            .. `{#engineFiles + #cliFiles} file(s), {SinkCallsSeen.count} sink call(s) and `
            .. `{KeyLiteralsSeen.count} key literal(s) swept`
    )

    -- A sweep that matched nothing would report a clean tree forever, which is
    -- how this lint spent one revision looking for `makeError(` in an engine
    -- that only ever writes `core::makeError(`.
    if SinkCallsSeen.count == 0 then
        fail("the sweep matched no sink calls at all, so it proved nothing")
    end
    if KeyLiteralsSeen.count == 0 then
        fail("the sweep matched no key-shaped literals at all, so it proved nothing")
    end

    if missing > 0 then
        fail(`{missing} key(s) referenced but missing from a catalog`)
    end
    if #findings > 0 then
        fail(`{#findings} hardcoded user-facing string(s) — every one goes through a catalog (R3)`)
    end
end

main()
