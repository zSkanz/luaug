--!strict
--- Conformance: materials (ADR 0090).
---
--- A material is an asset and never an instance. A part WEARS one and has no
--- colour of its own; it may override only the parameters its material
--- declares. At runtime a loaded asset is read-only, `Clone` is the copy a
--- script changes, and nothing clones implicitly.
---
--- The files are under `tests/conformance/content/materials/`: `brick` (a base
--- declaring `Color`), `moss` (a variant of brick that declares `Roughness`
--- too), `plain` (a base declaring nothing) and `surfaced` (a base naming a
--- surface shader and setting three of its parameters, ADR 0091).

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local afterEach = testing.afterEach

local Brick = "asset://materials/brick.material.json" :: Content
local Moss = "asset://materials/moss.material.json" :: Content
local Plain = "asset://materials/plain.material.json" :: Content
local Surfaced = "asset://materials/surfaced.material.json" :: Content

local spawned: { Instance } = {}

local function part(): Part
    local made = Instance.new("Part")
    made.Anchored = true
    made.Parent = workspace
    table.insert(spawned, made)
    return made
end

afterEach(function()
    for _, instance in spawned do
        instance:Destroy()
    end
    table.clear(spawned)
end)

describe("Material", function()
    it("is a data type, not a class", function()
        expect(typeof(Material.load(Brick))):ToBe("Material")
        expect(function()
            return (Instance.new :: any)("Material")
        end):ToThrow("scene.err.unknown_class")
    end)

    it("loads an asset and reads the file's values", function()
        local brick = Material.load(Brick)
        expect(brick.Source):ToBe(Brick)
        expect(brick.Color.R):ToBeCloseTo(0.6, 1e-5)
        expect(brick.Roughness):ToBeCloseTo(0.9, 1e-5)
        expect(brick.AlphaMode):ToBe(Enum.AlphaMode.Opaque)
        expect(brick.DoubleSided):ToBe(false)
    end)

    it("refuses a Content that names no material", function()
        expect(function()
            return Material.load("asset://materials/none.material.json" :: Content)
        end):ToThrow("script.err.material_not_found")
        expect(function()
            return Material.load("asset://textures/brick.png" :: Content)
        end):ToThrow("script.err.material_not_found")
    end)

    it("resolves a variant through its parent", function()
        local moss = Material.load(Moss)
        -- Its own colour, and its parent's roughness.
        expect(moss.Color.G):ToBeCloseTo(0.5, 1e-5)
        expect(moss.Roughness):ToBeCloseTo(0.9, 1e-5)
    end)

    it("is read-only as a loaded asset", function()
        local brick = Material.load(Brick)
        expect(function()
            brick.Roughness = 0.1
        end):ToThrow("script.err.material_read_only")
    end)

    it("clones into a writable copy that changes nothing else", function()
        local brick = Material.load(Brick)
        local copy = brick:Clone()
        copy.Roughness = 0.1
        copy.Color = Color3.new(0, 0, 1)
        expect(copy.Roughness):ToBeCloseTo(0.1, 1e-5)
        expect(brick.Roughness):ToBeCloseTo(0.9, 1e-5)
        -- The source is the asset, a clone of a clone included.
        expect(copy.Source):ToBe(Brick)
        local again = copy:Clone()
        expect(again.Source):ToBe(Brick)
        expect(again.Roughness):ToBeCloseTo(0.1, 1e-5)
        expect(again == copy):ToBe(false)
    end)

    it("refuses a value of the wrong kind for a property", function()
        local copy = Material.load(Brick):Clone()
        expect(function()
            (copy :: any).Roughness = "shiny"
        end):ToThrow("script.err.material_value_type")
    end)
end)

describe("BasePart.Material", function()
    it("is nil for the engine default, and a part wears what it is given", function()
        local wall = part()
        expect(wall.Material):ToBeNil()
        wall.Material = Material.load(Brick)
        expect(wall.Material == Material.load(Brick)):ToBe(true)
        wall.Material = nil
        expect(wall.Material):ToBeNil()
    end)

    it("does not clone on read", function()
        local wall = part()
        wall.Material = Material.load(Brick)
        -- The handle the part wears is the asset's, so it is read-only: a read
        -- that silently made a copy would make this write succeed and change
        -- nothing anybody could see.
        expect(function()
            (wall.Material :: Material).Roughness = 0.1
        end):ToThrow("script.err.material_read_only")
    end)

    it("shares a clone between the parts that wear it", function()
        local copy = Material.load(Brick):Clone()
        local one = part()
        local two = part()
        one.Material = copy
        two.Material = copy
        copy.Color = Color3.new(0, 1, 0)
        -- Brick declares Color and neither part overrides it, so both draw the
        -- clone's.
        expect((one:GetMaterialParameter("Color") :: Color3).G):ToBeCloseTo(1, 1e-5)
        expect((two:GetMaterialParameter("Color") :: Color3).G):ToBeCloseTo(1, 1e-5)
        expect(one.Material == two.Material):ToBe(true)
    end)
end)

describe("material parameters", function()
    it("lets a part override what its material declares", function()
        local wall = part()
        wall.Material = Material.load(Brick)
        wall:SetMaterialParameter("Color", Color3.new(1, 0, 0))
        expect((wall:GetMaterialParameter("Color") :: Color3).R):ToBeCloseTo(1, 1e-5)
        expect((wall.MaterialParameters.Color :: Color3).R):ToBeCloseTo(1, 1e-5)
    end)

    it("raises for a parameter the material does not declare", function()
        local wall = part()
        wall.Material = Material.load(Plain)
        expect(function()
            wall:SetMaterialParameter("Color", Color3.new(1, 0, 0))
        end):ToThrow("script.err.material_parameter_undeclared")
    end)

    it("lets a part wearing nothing tint and fade, and nothing else", function()
        local wall = part()
        wall:SetMaterialParameter("Color", Color3.new(0, 0, 1))
        wall:SetMaterialParameter("Transparency", 0.5)
        expect(wall:GetMaterialParameter("Transparency") :: number):ToBeCloseTo(0.5, 1e-5)
        expect(function()
            wall:SetMaterialParameter("Roughness", 0.1)
        end):ToThrow("script.err.material_parameter_undeclared_default")
    end)

    it("reads the material's value when nothing is overridden", function()
        local wall = part()
        wall.Material = Material.load(Brick)
        expect(wall:GetMaterialParameter("Roughness") :: number):ToBeCloseTo(0.9, 1e-5)
    end)

    it("keeps an undeclared override and ignores it, so switching back restores the look", function()
        local wall = part()
        wall.Material = Material.load(Brick)
        wall:SetMaterialParameter("Color", Color3.new(1, 0, 0))
        wall.Material = Material.load(Plain)
        -- Plain does not declare Color: the part draws plain's white...
        expect((wall:GetMaterialParameter("Color") :: Color3).G):ToBeCloseTo(1, 1e-5)
        -- ...and still holds the red.
        expect(wall.MaterialParameters.Color ~= nil):ToBe(true)
        wall.Material = Material.load(Brick)
        expect((wall:GetMaterialParameter("Color") :: Color3).G):ToBeCloseTo(0, 1e-5)
    end)

    it("a variant declares its parent's parameters and its own", function()
        local wall = part()
        wall.Material = Material.load(Moss)
        wall:SetMaterialParameter("Color", Color3.new(1, 1, 0))
        wall:SetMaterialParameter("Roughness", 0.2)
        expect(wall:GetMaterialParameter("Roughness") :: number):ToBeCloseTo(0.2, 1e-5)
    end)

    it("clears an override back to the material's value", function()
        local wall = part()
        wall.Material = Material.load(Brick)
        wall:SetMaterialParameter("Color", Color3.new(1, 0, 0))
        wall:ClearMaterialParameter("Color")
        expect((wall:GetMaterialParameter("Color") :: Color3).R):ToBeCloseTo(0.6, 1e-5)
        expect(wall.MaterialParameters.Color):ToBeNil()
    end)

    it("names what is wrong with a parameter", function()
        local wall = part()
        -- A name no parameter can have, and a built-in field no part may change.
        expect(function()
            wall:SetMaterialParameter("no such thing", 1)
        end):ToThrow("script.err.material_parameter_unknown")
        -- A name a surface shader's parameter could have, which the default
        -- material does not declare.
        expect(function()
            wall:SetMaterialParameter("Sparkle", 1)
        end):ToThrow("script.err.material_parameter_undeclared_default")
        expect(function()
            (wall :: any):SetMaterialParameter("ColorMap", "asset://textures/brick.png")
        end):ToThrow("script.err.material_parameter_unknown")
        expect(function()
            wall:SetMaterialParameter("Color", 1)
        end):ToThrow("script.err.material_parameter_type")
    end)

    it("reads the overrides as a fresh table", function()
        local wall = part()
        wall:SetMaterialParameter("Transparency", 0.25)
        local first = wall.MaterialParameters
        first.Transparency = 0.9
        expect(wall.MaterialParameters.Transparency):ToBeCloseTo(0.25, 1e-5)
    end)
end)

describe("surface shader parameters", function()
    it("read off a loaded asset, as the file sets them", function()
        local surfaced = Material.load(Surfaced)
        expect(surfaced:GetShaderParameter("Height")):ToBe(0.5)
        local tint = surfaced:GetShaderParameter("Tint") :: vector
        expect(tint.x):ToBe(1)
        expect(tint.y):ToBe(0.5)
        expect(tint.z):ToBe(0.25)
        expect(surfaced:GetShaderParameter("Foam")):ToBe("asset://textures/foam.png")
        -- Left to the shader's own default: the material says nothing.
        expect(surfaced:GetShaderParameter("Speed")):ToBe(nil)
    end)

    it("are read-only on a loaded asset", function()
        local surfaced = Material.load(Surfaced)
        expect(function()
            surfaced:SetShaderParameter("Height", 1)
        end):ToThrow("script.err.material_read_only")
    end)

    it("are written on a clone, which changes nothing else and clones on", function()
        local surfaced = Material.load(Surfaced)
        local copy = surfaced:Clone()
        copy:SetShaderParameter("Height", 2)
        copy:SetShaderParameter("Speed", true)
        copy:SetShaderParameter("Offset", Vector2.new(0.25, 0.75))
        expect(copy:GetShaderParameter("Height")):ToBe(2)
        expect(copy:GetShaderParameter("Speed")):ToBe(1)
        local offset = copy:GetShaderParameter("Offset") :: Vector2
        expect(offset.X):ToBe(0.25)
        expect(offset.Y):ToBe(0.75)
        expect(surfaced:GetShaderParameter("Height")):ToBe(0.5)
        expect(copy:Clone():GetShaderParameter("Height")):ToBe(2)
    end)

    it("refuses a value no shader parameter can hold", function()
        local copy = Material.load(Surfaced):Clone()
        expect(function()
            copy:SetShaderParameter("Height", (function() end) :: any)
        end):ToThrow("script.err.material_value_type")
        expect(function()
            copy:SetShaderParameter("Height", math.huge)
        end):ToThrow("script.err.material_value_type")
    end)
end)

describe("a part's own surface shader parameters", function()
    it("set, read and clear a parameter the material lets a part change", function()
        local wave = part()
        wave.Material = Material.load(Surfaced)
        -- The material's own value until the part says otherwise.
        expect(wave:GetMaterialParameter("Height")):ToBe(0.5)
        wave:SetMaterialParameter("Height", 2)
        expect(wave:GetMaterialParameter("Height")):ToBe(2)
        -- Another part wearing the same material is untouched.
        local other = part()
        other.Material = Material.load(Surfaced)
        expect(other:GetMaterialParameter("Height")):ToBe(0.5)
        -- A clone of the part carries it.
        local copy = wave:Clone() :: Part
        table.insert(spawned, copy)
        expect(copy:GetMaterialParameter("Height")):ToBe(2)
        wave:ClearMaterialParameter("Height")
        expect(wave:GetMaterialParameter("Height")):ToBe(0.5)
    end)

    it("reads nil for one neither the part nor the material sets", function()
        local wave = part()
        wave.Material = Material.load(Surfaced)
        wave:SetMaterialParameter("Offset", Vector2.new(1, 2))
        expect(((wave:GetMaterialParameter("Offset") :: any) :: Vector2).Y):ToBe(2)
        wave:ClearMaterialParameter("Offset")
        expect(wave:GetMaterialParameter("Offset")):ToBe(nil)
    end)

    it("raises for one the material does not declare, and for a texture", function()
        local wave = part()
        wave.Material = Material.load(Surfaced)
        expect(function()
            wave:SetMaterialParameter("Tint", Color3.new(1, 0, 0))
        end):ToThrow("script.err.material_parameter_undeclared")
        expect(function()
            wave:SetMaterialParameter("Height", "asset://textures/foam.png" :: any)
        end):ToThrow("script.err.material_parameter_type")
        expect(function()
            part():SetMaterialParameter("Height", 1)
        end):ToThrow("script.err.material_parameter_undeclared_default")
    end)
end)
