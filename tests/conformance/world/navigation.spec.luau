--!strict
--- Conformance: `NavigationService` (ADR 0089).
---
--- Paths over the walkable ground a game builds: anchored, colliding parts. A
--- door in a wall is the one case that shows a path is a path and not a line;
--- an island is the case that shows a partial answer is still an answer.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local afterEach = testing.afterEach

local NavigationService = game:GetService("NavigationService")

local spawned: { Instance } = {}

local function block(position: vector, size: vector): Part
    local part = Instance.new("Part")
    part.Size = size
    part.Position = position
    part.Anchored = true
    part.Parent = workspace
    table.insert(spawned, part)
    return part
end

-- Forty metres of floor whose top is y = 0, far from anything the other specs
-- build.
local Offset = vector.create(500, 0, 500)
local function at(x: number, y: number, z: number): vector
    return Offset + vector.create(x, y, z)
end

local function floor(): Part
    return block(at(0, -0.5, 0), vector.create(40, 1, 40))
end

describe("NavigationService", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("finds a straight path across open floor, whole", function()
        floor()
        local points, complete = NavigationService:FindPath(at(-15, 0, -15), at(15, 0, 12))
        expect(points).Never:ToBeNil()
        expect(complete):ToBe(true)
        if points then
            expect(#points):ToBe(2)
            local last = points[#points]
            expect(last.x):ToBeCloseTo(Offset.x + 15, 0.1)
            expect(last.z):ToBeCloseTo(Offset.z + 12, 0.1)
        end
    end)

    it("goes through the door in a wall", function()
        floor()
        block(at(0, 1.5, -11), vector.create(1, 3, 18))
        block(at(0, 1.5, 11), vector.create(1, 3, 18))
        local points, complete = NavigationService:FindPath(at(-10, 0, -12), at(10, 0, -12))
        expect(complete):ToBe(true)
        local throughDoor = false
        if points then
            for _, point in points do
                if math.abs(point.x - Offset.x) < 1.5 and math.abs(point.z - Offset.z) < 2 then
                    throughDoor = true
                end
            end
        end
        expect(throughDoor):ToBe(true)
    end)

    it("answers a partial path to an island, and nil from nowhere", function()
        floor()
        block(at(60, -0.5, 0), vector.create(10, 1, 10))
        local points, complete = NavigationService:FindPath(at(0, 0, 0), at(60, 0, 0))
        expect(points).Never:ToBeNil()
        expect(complete):ToBe(false)

        local none, reached = NavigationService:FindPath(at(0, 50, 300), at(0, 0, 0))
        expect(none):ToBeNil()
        expect(reached):ToBe(false)
    end)

    it("sees a wall moved on the next tick", function()
        floor()
        local wall = block(at(0, 1.5, 0), vector.create(1, 3, 40))
        local _, blocked = NavigationService:FindPath(at(-10, 0, 0), at(10, 0, 0))
        expect(blocked):ToBe(false)
        wall.Position = at(0, 1.5, 200)
        task.wait()
        local _, open = NavigationService:FindPath(at(-10, 0, 0), at(10, 0, 0))
        expect(open):ToBe(true)
    end)

    it("answers the nearest walkable point, and a straight walk that stops at a wall", function()
        floor()
        block(at(0, 1.5, 0), vector.create(1, 3, 40))
        local ground = NavigationService:NearestPoint(at(-5, 1.5, 3))
        expect(ground).Never:ToBeNil()
        if ground then
            expect(ground.y):ToBeCloseTo(0, 0.3)
        end
        expect(NavigationService:NearestPoint(at(-5, 30, 3), 2)):ToBeNil()

        local stop = NavigationService:Raycast(at(-10, 0, 5), at(10, 0, 5))
        expect(stop).Never:ToBeNil()
        if stop then
            expect(stop.x < Offset.x - 0.4):ToBe(true)
        end
    end)

    it("builds a region ahead of time, once", function()
        floor()
        expect(NavigationService:BuildRegion(at(-20, -1, -20), at(20, 1, 20)) > 0):ToBe(true)
        expect(NavigationService:BuildRegion(at(-20, -1, -20), at(20, 1, 20))):ToBe(0)
    end)

    it("keeps an agent that no body could be out of its properties", function()
        expect(NavigationService.AgentRadius):ToBe(0.5)
        expect(function()
            (NavigationService :: any).AgentRadius = 0
        end):ToThrow()
        expect(function()
            (NavigationService :: any).AgentMaxSlope = 95
        end):ToThrow("scene.err.nav_slope")
    end)
end)
