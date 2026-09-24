--!strict
--- Conformance: the 2D layer's classes, as data (phase 3).
---
--- `Tilemap2D`'s cells through its four methods, the defaults a new `Part2D`
--- and `Tilemap2D` start with, and the orthographic `Camera`. What the
--- simulation does with them is `physics/physics2d.spec.luau`.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local afterEach = testing.afterEach

local spawned: { Instance } = {}

local function track<T>(created: T): T
    table.insert(spawned, created :: any)
    return created
end

-- Writes through an untyped handle, which is how a test reaches a value the
-- type checker would refuse at the call site.
local function writeAny(target: Instance, property: string, value: any): () -> ()
    return function()
        (target :: any)[property] = value
    end
end

describe("Tilemap2D", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("starts empty, with one-metre cells and 16-pixel tiles", function()
        local tiles = track(Instance.new("Tilemap2D"))
        expect(tiles:GetCell(0, 0)):ToBe(0)
        expect(tiles.CellSize):ToBe(1)
        expect(tiles.TileSize.X):ToBe(16)
        expect(tiles.Collides):ToBe(true)
        expect(tiles.Filter):ToBe(Enum.TextureFilter.Nearest)
    end)

    it("reads back what was set, anywhere, negative cells included", function()
        local tiles = track(Instance.new("Tilemap2D"))
        tiles:SetCell(0, 0, 1)
        tiles:SetCell(-17, 40, 65535)
        tiles:SetCell(1000000, -1000000, 3)
        expect(tiles:GetCell(0, 0)):ToBe(1)
        expect(tiles:GetCell(-17, 40)):ToBe(65535)
        expect(tiles:GetCell(1000000, -1000000)):ToBe(3)
        expect(tiles:GetCell(1, 0)):ToBe(0)
        tiles:SetCell(0, 0, 0)
        expect(tiles:GetCell(0, 0)):ToBe(0)
    end)

    it("fills a rectangle with both corners included, in either order", function()
        local tiles = track(Instance.new("Tilemap2D"))
        tiles:FillRect(3, 2, -1, 0, 5)
        for x = -1, 3 do
            for y = 0, 2 do
                expect(tiles:GetCell(x, y)):ToBe(5)
            end
        end
        expect(tiles:GetCell(4, 0)):ToBe(0)
        expect(tiles:GetCell(-1, 3)):ToBe(0)
    end)

    it("empties every cell with Clear", function()
        local tiles = track(Instance.new("Tilemap2D"))
        tiles:FillRect(0, 0, 40, 40, 2)
        tiles:Clear()
        expect(tiles:GetCell(20, 20)):ToBe(0)
    end)

    it("refuses a cell that is not a whole number, and a tile out of range", function()
        local tiles = track(Instance.new("Tilemap2D"))
        expect(function()
            tiles:SetCell(0.5, 0, 1)
        end):ToThrow("script.err.tile_cell")
        expect(function()
            tiles:SetCell(0, 0, 65536)
        end):ToThrow("script.err.tile_id")
        expect(function()
            tiles:SetCell(0, 0, -1)
        end):ToThrow("script.err.tile_id")
    end)

    it("refuses to fill more than a million cells at once", function()
        local tiles = track(Instance.new("Tilemap2D"))
        expect(function()
            tiles:FillRect(0, 0, 1000, 1000, 1)
        end):ToThrow("script.err.tile_rect_too_large")
        expect(tiles:GetCell(0, 0)):ToBe(0)
    end)

    it("refuses a cell size that is not a size", function()
        local tiles = track(Instance.new("Tilemap2D"))
        expect(writeAny(tiles, "CellSize", 0)):ToThrow()
        expect(writeAny(tiles, "CellSize", -2)):ToThrow()
    end)
end)

describe("Part2D", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("starts as a one-metre box that falls and collides", function()
        local part = track(Instance.new("Part2D"))
        expect(part.Size.X):ToBe(1)
        expect(part.Size.Y):ToBe(1)
        expect(part.Shape):ToBe(Enum.Shape2D.Box)
        expect(part.Anchored):ToBe(false)
        expect(part.CanCollide):ToBe(true)
        expect(part.Sensor):ToBe(false)
        expect(part.GravityScale):ToBe(1)
        expect(part.CollisionGroup):ToBe("Default")
        expect(part.Filter):ToBe(Enum.TextureFilter.Linear)
    end)

    it("refuses a size that is not positive on both axes", function()
        local part = track(Instance.new("Part2D"))
        expect(writeAny(part, "Size", Vector2.new(0, 1))):ToThrow("scene.err.size2_positive")
        expect(writeAny(part, "Size", Vector2.new(1, -1))):ToThrow("scene.err.size2_positive")
    end)
end)

describe("an orthographic Camera", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("is perspective until asked, with a ten-metre half height ready", function()
        local camera = track(Instance.new("Camera"))
        expect(camera.Projection):ToBe(Enum.CameraProjection.Perspective)
        expect(camera.OrthographicSize):ToBe(10)
        camera.Projection = Enum.CameraProjection.Orthographic
        expect(camera.Projection):ToBe(Enum.CameraProjection.Orthographic)
    end)

    it("refuses a half height that is not above zero", function()
        local camera = track(Instance.new("Camera"))
        expect(writeAny(camera, "OrthographicSize", 0)):ToThrow()
    end)
end)
