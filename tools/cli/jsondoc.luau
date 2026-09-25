--!strict
--[[
	An order-preserving JSON document, for tools that EDIT a file a person
	reviews as a diff (`luaug migrate`).

	`@std/json` decodes into tables, which forgets key order and turns every
	number into a double -- so a rewrite through it moves every key and
	reprints every number, and the one change a migration made is lost in a
	diff of the whole file. This keeps each object's keys in the order they
	were read and each number and string as the text it was written as, and
	prints in the layout the file already had: compact, or indented by the
	width its second line uses.
]]

export type Node =
    { kind: "object", keys: { string }, values: { [string]: Node } }
    | { kind: "array", items: { Node } }
    | { kind: "string", value: string, raw: string? }
    | { kind: "number", raw: string }
    | { kind: "bool", value: boolean }
    | { kind: "null" }

export type Layout = { indent: number }

local JsonDoc = {}

local function fail(text: string, at: number, what: string): never
    local line = 1
    for _ in string.gmatch(string.sub(text, 1, at), "\n") do
        line += 1
    end
    error(`json: {what} at line {line}`, 0)
end

local function skipSpace(text: string, at: number): number
    local nextAt = string.find(text, "[^ \t\r\n]", at)
    return nextAt or #text + 1
end

local Escapes: { [string]: string } = {
    ['"'] = '"',
    ["\\"] = "\\",
    ["/"] = "/",
    b = "\b",
    f = "\f",
    n = "\n",
    r = "\r",
    t = "\t",
}

local function readString(text: string, at: number): (string, string, number)
    -- `at` is the opening quote.
    local out: { string } = {}
    local index = at + 1
    while true do
        local char = string.sub(text, index, index)
        if char == "" then
            fail(text, at, "an unterminated string")
        end
        if char == '"' then
            break
        end
        if char == "\\" then
            local escape = string.sub(text, index + 1, index + 1)
            if escape == "u" then
                local hex = string.sub(text, index + 2, index + 5)
                local code = tonumber(hex, 16)
                if code == nil then
                    fail(text, index, "a bad \\u escape")
                end
                table.insert(out, utf8.char(code :: number))
                index += 6
            else
                local plain = Escapes[escape]
                if plain == nil then
                    fail(text, index, "a bad escape")
                end
                table.insert(out, plain)
                index += 2
            end
        else
            table.insert(out, char)
            index += 1
        end
    end
    return table.concat(out), string.sub(text, at, index), index + 1
end

local function readValue(text: string, at: number): (Node, number)
    at = skipSpace(text, at)
    local char = string.sub(text, at, at)
    if char == "{" then
        local keys: { string } = {}
        local values: { [string]: Node } = {}
        at = skipSpace(text, at + 1)
        if string.sub(text, at, at) == "}" then
            return { kind = "object", keys = keys, values = values }, at + 1
        end
        while true do
            at = skipSpace(text, at)
            if string.sub(text, at, at) ~= '"' then
                fail(text, at, "a key that is not a string")
            end
            local key, _, afterKey = readString(text, at)
            at = skipSpace(text, afterKey)
            if string.sub(text, at, at) ~= ":" then
                fail(text, at, "a missing ':'")
            end
            local value, afterValue = readValue(text, at + 1)
            if values[key] == nil then
                table.insert(keys, key)
            end
            values[key] = value
            at = skipSpace(text, afterValue)
            local separator = string.sub(text, at, at)
            if separator == "}" then
                return { kind = "object", keys = keys, values = values }, at + 1
            end
            if separator ~= "," then
                fail(text, at, "a missing ',' in an object")
            end
            at += 1
        end
    elseif char == "[" then
        local items: { Node } = {}
        at = skipSpace(text, at + 1)
        if string.sub(text, at, at) == "]" then
            return { kind = "array", items = items }, at + 1
        end
        while true do
            local value, afterValue = readValue(text, at)
            table.insert(items, value)
            at = skipSpace(text, afterValue)
            local separator = string.sub(text, at, at)
            if separator == "]" then
                return { kind = "array", items = items }, at + 1
            end
            if separator ~= "," then
                fail(text, at, "a missing ',' in an array")
            end
            at += 1
        end
    elseif char == '"' then
        local value, raw, afterString = readString(text, at)
        return { kind = "string", value = value, raw = raw }, afterString
    elseif string.sub(text, at, at + 3) == "true" then
        return { kind = "bool", value = true }, at + 4
    elseif string.sub(text, at, at + 4) == "false" then
        return { kind = "bool", value = false }, at + 5
    elseif string.sub(text, at, at + 3) == "null" then
        return { kind = "null" }, at + 4
    end
    local first, last = string.find(text, "^-?%d+%.?%d*[eE]?[-+]?%d*", at)
    if first == nil then
        fail(text, at, "an unexpected character")
    end
    return { kind = "number", raw = string.sub(text, first :: number, last :: number) }, (last :: number) + 1
end

--- Parses a document, and the layout it was written in: compact, or indented
--- by the width of its second line.
function JsonDoc.parse(text: string): (Node, Layout)
    local node, after = readValue(text, 1)
    if skipSpace(text, after) <= #text then
        fail(text, after, "trailing text")
    end
    local indent = 0
    local secondLine = string.match(text, "\n( +)")
    if secondLine ~= nil then
        indent = #secondLine
    end
    return node, { indent = indent }
end

local function quote(text: string): string
    local out = string.gsub(text, '[%c"\\]', function(char: string): string
        if char == '"' then
            return '\\"'
        elseif char == "\\" then
            return "\\\\"
        elseif char == "\n" then
            return "\\n"
        elseif char == "\r" then
            return "\\r"
        elseif char == "\t" then
            return "\\t"
        end
        return string.format("\\u%04x", string.byte(char))
    end)
    return `"{out}"`
end

local function print(node: Node, layout: Layout, depth: number, out: { string })
    local indent = layout.indent
    local function newline(level: number)
        if indent > 0 then
            table.insert(out, "\n" .. string.rep(" ", indent * level))
        end
    end
    if node.kind == "object" then
        if #node.keys == 0 then
            table.insert(out, "{}")
            return
        end
        table.insert(out, "{")
        for index, key in node.keys do
            if index > 1 then
                table.insert(out, ",")
            end
            newline(depth + 1)
            table.insert(out, quote(key))
            table.insert(out, if indent > 0 then ": " else ":")
            print(node.values[key], layout, depth + 1, out)
        end
        newline(depth)
        table.insert(out, "}")
    elseif node.kind == "array" then
        if #node.items == 0 then
            table.insert(out, "[]")
            return
        end
        table.insert(out, "[")
        for index, item in node.items do
            if index > 1 then
                table.insert(out, ",")
            end
            newline(depth + 1)
            print(item, layout, depth + 1, out)
        end
        newline(depth)
        table.insert(out, "]")
    elseif node.kind == "string" then
        table.insert(out, node.raw or quote(node.value))
    elseif node.kind == "number" then
        table.insert(out, node.raw)
    elseif node.kind == "bool" then
        table.insert(out, if node.value then "true" else "false")
    else
        table.insert(out, "null")
    end
end

--- The document as text in `layout`, with the trailing newline an indented
--- file carries.
function JsonDoc.print(node: Node, layout: Layout): string
    local out: { string } = {}
    print(node, layout, 0, out)
    if layout.indent > 0 then
        table.insert(out, "\n")
    end
    return table.concat(out)
end

-- Constructors, so a caller never spells a node's shape by hand.

function JsonDoc.object(): Node
    return { kind = "object", keys = {}, values = {} }
end

function JsonDoc.array(items: { Node }): Node
    return { kind = "array", items = items }
end

function JsonDoc.string(value: string): Node
    return { kind = "string", value = value }
end

function JsonDoc.number(value: number): Node
    -- `%.17g` round-trips a double; a whole number prints as one.
    local text = if value == math.floor(value) and math.abs(value) < 1e15
        then string.format("%d", value)
        else string.format("%.17g", value)
    return { kind = "number", raw = text }
end

--- Sets `key` on an object, appending it when it is new and keeping its place
--- when it is not.
function JsonDoc.set(object: Node, key: string, value: Node)
    if object.kind ~= "object" then
        return
    end
    if object.values[key] == nil then
        table.insert(object.keys, key)
    end
    object.values[key] = value
end

--- Sets `key` at position `index` of an object's key order (clamped), or in
--- place when it is already there.
function JsonDoc.insert(object: Node, index: number, key: string, value: Node)
    if object.kind ~= "object" then
        return
    end
    if object.values[key] == nil then
        table.insert(object.keys, math.clamp(index, 1, #object.keys + 1), key)
    end
    object.values[key] = value
end

function JsonDoc.remove(object: Node, key: string)
    if object.kind ~= "object" or object.values[key] == nil then
        return
    end
    object.values[key] = nil
    local index = table.find(object.keys, key)
    if index ~= nil then
        table.remove(object.keys, index)
    end
end

function JsonDoc.get(object: Node, key: string): Node?
    if object.kind ~= "object" then
        return nil
    end
    return object.values[key]
end

--- A number node's value, or nil.
function JsonDoc.numberOf(node: Node?): number?
    if node == nil or node.kind ~= "number" then
        return nil
    end
    return tonumber(node.raw)
end

function JsonDoc.stringOf(node: Node?): string?
    if node == nil or node.kind ~= "string" then
        return nil
    end
    return node.value
end

return JsonDoc
