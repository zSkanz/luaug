--!strict
--- Pins `api-design.md` §2.3 (the `Vector3` row) and divergence #9: Vector3 IS
--- the native Luau vector primitive (ADR 0013), its canonical component fields
--- are lowercase, and the uppercase spelling is frozen out.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

--- Reads a member without deciding whether an absent member is `nil` or a raise:
--- §2.3 freezes the spelling, not the failure mode.
local function tryIndex(value: any, key: string): any
    local ok, result = pcall(function(): any
        return value[key]
    end)
    return if ok then result else nil
end

local function expectVectorClose(actual: vector, expected: vector, tolerance: number?): ()
    local epsilon = tolerance or 1e-5
    expect(actual.x):ToBeCloseTo(expected.x, epsilon)
    expect(actual.y):ToBeCloseTo(expected.y, epsilon)
    expect(actual.z):ToBeCloseTo(expected.z, epsilon)
end

describe("Vector3", function()
    describe("identity with the native vector primitive", function()
        it("reports the same typeof for Vector3.new and vector.create", function()
            expect(typeof(Vector3.new(1, 2, 3))):ToBe(typeof(vector.create(1, 2, 3)))
        end)

        it("reports typeof vector, the language primitive's own name", function()
            -- §9 calls the value a language primitive "like `string`", and every
            -- signature in §2.1 spells its type lowercase `vector`.
            expect(typeof(Vector3.new(1, 2, 3))):ToBe("vector")
        end)

        it("builds the same value through Vector3.new and vector.create", function()
            expect(Vector3.new(1, 2, 3)):ToBe(vector.create(1, 2, 3))
        end)

        it("compares by component value rather than by identity", function()
            expect(Vector3.new(4, 5, 6)):ToBe(Vector3.new(4, 5, 6))
            expect(Vector3.new(4, 5, 6)).Never:ToBe(Vector3.new(4, 5, 7))
        end)

        it("is not a table or a userdata", function()
            expect(typeof(Vector3.new(0, 0, 0))).Never:ToBe("table")
            expect(typeof(Vector3.new(0, 0, 0))).Never:ToBe("userdata")
        end)
    end)

    describe("components", function()
        it("exposes the components as lowercase x, y and z", function()
            local v = Vector3.new(1.5, -2.5, 3.5)
            expect(v.x):ToBe(1.5)
            expect(v.y):ToBe(-2.5)
            expect(v.z):ToBe(3.5)
        end)

        it("does not expose uppercase X, Y or Z components", function()
            -- Divergence #9 is frozen: `.Magnitude` and `.Unit` keep their
            -- PascalCase spelling, but the components never gain one.
            local v = Vector3.new(1, 2, 3)
            expect(tryIndex(v, "X")):ToBeNil()
            expect(tryIndex(v, "Y")):ToBeNil()
            expect(tryIndex(v, "Z")):ToBeNil()
        end)

        it("takes the components in x, y, z argument order", function()
            local v = vector.create(7, 8, 9)
            expect(v.x):ToBe(7)
            expect(v.y):ToBe(8)
            expect(v.z):ToBe(9)
        end)

        it("exposes components as plain numbers", function()
            expect(Vector3.new(1, 2, 3).x):ToBeA("number")
        end)
    end)

    describe("the Vector3 global carries the vector library", function()
        it("exposes create alongside new", function()
            expect(Vector3.create):ToBeA("function")
            expect(Vector3.new):ToBeA("function")
        end)

        it("exposes every documented library function", function()
            local names = {
                "create",
                "magnitude",
                "normalize",
                "dot",
                "cross",
                "angle",
                "lerp",
                "floor",
                "ceil",
                "abs",
                "sign",
                "clamp",
                "min",
                "max",
            }
            for _, name in names do
                expect((Vector3 :: any)[name]):ToBeA("function")
            end
        end)

        it("exposes the zero and one constants of the vector library", function()
            expect(Vector3.zero):ToBe(vector.zero)
            expect(Vector3.one):ToBe(vector.one)
        end)

        it("agrees with the vector library it carries", function()
            local a = Vector3.new(3, 4, 0)
            local b = Vector3.new(0, 0, 2)
            expect(Vector3.magnitude(a)):ToBeCloseTo(vector.magnitude(a), 1e-6)
            expect(Vector3.dot(a, b)):ToBeCloseTo(vector.dot(a, b), 1e-6)
            expectVectorClose(Vector3.cross(a, b), vector.cross(a, b))
        end)
    end)

    -- The metatable members exist at runtime and cannot be type-checked. Luau's
    -- `vector` is a builtin type and `declare extern type vector` does not
    -- augment it, so `v.Magnitude` is a type error however the definitions are
    -- written (U-54). Reaching them through this widened alias is what lets the
    -- spec prove the runtime member is there; it is the same shape
    -- `instance_construction.spec` uses for a name the singleton overloads
    -- cannot spell.
    local function widened(v: vector): any
        return v :: any
    end

    describe("convenience metatable surface", function()
        it("exposes Magnitude as a number, not a method", function()
            local v = Vector3.new(3, 4, 0)
            expect(widened(v).Magnitude):ToBeA("number")
            expect(widened(v).Magnitude):ToBeCloseTo(5, 1e-6)
        end)

        it("exposes Unit as the normalized vector", function()
            local v = Vector3.new(0, 6, 0)
            expectVectorClose(widened(v).Unit, vector.create(0, 1, 0))
            expect(vector.magnitude(widened(v).Unit)):ToBeCloseTo(1, 1e-6)
        end)

        it("agrees with vector.magnitude and vector.normalize", function()
            local v = Vector3.new(1, -2, 3)
            expect(widened(v).Magnitude):ToBeCloseTo(vector.magnitude(v), 1e-6)
            expectVectorClose(widened(v).Unit, vector.normalize(v))
        end)

        it("answers Dot with the scalar product", function()
            local a = Vector3.new(1, 2, 3)
            local b = Vector3.new(4, -5, 6)
            expect(widened(a):Dot(b)):ToBeCloseTo(12, 1e-6)
            expect(widened(a):Dot(b)):ToBeCloseTo(vector.dot(a, b), 1e-6)
        end)

        it("answers Cross with the right-handed cross product", function()
            local a = Vector3.new(1, 0, 0)
            local b = Vector3.new(0, 1, 0)
            expectVectorClose(widened(a):Cross(b), vector.create(0, 0, 1))
            expectVectorClose(widened(a):Cross(b), vector.cross(a, b))
        end)

        it("answers Lerp with the interpolated vector", function()
            local a = Vector3.new(0, 0, 0)
            local b = Vector3.new(10, 20, 30)
            expectVectorClose(widened(a):Lerp(b, 0.5), vector.create(5, 10, 15))
            expectVectorClose(widened(a):Lerp(b, 0.25), vector.lerp(a, b, 0.25))
        end)

        it("answers Angle with the unsigned angle in radians", function()
            local a = Vector3.new(1, 0, 0)
            local b = Vector3.new(0, 1, 0)
            expect(widened(a):Angle(b)):ToBeCloseTo(math.pi / 2, 1e-5)
            expect(widened(a):Angle(b)):ToBeCloseTo(vector.angle(a, b), 1e-6)
        end)
    end)

    describe("operators", function()
        it("adds component-wise", function()
            expect(Vector3.new(1, 2, 3) + Vector3.new(4, 5, 6)):ToBe(Vector3.new(5, 7, 9))
        end)

        it("subtracts component-wise", function()
            expect(Vector3.new(5, 7, 9) - Vector3.new(4, 5, 6)):ToBe(Vector3.new(1, 2, 3))
        end)

        it("negates every component", function()
            expect(-Vector3.new(1, -2, 3)):ToBe(Vector3.new(-1, 2, -3))
        end)

        it("multiplies by a scalar from either side", function()
            expect(Vector3.new(1, 2, 3) * 2):ToBe(Vector3.new(2, 4, 6))
            expect(2 * Vector3.new(1, 2, 3)):ToBe(Vector3.new(2, 4, 6))
        end)

        it("divides by a scalar", function()
            expect(Vector3.new(2, 4, 6) / 2):ToBe(Vector3.new(1, 2, 3))
        end)

        it("multiplies component-wise by another vector", function()
            expect(Vector3.new(1, 2, 3) * Vector3.new(4, 5, 6)):ToBe(Vector3.new(4, 10, 18))
        end)

        it("divides component-wise by another vector", function()
            expect(Vector3.new(4, 10, 18) / Vector3.new(4, 5, 6)):ToBe(Vector3.new(1, 2, 3))
        end)

        it("leaves the operand untouched, values being immutable", function()
            local a = Vector3.new(1, 2, 3)
            local _sum = a + Vector3.new(1, 1, 1)
            expect(a):ToBe(Vector3.new(1, 2, 3))
        end)
    end)

    describe("f32 precision", function()
        it("stores small exact values without loss", function()
            local v = Vector3.new(1.5, -2.25, 3.75)
            expect(v.x):ToBe(1.5)
            expect(v.y):ToBe(-2.25)
            expect(v.z):ToBe(3.75)
        end)

        it("is exact at the documented 131 km boundary for a representable value", function()
            expect(Vector3.new(131072, 0, 0).x):ToBe(131072)
        end)

        it("loses a quarter-metre offset at ten thousand kilometres", function()
            -- §2.3: "Precision: f32 -- exact to ~+-131 km; CFrame is the f64
            -- source of truth". This is the loss the guideline warns about.
            expect(Vector3.new(1e7 + 0.25, 0, 0).x).Never:ToBe(1e7 + 0.25)
        end)

        it("keeps ten million itself, which f32 can represent", function()
            expect(Vector3.new(1e7, 0, 0).x):ToBe(1e7)
        end)

        it("collapses two positions a quarter metre apart at ten thousand kilometres", function()
            local near = Vector3.new(1e7, 0, 0)
            local far = Vector3.new(1e7 + 0.25, 0, 0)
            expect(far.x - near.x).Never:ToBeCloseTo(0.25, 1e-3)
        end)
    end)
end)
