--!strict
--- Conformance: an `Anchored` part a tween moves carries what stands on it
--- (D027, D031).
---
--- **This is the case a person found by playing, twice.** First the character
--- did not inherit the platform's motion at all; then, with that fixed, the
--- platform still had no motion to inherit -- an `Anchored` part was classified
--- as a STATIC body, and Jolt neither moves one nor derives a velocity from one.
---
--- It is a conformance spec rather than a seam test because both halves are only
--- true END TO END: the tween has to write through the property setter, the
--- mirror has to notice the write and reclassify the body, the backend has to
--- move it rather than teleport it, and the character has to ask its ground how
--- fast it is going. Any one of those missing and the capsule stands still.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local afterEach = testing.afterEach

local PhysicsService = game:GetService("PhysicsService")
local TweenService = game:GetService("TweenService")

local function step(ticks: number)
    task.wait(ticks * PhysicsService.FixedTimestep)
end

describe("a moving platform", function()
    local platform: Part = nil :: any
    local rider: CharacterBody = nil :: any

    afterEach(function()
        -- Both are set by every case here, so this is a teardown rather than a
        -- guard: a case that failed before creating them would fail on the
        -- assertion, not on the cleanup.
        platform:Destroy()
        rider:Destroy()
    end)

    it("carries a character that is standing on it", function()
        platform = Instance.new("Part")
        platform.Name = "Platform"
        platform.Size = vector.create(10, 1, 10)
        platform.Position = vector.create(0, 0, 0)
        platform.Anchored = true
        platform.Parent = workspace

        rider = Instance.new("CharacterBody")
        rider.Name = "Rider"
        rider.Size = vector.create(2, 4, 2)
        rider.Position = vector.create(0, 3, 0)
        rider.Parent = workspace

        -- Land, and settle. A character that is still falling is not standing on
        -- anything, and `GetGroundVelocity` is asked only of one that is.
        step(40)
        expect(rider.Grounded):ToBe(true)

        local tween = TweenService:Create(platform, TweenInfo.new(1, Enum.EasingStyle.Linear), {
            CFrame = CFrame.new(0, 0, -8),
        })
        tween:Play()
        step(75)

        -- The platform arrived, and so did the rider. It asked for nothing: every
        -- metre it covered was the platform's.
        expect(platform.Position.z):ToBeCloseTo(-8, 0.1)
        expect(rider.Position.z):ToBeCloseTo(-8, 0.6)
        expect(rider.Grounded):ToBe(true)
    end)

    it("goes back to being scenery when nothing is writing it", function()
        -- The other half of the hysteresis: a platform that stopped must not stay
        -- in the moving broadphase layer forever, and one that is still is still.
        platform = Instance.new("Part")
        platform.Name = "Platform"
        platform.Size = vector.create(10, 1, 10)
        platform.Anchored = true
        platform.Parent = workspace

        rider = Instance.new("CharacterBody")
        rider.Name = "Rider"
        rider.Size = vector.create(2, 4, 2)
        rider.Position = vector.create(0, 3, 0)
        rider.Parent = workspace
        step(40)

        local before = rider.Position
        -- Long past the twelve-tick hold, with nothing writing the platform.
        step(60)
        expect(rider.Position.z):ToBeCloseTo(before.z, 0.05)
        expect(rider.Grounded):ToBe(true)
    end)
end)
