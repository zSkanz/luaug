--!strict
--- Conformance: collision groups (api-design.md §2.1, `PhysicsService`, and
--- §2.2, `BasePart.CollisionGroup`).
---
--- What the document promises and this pins: `Default` always exists;
--- registering a name twice is a no-op rather than an error, which is what lets
--- a script register at file scope and survive a hot reload; the collidable
--- matrix is symmetric; the registered list comes back in registration order as
--- a fresh array; and an unregistered name on a part is refused rather than
--- folded into `Default`, because the failure mode of a typo is a wall players
--- walk through.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local afterEach = testing.afterEach

local PhysicsService = game:GetService("PhysicsService")

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

local function has(list: { string }, name: string): boolean
    for _, entry in list do
        if entry == name then
            return true
        end
    end
    return false
end

describe("PhysicsService collision groups", function()
    afterEach(function()
        for _, instance in spawned do
            instance:Destroy()
        end
        table.clear(spawned)
    end)

    it("always has Default", function()
        expect(has(PhysicsService:GetRegisteredCollisionGroups(), "Default")):ToBe(true)
    end)

    it("returns Default first, in registration order", function()
        PhysicsService:RegisterCollisionGroup("Ordering")
        local groups = PhysicsService:GetRegisteredCollisionGroups()
        expect(groups[1]):ToBe("Default")
        expect(groups[#groups]):ToBe("Ordering")
    end)

    it("returns a fresh array every call, so a caller may sort it", function()
        local first = PhysicsService:GetRegisteredCollisionGroups()
        local second = PhysicsService:GetRegisteredCollisionGroups()
        expect(first):ToEqual(second)
        expect(first == second):ToBe(false)
    end)

    it("registers a name twice without complaint", function()
        PhysicsService:RegisterCollisionGroup("Twice")
        local before = #PhysicsService:GetRegisteredCollisionGroups()
        PhysicsService:RegisterCollisionGroup("Twice")
        expect(#PhysicsService:GetRegisteredCollisionGroups()):ToBe(before)
    end)

    it("refuses an unregistered name on a part", function()
        local crate = part("Crate", vector.create(0, 10, 0))
        expect(function()
            crate.CollisionGroup = "NeverRegistered"
        end):ToThrow()
        expect(crate.CollisionGroup):ToBe("Default")
    end)

    it("accepts a registered name and reads it back", function()
        PhysicsService:RegisterCollisionGroup("Props")
        local crate = part("Crate", vector.create(0, 10, 0))
        crate.CollisionGroup = "Props"
        expect(crate.CollisionGroup):ToBe("Props")
    end)

    it("refuses to set collidability for a name nobody registered", function()
        expect(function()
            PhysicsService:CollisionGroupSetCollidable("Default", "AlsoNeverRegistered", false)
        end):ToThrow()
    end)

    it("stops two groups from colliding, in both directions", function()
        PhysicsService:RegisterCollisionGroup("Ghosts")
        PhysicsService:CollisionGroupSetCollidable("Ghosts", "Default", false)

        local ground = part("Ground", vector.create(0, -1, 0))
        ground.Size = vector.create(60, 2, 60)
        ground.Anchored = true

        local ghost = part("Ghost", vector.create(0, 6, 0))
        ghost.CollisionGroup = "Ghosts"

        task.wait(1.5)
        -- Through the floor, because its group and the floor's do not collide.
        expect(ghost.Position.y < -2):ToBe(true)

        -- And the same pair set back on again collides: the matrix is state, not
        -- a one-way switch.
        PhysicsService:CollisionGroupSetCollidable("Default", "Ghosts", true)
        local solid = part("Solid", vector.create(0, 6, 0))
        solid.CollisionGroup = "Ghosts"
        task.wait(2)
        expect(solid.Position.y):ToBeCloseTo(1, 0.3)
    end)
end)
