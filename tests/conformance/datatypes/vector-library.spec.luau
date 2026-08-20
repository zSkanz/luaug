--!strict
--- Pins the `vector` stdlib promised by `api-design.md` §2.3: "the full official
--- `vector` stdlib (`vector.create/magnitude/normalize/dot/cross/angle/lerp/
--- floor/ceil/abs/sign/clamp/min/max`, `vector.zero/one`)".

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

local function expectVectorClose(actual: vector, expected: vector, tolerance: number?): ()
    local epsilon = tolerance or 1e-5
    expect(actual.x):ToBeCloseTo(expected.x, epsilon)
    expect(actual.y):ToBeCloseTo(expected.y, epsilon)
    expect(actual.z):ToBeCloseTo(expected.z, epsilon)
end

describe("vector library", function()
    describe("create", function()
        it("builds a vector from three components", function()
            local v = vector.create(1, 2, 3)
            expect(v.x):ToBe(1)
            expect(v.y):ToBe(2)
            expect(v.z):ToBe(3)
        end)

        it("returns a value of the vector type", function()
            expect(vector.create(0, 0, 0)):ToBeA("vector")
        end)
    end)

    describe("constants", function()
        it("offers zero as the origin vector", function()
            expect(vector.zero):ToBe(vector.create(0, 0, 0))
        end)

        it("offers one as the all-ones vector", function()
            expect(vector.one):ToBe(vector.create(1, 1, 1))
        end)
    end)

    describe("magnitude", function()
        it("returns the euclidean length", function()
            expect(vector.magnitude(vector.create(3, 4, 0))):ToBeCloseTo(5, 1e-6)
        end)

        it("returns zero for the zero vector", function()
            expect(vector.magnitude(vector.zero)):ToBe(0)
        end)

        it("ignores the sign of the components", function()
            expect(vector.magnitude(vector.create(-3, 0, -4))):ToBeCloseTo(5, 1e-6)
        end)
    end)

    describe("normalize", function()
        it("returns a unit-length vector", function()
            expect(vector.magnitude(vector.normalize(vector.create(1, 2, 3)))):ToBeCloseTo(1, 1e-5)
        end)

        it("keeps the direction of the input", function()
            expectVectorClose(vector.normalize(vector.create(0, 6, 0)), vector.create(0, 1, 0))
        end)

        it("leaves an already-unit vector unchanged", function()
            expectVectorClose(vector.normalize(vector.create(0, 0, -1)), vector.create(0, 0, -1))
        end)
    end)

    describe("dot", function()
        it("returns the scalar product", function()
            expect(vector.dot(vector.create(1, 2, 3), vector.create(4, -5, 6))):ToBeCloseTo(12, 1e-6)
        end)

        it("returns zero for perpendicular vectors", function()
            expect(vector.dot(vector.create(1, 0, 0), vector.create(0, 1, 0))):ToBeCloseTo(0, 1e-6)
        end)

        it("is commutative", function()
            local a = vector.create(1, -2, 3)
            local b = vector.create(-4, 5, 6)
            expect(vector.dot(a, b)):ToBeCloseTo(vector.dot(b, a), 1e-6)
        end)
    end)

    describe("cross", function()
        it("follows the right-hand rule on the basis vectors", function()
            expectVectorClose(vector.cross(vector.create(1, 0, 0), vector.create(0, 1, 0)), vector.create(0, 0, 1))
            expectVectorClose(vector.cross(vector.create(0, 1, 0), vector.create(0, 0, 1)), vector.create(1, 0, 0))
            expectVectorClose(vector.cross(vector.create(0, 0, 1), vector.create(1, 0, 0)), vector.create(0, 1, 0))
        end)

        it("is anti-commutative", function()
            local a = vector.create(1, 2, 3)
            local b = vector.create(4, 5, 6)
            expectVectorClose(vector.cross(b, a), -vector.cross(a, b))
        end)

        it("returns a vector perpendicular to both operands", function()
            local a = vector.create(1, 2, 3)
            local b = vector.create(-4, 5, 6)
            local c = vector.cross(a, b)
            expect(vector.dot(c, a)):ToBeCloseTo(0, 1e-4)
            expect(vector.dot(c, b)):ToBeCloseTo(0, 1e-4)
        end)

        it("returns the zero vector for parallel operands", function()
            expectVectorClose(vector.cross(vector.create(2, 0, 0), vector.create(5, 0, 0)), vector.zero)
        end)
    end)

    describe("angle", function()
        it("returns the angle between two vectors in radians", function()
            expect(vector.angle(vector.create(1, 0, 0), vector.create(0, 1, 0))):ToBeCloseTo(math.pi / 2, 1e-5)
        end)

        it("returns zero for identical directions", function()
            local a = vector.create(2, 2, 2)
            expect(vector.angle(a, a)):ToBeCloseTo(0, 1e-5)
        end)

        it("returns pi for opposite directions", function()
            expect(vector.angle(vector.create(0, 1, 0), vector.create(0, -1, 0))):ToBeCloseTo(math.pi, 1e-5)
        end)

        it("ignores the magnitudes of its operands", function()
            local wide = vector.angle(vector.create(10, 0, 0), vector.create(0, 0.001, 0))
            expect(wide):ToBeCloseTo(math.pi / 2, 1e-4)
        end)
    end)

    describe("lerp", function()
        it("returns the first vector at alpha zero", function()
            expectVectorClose(vector.lerp(vector.create(1, 2, 3), vector.create(9, 9, 9), 0), vector.create(1, 2, 3))
        end)

        it("returns the second vector at alpha one", function()
            expectVectorClose(vector.lerp(vector.create(1, 2, 3), vector.create(9, 9, 9), 1), vector.create(9, 9, 9))
        end)

        it("returns the midpoint at alpha one half", function()
            local midpoint = vector.lerp(vector.zero, vector.create(10, 20, 30), 0.5)
            expectVectorClose(midpoint, vector.create(5, 10, 15))
        end)
    end)

    describe("component-wise rounding", function()
        it("floors every component toward negative infinity", function()
            expectVectorClose(vector.floor(vector.create(1.7, -1.2, 3)), vector.create(1, -2, 3))
        end)

        it("ceils every component toward positive infinity", function()
            expectVectorClose(vector.ceil(vector.create(1.2, -1.7, 3)), vector.create(2, -1, 3))
        end)

        it("takes the absolute value of every component", function()
            expectVectorClose(vector.abs(vector.create(-1.5, 2, -3)), vector.create(1.5, 2, 3))
        end)

        it("reduces every component to its sign", function()
            expectVectorClose(vector.sign(vector.create(-3, 0, 7)), vector.create(-1, 0, 1))
        end)
    end)

    describe("clamp", function()
        it("bounds every component between the two limit vectors", function()
            local clamped = vector.clamp(vector.create(-5, 0.5, 10), vector.zero, vector.one)
            expectVectorClose(clamped, vector.create(0, 0.5, 1))
        end)

        it("leaves a vector already inside the bounds untouched", function()
            local inside = vector.create(0.25, 0.5, 0.75)
            expectVectorClose(vector.clamp(inside, vector.zero, vector.one), inside)
        end)
    end)

    describe("min and max", function()
        it("takes the component-wise minimum of two vectors", function()
            expectVectorClose(vector.min(vector.create(1, 5, 3), vector.create(4, 2, 6)), vector.create(1, 2, 3))
        end)

        it("takes the component-wise maximum of two vectors", function()
            expectVectorClose(vector.max(vector.create(1, 5, 3), vector.create(4, 2, 6)), vector.create(4, 5, 6))
        end)

        it("accepts more than two vectors", function()
            local lowest = vector.min(vector.create(3, 3, 3), vector.create(2, 9, 9), vector.create(9, 1, 0))
            expectVectorClose(lowest, vector.create(2, 1, 0))
        end)

        it("returns its only argument unchanged", function()
            expectVectorClose(vector.max(vector.create(1, 2, 3)), vector.create(1, 2, 3))
        end)
    end)
end)
