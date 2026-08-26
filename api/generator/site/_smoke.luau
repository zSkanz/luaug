--!strict
local markdown = require("./markdown")

local options = {
    resolveSymbol = function(text: string): string?
        return if text == "Part" then "reference/Part.html" else nil
    end,
    resolveLink = function(target: string): string?
        return if string.match(target, "^api:") ~= nil then "reference/resolved.html" else nil
    end,
}

local Source = [==[
# Hello `Part`

A para with **bold**, *em*, a [link](api:Part.Shape) and `code`.

- one
- two

| A | B |
|---|---|
| `x \| y` | z |
]==]

print(markdown.render(Source, options))
