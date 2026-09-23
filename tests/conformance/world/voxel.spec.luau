--!strict
--- Conformance: the block world, from a script (V1, `VoxelService`).
---
--- **Not the terrain.** These are the verbs a block game is built from --
--- register a block type, place one, break one, ask what is where, and cast a
--- ray to find the block under the crosshair -- asserted against the shipped
--- API rather than against the C++ behind it.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach = testing.beforeEach

describe("voxel service", function()
    local voxels = game:GetService("VoxelService")
    local Stone = 0
    local Dirt = 0

    beforeEach(function()
        voxels:Clear()
        Stone = voxels:RegisterBlock("Stone", Color3.fromRGB(128, 128, 128))
        Dirt = voxels:RegisterBlock("Dirt", Color3.fromRGB(120, 85, 50))
    end)

    it("hands out ids in registration order, and a name keeps its id", function()
        expect(Stone):ToBe(1)
        expect(Dirt):ToBe(2)
        expect(voxels:RegisterBlock("Stone", Color3.fromRGB(100, 100, 100))):ToBe(1)
        expect(voxels:GetBlockId("Dirt")):ToBe(2)
        expect(voxels:GetBlockId("Nothing")):ToBe(nil)
        expect(voxels.BlockTypeCount):ToBe(2)
    end)

    it("reads back a placed block, and breaking it leaves air", function()
        expect(voxels:SetBlock(vector.create(3, 4, 5), Stone)):ToBe(true)
        expect(voxels:GetBlock(vector.create(3, 4, 5))):ToBe(Stone)
        expect(voxels:SetBlock(vector.create(3, 4, 5), Stone)):ToBe(false)
        expect(voxels:SetBlock(vector.create(3, 4, 5), 0)):ToBe(true)
        expect(voxels:GetBlock(vector.create(3, 4, 5))):ToBe(0)
    end)

    it("rounds a block coordinate down, including below zero", function()
        voxels:SetBlock(vector.create(-0.5, 0.9, 0), Dirt)
        expect(voxels:GetBlock(vector.create(-1, 0, 0))):ToBe(Dirt)
    end)

    it("fills a box in one call and counts every block it changed", function()
        expect(voxels:FillBlocks(vector.create(0, 0, 0), vector.create(15, 0, 15), Stone)):ToBe(256)
        expect(voxels.ChunkCount):ToBe(1)
        expect(voxels:FillBlocks(vector.create(0, 0, 0), vector.create(15, 0, 15), 0)):ToBe(256)
        expect(voxels.ChunkCount):ToBe(0)
    end)

    it("refuses a block id nobody registered", function()
        expect(function()
            voxels:SetBlock(vector.create(0, 0, 0), 99)
        end):ToThrow()
    end)

    it("converts between world positions and block coordinates", function()
        local block = voxels:WorldToBlock(vector.create(2.5, -0.25, 7.99))
        expect(block.x):ToBe(2)
        expect(block.y):ToBe(-1)
        expect(block.z):ToBe(7)
        local centre = voxels:BlockToWorld(vector.create(2, -1, 7))
        expect(centre.x):ToBeCloseTo(2.5, 3)
        expect(centre.y):ToBeCloseTo(-0.5, 3)
    end)

    it("casts a ray to the first block it enters, and the face it came through", function()
        voxels:FillBlocks(vector.create(-4, 0, -4), vector.create(4, 0, 4), Stone)
        -- Straight down from above the middle.
        local block, normal = voxels:Raycast(vector.create(0.5, 10, 0.5), vector.create(0, -20, 0))
        expect(block ~= nil):ToBe(true)
        expect(normal ~= nil):ToBe(true)
        if block and normal then
            expect(block.y):ToBe(0)
            expect(normal.y):ToBe(1)
        end
        -- Too short to reach it.
        local missed = voxels:Raycast(vector.create(0.5, 10, 0.5), vector.create(0, -5, 0))
        expect(missed):ToBe(nil)
    end)

    it("gives a block type images, and refuses air or a type nobody registered", function()
        -- Images are a look, not a fact about the world's shape: setting them
        -- changes no block and fails nothing, even before a file is found.
        voxels:SetBlockTextures(Stone, "asset://textures/stone.png")
        voxels:SetBlockTextures(Dirt, "asset://textures/top.png", "asset://textures/side.png", "")
        voxels:SetBlockTextures(Stone, "")
        local anyVoxels = voxels :: any
        expect(function()
            anyVoxels:SetBlockTextures(0, "asset://textures/air.png")
        end):ToThrow()
        expect(function()
            anyVoxels:SetBlockTextures(999, "asset://textures/nobody.png")
        end):ToThrow()
    end)

    it("says what shows through a block type, and refuses what is not an opacity", function()
        voxels:SetBlockOpacity(Stone, Enum.BlockOpacity.Cutout)
        voxels:SetBlockOpacity(Stone, Enum.BlockOpacity.Translucent, 0.3)
        voxels:SetBlockOpacity(Stone, Enum.BlockOpacity.Opaque)
        local anyVoxels = voxels :: any
        expect(function()
            anyVoxels:SetBlockOpacity(Stone, Enum.PartShape.Block)
        end):ToThrow()
        expect(function()
            anyVoxels:SetBlockOpacity(0, Enum.BlockOpacity.Opaque)
        end):ToThrow()
    end)

    it("lets a fluid fall, spread its reach, and drain when its source is taken", function()
        local PhysicsService = game:GetService("PhysicsService")
        local Water = voxels:RegisterBlock("Water", Color3.fromRGB(40, 90, 200))
        voxels:SetBlockFluid(Water, 3, 1)
        voxels:FillBlocks(vector.create(-8, 0, -8), vector.create(8, 0, 8), Stone)
        voxels:SetBlock(vector.create(0, 3, 0), Water)
        task.wait(30 * PhysicsService.FixedTimestep)

        -- Down to the floor first, then three blocks along it and no further.
        expect(voxels:GetBlock(vector.create(0, 2, 0))):ToBe(Water)
        expect(voxels:GetBlock(vector.create(0, 1, 0))):ToBe(Water)
        expect(voxels:GetBlock(vector.create(3, 1, 0))):ToBe(Water)
        expect(voxels:GetBlock(vector.create(4, 1, 0))):ToBe(0)
        expect(voxels:GetBlock(vector.create(1, 2, 0))):ToBe(0)
        -- Falling water is full; spreading water gets shallower; stone is dry.
        expect(voxels:GetFluidDepth(vector.create(0, 2, 0))):ToBe(1)
        expect(voxels:GetFluidDepth(vector.create(3, 1, 0)) < voxels:GetFluidDepth(vector.create(1, 1, 0))):ToBe(true)
        expect(voxels:GetFluidDepth(vector.create(0, 0, 0))):ToBe(0)
        -- A ray goes through the water to the floor under it.
        local block = voxels:Raycast(vector.create(0.5, 10, 0.5), vector.create(0, -20, 0))
        expect(block):ToBe(vector.create(0, 0, 0))

        voxels:SetBlock(vector.create(0, 3, 0), 0)
        task.wait(60 * PhysicsService.FixedTimestep)
        expect(voxels:GetBlock(vector.create(0, 1, 0))):ToBe(0)
        expect(voxels:GetBlock(vector.create(2, 1, 0))):ToBe(0)
        voxels:SetBlockFluid(Water, 0)
    end)

    it("refuses a fluid reach it cannot hold, or a fluid made of air", function()
        local Water = voxels:RegisterBlock("Water", Color3.fromRGB(40, 90, 200))
        expect(function()
            voxels:SetBlockFluid(Water, 8)
        end):ToThrow()
        expect(function()
            voxels:SetBlockFluid(Water, 2, 0)
        end):ToThrow()
        expect(function()
            voxels:SetBlockFluid(0, 2)
        end):ToThrow()
    end)
end)
