--!strict
--- Conformance: the 2D layer's physics (post-v1 phase 3).
---
--- `Part2D` and `Tilemap2D` on the world's XY plane, simulated on the same
--- fixed tick as the 3D world: a part falls onto painted tiles and stays there,
--- slides along a floor of them without catching on the seams between tiles,
--- reports `Touched`, and a ray on the plane finds what it should.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local afterEach = testing.afterEach

local PhysicsService = game:GetService("PhysicsService")

local function step(ticks: number)
    task.wait(ticks * PhysicsService.FixedTimestep)
end

local spawned: { Instance } = {}
local connections: { Connection } = {}

local function part(position: Vector2, size: Vector2): Part2D
    local created = Instance.new("Part2D")
    created.Position = position
    created.Size = size
    created.Parent = workspace
    table.insert(spawned, created)
    return created
end

-- A floor of 40 tiles, one metre each, whose top is at y = 0, across
-- negative and positive cells and over the seam between two blocks.
local function floor(): Tilemap2D
    local tiles = Instance.new("Tilemap2D")
    tiles:FillRect(-20, -1, 19, -1, 1)
    tiles.Parent = workspace
    table.insert(spawned, tiles)
    return tiles
end

describe("Part2D", function()
    afterEach(function()
        for _, connection in connections do
            connection:Disconnect()
        end
        table.clear(connections)
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("falls under gravity and comes to rest on painted tiles", function()
        floor()
        local crate = part(Vector2.new(0, 5), Vector2.new(1, 1))
        step(120)
        expect(crate.Position.Y):ToBeCloseTo(0.5, 0.05)
        expect(math.abs(crate.Velocity.Y) < 0.1):ToBe(true)
    end)

    it("stays where it is when anchored", function()
        local post = part(Vector2.new(3, 3), Vector2.new(1, 1))
        post.Anchored = true
        step(30)
        expect(post.Position.X):ToBe(3)
        expect(post.Position.Y):ToBe(3)
    end)

    it("slides along a floor of tiles without catching on their seams", function()
        local tiles = floor()
        tiles.Friction = 0
        local puck = part(Vector2.new(-15, 0.5), Vector2.new(1, 1))
        puck.Friction = 0
        puck.FixedRotation = true
        step(10)
        puck.Velocity = Vector2.new(8, 0)
        step(150)
        -- Twenty tiles crossed: a box catching on any seam would have lost speed.
        expect(puck.Velocity.X > 7.9):ToBe(true)
        expect(puck.Position.X > 0):ToBe(true)
    end)

    it("is pushed by ApplyImpulse", function()
        floor()
        local crate = part(Vector2.new(0, 0.5), Vector2.new(1, 1))
        crate.Friction = 0
        step(5)
        crate:ApplyImpulse(Vector2.new(5, 0))
        step(10)
        expect(crate.Velocity.X > 1):ToBe(true)
    end)

    it("fires Touched when it lands, naming the tilemap", function()
        local tiles = floor()
        local crate = part(Vector2.new(0, 3), Vector2.new(1, 1))
        local hits = 0
        local other: Instance? = nil
        table.insert(
            connections,
            crate.Touched:Connect(function(touched: Instance)
                hits += 1
                other = touched
            end)
        )
        step(90)
        expect(hits):ToBe(1)
        expect(other):ToBe(tiles)
    end)

    it("falls through where the tiles were cleared", function()
        local tiles = floor()
        local crate = part(Vector2.new(0.5, 0.5), Vector2.new(0.8, 0.8))
        step(10)
        tiles:SetCell(0, -1, 0)
        step(60)
        expect(crate.Position.Y < -1):ToBe(true)
    end)

    it("falls through a floor that stops colliding", function()
        local tiles = floor()
        local crate = part(Vector2.new(0, 0.5), Vector2.new(1, 1))
        step(5)
        tiles.Collides = false
        step(60)
        expect(crate.Position.Y < -1):ToBe(true)
    end)
end)

describe("Workspace:Raycast2D", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("finds the tiles' surface, and nil where there is nothing", function()
        local tiles = floor()
        step(1)
        local hit = workspace:Raycast2D(Vector2.new(2.5, 10), Vector2.new(0, -20))
        expect(hit).Never:ToBeNil()
        if hit then
            expect(hit.Instance):ToBe(tiles)
            expect(hit.Position.Y):ToBeCloseTo(0, 1e-3)
            expect(hit.Normal.Y):ToBe(1)
            expect(hit.Distance):ToBeCloseTo(10, 1e-3)
        end
        expect(workspace:Raycast2D(Vector2.new(100, 10), Vector2.new(0, -20))):ToBeNil()
    end)

    it("honours a filter, and passes through sensors", function()
        floor()
        local crate = part(Vector2.new(2.5, 3), Vector2.new(1, 1))
        crate.Anchored = true
        local sensor = part(Vector2.new(2.5, 6), Vector2.new(1, 1))
        sensor.Anchored = true
        sensor.Sensor = true
        step(1)
        local hit = workspace:Raycast2D(Vector2.new(2.5, 10), Vector2.new(0, -20))
        expect(hit and hit.Instance):ToBe(crate)

        local ignored: { Instance } = { crate }
        local params = RaycastParams.new({ Filter = ignored })
        local past = workspace:Raycast2D(Vector2.new(2.5, 10), Vector2.new(0, -20), params)
        expect(past and past.Instance.ClassName):ToBe("Tilemap2D")
    end)
end)
