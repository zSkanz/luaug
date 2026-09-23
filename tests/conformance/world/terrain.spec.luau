--!strict
--- Conformance: terrain, from a script (ADR 0067, F1; a grid of voxels by ADR 0082).
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
        -- The world's floor and ceiling: no voxel is written outside them.
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
        -- chunk as the middle. This is the case that once came back as no ground
        -- at all.
        expect(terrain:HeightAt(7, 7)):ToBeCloseTo(0, 1)
        expect(terrain:HeightAt(-7, -7)):ToBeCloseTo(0, 1)
    end)

    it("filling one corner of a tile does not put ground under the rest of it", function()
        terrain:FillBlock(vector.create(4, -20, 4), vector.create(4, 40, 4), Rock)
        expect(terrain:HeightAt(4, 4)):ToBeCloseTo(0, 1)
        -- Ten metres away, sharing the chunk, there is no ground.
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
        -- with ground above AND below it. `HeightAt` answers the top of the
        -- column, so the roof is still where it was -- and a ray from above finds
        -- the roof, not the cave.
        terrain:FillBlock(vector.create(0, -20, 0), vector.create(48, 40, 48), Rock)
        local roof = terrain:HeightAt(0, 0)
        expect(roof).Never:ToBe(nil)

        terrain:FillBall(vector.create(0, -6, 0), 4, 0)
        expect(terrain:HeightAt(0, 0)):ToBeCloseTo(roof :: number, 1)
    end)
    it("writes a heightmap in one call, row after row, clamped to its range", function()
        -- Three columns by two rows at the default metre voxel, starting at the
        -- column the origin is in -- whose centre is half a metre in.
        local written = terrain:WriteHeights(vector.create(0, 0, 0), 3, { 1, 2, 3, 4, 5, 100 }, Sand)
        expect(written > 0):ToBe(true)
        expect(terrain:HeightAt(0.5, 0.5)):ToBeCloseTo(1, 2)
        expect(terrain:HeightAt(2.5, 0.5)):ToBeCloseTo(3, 2)
        expect(terrain:HeightAt(0.5, 1.5)):ToBeCloseTo(4, 2)
        -- Past `MaxHeight`, it stops there, within the voxel the ceiling is in.
        local ceiling = terrain:HeightAt(2.5, 1.5) :: number
        expect(ceiling > 31 and ceiling <= 32):ToBe(true)

        expect(function()
            terrain:WriteHeights(vector.create(0, 0, 0), 4, { 1, 2, 3, 4, 5, 6 })
        end):ToThrow()
        expect(function()
            terrain:WriteHeights(vector.create(0, 0, 0), 0, { 1 })
        end):ToThrow()
    end)

    it("reads voxels out and writes them back exactly", function()
        terrain:FillBlock(vector.create(0, -20, 0), vector.create(16, 40, 16), Rock)
        terrain:FillBall(vector.create(2, 0, 2), 3, Sand)
        local materials, occupancies, size = terrain:ReadVoxels(vector.create(-4, -4, -4), vector.create(4, 4, 4))
        expect(size.x):ToBe(8)
        expect(#materials):ToBe(size.x * size.y * size.z)
        expect(#occupancies):ToBe(#materials)
        -- The voxel at the low corner is deep in the rock, full.
        expect(materials[1]):ToBe(Rock)
        expect(occupancies[1]):ToBeCloseTo(1, 3)

        terrain:Clear()
        expect(terrain:HeightAt(0, 0)):ToBe(nil)
        local changed = terrain:WriteVoxels(vector.create(-4, -4, -4), size, materials, occupancies)
        expect(changed > 0):ToBe(true)
        local again, againOccupancies = terrain:ReadVoxels(vector.create(-4, -4, -4), vector.create(4, 4, 4))
        for index = 1, #materials do
            expect(again[index]):ToBe(materials[index])
            expect(againOccupancies[index]):ToBeCloseTo(occupancies[index], 3)
        end

        expect(function()
            terrain:WriteVoxels(vector.create(0, 0, 0), vector.create(2, 2, 2), { 1 }, { 1 })
        end):ToThrow()
    end)

    it("names a voxel by its index and finds its centre", function()
        local cell = terrain:WorldToCell(vector.create(2.7, -0.2, -3.1))
        expect(cell.x):ToBe(2)
        expect(cell.y):ToBe(-1)
        expect(cell.z):ToBe(-4)
        local centre = terrain:CellCenterToWorld(cell)
        expect(centre.x):ToBeCloseTo(2.5, 3)
        expect(centre.y):ToBeCloseTo(-0.5, 3)
        expect(centre.z):ToBeCloseTo(-3.5, 3)
    end)

    it("cuts a well with a cylinder and swaps what the ground is made of", function()
        terrain:FillBlock(vector.create(0, -20, 0), vector.create(32, 40, 32), Rock)
        terrain:FillCylinder(vector.create(0, -10, 0), 20, 2, 0)
        -- Down the middle is less than half full to the bottom of the well:
        -- occupancy ramps across four voxels, so a well two metres wide is
        -- partly full everywhere and air where it crosses a half.
        local _, occupancies = terrain:ReadVoxels(vector.create(0, -12, 0), vector.create(1, -11, 1))
        expect(occupancies[1] < 0.5):ToBe(true)

        local swapped = terrain:ReplaceMaterial(vector.create(-8, -8, -8), vector.create(8, 0, 8), Rock, Sand)
        expect(swapped > 0):ToBe(true)
        local after = terrain:ReadVoxels(vector.create(5, -3, 5), vector.create(6, -2, 6))
        expect(after[1]):ToBe(Sand)
    end)

    it("smooths a step and flattens a hill", function()
        terrain:FillBlock(vector.create(0, -20, 0), vector.create(32, 40, 32), Rock)
        terrain:RaiseBall(vector.create(0, 0, 0), 6, 4)
        local peak = terrain:HeightAt(0, 0) :: number
        expect(peak > 3):ToBe(true)
        terrain:FlattenBall(vector.create(0, 2, 0), 5, 1, 1)
        expect((terrain:HeightAt(0, 0) :: number) < peak):ToBe(true)
        expect(terrain:SmoothBall(vector.create(0, 1, 0), 5) > 0):ToBe(true)
    end)

    it("a ray meets the ground with nothing moving anywhere near it", function()
        -- Colliders are built near things that move, and there are none here:
        -- the ray is answered by the voxels themselves (ADR 0082).
        terrain:FillBlock(vector.create(0, -20, 0), vector.create(32, 40, 32), Rock)
        local hit = workspace:Raycast(vector.create(3, 50, 3), vector.create(0, -100, 0))
        expect(hit).Never:ToBe(nil)
        local result = hit :: RaycastResult
        expect(result.Instance):ToBe(terrain)
        expect(result.Position.y):ToBeCloseTo(0, 1)
        expect(result.Normal.y > 0.99):ToBe(true)

        local params = RaycastParams.new({ Filter = { terrain } :: { Instance } })
        expect(workspace:Raycast(vector.create(3, 50, 3), vector.create(0, -100, 0), params)):ToBe(nil)
    end)
end)
