--!strict
--- What a property says when it refuses a write (D021).
---
--- A setter answers the engine with a bool and has no way to say WHICH of its
--- conditions failed, so the message a refusal carries is a property of the
--- property rather than of the value. Before this milestone that message was
--- derived from the property's TYPE alone, which made
--- `PhysicsService.FixedTimestep = 1/10` answer "it takes a number" about a
--- number.
---
--- These cases hold the corrected shape down: a property with a range names its
--- range, and it names it for a wrong-typed value too, because "it takes a
--- number from 0 to 1" is equally true of `Restitution = "loud"`.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect
local beforeEach, afterEach = testing.beforeEach, testing.afterEach

-- The definitions type these properties as `number`, which is the point: a
-- value outside the range is spellable and a value of the wrong type is not.
-- Both go through this widened view.
local function writeAny(instance: Instance, property: string, value: any)
    return function()
        (instance :: any)[property] = value
    end
end

describe("Property range refusals", function()
    local part: Part = nil :: any

    beforeEach(function()
        part = Instance.new("Part")
        part.Name = "RangeRefusalSpecPart"
        part.Parent = workspace
    end)

    afterEach(function()
        part:Destroy()
    end)

    describe("a property with a range", function()
        it("names the range rather than the type", function()
            expect(writeAny(part, "Restitution", 2)):ToThrow("scene.err.number_zero_to_one")
            expect(writeAny(part, "Restitution", -0.5)):ToThrow("scene.err.number_zero_to_one")
        end)

        it("names the range for a wrong-typed value too", function()
            -- The domain statement subsumes the type statement, and one key per
            -- property is all a bool-returning setter can carry.
            expect(writeAny(part, "Restitution", "loud")):ToThrow("scene.err.number_zero_to_one")
        end)

        it("distinguishes at-least-zero from above-zero", function()
            expect(writeAny(part, "Friction", -1)):ToThrow("scene.err.number_at_least_zero")
            expect(writeAny(part, "Density", 0)):ToThrow("scene.err.number_above_zero")
        end)

        it("accepts the ends of the range it states", function()
            part.Restitution = 0
            expect(part.Restitution):ToBe(0)
            part.Restitution = 1
            expect(part.Restitution):ToBe(1)
            part.Friction = 0
            expect(part.Friction):ToBe(0)
        end)

        it("reports a tick length for FixedTimestep, which is what D021 was about", function()
            local physics = game:GetService("PhysicsService")
            local before = physics.FixedTimestep
            expect(writeAny(physics, "FixedTimestep", 1 / 10)):ToThrow("scene.err.number_fixed_timestep")
            expect(writeAny(physics, "FixedTimestep", 1 / 1000)):ToThrow("scene.err.number_fixed_timestep")
            expect(physics.FixedTimestep):ToBe(before)
        end)

        it("reports an angle for the two properties measured in degrees", function()
            -- Both are left unparented, and that is not tidiness: a failing
            -- expectation aborts the case before any `Destroy` after it runs, so
            -- anything this case parents into the world outlives it and lands on
            -- an unrelated spec. Found by break-verifying this file, where two
            -- physics specs went red for a defect that was three files away.
            local character = Instance.new("CharacterBody")
            expect(writeAny(character, "MaxSlopeAngle", 90)):ToThrow("scene.err.number_slope_degrees")

            local spot = Instance.new("SpotLight")
            expect(writeAny(spot, "Angle", 180)):ToThrow("scene.err.number_cone_degrees")
        end)
    end)

    describe("a property without a range", function()
        it("still names its type", function()
            expect(writeAny(part, "Anchored", 1)):ToThrow("scene.err.expected_boolean")
            expect(writeAny(part, "Name", 1)):ToThrow("scene.err.expected_string")
            expect(writeAny(part, "Size", "big")):ToThrow("scene.err.expected_vector")
        end)
    end)
end)
