--!strict
--- Conformance: terrain, from a script (ADR 0067, F1).
---
--- **The point of these is that a brush in the editor and a script are the same
--- verbs.** Every rule the sculpting tool relies on -- a fill that reaches the
--- floor makes ground, one that does not makes a slab, painting moves nothing,
--- material zero erases in one verb and is refused by the other -- is asserted
--- here, against the shipped API rather than against the C++ that implements it.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

--- A material id. Zero means "remove" to `FillBall` and `FillBlock`, so every
--- non-zero value is some kind of ground.
local Rock = 1
local Sand = 2

describe("terrain", function()
    local terrain: Terrain

    beforeEach(function()
        terrain = Instance.new("Terrain")
        terrain.Name = "ConformanceTerrain"
        -- Reserved before anything is sculpted: a collider's height precision is
        -- spread across this range when it is built and cannot be widened after.
        terrain.MinHeight = -32
        terrain.MaxHeight = 32
        terrain.Parent = workspace
    end)

    afterEach(function()
        terrain:Destroy()
    end)

    it("a fill that reaches the floor makes ground you can stand on", function()
        local changed = terrain:FillBlock(vector.create(0, -20, 0), vector.create(16, 40, 16), Rock)
        expect(changed > 0):ToBe(true)

        local height = terrain:HeightAt(0, 0)
        expect(height):ToBeCloseTo(0, 1)
    end)

    it("every column of a block carries its material, not just the first", function()
        terrain:FillBlock(vector.create(0, -20, 0), vector.create(16, 40, 16), Rock)
        -- Seven metres from the middle, which is inside the block and in the same
        -- 32-column tile as the middle. This is the case that used to come back
        -- as no ground at all.
        expect(terrain:HeightAt(7, 7)):ToBeCloseTo(0, 1)
        expect(terrain:HeightAt(-7, -7)):ToBeCloseTo(0, 1)
    end)

    it("filling one corner of a tile does not put ground under the rest of it", function()
        terrain:FillBlock(vector.create(4, -20, 4), vector.create(4, 40, 4), Rock)
        expect(terrain:HeightAt(4, 4)):ToBeCloseTo(0, 1)
        -- Ten metres away, sharing the tile, there is no ground.
        expect(terrain:HeightAt(14, 14)):ToBe(nil)
    end)

    it("digging with material zero removes ground", function()
        terrain:FillBlock(vector.create(0, -20, 0), vector.create(32, 40, 32), Rock)
        local before = terrain:HeightAt(0, 0)
        expect(before).Never:ToBe(nil)

        terrain:FillBall(vector.create(0, 0, 0), 6, 0)
        local after = terrain:HeightAt(0, 0)
        expect(after).Never:ToBe(nil)
        expect((after :: number) < (before :: number)):ToBe(true)
    end)

    it("painting changes the material and leaves the ground where it is", function()
        terrain:FillBlock(vector.create(0, -20, 0), vector.create(32, 40, 32), Rock)
        local before = terrain:HeightAt(0, 0)
        expect(before).Never:ToBe(nil)

        local painted = terrain:PaintBall(vector.create(0, before :: number, 0), 4, Sand)
        expect(painted > 0):ToBe(true)
        expect(terrain:HeightAt(0, 0)):ToBeCloseTo(before :: number, 3)
    end)

    it("painting with material zero is refused rather than erasing", function()
        terrain:FillBlock(vector.create(0, -20, 0), vector.create(32, 40, 32), Rock)
        local before = terrain:HeightAt(0, 0)

        expect(terrain:PaintBall(vector.create(0, 0, 0), 8, 0)):ToBe(0)
        expect(terrain:HeightAt(0, 0)):ToBeCloseTo(before :: number, 3)
    end)

    it("painting out of reach of the surface touches nothing", function()
        terrain:FillBlock(vector.create(0, -20, 0), vector.create(32, 40, 32), Rock)
        expect(terrain:PaintBall(vector.create(0, 20, 0), 1, Sand)):ToBe(0)
    end)

    it("clearing empties the field", function()
        terrain:FillBlock(vector.create(0, -20, 0), vector.create(32, 40, 32), Rock)
        expect(terrain:HeightAt(0, 0)).Never:ToBe(nil)

        terrain:Clear()
        expect(terrain:HeightAt(0, 0)):ToBe(nil)
    end)

    it("a cave leaves the ground above it standing", function()
        -- The thing a height map cannot express, as one script can see it: a hole
        -- with ground above AND below it. `HeightAt` still answers about the
        -- height layer, so the roof is still where it was -- which is the honest
        -- shape of the question and is why the docs say to cast a ray when what
        -- you want is the first surface along a direction.
        terrain:FillBlock(vector.create(0, -20, 0), vector.create(48, 40, 48), Rock)
        local roof = terrain:HeightAt(0, 0)
        expect(roof).Never:ToBe(nil)

        terrain:FillBall(vector.create(0, -6, 0), 4, 0)
        expect(terrain:HeightAt(0, 0)):ToBeCloseTo(roof :: number, 1)
    end)
end)
