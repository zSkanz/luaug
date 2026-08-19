// Engine math (architecture.md §2, ADR 0013).
//
// Only `Vec3` and `Mat4` live here so far. The full list in architecture.md --
// Vec2, Quat, Mat3, DVec3, CFrameD, AABB, Frustum, Color3 -- arrives with its
// first consumer; a vector type nobody constructs is a vector type nobody has
// checked the sign conventions of.
//
// **Conventions, stated once because getting them wrong is silent.** Matrices
// are column-major in storage and column-vector in use: a transform applies as
// `v' = M * v`, and composing "first A, then B" is `B * A`. Coordinates are
// right-handed with +Y up and the camera looking down **-Z**, matching the
// LookVector definition in api-design.md. Depth maps to [0, 1], which is what
// Vulkan, D3D12 and Metal all want -- and what SDL_GPU therefore expects.
#pragma once

#include "luaug/core/types.h"

namespace luaug::core
{

// Bit-identical to the Luau `vector` primitive, which IS Vector3 (ADR 0013):
// three contiguous f32, no padding, no fourth lane. `lua_tovector` hands back a
// `const float*` into exactly this shape, so a binding can reinterpret rather
// than copy. The static_asserts below are the contract; if one ever fires, the
// script-facing vector and the engine's have diverged and every binding that
// reinterprets is silently wrong.
struct Vec3
{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;

    [[nodiscard]] constexpr bool operator==(const Vec3&) const noexcept = default;
};

static_assert(sizeof(Vec3) == 3 * sizeof(f32), "Vec3 must be three f32 with no padding (ADR 0013)");
static_assert(alignof(Vec3) == alignof(f32), "Vec3 must not be over-aligned; Luau's vector is not");

[[nodiscard]] constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] constexpr Vec3 operator-(Vec3 v) noexcept
{
    return {-v.x, -v.y, -v.z};
}

[[nodiscard]] constexpr Vec3 operator*(Vec3 v, f32 s) noexcept
{
    return {v.x * s, v.y * s, v.z * s};
}

[[nodiscard]] constexpr Vec3 operator*(f32 s, Vec3 v) noexcept
{
    return v * s;
}

// Component-wise, for extents and scales -- not a dot or a cross.
[[nodiscard]] constexpr Vec3 mul(Vec3 a, Vec3 b) noexcept
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

[[nodiscard]] constexpr f32 dot(Vec3 a, Vec3 b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] f32 length(Vec3 v) noexcept;

// Returns the zero vector for a zero-length input rather than NaN. Callers
// normalizing a direction that can legitimately be zero -- a stopped velocity,
// a degenerate edge -- would otherwise poison everything downstream, and a NaN
// in a transform is far harder to trace than a zero.
[[nodiscard]] Vec3 normalize(Vec3 v) noexcept;

// Column-major storage: `m[c][r]`, so `m[3]` is the translation column and the
// whole struct uploads to a shader constant buffer without a transpose.
struct Mat4
{
    // Identity by default: an uninitialised transform that silently collapses
    // geometry to a point is a worse default than one that does nothing.
    f32 m[4][4]{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
};

static_assert(sizeof(Mat4) == 16 * sizeof(f32), "Mat4 must upload as 16 tightly packed f32");

// "First `b`, then `a`" -- the column-vector convention, so this reads
// backwards compared to the order the transforms happen in.
[[nodiscard]] Mat4 operator*(const Mat4& a, const Mat4& b) noexcept;

// Treats `v` as a point (w = 1), so translation applies.
[[nodiscard]] Vec3 transformPoint(const Mat4& m, Vec3 v) noexcept;

// Treats `v` as a direction (w = 0), so translation does not.
[[nodiscard]] Vec3 transformDirection(const Mat4& m, Vec3 v) noexcept;

[[nodiscard]] Mat4 translation(Vec3 t) noexcept;
[[nodiscard]] Mat4 scaling(Vec3 s) noexcept;

// Right-handed, looking down -Z, depth in [0, 1]. `fovYRadians` is the vertical
// field of view.
[[nodiscard]] Mat4 perspective(f32 fovYRadians, f32 aspect, f32 nearZ, f32 farZ) noexcept;

// Right-handed view matrix. `up` need not be perpendicular to the view
// direction; it is only used to establish the roll.
[[nodiscard]] Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) noexcept;

} // namespace luaug::core
