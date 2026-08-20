--!strict
--- Pins the `CFrame` methods and operators of `api-design.md` §2.3: `Inverse`,
--- `Lerp`, `Orthonormalize`, the four space-conversion pairs, the three
--- decompositions, and `CFrame * CFrame` / `CFrame * vector` -- including which
--- operand of a composition transforms the other's translation.

local testing = require("@luaug/testing")

local describe, it, expect = testing.describe, testing.it, testing.expect

local function expectVectorClose(actual: vector, expected: vector, tolerance: number?): ()
    local epsilon = tolerance or 1e-5
    expect(actual.x):ToBeCloseTo(expected.x, epsilon)
    expect(actual.y):ToBeCloseTo(expected.y, epsilon)
    expect(actual.z):ToBeCloseTo(expected.z, epsilon)
end

local function expectCFrameClose(actual: CFrame, expected: CFrame, tolerance: number?): ()
    local epsilon = tolerance or 1e-5
    expectVectorClose(actual.Position, expected.Position, epsilon)
    expectVectorClose(actual.RightVector, expected.RightVector, epsilon)
    expectVectorClose(actual.UpVector, expected.UpVector, epsilon)
    expectVectorClose(actual.LookVector, expected.LookVector, epsilon)
end

--- A quarter turn built from explicit axes, so the cases that pin composition
--- order do not also depend on the euler sign convention.
local QUARTER_TURN =
    CFrame.fromMatrix(vector.zero, vector.create(0, 0, -1), vector.create(0, 1, 0), vector.create(1, 0, 0))

local SAMPLE = CFrame.new(4, -5, 6) * CFrame.fromEuler(0.3, -0.7, 1.1)

describe("CFrame transforms", function()
    describe("composition", function()
        it("is associative", function()
            local a = CFrame.new(1, 2, 3)
            local b = QUARTER_TURN
            local c = CFrame.new(0, 0, -5)
            expectCFrameClose((a * b) * c, a * (b * c))
        end)

        it("lets the left operand's rotation transform the right operand's translation", function()
            expectVectorClose((QUARTER_TURN * CFrame.new(0, 0, -5)).Position, vector.create(-5, 0, 0))
        end)

        it("leaves the left operand's translation unrotated by the right operand", function()
            expectVectorClose((CFrame.new(0, 0, -5) * QUARTER_TURN).Position, vector.create(0, 0, -5))
        end)

        it("adds the translations of two pure translations", function()
            expectVectorClose((CFrame.new(1, 2, 3) * CFrame.new(4, 5, 6)).Position, vector.create(5, 7, 9))
        end)

        it("composes the rotations of two pure rotations", function()
            local halfTurn = QUARTER_TURN * QUARTER_TURN
            expectVectorClose(halfTurn.LookVector, vector.create(0, 0, 1))
        end)
    end)

    describe("CFrame times vector", function()
        it("transforms a point, applying the translation", function()
            expectVectorClose(CFrame.new(10, 0, 0) * vector.create(1, 2, 3), vector.create(11, 2, 3))
        end)

        it("applies the rotation as well as the translation", function()
            expectVectorClose(QUARTER_TURN * vector.create(0, 0, -5), vector.create(-5, 0, 0))
        end)

        it("leaves a point untouched under the identity transform", function()
            expectVectorClose(CFrame.identity * vector.create(1, 2, 3), vector.create(1, 2, 3))
        end)

        it("agrees with the composition of the two transforms", function()
            local point = vector.create(1, -2, 3)
            local a = CFrame.new(7, 8, 9)
            expectVectorClose((a * QUARTER_TURN) * point, a * (QUARTER_TURN * point))
        end)

        it("returns a Vector3", function()
            expect(typeof(SAMPLE * vector.one)):ToBe(typeof(vector.zero))
        end)
    end)

    describe("Inverse", function()
        it("negates the translation of a pure translation", function()
            expectVectorClose(CFrame.new(1, 2, 3):Inverse().Position, vector.create(-1, -2, -3))
        end)

        it("undoes the transform when composed on the right", function()
            expectCFrameClose(SAMPLE * SAMPLE:Inverse(), CFrame.identity)
        end)

        it("undoes the transform when composed on the left", function()
            expectCFrameClose(SAMPLE:Inverse() * SAMPLE, CFrame.identity)
        end)

        it("leaves the identity transform unchanged", function()
            expectCFrameClose(CFrame.identity:Inverse(), CFrame.identity)
        end)

        it("is its own inverse", function()
            expectCFrameClose(SAMPLE:Inverse():Inverse(), SAMPLE)
        end)
    end)

    describe("ToWorldSpace and ToObjectSpace", function()
        it("treats ToWorldSpace as composition on the right", function()
            expectCFrameClose(SAMPLE:ToWorldSpace(QUARTER_TURN), SAMPLE * QUARTER_TURN)
        end)

        it("treats ToObjectSpace as composition against the inverse", function()
            expectCFrameClose(SAMPLE:ToObjectSpace(QUARTER_TURN), SAMPLE:Inverse() * QUARTER_TURN)
        end)

        it("expresses a translated frame as a relative offset", function()
            local relative = CFrame.new(10, 0, 0):ToObjectSpace(CFrame.new(13, 0, 0))
            expectVectorClose(relative.Position, vector.create(3, 0, 0))
        end)

        it("round-trips a frame through object space and back", function()
            local other = CFrame.new(-3, 12, 0.5) * CFrame.fromEuler(1.0, 0.2, -0.4)
            expectCFrameClose(SAMPLE:ToWorldSpace(SAMPLE:ToObjectSpace(other)), other, 1e-4)
        end)
    end)

    describe("PointToWorldSpace and PointToObjectSpace", function()
        it("applies the full transform to a point", function()
            expectVectorClose(SAMPLE:PointToWorldSpace(vector.create(1, 2, 3)), SAMPLE * vector.create(1, 2, 3))
        end)

        it("applies the inverse transform to a point", function()
            local relative = CFrame.new(10, 0, 0):PointToObjectSpace(vector.create(13, 0, 0))
            expectVectorClose(relative, vector.create(3, 0, 0))
        end)

        it("round-trips a point through object space and back", function()
            local point = vector.create(11, -4, 7)
            expectVectorClose(SAMPLE:PointToWorldSpace(SAMPLE:PointToObjectSpace(point)), point, 1e-4)
        end)
    end)

    describe("VectorToWorldSpace and VectorToObjectSpace", function()
        it("ignores the translation when converting a direction to world space", function()
            local shifted = CFrame.new(100, 0, 0)
            expectVectorClose(shifted:VectorToWorldSpace(vector.create(0, 0, -1)), vector.create(0, 0, -1))
            expectVectorClose(shifted:PointToWorldSpace(vector.create(0, 0, -1)), vector.create(100, 0, -1))
        end)

        it("applies only the rotation", function()
            expectVectorClose(QUARTER_TURN:VectorToWorldSpace(vector.create(0, 0, -1)), vector.create(-1, 0, 0))
        end)

        it("ignores the translation when converting a direction to object space", function()
            local shifted = CFrame.new(100, 0, 0)
            expectVectorClose(shifted:VectorToObjectSpace(vector.create(0, 0, -1)), vector.create(0, 0, -1))
        end)

        it("round-trips a direction through object space and back", function()
            local direction = vector.normalize(vector.create(1, -2, 3))
            expectVectorClose(SAMPLE:VectorToWorldSpace(SAMPLE:VectorToObjectSpace(direction)), direction)
        end)

        it("preserves the length of a direction", function()
            local direction = vector.create(0, 3, 4)
            expect(vector.magnitude(SAMPLE:VectorToWorldSpace(direction))):ToBeCloseTo(5, 1e-4)
        end)
    end)

    describe("Lerp", function()
        it("returns the receiver at alpha zero", function()
            expectCFrameClose(CFrame.identity:Lerp(QUARTER_TURN, 0), CFrame.identity)
        end)

        it("returns the goal at alpha one", function()
            expectCFrameClose(CFrame.identity:Lerp(QUARTER_TURN, 1), QUARTER_TURN)
        end)

        it("returns the midpoint translation at alpha one half", function()
            local midpoint = CFrame.new(0, 0, 0):Lerp(CFrame.new(10, 20, 30), 0.5)
            expectVectorClose(midpoint.Position, vector.create(5, 10, 15))
        end)

        it("places the halfway rotation an equal angle from each end", function()
            local midpoint = CFrame.identity:Lerp(QUARTER_TURN, 0.5)
            local toStart = vector.angle(midpoint.LookVector, CFrame.identity.LookVector)
            local toGoal = vector.angle(midpoint.LookVector, QUARTER_TURN.LookVector)
            expect(toStart):ToBeCloseTo(toGoal, 1e-4)
        end)

        it("keeps the interpolated basis unit-length", function()
            local midpoint = CFrame.identity:Lerp(QUARTER_TURN, 0.37)
            expect(vector.magnitude(midpoint.LookVector)):ToBeCloseTo(1, 1e-5)
            expect(vector.magnitude(midpoint.UpVector)):ToBeCloseTo(1, 1e-5)
        end)
    end)

    describe("Orthonormalize", function()
        it("leaves an orthonormal transform alone", function()
            expectCFrameClose(SAMPLE:Orthonormalize(), SAMPLE, 1e-4)
        end)

        it("keeps the translation", function()
            local scaled = CFrame.fromMatrix(
                vector.create(1, 2, 3),
                vector.create(2, 0, 0),
                vector.create(0, 3, 0),
                vector.create(0, 0, 4)
            )
            expectVectorClose(scaled:Orthonormalize().Position, vector.create(1, 2, 3))
        end)

        it("returns unit-length, mutually perpendicular axes", function()
            local scaled =
                CFrame.fromMatrix(vector.zero, vector.create(2, 0, 0), vector.create(0, 3, 0), vector.create(0, 0, 4))
            local fixed = scaled:Orthonormalize()
            expect(vector.magnitude(fixed.RightVector)):ToBeCloseTo(1, 1e-5)
            expect(vector.magnitude(fixed.UpVector)):ToBeCloseTo(1, 1e-5)
            expect(vector.magnitude(fixed.LookVector)):ToBeCloseTo(1, 1e-5)
            expect(vector.dot(fixed.RightVector, fixed.UpVector)):ToBeCloseTo(0, 1e-5)
        end)
    end)

    describe("decompositions", function()
        it("returns three angles from ToEuler", function()
            local rx, ry, rz = SAMPLE:ToEuler()
            expect(rx):ToBeA("number")
            expect(ry):ToBeA("number")
            expect(rz):ToBeA("number")
        end)

        it("defaults ToEuler to the YXZ order", function()
            local ax, ay, az = SAMPLE:ToEuler()
            local bx, by, bz = SAMPLE:ToEuler(Enum.RotationOrder.YXZ)
            expect(ax):ToBeCloseTo(bx, 1e-6)
            expect(ay):ToBeCloseTo(by, 1e-6)
            expect(az):ToBeCloseTo(bz, 1e-6)
        end)

        it("round-trips ToEuler against fromEuler in a matching order", function()
            local order = Enum.RotationOrder.YXZ
            local rx, ry, rz = SAMPLE:ToEuler(order)
            expectCFrameClose(CFrame.fromEuler(rx, ry, rz, order), SAMPLE.Rotation, 1e-4)
        end)

        it("returns an axis and an angle from ToAxisAngle", function()
            local axis, angle = SAMPLE:ToAxisAngle()
            expect(typeof(axis)):ToBe(typeof(vector.zero))
            expect(angle):ToBeA("number")
            expect(vector.magnitude(axis)):ToBeCloseTo(1, 1e-4)
        end)

        it("round-trips ToAxisAngle against fromAxisAngle", function()
            local axis, angle = SAMPLE:ToAxisAngle()
            expectCFrameClose(CFrame.fromAxisAngle(axis, angle), SAMPLE.Rotation, 1e-4)
        end)

        it("returns four quaternion components from ToQuaternion", function()
            local qx, qy, qz, qw = SAMPLE:ToQuaternion()
            expect(qx):ToBeA("number")
            expect(qy):ToBeA("number")
            expect(qz):ToBeA("number")
            expect(qw):ToBeA("number")
            expect(math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw)):ToBeCloseTo(1, 1e-4)
        end)

        it("round-trips ToQuaternion against fromQuaternion", function()
            local qx, qy, qz, qw = SAMPLE:ToQuaternion()
            expectCFrameClose(CFrame.fromQuaternion(SAMPLE.Position, qx, qy, qz, qw), SAMPLE, 1e-4)
        end)
    end)
end)
