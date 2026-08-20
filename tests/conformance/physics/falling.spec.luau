--!strict
--- Conformance: a part falls (api-design.md §2.2, `BasePart`'s physical half).
---
--- The milestone's own model is "cube falls onto ground, scripted in Luau", and
--- this is that sentence as an assertion. It is written against the API rather
--- than against the engine: what it knows is that `Anchored` decides whether the
--- simulation moves a part, that `Workspace.Gravity` is signed and SI, and that
--- `LinearVelocity` reports the last tick's motion.
---
--- Everything here waits in whole ticks through `task.wait`, because the tick is
--- the only clock a spec may read (ADR 0025).

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local afterEach = testing.afterEach

local spawned: { Instance } = {}

local function part(name: string, position: vector): Part
    local created = Instance.new("Part")
    created.Name = name
    created.Size = vector.create(2, 2, 2)
    created.Position = position
    created.Parent = workspace
    table.insert(spawned, created)
    return created
end

local function ground(): Part
    local floor = part("Ground", vector.create(0, -1, 0))
    floor.Size = vector.create(80, 2, 80)
    floor.Anchored = true
    return floor
end

describe("a part under gravity", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("defaults to unanchored, collidable and queryable", function()
        local crate = part("Crate", vector.create(0, 10, 0))
        expect(crate.Anchored):ToBe(false)
        expect(crate.CanCollide):ToBe(true)
        expect(crate.CanQuery):ToBe(true)
        expect(crate.CollisionGroup):ToBe("Default")
    end)

    it("falls when it is not anchored", function()
        local crate = part("Crate", vector.create(0, 20, 0))
        local start = crate.Position.y
        task.wait(0.25)
        expect(crate.Position.y < start):ToBe(true)
    end)

    it("does not fall when it is anchored", function()
        local crate = part("Crate", vector.create(0, 20, 0))
        crate.Anchored = true
        task.wait(0.25)
        expect(crate.Position.y):ToBeCloseTo(20, 1e-3)
    end)

    it("reports the velocity it fell at, downwards", function()
        local crate = part("Crate", vector.create(0, 20, 0))
        task.wait(0.25)
        -- Signed and SI: falling is negative Y, and a quarter second of 9.81
        -- is about 2.45 m/s. The bound is loose because the assertion is about
        -- the sign and the order of magnitude, not about the integrator.
        expect(crate.LinearVelocity.y < -1):ToBe(true)
        expect(crate.LinearVelocity.y > -5):ToBe(true)
    end)

    it("lands on an anchored part and stops there", function()
        ground()
        local crate = part("Crate", vector.create(0, 8, 0))
        task.wait(2)
        -- The floor's top face is at y = 0 and the crate is two metres tall, so
        -- resting means its centre is at one.
        expect(crate.Position.y):ToBeCloseTo(1, 0.2)
        expect(math.abs(crate.LinearVelocity.y) < 0.5):ToBe(true)
    end)

    it("passes through a part that cannot collide", function()
        local floor = ground()
        floor.CanCollide = false
        local crate = part("Crate", vector.create(0, 8, 0))
        task.wait(1.5)
        -- Free fall for a second and a half is about eleven metres, so a crate
        -- that started at eight is well below the floor it passed through. The
        -- assertion is "below the floor", not a distance.
        expect(crate.Position.y < -2):ToBe(true)
    end)

    it("takes an impulse and moves the way it was pushed", function()
        ground()
        local crate = part("Crate", vector.create(0, 2, 0))
        task.wait(0.5)
        crate:ApplyImpulse(vector.create(200, 0, 0))
        task.wait(0.25)
        expect(crate.Position.x > 0.5):ToBe(true)
    end)

    it("scales the same impulse by density, because mass is density times volume", function()
        ground()
        local light = part("Light", vector.create(-6, 2, 0))
        light.Density = 0.2
        local heavy = part("Heavy", vector.create(6, 2, 0))
        heavy.Density = 20

        task.wait(0.5)
        light:ApplyImpulse(vector.create(100, 0, 0))
        heavy:ApplyImpulse(vector.create(100, 0, 0))
        task.wait(0.1)

        expect(math.abs(light.LinearVelocity.x) > math.abs(heavy.LinearVelocity.x) * 2):ToBe(true)
    end)

    it("refuses a density of zero, because a massless body is a division rather than a light one", function()
        local crate = part("Crate", vector.create(0, 10, 0))
        expect(function()
            crate.Density = 0
        end):ToThrow()
        expect(crate.Density):ToBe(1)
    end)

    it("refuses a velocity assignment, because ApplyImpulse is that operation named", function()
        local crate = part("Crate", vector.create(0, 10, 0))
        expect(function()
            (crate :: any).LinearVelocity = vector.create(1, 0, 0)
        end):ToThrow()
    end)
end)

describe("Workspace.Gravity", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
        workspace.Gravity = vector.create(0, -9.81, 0)
    end)

    it("defaults to SI and downwards", function()
        expect(workspace.Gravity.y):ToBeCloseTo(-9.81, 1e-4)
    end)

    it("changes how fast a part falls", function()
        local slow = part("Slow", vector.create(0, 40, 0))
        workspace.Gravity = vector.create(0, -1.62, 0)
        task.wait(0.5)
        local moon = 40 - slow.Position.y

        -- Out of the world rather than destroyed: `afterEach` destroys
        -- everything this file spawned, and destroying twice is an error.
        slow.Parent = nil
        workspace.Gravity = vector.create(0, -9.81, 0)
        local fast = part("Fast", vector.create(0, 40, 0))
        task.wait(0.5)
        local earth = 40 - fast.Position.y

        -- A differential rather than a number: the same scene under two
        -- gravities must not fall the same distance, which is the assertion a
        -- recorded value cannot make.
        expect(earth > moon * 2):ToBe(true)
    end)
end)
