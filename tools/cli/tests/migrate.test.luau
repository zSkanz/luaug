--!strict
--[[
	`luaug migrate materials` (ADR 0090). The properties worth holding down:
	a plain part's tint becomes an override and nothing else in the file moves;
	a `Material` instance becomes an asset and a part wearing it keeps the look
	it drew; a stamp of a material becomes a material; a second run does
	nothing.
]]

local assert = require("@std/test/assert")
local test = require("@std/test")

local JsonDoc = require("../jsondoc")
local Migrate = require("../commands/migrate")

-- An indented scene the way the examples are written (one-space indent).
local function scene(children: string): string
    return `\{\n "format": "luaug-scene",\n "version": 1,\n "root": \{\n  "class": "Workspace",\n  "name": "Workspace",\n  "children": [\n{children}\n  ]\n }\n}\n`
end

test.suite("jsondoc", function(suite)
    suite:case("prints a document back as it was read", function()
        local text = '{\n "a": 1.0,\n "b": [\n  -0.5,\n  "x\\u00e9"\n ],\n "c": {}\n}\n'
        local node, layout = JsonDoc.parse(text)
        assert.eq(layout.indent, 1)
        assert.eq(JsonDoc.print(node, layout), text)

        local compact = '{"a":[1,2],"b":null,"c":true}'
        local node2, layout2 = JsonDoc.parse(compact)
        assert.eq(JsonDoc.print(node2, layout2), compact)
    end)
end)

test.suite("migrate materials", function(suite)
    suite:case("a plain part's tint becomes an override, and nothing else moves", function()
        local before = scene(
            '   {\n    "class": "Part",\n    "name": "Red",\n    "properties": {\n     "Anchored": true,\n     "Color": [\n      1.0,\n      0.0,\n      0.0\n     ],\n     "Transparency": 0.0\n    }\n   }'
        )
        local result = Migrate.materials({ ["scenes/main.scene.json"] = before })
        local after = result.writes["scenes/main.scene.json"]
        assert.that(after ~= nil)
        local expected = scene(
            '   {\n    "class": "Part",\n    "name": "Red",\n    "properties": {\n     "Anchored": true,\n     "MaterialParameters": {\n      "Color": [\n       1.0,\n       0.0,\n       0.0\n      ]\n     }\n    }\n   }'
        )
        expected = string.gsub(expected, '"version": 1', '"version": 2')
        assert.eq(after, expected)
    end)

    suite:case("a Material instance becomes an asset, and a part wearing it keeps what it drew", function()
        local before = scene(
            '   {"class": "Material", "name": "Brick", "properties": {"Color": [0.5, 0.5, 0.5], "Transparency": 0.5}},\n'
                .. '   {"class": "Part", "name": "Wall", "properties": {"Material": "Workspace.Brick", "Color": [1, 0, 0], "Transparency": 0.5}}'
        )
        local result = Migrate.materials({ ["scenes/main.scene.json"] = before })
        local asset = result.writes["materials/brick.material.json"]
        assert.that(asset ~= nil)
        -- The tint and the fade are declared, because the part used them.
        assert.neq(string.find(asset :: string, '"instanceParameters": ["Color", "Transparency"]', 1, true), nil)

        local doc = JsonDoc.parse(result.writes["scenes/main.scene.json"] :: string)
        local root = JsonDoc.get(doc, "root") :: JsonDoc.Node
        local children = JsonDoc.get(root, "children") :: JsonDoc.Node
        assert.eq(children.kind, "array")
        if children.kind ~= "array" then
            return
        end
        -- The material left the tree.
        assert.eq(#children.items, 1)
        local properties = JsonDoc.get(children.items[1], "properties") :: JsonDoc.Node
        assert.eq(JsonDoc.stringOf(JsonDoc.get(properties, "Material")), "asset://materials/brick.material.json")
        local parameters = JsonDoc.get(properties, "MaterialParameters") :: JsonDoc.Node
        -- Grey times red, and one minus the product of the two opacities.
        local colour = JsonDoc.get(parameters, "Color") :: JsonDoc.Node
        assert.eq(colour.kind, "array")
        if colour.kind == "array" then
            assert.eq(JsonDoc.numberOf(colour.items[1]), 0.5)
            assert.eq(JsonDoc.numberOf(colour.items[2]), 0)
        end
        assert.eq(JsonDoc.numberOf(JsonDoc.get(parameters, "Transparency")), 0.75)
        assert.eq(JsonDoc.get(properties, "Color"), nil)
    end)

    suite:case("a stamp whose root is a material becomes that material", function()
        local stamp =
            '{"format": "luaug-scene", "version": 1, "root": {"class": "Material", "name": "Wood", "properties": {"Roughness": 0.4}}}'
        local placed = scene(
            '   {"class": "Material", "name": "Wood", "stamp": "stamps/wood.stamp.json"},\n'
                .. '   {"class": "Part", "name": "Crate", "properties": {"Material": "Workspace.Wood"}}'
        )
        local result = Migrate.materials({
            ["stamps/wood.stamp.json"] = stamp,
            ["scenes/main.scene.json"] = placed,
        })
        assert.eq(result.removes[1], "stamps/wood.stamp.json")
        local asset = result.writes["materials/wood.material.json"]
        assert.that(asset ~= nil)
        assert.neq(string.find(asset :: string, '"Roughness": 0.4', 1, true), nil)
        assert.neq(
            string.find(
                result.writes["scenes/main.scene.json"] :: string,
                "asset://materials/wood.material.json",
                1,
                true
            ),
            nil
        )
    end)

    suite:case("a second run does nothing", function()
        local before = scene('   {"class": "Part", "name": "Blue", "properties": {"Color": [0, 0, 1]}}')
        local first = Migrate.materials({ ["scenes/main.scene.json"] = before })
        local second =
            Migrate.materials({ ["scenes/main.scene.json"] = first.writes["scenes/main.scene.json"] :: string })
        assert.eq(next(second.writes), nil)
        assert.eq(#second.notes, 0)
    end)
end)
