#include <doctest/doctest.h>

#include <cmath>

#include "luaug/core/math.h"

using namespace luaug::core;

namespace
{

constexpr f32 kEpsilon = 1e-5f;

bool near(f32 a, f32 b) noexcept
{
    return std::fabs(a - b) <= kEpsilon;
}

bool near(Vec3 a, Vec3 b) noexcept
{
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
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
