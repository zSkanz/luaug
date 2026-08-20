#include <doctest/doctest.h>
#include <ostream>

#include "script_fixture.h"

using luaug::script::testing::Fixture;

TEST_CASE("typeof answers the names api-design promises")
{
    Fixture fixture;

    // Three names for three different things, and the plural is the one people
    // forget. `Enum` and an enum object are userdata rather than tables for
    // exactly this reason: `typeof` reads `__type` off a userdata's metatable
    // and deliberately does not off a table's.
    CHECK(fixture.failure(R"(
        assert(typeof(Vector3.new(1, 2, 3)) == "vector")
        assert(typeof(CFrame.new()) == "CFrame")
        assert(typeof(Color3.new()) == "Color3")
        assert(typeof(Random.new(1)) == "Random")
        assert(typeof(Enum.PartShape.Ball) == "EnumItem")
        assert(typeof(Enum.PartShape) == "Enum")
        assert(typeof(Enum) == "Enums")
    )") == "");
}

// --- Vector3 -----------------------------------------------------------------

TEST_CASE("Vector3 is the vector primitive, and the superset table reaches both spellings")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local v = Vector3.new(1, 2, 3)
        assert(v == vector.create(1, 2, 3))
        assert(Vector3.zero == vector.zero)
        assert(Vector3.one == vector.one)
        assert(Vector3.magnitude == vector.magnitude)

        -- A missing component is 0, unlike `create`, which requires x and y.
        assert(Vector3.new() == Vector3.zero)
        assert(Vector3.new(5) == vector.create(5, 0, 0))
    )") == "");
}

TEST_CASE("a vector's components have exactly one spelling")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local v = Vector3.new(3, 4, 12)
        assert(v.x == 3 and v.y == 4 and v.z == 12)
        assert(v.Magnitude == 13)
        assert(v.Unit.Magnitude > 0.999 and v.Unit.Magnitude < 1.001)
    )") == "");

    // The uppercase spelling reads the same component and does NOT raise -- the
    // interpreter answers a single-character index on a vector inline and
    // case-insensitively, before any metatable is consulted
    // (`lvmexecute.cpp:619-635`, addendum A1). api-design.md §2.3 asked for a
    // raise here until 2026-08-20, and no metatable could have delivered it.
    // Lowercase is still the only declared spelling, so `v.X` stays a type
    // error; this pins the runtime half.
    CHECK(fixture.failure(R"(
        local v = Vector3.new(3, 4, 12)
        assert(v.X == 3 and v.Y == 4 and v.Z == 12)
    )") == "");

    // The guard is `name[1] == '\0'`, so anything longer than one character
    // reaches our metatable and raises as documented.
    CHECK(fixture.raises("return Vector3.new(1, 2, 3).Nope", "script.err.unknown_member"));
    CHECK(fixture.raises("return Vector3.new(1, 2, 3).XY", "script.err.unknown_member"));
}

TEST_CASE("the vector methods agree with the library functions")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local a = Vector3.new(1, 0, 0)
        local b = Vector3.new(0, 1, 0)

        assert(a:Dot(b) == vector.dot(a, b))
        assert(a:Cross(b) == vector.cross(a, b))
        assert(a:Lerp(b, 0.5) == Vector3.new(0.5, 0.5, 0))

        -- Right-handed: cross(right, up) == -look, so X cross Y is +Z.
        assert(a:Cross(b) == Vector3.new(0, 0, 1))
        -- Does not commute.
        assert(b:Cross(a) == Vector3.new(0, 0, -1))

        local right = math.pi / 2
        assert(math.abs(a:Angle(b) - right) < 1e-5)
        -- With an axis the result is signed, which is the only way to tell a
        -- rotation from its mirror image.
        assert(a:Angle(b, Vector3.new(0, 0, 1)) > 0)
        assert(a:Angle(b, Vector3.new(0, 0, -1)) < 0)
    )") == "");

    CHECK(fixture.raises("return Vector3.new(1, 2, 3):Nope()", "script.err.unknown_member"));
}

// --- CFrame ------------------------------------------------------------------

TEST_CASE("CFrame carries an f64 translation, which is the whole of ADR 0014")
{
    Fixture fixture;

    // Two frames ten million metres out, one metre apart. Through f32 the
    // difference does not survive; through the f64 translation it does.
    CHECK(fixture.failure(R"(
        local near = CFrame.new(10000000, 0, 0)
        local far = CFrame.new(10000001, 0, 0)
        assert(near ~= far)

        local middle = near:Lerp(far, 0.5)
        -- Position is the f32 rounding, so read the difference through the
        -- transform rather than through a vector.
        local delta = near:ToObjectSpace(middle)
        assert(math.abs(delta.Position.x - 0.5) < 1e-3, tostring(delta.Position.x))
    )") == "");
}

TEST_CASE("the basis columns follow the handedness the document fixes")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local identity = CFrame.identity
        assert(identity.RightVector == Vector3.new(1, 0, 0))
        assert(identity.UpVector == Vector3.new(0, 1, 0))
        -- Forward is -Z, which is why this is not (0, 0, 1).
        assert(identity.LookVector == Vector3.new(0, 0, -1))
        assert(identity.RightVector:Cross(identity.UpVector) == -identity.LookVector)

        -- A +pi/2 yaw takes LookVector from (0, 0, -1) to (-1, 0, 0).
        local yawed = CFrame.fromEuler(0, math.pi / 2, 0)
        local look = yawed.LookVector
        assert(math.abs(look.x + 1) < 1e-5, tostring(look.x))
        assert(math.abs(look.z) < 1e-5, tostring(look.z))
    )") == "");
}

TEST_CASE("CFrame composition and the space conversions mean what they say")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local frame = CFrame.new(1, 2, 3)

        -- `*` on a vector transforms a POINT: the translation applies.
        assert(frame * Vector3.new(0, 0, 0) == Vector3.new(1, 2, 3))
        assert(frame:PointToWorldSpace(Vector3.zero) == Vector3.new(1, 2, 3))
        -- A direction takes the rotation only, so a pure translation leaves it.
        assert(frame:VectorToWorldSpace(Vector3.new(1, 0, 0)) == Vector3.new(1, 0, 0))

        assert(frame:PointToObjectSpace(Vector3.new(1, 2, 3)) == Vector3.zero)
        assert(frame:ToObjectSpace(frame) == CFrame.identity)
        assert(frame:ToWorldSpace(CFrame.identity) == frame)
        assert(frame * frame:Inverse() == CFrame.identity)

        -- Rotation is the same basis at the origin, so a pure translation's is
        -- the identity.
        assert(frame.Rotation == CFrame.identity)
    )") == "");
}

TEST_CASE("the euler round trip is a round trip in every order")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local orders = {
            Enum.RotationOrder.XYZ,
            Enum.RotationOrder.XZY,
            Enum.RotationOrder.YXZ,
            Enum.RotationOrder.YZX,
            Enum.RotationOrder.ZXY,
            Enum.RotationOrder.ZYX,
        }

        for _, order in orders do
            local built = CFrame.fromEuler(0.3, -0.8, 1.1, order)
            local rx, ry, rz = built:ToEuler(order)
            local rebuilt = CFrame.fromEuler(rx, ry, rz, order)
            -- The angles may differ by an equivalent triple; the rotation must
            -- not. Compared through a transformed direction, since a CFrame
            -- comparison is exact and floating point is not.
            local a = built:VectorToWorldSpace(Vector3.new(1, 2, 3))
            local b = rebuilt:VectorToWorldSpace(Vector3.new(1, 2, 3))
            assert((a - b).Magnitude < 1e-4, tostring(order))
        end

        -- YXZ wherever the order is omitted.
        local defaulted = CFrame.fromEuler(0.3, -0.8, 1.1)
        local explicit = CFrame.fromEuler(0.3, -0.8, 1.1, Enum.RotationOrder.YXZ)
        assert(defaulted == explicit)
    )") == "");
}

TEST_CASE("axis-angle and quaternion round-trip, with w last")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local built = CFrame.fromAxisAngle(Vector3.new(0.3, -0.8, 0.5), 1.1)
        local axis, angle = built:ToAxisAngle()
        local rebuilt = CFrame.fromAxisAngle(axis, angle)
        assert((rebuilt.LookVector - built.LookVector).Magnitude < 1e-4)

        -- The axis is normalized on the way in, so its length carries no
        -- meaning.
        local scaled = CFrame.fromAxisAngle(Vector3.new(3, -8, 5), 1.1)
        local unit = CFrame.fromAxisAngle(Vector3.new(0.3, -0.8, 0.5), 1.1)
        assert((scaled.LookVector - unit.LookVector).Magnitude < 1e-5)

        local qx, qy, qz, qw = built:ToQuaternion()
        local fromQuat = CFrame.fromQuaternion(Vector3.zero, qx, qy, qz, qw)
        assert((fromQuat.LookVector - built.LookVector).Magnitude < 1e-4)
    )") == "");
}

TEST_CASE("fromMatrix stores what it is given, and Orthonormalize is the explicit repair")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        -- A deliberately skewed basis. A constructor that silently repaired it
        -- would hide the bug that produced it.
        local skewed = CFrame.fromMatrix(
            Vector3.new(1, 2, 3),
            Vector3.new(2, 0, 0),
            Vector3.new(0.1, 3, 0),
            Vector3.new(0, 0, 1)
        )
        assert(skewed.RightVector == Vector3.new(2, 0, 0))
        assert(skewed.Position == Vector3.new(1, 2, 3))

        local repaired = skewed:Orthonormalize()
        assert(math.abs(repaired.RightVector.Magnitude - 1) < 1e-5)
        assert(math.abs(repaired.UpVector.Magnitude - 1) < 1e-5)
        assert(math.abs(repaired.RightVector:Dot(repaired.UpVector)) < 1e-5)
        -- The translation is preserved and the look axis is authoritative.
        assert(repaired.Position == Vector3.new(1, 2, 3))
        assert((repaired.LookVector - Vector3.new(0, 0, -1)).Magnitude < 1e-5)
    )") == "");
}

TEST_CASE("lookAt degenerates to the identity rotation rather than to NaN")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local at = CFrame.lookAt(Vector3.new(0, 0, 5), Vector3.new(0, 0, 0))
        assert((at.LookVector - Vector3.new(0, 0, -1)).Magnitude < 1e-5)
        assert(at.Position == Vector3.new(0, 0, 5))

        -- A camera pointed at itself should stop moving, not poison every value
        -- it touches for the rest of the run.
        local degenerate = CFrame.lookAt(Vector3.new(1, 1, 1), Vector3.new(1, 1, 1))
        assert(degenerate.Position == Vector3.new(1, 1, 1))
        assert(degenerate.LookVector == Vector3.new(0, 0, -1))
    )") == "");
}

TEST_CASE("a CFrame is immutable and its members have one spelling")
{
    Fixture fixture;

    CHECK(fixture.raises("CFrame.new().Position = Vector3.zero", "script.err.unknown_member"));
    CHECK(fixture.raises("return CFrame.new().position", "script.err.unknown_member"));
    CHECK(fixture.raises("return CFrame.new():Nope()", "script.err.unknown_member"));
}

// --- Color3 ------------------------------------------------------------------

TEST_CASE("Color3 channels are not clamped, because HDR values are legal")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local hdr = Color3.new(2.5, -0.5, 1)
        assert(hdr.R == 2.5)
        assert(hdr.G == -0.5)
        assert(hdr.B == 1)

        assert(Color3.new() == Color3.new(0, 0, 0))
        assert(Color3.fromRGB(255, 0, 0) == Color3.new(1, 0, 0))
    )") == "");

    // One spelling: `c.r` is an error, not nil.
    CHECK(fixture.raises("return Color3.new().r", "script.err.unknown_member"));
}

TEST_CASE("HSV is a turn in all three components")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        -- fromHSV(1/3, 1, 1) is green, not a hue of one third of a degree.
        local green = Color3.fromHSV(1 / 3, 1, 1)
        assert(math.abs(green.R) < 1e-5)
        assert(math.abs(green.G - 1) < 1e-5)
        assert(math.abs(green.B) < 1e-5)

        local h, s, v = Color3.new(1, 0, 0):ToHSV()
        assert(math.abs(h) < 1e-5 and math.abs(s - 1) < 1e-5 and math.abs(v - 1) < 1e-5)
    )") == "");
}

TEST_CASE("the hex round trip is defined in both directions")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        assert(Color3.fromHex("#ff8800"):ToHex() == "ff8800")
        assert(Color3.fromHex("ff8800") == Color3.fromHex("#ff8800"))
        assert(Color3.fromHex("#FF8800") == Color3.fromHex("#ff8800"))
        -- The shorthand doubles each digit, so #f80 and #ff8800 are the same.
        assert(Color3.fromHex("#f80") == Color3.fromHex("#ff8800"))
        assert(Color3.fromHex("f80") == Color3.fromHex("#ff8800"))

        assert(Color3.new(0, 0, 0):ToHex() == "000000")
        assert(Color3.new(1, 1, 1):ToHex() == "ffffff")
        -- Clamped on the way out, because eight bits cannot hold an HDR value.
        assert(Color3.new(2, -1, 0.5):ToHex() == "ff0080")
    )") == "");

    CHECK(fixture.raises(R"(Color3.fromHex("nothex"))", "script.err.color_hex_invalid"));
    CHECK(fixture.raises(R"(Color3.fromHex("#ff88"))", "script.err.color_hex_invalid"));
    CHECK(fixture.raises(R"(Color3.fromHex(""))", "script.err.color_hex_invalid"));
}

TEST_CASE("Color3 lerp hits both ends")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local black = Color3.new(0, 0, 0)
        local white = Color3.new(1, 1, 1)
        assert(black:Lerp(white, 0) == black)
        assert(black:Lerp(white, 1) == white)
        assert(black:Lerp(white, 0.5) == Color3.new(0.5, 0.5, 0.5))
    )") == "");
}

// --- Random ------------------------------------------------------------------

TEST_CASE("a seeded stream is the same stream twice")
{
    Fixture fixture;

    // The level-B guarantee recorded replays rest on: replays store seeds, not
    // draws (ADR 0025).
    CHECK(fixture.failure(R"(
        local a = Random.new(42)
        local b = Random.new(42)
        for _ = 1, 32 do
            assert(a:NextNumber() == b:NextNumber())
        end

        -- Truncated toward zero, so these are the same stream.
        assert(Random.new(2.9):NextNumber() == Random.new(2):NextNumber())
    )") == "");
}

TEST_CASE("Clone continues the same sequence independently")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local source = Random.new(7)
        source:NextNumber()
        source:NextNumber()

        local branch = source:Clone()
        -- Not the same object, and not equal by value either: a generator has
        -- identity, not equality.
        assert(branch ~= source)

        for _ = 1, 16 do
            assert(branch:NextNumber() == source:NextNumber())
        end
    )") == "");
}

TEST_CASE("ranges are half-open except NextInteger, which says so")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local rng = Random.new(1)
        for _ = 1, 256 do
            local n = rng:NextNumber()
            assert(n >= 0 and n < 1)
            local ranged = rng:NextNumber(10, 20)
            assert(ranged >= 10 and ranged < 20)
            local whole = rng:NextInteger(3, 5)
            assert(whole == 3 or whole == 4 or whole == 5)
        end

        -- Uniform over the sphere, not merely unit length.
        for _ = 1, 64 do
            assert(math.abs(rng:NextUnitVector().Magnitude - 1) < 1e-5)
        end
    )") == "");

    CHECK(fixture.raises("Random.new(1):NextInteger(5, 3)", "script.err.random_range"));
    CHECK(fixture.raises("Random.new(1):NextInteger(1.5, 3)", "script.err.random_range"));
    CHECK(fixture.raises("Random.new(1):NextNumber(20, 10)", "script.err.random_range"));
}

TEST_CASE("an unseeded generator draws from the world, not from a clock")
{
    Fixture fixture;

    // R10: a wall-clock seed would make a replay diverge on the second run and
    // there would be nothing to compare. Two worlds with the same seed have to
    // agree even here.
    Fixture other;
    const std::string source = R"(
        local rng = Random.new()
        local total = 0
        for _ = 1, 8 do
            total = total + rng:NextNumber()
        end
        assert(total > 0)
        return total
    )";

    CHECK(fixture.failure(source) == "");
    CHECK(other.failure(source) == "");
}

// --- Enum --------------------------------------------------------------------

TEST_CASE("an enum item carries its name, its value and its enum object")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local ball = Enum.PartShape.Ball
        assert(ball.Name == "Ball")
        assert(ball.Value == 1)
        -- The enum OBJECT, not its name as a string.
        assert(ball.EnumType == Enum.PartShape)

        assert(Enum.PartShape.Ball == Enum.PartShape.Ball)
        assert(Enum.PartShape.Ball ~= Enum.PartShape.Block)
        assert(tostring(ball) == "Enum.PartShape.Ball")
        assert(tostring(Enum.PartShape) == "Enum.PartShape")
    )") == "");

    CHECK(fixture.raises("return Enum.PartShape.NotAShape", "script.err.unknown_member"));
    CHECK(fixture.raises("return Enum.NotAnEnum", "script.err.unknown_member"));
}

TEST_CASE("GetEnumItems is fresh and in declaration order")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local items = Enum.PartShape:GetEnumItems()
        assert(#items == 5)
        assert(items[1] == Enum.PartShape.Block)
        assert(items[2] == Enum.PartShape.Ball)
        assert(items[5] == Enum.PartShape.Wedge)

        -- Fresh on every call, so a caller may sort it.
        local again = Enum.PartShape:GetEnumItems()
        assert(again ~= items)
        table.remove(items, 1)
        assert(#Enum.PartShape:GetEnumItems() == 5)

        -- LogLevel's values order the levels, which is what a handler filters on.
        assert(Enum.LogLevel.Warning.Value > Enum.LogLevel.Info.Value)
        assert(#Enum.RotationOrder:GetEnumItems() == 6)
    )") == "");
}

TEST_CASE("an enum property takes an item of its own enum and nothing else")
{
    Fixture fixture;

    CHECK(fixture.failure(R"(
        local part = Instance.new("Part")
        assert(part.Shape == Enum.PartShape.Block)

        part.Shape = Enum.PartShape.Cylinder
        assert(part.Shape == Enum.PartShape.Cylinder)
        assert(part.Shape.Name == "Cylinder")
    )") == "");

    // An item of a different enum carries a different id, and a raw number is
    // not an item at all.
    CHECK(fixture.raises("Instance.new('Part').Shape = Enum.LogLevel.Info", "scene.err.expected_enum_item"));
    CHECK(fixture.raises("Instance.new('Part').Shape = 1", "scene.err.expected_enum_item"));
}
