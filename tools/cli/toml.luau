--!strict
--[[
	A TOML subset reader for `luaug.toml` (M3 brief, Decision 9).

	Deliberately a subset, and deliberately here rather than in the engine. What
	`luaug.toml` actually holds is api-design.md §4's list -- a handful of named
	tables of scalars and string arrays -- and everything that reads it in M3 is
	this CLI. The engine gets a TOML reader in the milestone that first needs
	engine-side configuration, and not before: a second parser with no caller is
	the speculative abstraction MASTER_PROMPT §5 rejects.

	Supported: comments, bare and quoted keys, `[table]` and `[nested.table]`
	headers, strings (basic and literal), integers, floats, booleans, and
	single-line arrays of those. Not supported, and each an explicit error
	rather than a silent misread: multi-line strings, inline tables, arrays of
	tables, dates, and anything else. A config format that quietly ignores what
	it does not understand is how a `[dev] port` ends up not applying.
]]

export type Value = string | number | boolean | { Value }
export type Table = { [string]: any }

export type Result = {
    ok: boolean,
    table: Table,
    -- Developer-facing: a malformed config is a startup failure and never
    -- something a player sees, so this is exempt from R3 the same way the
    -- engine's JSON diagnostics are.
    diagnostic: string,
    line: number,
}

local function isSpace(c: string): boolean
    return c == " " or c == "\t"
end

local function trim(text: string): string
    return (string.gsub(string.gsub(text, "^[ \t]+", ""), "[ \t]+$", ""))
end

--- Splits a line at the first `#` that is not inside a string.
local function stripComment(line: string): string
    local inBasic = false
    local inLiteral = false
    local index = 1
    while index <= #line do
        local c = string.sub(line, index, index)
        if inBasic then
            if c == "\\" then
                index += 1
            elseif c == '"' then
                inBasic = false
            end
        elseif inLiteral then
            if c == "'" then
                inLiteral = false
            end
        elseif c == '"' then
            inBasic = true
        elseif c == "'" then
            inLiteral = true
        elseif c == "#" then
            return string.sub(line, 1, index - 1)
        end
        index += 1
    end
    return line
end

type Parsed = { value: any, rest: string, error: string? }

local parseValue: (text: string) -> Parsed

local function parseBasicString(text: string): Parsed
    local out = {}
    local index = 2
    while index <= #text do
        local c = string.sub(text, index, index)
        if c == "\\" then
            local escape = string.sub(text, index + 1, index + 1)
            if escape == "n" then
                table.insert(out, "\n")
            elseif escape == "t" then
                table.insert(out, "\t")
            elseif escape == "r" then
                table.insert(out, "\r")
            elseif escape == '"' or escape == "\\" then
                table.insert(out, escape)
            else
                return { value = nil, rest = text, error = `unsupported escape \\{escape}` }
            end
            index += 2
        elseif c == '"' then
            return { value = table.concat(out), rest = string.sub(text, index + 1) }
        else
            table.insert(out, c)
            index += 1
        end
    end
    return { value = nil, rest = text, error = "unterminated string" }
end

local function parseLiteralString(text: string): Parsed
    local closing = string.find(text, "'", 2, true)
    if closing == nil then
        return { value = nil, rest = text, error = "unterminated literal string" }
    end
    return { value = string.sub(text, 2, closing - 1), rest = string.sub(text, closing + 1) }
end

local function parseArray(text: string): Parsed
    local items: { any } = {}
    local rest = trim(string.sub(text, 2))

    if string.sub(rest, 1, 1) == "]" then
        return { value = items, rest = string.sub(rest, 2) }
    end

    while true do
        local parsed = parseValue(rest)
        if parsed.error ~= nil then
            return parsed
        end
        table.insert(items, parsed.value)

        rest = trim(parsed.rest)
        local head = string.sub(rest, 1, 1)
        if head == "]" then
            return { value = items, rest = string.sub(rest, 2) }
        end
        if head ~= "," then
            return { value = nil, rest = rest, error = "expected , or ] in an array" }
        end
        rest = trim(string.sub(rest, 2))
        -- A trailing comma before the bracket is legal TOML.
        if string.sub(rest, 1, 1) == "]" then
            return { value = items, rest = string.sub(rest, 2) }
        end
    end
end

function parseValue(text: string): Parsed
    local body = trim(text)
    local head = string.sub(body, 1, 1)

    if head == '"' then
        return parseBasicString(body)
    end
    if head == "'" then
        return parseLiteralString(body)
    end
    if head == "[" then
        return parseArray(body)
    end

    local token = string.match(body, "^[^,%]%s]+")
    if token == nil then
        return { value = nil, rest = body, error = "expected a value" }
    end
    local rest = string.sub(body, #token + 1)

    if token == "true" then
        return { value = true, rest = rest }
    end
    if token == "false" then
        return { value = false, rest = rest }
    end

    -- Underscores are legal digit separators in TOML numbers.
    local number = tonumber((string.gsub(token, "_", "")))
    if number ~= nil then
        return { value = number, rest = rest }
    end

    return { value = nil, rest = body, error = `not a value this reader supports: {token}` }
end

--- Splits `a.b.c` into its parts, honouring quoted segments.
local function splitKey(text: string): { string }?
    local parts: { string } = {}
    local index = 1
    local current = {}

    while index <= #text do
        local c = string.sub(text, index, index)
        if c == '"' or c == "'" then
            local parsed = if c == '"'
                then parseBasicString(string.sub(text, index))
                else parseLiteralString(string.sub(text, index))
            if parsed.error ~= nil then
                return nil
            end
            table.insert(current, parsed.value :: string)
            index = #text - #parsed.rest + 1
        elseif c == "." then
            table.insert(parts, table.concat(current))
            current = {}
            index += 1
        elseif isSpace(c) then
            index += 1
        else
            table.insert(current, c)
            index += 1
        end
    end

    table.insert(parts, table.concat(current))
    for _, part in parts do
        if part == "" then
            return nil
        end
    end
    return parts
end

local function descend(root: Table, parts: { string }): Table
    local node = root
    for _, part in parts do
        local child = node[part]
        if typeof(child) ~= "table" then
            child = {}
            node[part] = child
        end
        node = child :: Table
    end
    return node
end

--- Parses `text`. Never raises: a malformed file is a result with `ok = false`
--- and a diagnostic naming the line, because the caller is a CLI that has to
--- print something useful rather than a stack trace.
local function parse(text: string): Result
    local root: Table = {}
    local current = root
    local lineNumber = 0

    for rawLine in string.gmatch(text .. "\n", "([^\n]*)\n") do
        lineNumber += 1
        local line = trim(stripComment(rawLine))
        if line == "" then
            continue
        end

        if string.sub(line, 1, 2) == "[[" then
            return {
                ok = false,
                table = root,
                diagnostic = "arrays of tables are not supported in luaug.toml",
                line = lineNumber,
            }
        end

        if string.sub(line, 1, 1) == "[" then
            local closing = string.find(line, "]", 2, true)
            if closing == nil then
                return { ok = false, table = root, diagnostic = "unterminated table header", line = lineNumber }
            end
            local parts = splitKey(string.sub(line, 2, closing - 1))
            if parts == nil then
                return { ok = false, table = root, diagnostic = "malformed table header", line = lineNumber }
            end
            current = descend(root, parts)
            continue
        end

        local equals = string.find(line, "=", 1, true)
        if equals == nil then
            return { ok = false, table = root, diagnostic = "expected key = value", line = lineNumber }
        end

        local parts = splitKey(string.sub(line, 1, equals - 1))
        if parts == nil then
            return { ok = false, table = root, diagnostic = "malformed key", line = lineNumber }
        end

        local parsed = parseValue(string.sub(line, equals + 1))
        if parsed.error ~= nil then
            return { ok = false, table = root, diagnostic = parsed.error :: string, line = lineNumber }
        end
        if trim(parsed.rest) ~= "" then
            return {
                ok = false,
                table = root,
                diagnostic = `trailing text after the value: {trim(parsed.rest)}`,
                line = lineNumber,
            }
        end

        local leaf = table.remove(parts) :: string
        descend(current, parts)[leaf] = parsed.value
    end

    return { ok = true, table = root, diagnostic = "", line = 0 }
end

return {
    parse = parse,
}
