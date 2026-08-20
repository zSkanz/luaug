#include <doctest/doctest.h>

#include <cmath>

#include "luaug/core/math.h"

using namespace luaug::core;

namespace
{

constexpr f32 kEpsilon = 1e-5f;

// A quarter turn. Written out rather than derived so the sign tests below read
// as "a +90 degree turn about X takes +Y to +Z" and nothing else.
constexpr f32 kHalfPi = 1.57079632679f;

bool near(f32 a, f32 b) noexcept
{
    return std::fabs(a - b) <= kEpsilon;
}

bool near(Vec3 a, Vec3 b) noexcept
{
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

bool near(Color3 a, Color3 b) noexcept
{
    return near(a.r, b.r) && near(a.g, b.g) && near(a.b, b.b);
}

bool near(const Mat3& a, const Mat3& b) noexcept
{
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row)
            if (!near(a.m[c][row], b.m[c][row]))
                return false;
    return true;
}

// f64 comparisons keep their own helper: promoting an f32 epsilon to compare
// world coordinates would be both wrong and a -Wdouble-promotion error. The
// default tolerance is loose enough for a round trip through an f32 rotation,
// whose entries are only orthonormal to about 1e-7.
bool nearD(f64 a, f64 b, f64 tolerance = 1e-5) noexcept
{
    return std::fabs(a - b) <= tolerance;
}

bool nearD(DVec3 a, DVec3 b, f64 tolerance = 1e-5) noexcept
{
    return nearD(a.x, b.x, tolerance) && nearD(a.y, b.y, tolerance) && nearD(a.z, b.z, tolerance);
}

Vec3 col(const Mat3& m, int index) noexcept
{
    return {m.m[index][0], m.m[index][1], m.m[index][2]};
}

Vec3 translationOf(const Mat4& m) noexcept
{
    return {m.m[3][0], m.m[3][1], m.m[3][2]};
}

bool anyNan(const Mat3& m) noexcept
{
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row)
            if (std::isnan(m.m[c][row]))
                return true;
    return false;
}

bool anyNan(DVec3 v) noexcept
{
    return std::isnan(v.x) || std::isnan(v.y) || std::isnan(v.z);
}

// True when the three columns are unit length, mutually perpendicular and
// right-handed -- the last of which is the half people forget, and the half
// that mirrors a scene if it is wrong.
bool isOrthonormal(const Mat3& m) noexcept
{
    const Vec3 right = col(m, 0);
    const Vec3 up = col(m, 1);
    const Vec3 back = col(m, 2);

    return near(length(right), 1.0f) && near(length(up), 1.0f) && near(length(back), 1.0f) &&
           near(dot(right, up), 0.0f) && near(dot(up, back), 0.0f) && near(dot(right, back), 0.0f) &&
           near(cross(right, up), back);
}

} // namespace

TEST_CASE("Vec3 has the layout the Luau vector has")
{
    // ADR 0013: the script-facing Vector3 IS the native Luau vector, and a
    // binding reinterprets `lua_tovector`'s `const float*` as this. The
    // static_asserts in math.h are the real guard; this checks the consequence
    // they exist for -- that the three components are contiguous in order.
    const Vec3 v{1.0f, 2.0f, 3.0f};
    const f32* raw = &v.x;

    CHECK(raw[0] == 1.0f);
    CHECK(raw[1] == 2.0f);
    CHECK(raw[2] == 3.0f);
}

TEST_CASE("vector algebra")
{
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{4.0f, 5.0f, 6.0f};

    CHECK(a + b == Vec3{5.0f, 7.0f, 9.0f});
    CHECK(b - a == Vec3{3.0f, 3.0f, 3.0f});
    CHECK(a * 2.0f == Vec3{2.0f, 4.0f, 6.0f});
    CHECK(dot(a, b) == 32.0f);

    // Right-handed: X cross Y is +Z. Getting this backwards flips every normal
    // in the engine, and the symptom is "lighting looks inside out", not an
    // error.
    CHECK(cross(Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}) == Vec3{0.0f, 0.0f, 1.0f});

    CHECK(near(length(Vec3{3.0f, 4.0f, 0.0f}), 5.0f));
    CHECK(near(normalize(Vec3{0.0f, 8.0f, 0.0f}), Vec3{0.0f, 1.0f, 0.0f}));
}

TEST_CASE("normalizing zero yields zero, not NaN")
{
    // A stopped velocity and a degenerate edge are both legitimate zero
    // vectors. A NaN here would propagate through a transform and be far
    // harder to trace than a zero.
    const Vec3 result = normalize(Vec3{});

    CHECK(result == Vec3{});
    CHECK_FALSE(std::isnan(result.x));
}

TEST_CASE("matrix composition reads backwards from the order it applies")
{
    const Mat4 move = translation({10.0f, 0.0f, 0.0f});
    const Mat4 grow = scaling({2.0f, 2.0f, 2.0f});

    // Column-vector convention: `move * grow` scales first, then translates.
    CHECK(near(transformPoint(move * grow, Vec3{1.0f, 0.0f, 0.0f}), Vec3{12.0f, 0.0f, 0.0f}));
    // The other order translates first, so the scale multiplies the offset too.
    CHECK(near(transformPoint(grow * move, Vec3{1.0f, 0.0f, 0.0f}), Vec3{22.0f, 0.0f, 0.0f}));
}

TEST_CASE("a direction ignores translation, a point does not")
{
    const Mat4 move = translation({5.0f, 5.0f, 5.0f});

    CHECK(near(transformPoint(move, Vec3{1.0f, 0.0f, 0.0f}), Vec3{6.0f, 5.0f, 5.0f}));
    CHECK(near(transformDirection(move, Vec3{1.0f, 0.0f, 0.0f}), Vec3{1.0f, 0.0f, 0.0f}));
}

TEST_CASE("lookAt puts the target down -Z in view space")
{
    // The convention that decides whether anything is visible at all: the
    // camera looks along -Z (api-design.md's LookVector). A target in front of
    // the eye must land at negative Z after the view transform.
    const Mat4 view = lookAt({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    const Vec3 target = transformPoint(view, Vec3{0.0f, 0.0f, 0.0f});

    CHECK(near(target.x, 0.0f));
    CHECK(near(target.y, 0.0f));
    CHECK(near(target.z, -10.0f));

    // The eye itself lands at the origin of view space.
    CHECK(near(transformPoint(view, Vec3{0.0f, 0.0f, 10.0f}), Vec3{}));
}

TEST_CASE("perspective maps the depth range to [0, 1]")
{
    // Vulkan, D3D12 and Metal all want [0, 1], not OpenGL's [-1, 1]. The wrong
    // one does not error -- it wastes half the depth buffer and makes
    // z-fighting appear at distances that look arbitrary.
    constexpr f32 nearZ = 0.1f;
    constexpr f32 farZ = 100.0f;
    const Mat4 projection = perspective(1.0472f, 16.0f / 9.0f, nearZ, farZ);

    const auto depthOf = [&projection](f32 viewZ)
    {
        // Manual w-divide: transformPoint drops w, and w is the whole point of
        // a projection matrix.
        const f32 clipZ = projection.m[2][2] * viewZ + projection.m[3][2];
        const f32 clipW = projection.m[2][3] * viewZ;
        return clipZ / clipW;
    };

    // Looking down -Z, so the near and far planes sit at negative view Z.
    CHECK(near(depthOf(-nearZ), 0.0f));
    CHECK(near(depthOf(-farZ), 1.0f));
}

// --- Mat3 --------------------------------------------------------------------

TEST_CASE("the default Mat3 is the identity and behaves like one")
{
    const Mat3 identity;
    const Vec3 v{1.0f, -2.0f, 3.0f};

    CHECK(near(identity * v, v));
    CHECK(near(identity * identity, identity));
    CHECK(isOrthonormal(identity));

    // The column meanings the whole file rests on: m[0] right, m[1] up, m[2]
    // back -- so the look direction is -m[2], not m[2] (api-design.md §2.3).
    CHECK(near(col(identity, 0), Vec3{1.0f, 0.0f, 0.0f}));
    CHECK(near(col(identity, 1), Vec3{0.0f, 1.0f, 0.0f}));
    CHECK(near(col(identity, 2), Vec3{0.0f, 0.0f, 1.0f}));
}

TEST_CASE("Mat3 composition is associative and reads backwards from the order it applies")
{
    const Mat3 yaw = rotationY(kHalfPi);
    const Mat3 roll = rotationZ(kHalfPi);
    const Vec3 v{1.0f, 2.0f, 3.0f};

    // The defining property of the product: composing then applying is the same
    // as applying twice, in the right order.
    CHECK(near((yaw * roll) * v, yaw * (roll * v)));
    CHECK(near((roll * yaw) * v, roll * (yaw * v)));

    // And the order matters, which is what makes the check above worth making.
    // `yaw * roll` rolls first: +X goes to +Y (roll), and +Y is the yaw axis, so
    // it survives. `roll * yaw` yaws first: +X goes to -Z, which the roll leaves
    // alone.
    CHECK(near(yaw * roll * Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}));
    CHECK(near(roll * yaw * Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -1.0f}));
}

TEST_CASE("each rotation turns the axis the right-hand rule says, in the direction it says")
{
    // Signs, not magnitudes. A rotation that turns the right pair of axes the
    // wrong way still has unit columns and passes every orthonormality check --
    // it just mirrors the game.
    const Mat3 x = rotationX(kHalfPi);
    CHECK(near(x * Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}));  // +Y -> +Z
    CHECK(near(x * Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, -1.0f, 0.0f})); // +Z -> -Y
    CHECK(near(x * Vec3{1.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}));  // the axis is fixed

    const Mat3 y = rotationY(kHalfPi);
    CHECK(near(y * Vec3{0.0f, 0.0f, 1.0f}, Vec3{1.0f, 0.0f, 0.0f}));  // +Z -> +X
    CHECK(near(y * Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -1.0f})); // +X -> -Z
    CHECK(near(y * Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}));

    const Mat3 z = rotationZ(kHalfPi);
    CHECK(near(z * Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}));  // +X -> +Y
    CHECK(near(z * Vec3{0.0f, 1.0f, 0.0f}, Vec3{-1.0f, 0.0f, 0.0f})); // +Y -> -X
    CHECK(near(z * Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 0.0f, 1.0f}));

    // api-design.md §2.3 states this exact consequence: "A +pi/2 rotation about
    // Y therefore takes LookVector from (0, 0, -1) to (-1, 0, 0)."
    CHECK(near(y * Vec3{0.0f, 0.0f, -1.0f}, Vec3{-1.0f, 0.0f, 0.0f}));

    // A rotation is orthonormal by construction; if one of these fails the
    // matrix is not a rotation at all.
    CHECK(isOrthonormal(x));
    CHECK(isOrthonormal(y));
    CHECK(isOrthonormal(z));

    // A negative angle undoes a positive one, which pins the sine's sign a
    // second way.
    CHECK(near(rotationX(-0.7f) * rotationX(0.7f), Mat3{}));
    CHECK(near(rotationY(-0.7f) * rotationY(0.7f), Mat3{}));
    CHECK(near(rotationZ(-0.7f) * rotationZ(0.7f), Mat3{}));
}

TEST_CASE("transpose swaps rows and columns, and inverts a rotation")
{
    Mat3 m;
    m.m[0][1] = 5.0f;
    m.m[2][0] = -3.0f;

    const Mat3 t = transpose(m);
    CHECK(near(t.m[1][0], 5.0f));
    CHECK(near(t.m[0][2], -3.0f));
    CHECK(near(transpose(t), m));

    // The property `inverse` relies on: for an orthonormal basis the transpose
    // IS the inverse, in both orders.
    const Mat3 rotation = rotationY(0.9f) * rotationX(-0.4f) * rotationZ(2.1f);
    CHECK(near(transpose(rotation) * rotation, Mat3{}));
    CHECK(near(rotation * transpose(rotation), Mat3{}));
}

TEST_CASE("orthonormalize repairs a skewed basis with the look axis authoritative")
{
    // Back is long, up is not perpendicular to it, and right is garbage that
    // agrees with neither. Only the look axis survives as a direction.
    Mat3 skewed;
    skewed.m[0][0] = 4.0f; // right: nonsense on purpose
    skewed.m[0][1] = 4.0f;
    skewed.m[0][2] = 4.0f;
    skewed.m[1][0] = 0.0f; // up: a hint, and not a perpendicular one
    skewed.m[1][1] = 1.0f;
    skewed.m[1][2] = 0.0f;
    skewed.m[2][0] = 2.0f; // back: length 2 sqrt(2), direction (1, 1, 0) normalised
    skewed.m[2][1] = 2.0f;
    skewed.m[2][2] = 0.0f;

    const Mat3 fixed = orthonormalize(skewed);

    CHECK(isOrthonormal(fixed));

    // The look axis is authoritative (math.h, api-design.md §2.3 on
    // `CFrame:Orthonormalize`): its direction comes through untouched.
    CHECK(near(col(fixed, 2), normalize(col(skewed, 2))));

    // Up was only a hint, so it moved to the perpendicular that the look axis
    // allows -- it is not the input up, and it is not garbage either.
    CHECK(near(col(fixed, 1), Vec3{-0.70710678f, 0.70710678f, 0.0f}));
    CHECK_FALSE(near(col(fixed, 1), normalize(col(skewed, 1))));

    // Right is derived, never preserved. If the input's right column survived in
    // any form this would be (1, 1, 1) normalised.
    CHECK(near(col(fixed, 0), Vec3{0.0f, 0.0f, -1.0f}));

    // An already-orthonormal basis is a fixed point.
    const Mat3 clean = rotationY(0.3f) * rotationX(1.1f);
    CHECK(near(orthonormalize(clean), clean));
}

TEST_CASE("orthonormalize does not produce NaN for a degenerate input")
{
    // Up parallel to the look axis: the hint carries no roll, so a roll is
    // chosen. The authoritative axis is what must survive.
    Mat3 parallel;
    parallel.m[1][0] = 0.0f;
    parallel.m[1][1] = 0.0f;
    parallel.m[1][2] = 3.0f; // up == back, scaled
    const Mat3 repaired = orthonormalize(parallel);

    CHECK_FALSE(anyNan(repaired));
    CHECK(isOrthonormal(repaired));
    CHECK(near(col(repaired, 2), Vec3{0.0f, 0.0f, 1.0f}));

    // Same again with the look axis along +Y, where the fallback hint cannot be
    // +Y itself. This is the branch that a single hardcoded fallback breaks.
    Mat3 upward;
    upward.m[1][0] = 0.0f;
    upward.m[1][1] = 1.0f;
    upward.m[1][2] = 0.0f;
    upward.m[2][0] = 0.0f;
    upward.m[2][1] = 1.0f;
    upward.m[2][2] = 0.0f;
    const Mat3 repairedUpward = orthonormalize(upward);

    CHECK_FALSE(anyNan(repairedUpward));
    CHECK(isOrthonormal(repairedUpward));
    CHECK(near(col(repairedUpward, 2), Vec3{0.0f, 1.0f, 0.0f}));

    // A zero look axis leaves nothing authoritative to keep, so the identity is
    // the only answer that is not a collapsed basis.
    Mat3 collapsed;
    collapsed.m[2][2] = 0.0f;
    CHECK(near(orthonormalize(collapsed), Mat3{}));
}

// --- CFrameD -----------------------------------------------------------------

TEST_CASE("CFrameD composition applies b first")
{
    CFrameD spin;
    spin.rotation = rotationZ(kHalfPi);

    CFrameD shift;
    shift.position = {2.0, 0.0, 0.0};

    // "First `shift`, then `spin`": the offset is expressed in spin's basis, so
    // the quarter turn about Z carries it onto +Y.
    CHECK(nearD((spin * shift).position, DVec3{0.0, 2.0, 0.0}));
    // The other order translates in world space, and the rotation never sees it.
    CHECK(nearD((shift * spin).position, DVec3{2.0, 0.0, 0.0}));

    // The property that makes the order a fact rather than a convention.
    const DVec3 p{1.0, 0.0, 0.0};
    CHECK(nearD(transformPoint(spin * shift, p), transformPoint(spin, transformPoint(shift, p))));
    CHECK(nearD(transformPoint(shift * spin, p), transformPoint(shift, transformPoint(spin, p))));
    CHECK(nearD(transformPoint(spin * shift, p), DVec3{0.0, 3.0, 0.0}));
    CHECK(nearD(transformPoint(shift * spin, p), DVec3{2.0, 1.0, 0.0}));

    // The rotations compose the same way the Mat3 product does.
    CFrameD other;
    other.rotation = rotationY(0.6f);
    CHECK(near((spin * other).rotation, spin.rotation * other.rotation));
}

TEST_CASE("a CFrameD point takes the translation, a direction does not")
{
    CFrameD cf;
    cf.position = {5.0, -2.0, 1.0};
    cf.rotation = rotationY(kHalfPi);

    // +Z rotates onto +X, then the translation applies.
    CHECK(nearD(transformPoint(cf, DVec3{0.0, 0.0, 1.0}), DVec3{6.0, -2.0, 1.0}));
    // The same input as a direction only rotates. A normal run through the point
    // path comes out wrong by exactly the frame's position (api-design.md §2.3).
    CHECK(near(transformDirection(cf, Vec3{0.0f, 0.0f, 1.0f}), Vec3{1.0f, 0.0f, 0.0f}));
}

TEST_CASE("CFrameD inverse round-trips a point")
{
    CFrameD cf;
    cf.position = {12.0, -3.0, 7.5};
    cf.rotation = rotationY(0.7f) * rotationX(-0.3f);

    const DVec3 p{2.0, 5.0, -1.0};
    CHECK(nearD(transformPoint(inverse(cf), transformPoint(cf, p)), p));
    CHECK(nearD(transformPoint(cf, transformPoint(inverse(cf), p)), p));

    // Composing with the inverse is the identity frame, in either order.
    CHECK(nearD((cf * inverse(cf)).position, DVec3{}));
    CHECK(near((cf * inverse(cf)).rotation, Mat3{}));
    CHECK(nearD((inverse(cf) * cf).position, DVec3{}));
    CHECK(near((inverse(cf) * cf).rotation, Mat3{}));

    // The rotation half is the transpose, not a general inverse -- stated in the
    // implementation and pinned here.
    CHECK(near(inverse(cf).rotation, transpose(cf.rotation)));

    // World -> object still round-trips to f64 precision ten million units out,
    // because the frame's translation is applied and removed by the SAME
    // rotation: the two 1e7-sized terms cancel in f64 before the f32 basis's
    // error can scale with them. This is the direction that matters -- bringing
    // world positions into a distant object's space -- and it is what an f32
    // CFrame could not do.
    CFrameD distant = cf;
    distant.position = {1.0e7, -3.0, 7.5};
    CHECK(nearD(transformPoint(inverse(distant), transformPoint(distant, p)), p));

    // The opposite direction is deliberately NOT asserted at this magnitude, and
    // the asymmetry is a property of the type rather than a defect: it puts the
    // translation through R^T and then through R, and an f32 R * R^T is the
    // identity only to about 1e-7 -- a metre of residual at 1e7. The answer is
    // not a looser epsilon, it is not composing inverses at world scale, which
    // is what the floating origin (ADR 0014, M7) exists to make unnecessary.
    const DVec3 wrongWay = transformPoint(distant, transformPoint(inverse(distant), p));
    CHECK_FALSE(anyNan(wrongWay));
    CHECK(nearD(wrongWay, p, 10.0));
}

TEST_CASE("lookAtCFrame aims -m[2] at the target")
{
    const DVec3 eye{0.0, 0.0, 10.0};
    const CFrameD cf = lookAtCFrame(eye, DVec3{0.0, 0.0, 0.0}, Vec3{0.0f, 1.0f, 0.0f});

    CHECK(nearD(cf.position, eye));
    CHECK(isOrthonormal(cf.rotation));
    // LookVector is -m[2] and points from the eye at the target.
    CHECK(near(-col(cf.rotation, 2), Vec3{0.0f, 0.0f, -1.0f}));
    CHECK(near(col(cf.rotation, 0), Vec3{1.0f, 0.0f, 0.0f}));
    CHECK(near(col(cf.rotation, 1), Vec3{0.0f, 1.0f, 0.0f}));

    // An off-axis target, where the sign is not hidden by an axis-aligned answer.
    const DVec3 from{3.0, 4.0, 5.0};
    const DVec3 to{-1.0, 4.0, 2.0};
    const CFrameD oblique = lookAtCFrame(from, to, Vec3{0.0f, 1.0f, 0.0f});
    CHECK(near(-col(oblique.rotation, 2), normalize(Vec3{-4.0f, 0.0f, -3.0f})));
    CHECK(isOrthonormal(oblique.rotation));

    // The frame really is the frame: its own -Z, transformed as a direction,
    // still points at the target.
    CHECK(near(transformDirection(oblique, Vec3{0.0f, 0.0f, -1.0f}), normalize(Vec3{-4.0f, 0.0f, -3.0f})));
}

TEST_CASE("lookAtCFrame degenerates to the identity rotation rather than NaN")
{
    // api-design.md §2.3: "a camera pointed at itself should stop moving, not
    // poison every value it touches for the rest of the run." Both degenerate
    // inputs land on the identity rotation AT the eye, not on a zero frame and
    // not on NaN.
    const DVec3 eye{4.0, -7.0, 2.0};

    SUBCASE("target equal to the eye")
    {
        const CFrameD cf = lookAtCFrame(eye, eye, Vec3{0.0f, 1.0f, 0.0f});
        CHECK_FALSE(anyNan(cf.rotation));
        CHECK_FALSE(anyNan(cf.position));
        CHECK(near(cf.rotation, Mat3{}));
        CHECK(cf.position == eye);
    }

    SUBCASE("up hint parallel to the look direction")
    {
        // Looking straight down -Y with up = +Y: the hint has no roll to give.
        const CFrameD cf = lookAtCFrame(eye, DVec3{eye.x, eye.y - 5.0, eye.z}, Vec3{0.0f, 1.0f, 0.0f});
        CHECK_FALSE(anyNan(cf.rotation));
        CHECK(near(cf.rotation, Mat3{}));
        CHECK(cf.position == eye);
    }

    SUBCASE("up hint antiparallel to the look direction")
    {
        const CFrameD cf = lookAtCFrame(eye, DVec3{eye.x, eye.y + 5.0, eye.z}, Vec3{0.0f, 1.0f, 0.0f});
        CHECK_FALSE(anyNan(cf.rotation));
        CHECK(near(cf.rotation, Mat3{}));
    }

    SUBCASE("a zero up hint")
    {
        const CFrameD cf = lookAtCFrame(eye, DVec3{0.0, 0.0, 0.0}, Vec3{});
        CHECK_FALSE(anyNan(cf.rotation));
        CHECK(near(cf.rotation, Mat3{}));
    }

    SUBCASE("very nearly parallel is still a real frame, not a snap to identity")
    {
        // The fallback must not swallow a camera that is merely looking steeply
        // up: a hundredth of a degree off vertical is a usable basis.
        const CFrameD cf = lookAtCFrame(eye, DVec3{eye.x + 0.001, eye.y - 5.0, eye.z}, Vec3{0.0f, 1.0f, 0.0f});
        CHECK_FALSE(anyNan(cf.rotation));
        CHECK(isOrthonormal(cf.rotation));
        CHECK_FALSE(near(cf.rotation, Mat3{}));
    }
}

TEST_CASE("toRenderMatrix subtracts in f64, which is the whole of ADR 0014")
{
    // At 1e7 the gap between neighbouring f32 values is a full unit, so anything
    // narrowed before the subtraction arrives already quantised to metres.
    const DVec3 origin{1.0e7, 1.0e7, 1.0e7};

    CFrameD anchor;
    anchor.position = origin;

    CFrameD neighbour;
    neighbour.position = DVec3{origin.x + 0.25, origin.y, origin.z};

    CHECK(near(translationOf(toRenderMatrix(anchor, origin)), Vec3{}));
    CHECK(near(translationOf(toRenderMatrix(neighbour, origin)), Vec3{0.25f, 0.0f, 0.0f}));

    // ...and this is what the narrow-first implementation would have returned.
    // Not a restatement of the ADR: the quarter metre is provably gone in f32,
    // so the check above can only pass if the subtraction happened in f64.
    CHECK(toVec3(neighbour.position).x - toVec3(origin).x == 0.0f);

    // Relative geometry, not just the translation: a rotated frame a quarter
    // metre from the origin still puts its own local +Z one unit along world +X.
    CFrameD spun = neighbour;
    spun.rotation = rotationY(kHalfPi);
    const Mat4 rendered = toRenderMatrix(spun, origin);

    CHECK(near(transformPoint(rendered, Vec3{0.0f, 0.0f, 1.0f}), Vec3{1.25f, 0.0f, 0.0f}));
    CHECK(near(transformDirection(rendered, Vec3{0.0f, 0.0f, 1.0f}), Vec3{1.0f, 0.0f, 0.0f}));

    // The rotation is carried through unchanged and the matrix is affine: the
    // bottom row must stay (0, 0, 0, 1) or the perspective divide eats it.
    for (int c = 0; c < 3; ++c)
    {
        for (int row = 0; row < 3; ++row)
            CHECK(rendered.m[c][row] == spun.rotation.m[c][row]);
        CHECK(rendered.m[c][3] == 0.0f);
    }
    CHECK(rendered.m[3][3] == 1.0f);

    // An origin at the frame itself rebases to zero however far out it is.
    CHECK(near(translationOf(toRenderMatrix(spun, spun.position)), Vec3{}));
}

// --- Color3 ------------------------------------------------------------------

TEST_CASE("Color3 lerp hits both endpoints exactly")
{
    const Color3 a{0.1f, 0.2f, 0.3f};
    const Color3 b{0.9f, 0.4f, 0.5f};

    CHECK(lerp(a, b, 0.0f) == a);
    CHECK(lerp(a, b, 1.0f) == b);
    CHECK(near(lerp(a, b, 0.5f), Color3{0.5f, 0.3f, 0.4f}));
    CHECK(near(lerp(a, b, 0.25f), Color3{0.3f, 0.25f, 0.35f}));

    // Not clamped, in either argument: alpha outside [0, 1] extrapolates.
    CHECK(near(lerp(a, b, 2.0f), Color3{1.7f, 0.6f, 0.7f}));
    CHECK(near(lerp(a, b, -1.0f), Color3{-0.7f, 0.0f, 0.1f}));
}

TEST_CASE("HSV round-trips the saturated colours, with hue as a turn")
{
    struct Case
    {
        f32 hue;
        Color3 rgb;
    };

    // Hue in turns, not degrees -- api-design.md §2.3 spells out that
    // fromHSV(1/3, 1, 1) is green, which is the row that catches a degree-based
    // implementation.
    const Case cases[] = {
        {0.0f, {1.0f, 0.0f, 0.0f}},          // red
        {1.0f / 6.0f, {1.0f, 1.0f, 0.0f}},   // yellow
        {1.0f / 3.0f, {0.0f, 1.0f, 0.0f}},   // green
        {0.5f, {0.0f, 1.0f, 1.0f}},          // cyan
        {2.0f / 3.0f, {0.0f, 0.0f, 1.0f}},   // blue
        {5.0f / 6.0f, {1.0f, 0.0f, 1.0f}},   // magenta
        {1.0f / 12.0f, {1.0f, 0.5f, 0.0f}},  // orange: not on a sextant boundary
        {7.0f / 12.0f, {0.0f, 0.5f, 1.0f}},  // azure
    };

    for (const Case& c : cases)
    {
        CHECK(near(fromHsv(c.hue, 1.0f, 1.0f), c.rgb));

        f32 hue = -1.0f;
        f32 saturation = -1.0f;
        f32 value = -1.0f;
        toHsv(c.rgb, hue, saturation, value);

        CHECK(near(hue, c.hue));
        CHECK(near(saturation, 1.0f));
        CHECK(near(value, 1.0f));
        CHECK(near(fromHsv(hue, saturation, value), c.rgb));
    }

    // Partly saturated and dim, so none of the three components is 0 or 1 and a
    // swapped p/q/t is visible.
    const Color3 muted = fromHsv(0.6f, 0.4f, 0.7f);
    f32 hue = 0.0f;
    f32 saturation = 0.0f;
    f32 value = 0.0f;
    toHsv(muted, hue, saturation, value);
    CHECK(near(hue, 0.6f));
    CHECK(near(saturation, 0.4f));
    CHECK(near(value, 0.7f));
}

TEST_CASE("hue is a turn, so it wraps")
{
    // 1.0 is the same red as 0.0. An animated hue that keeps counting up must
    // not fall off the end of the wheel.
    CHECK(fromHsv(1.0f, 1.0f, 1.0f) == fromHsv(0.0f, 1.0f, 1.0f));
    CHECK(near(fromHsv(2.5f, 1.0f, 1.0f), fromHsv(0.5f, 1.0f, 1.0f)));
    CHECK(near(fromHsv(-1.0f / 6.0f, 1.0f, 1.0f), fromHsv(5.0f / 6.0f, 1.0f, 1.0f)));
}

TEST_CASE("an achromatic colour reports hue 0 and round-trips through it")
{
    // Every hue names the same grey, so there is no answer to report. 0 is the
    // one that round-trips, and pinning it is what stops the value drifting to
    // whatever the last branch happened to compute.
    const Color3 grey{0.5f, 0.5f, 0.5f};

    f32 hue = 9.0f;
    f32 saturation = 9.0f;
    f32 value = 9.0f;
    toHsv(grey, hue, saturation, value);

    CHECK(hue == 0.0f);
    CHECK(saturation == 0.0f);
    CHECK(near(value, 0.5f));
    CHECK(near(fromHsv(hue, saturation, value), grey));

    // Black: value 0 as well, so the saturation divide has a zero denominator.
    toHsv(Color3{}, hue, saturation, value);
    CHECK(hue == 0.0f);
    CHECK(saturation == 0.0f);
    CHECK(value == 0.0f);
    CHECK(near(fromHsv(hue, saturation, value), Color3{}));

    // Saturation is zero at every hue, so the hue is free and the grey survives.
    CHECK(near(fromHsv(0.42f, 0.0f, 0.5f), grey));
}

TEST_CASE("Color3 channels are not clamped, because HDR values are legal")
{
    // api-design.md §2.3: values outside 0-1 are legal and meaningful (HDR
    // emissive, tint multipliers over 1), so clamping belongs to the consumer.
    const Color3 hdr{3.0f, 1.5f, 0.0f};

    f32 hue = 0.0f;
    f32 saturation = 0.0f;
    f32 value = 0.0f;
    toHsv(hdr, hue, saturation, value);

    CHECK(near(value, 3.0f));
    CHECK(near(saturation, 1.0f));
    CHECK(near(hue, 1.0f / 12.0f));
    CHECK(near(fromHsv(hue, saturation, value), hdr));

    // A value above 1 comes straight back out of fromHsv.
    CHECK(near(fromHsv(0.0f, 0.0f, 4.0f), Color3{4.0f, 4.0f, 4.0f}));

    // Below zero too: an all-negative colour is achromatic with a negative
    // value, which is the branch a `max > 0` guard exists for.
    const Color3 negative{-0.25f, -0.25f, -0.25f};
    toHsv(negative, hue, saturation, value);
    CHECK(hue == 0.0f);
    CHECK(saturation == 0.0f);
    CHECK(near(value, -0.25f));
    CHECK(near(fromHsv(hue, saturation, value), negative));

    // And a mixed one, where saturation itself exceeds 1. Still a round trip.
    const Color3 mixed{1.0f, -1.0f, 0.0f};
    toHsv(mixed, hue, saturation, value);
    CHECK(near(saturation, 2.0f));
    CHECK(near(fromHsv(hue, saturation, value), mixed));

    // lerp between HDR endpoints does not clamp on the way through.
    CHECK(near(lerp(Color3{0.0f, 0.0f, 0.0f}, Color3{4.0f, -2.0f, 0.0f}, 0.5f), Color3{2.0f, -1.0f, 0.0f}));
}

// --- YXZ euler round trip ----------------------------------------------------
//
// `BasePart.Orientation` reads through `toEulerYxz` and writes through
// `fromEulerYxz`, so the two have to be exact inverses or reading a part's
// orientation right after setting it would return something else.

TEST_CASE("euler YXZ round-trips through the rotation it builds")
{
    const f32 angles[] = {-2.9f, -1.2f, -0.4f, 0.0f, 0.3f, 1.1f, 2.7f};
    // Pitch stays clear of the poles here; the pole is its own test below,
    // because there the pair genuinely is not recoverable.
    const f32 pitches[] = {-1.2f, -0.5f, 0.0f, 0.5f, 1.2f};
    for (const f32 yaw : angles)
    {
        for (const f32 pitch : pitches)
        {
            for (const f32 roll : angles)
            {
                const Mat3 built = fromEulerYxz(Vec3{pitch, yaw, roll});
                const Vec3 recovered = toEulerYxz(built);
                const Mat3 rebuilt = fromEulerYxz(recovered);

                // The angles themselves may differ by a full turn or by the
                // equivalent mirrored triple; the ROTATION must not.
                for (int column = 0; column < 3; ++column)
                {
                    for (int row = 0; row < 3; ++row)
                        CHECK(static_cast<f64>(built.m[column][row])
                              == doctest::Approx(static_cast<f64>(rebuilt.m[column][row])).epsilon(1e-4));
                }
            }
        }
    }
}

TEST_CASE("euler YXZ keeps pitch in the principal range and resolves the poles")
{
    // Pitch is the middle rotation, so its branch is the one that has to be
    // chosen: [-pi/2, pi/2] is the documented half.
    const Vec3 recovered = toEulerYxz(fromEulerYxz(Vec3{2.0f, 0.3f, 0.4f}));
    CHECK(static_cast<f64>(recovered.x) <= doctest::Approx(1.5708).epsilon(1e-4));
    CHECK(static_cast<f64>(recovered.x) >= doctest::Approx(-1.5708).epsilon(1e-4));

    // At the pole, yaw and roll describe the same rotation and the pair is not
    // recoverable. Roll resolves to zero rather than splitting the angle
    // arbitrarily, which is what keeps a round trip stable.
    const Vec3 atPole = toEulerYxz(fromEulerYxz(Vec3{1.5707963f, 0.8f, 0.6f}));
    CHECK(static_cast<f64>(atPole.z) == doctest::Approx(0.0).epsilon(1e-4));

    const Mat3 built = fromEulerYxz(Vec3{1.5707963f, 0.8f, 0.6f});
    const Mat3 rebuilt = fromEulerYxz(atPole);
    for (int column = 0; column < 3; ++column)
    {
        for (int row = 0; row < 3; ++row)
            CHECK(static_cast<f64>(built.m[column][row])
                  == doctest::Approx(static_cast<f64>(rebuilt.m[column][row])).epsilon(1e-3));
    }
}