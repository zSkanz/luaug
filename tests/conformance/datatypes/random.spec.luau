--!strict
--- Pins the `Random` row of `api-design.md` §2.3: "Deterministic streams (R10)".
--- ADR 0025 makes that the load-bearing property -- same seed, same sequence is
--- what the level-B determinism guarantee and the replay hash rest on, so it is
--- the first thing asserted here.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

local SEED = 20260819

local function drawNumbers(generator: Random, count: number): { number }
    local drawn: { number } = {}
    for _ = 1, count do
        table.insert(drawn, generator:NextNumber())
    end
    return drawn
end

describe("Random", function()
    describe("determinism", function()
        it("produces the same NextNumber sequence for the same seed", function()
            -- R10 and ADR 0025: this is the guarantee every replay depends on.
            expect(drawNumbers(Random.new(SEED), 16)):ToEqual(drawNumbers(Random.new(SEED), 16))
        end)

        it("produces the same NextInteger sequence for the same seed", function()
            local first = Random.new(SEED)
            local second = Random.new(SEED)
            for _ = 1, 16 do
                expect(first:NextInteger(1, 1000)):ToBe(second:NextInteger(1, 1000))
            end
        end)

        it("produces the same NextUnitVector sequence for the same seed", function()
            local first = Random.new(SEED)
            local second = Random.new(SEED)
            for _ = 1, 8 do
                expect(first:NextUnitVector()):ToBe(second:NextUnitVector())
            end
        end)

        it("produces the same sequence for a mixed run of calls", function()
            local first = Random.new(SEED)
            local second = Random.new(SEED)
            for _ = 1, 8 do
                expect(first:NextNumber()):ToBe(second:NextNumber())
                expect(first:NextInteger(-50, 50)):ToBe(second:NextInteger(-50, 50))
                expect(first:NextUnitVector()):ToBe(second:NextUnitVector())
            end
        end)

        it("is unaffected by draws taken from another generator", function()
            local drawnOnce = drawNumbers(Random.new(SEED), 8)
            local other = Random.new(SEED + 1)
            drawNumbers(other, 5)
            expect(drawNumbers(Random.new(SEED), 8)):ToEqual(drawnOnce)
        end)

        it("produces a different sequence for a different seed", function()
            expect(drawNumbers(Random.new(SEED), 8)).Never:ToEqual(drawNumbers(Random.new(SEED + 1), 8))
        end)

        it("advances its stream, so consecutive draws are not all identical", function()
            local drawn = drawNumbers(Random.new(SEED), 8)
            local allSame = true
            for _, value in drawn do
                if value ~= drawn[1] then
                    allSame = false
                    break
                end
            end
            expect(allSame):ToBeFalsy()
        end)
    end)

    describe("the datatype itself", function()
        it("reports typeof Random", function()
            expect(typeof(Random.new(SEED))):ToBe("Random")
        end)

        it("accepts construction without a seed", function()
            expect(Random.new():NextNumber()):ToBeA("number")
        end)
    end)

    describe("NextNumber", function()
        it("returns a number in the unit interval with no arguments", function()
            local generator = Random.new(SEED)
            for _ = 1, 64 do
                local value = generator:NextNumber()
                expect(value >= 0):ToBeTruthy()
                expect(value < 1):ToBeTruthy()
            end
        end)

        it("returns a number inside the requested range", function()
            local generator = Random.new(SEED)
            for _ = 1, 64 do
                local value = generator:NextNumber(-2.5, 7.5)
                expect(value >= -2.5):ToBeTruthy()
                expect(value <= 7.5):ToBeTruthy()
            end
        end)

        it("returns the bound itself for an empty range", function()
            expect(Random.new(SEED):NextNumber(3.5, 3.5)):ToBeCloseTo(3.5, 1e-9)
        end)

        it("spreads its draws across the requested range", function()
            local generator = Random.new(SEED)
            local low = false
            local high = false
            for _ = 1, 128 do
                local value = generator:NextNumber(0, 10)
                low = low or value < 5
                high = high or value >= 5
            end
            expect(low):ToBeTruthy()
            expect(high):ToBeTruthy()
        end)
    end)

    describe("NextInteger", function()
        it("returns whole numbers", function()
            local generator = Random.new(SEED)
            for _ = 1, 32 do
                local value = generator:NextInteger(-10, 10)
                expect(value):ToBe(math.floor(value))
            end
        end)

        it("stays inside the requested range", function()
            local generator = Random.new(SEED)
            for _ = 1, 128 do
                local value = generator:NextInteger(3, 9)
                expect(value >= 3):ToBeTruthy()
                expect(value <= 9):ToBeTruthy()
            end
        end)

        it("returns the bound itself for a single-value range", function()
            expect(Random.new(SEED):NextInteger(5, 5)):ToBe(5)
        end)

        it("can return either bound of an inclusive range", function()
            local generator = Random.new(SEED)
            local sawLow = false
            local sawHigh = false
            for _ = 1, 128 do
                local value = generator:NextInteger(1, 2)
                sawLow = sawLow or value == 1
                sawHigh = sawHigh or value == 2
            end
            expect(sawLow):ToBeTruthy()
            expect(sawHigh):ToBeTruthy()
        end)
    end)

    describe("NextUnitVector", function()
        it("returns a Vector3", function()
            expect(typeof(Random.new(SEED):NextUnitVector())):ToBe(typeof(vector.zero))
        end)

        it("returns a vector of unit length", function()
            local generator = Random.new(SEED)
            for _ = 1, 32 do
                expect(vector.magnitude(generator:NextUnitVector())):ToBeCloseTo(1, 1e-5)
            end
        end)

        it("returns different directions across draws", function()
            local generator = Random.new(SEED)
            expect(generator:NextUnitVector()).Never:ToBe(generator:NextUnitVector())
        end)
    end)

    describe("Clone", function()
        it("returns a Random", function()
            expect(typeof(Random.new(SEED):Clone())):ToBe("Random")
        end)

        it("produces the same continuation as the generator it was cloned from", function()
            local original = Random.new(SEED)
            drawNumbers(original, 3)
            local copy = original:Clone()
            expect(drawNumbers(copy, 8)):ToEqual(drawNumbers(original, 8))
        end)

        it("advances independently of the original", function()
            local original = Random.new(SEED)
            local copy = original:Clone()
            drawNumbers(copy, 10)
            expect(drawNumbers(original, 8)):ToEqual(drawNumbers(Random.new(SEED), 8))
        end)

        it("leaves the clone unaffected by draws taken from the original", function()
            local original = Random.new(SEED)
            local copy = original:Clone()
            drawNumbers(original, 10)
            expect(drawNumbers(copy, 8)):ToEqual(drawNumbers(Random.new(SEED), 8))
        end)

        it("clones a clone", function()
            local original = Random.new(SEED)
            drawNumbers(original, 4)
            local nested = original:Clone():Clone()
            expect(drawNumbers(nested, 8)):ToEqual(drawNumbers(original, 8))
        end)
    end)
end)
