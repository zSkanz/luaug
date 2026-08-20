// Engine math (architecture.md §2, ADR 0013).
//
// The list grows with its consumers: a vector type nobody constructs is a
// vector type nobody has checked the sign conventions of. `Vec2`, `AABB` and
// `Frustum` are still absent for that reason.
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

// --- World transforms (ADR 0014) -------------------------------------------
//
// The script-facing `CFrame` is the engine's f64 source of truth for position.
// `Vector3` is f32 and stays that way (ADR 0013): millimetre-exact to roughly
// ±8 km and ~16 mm at ±131 km, which is fine for a direction or an extent and
// not fine for a world position in an open world. Splitting the two here, from
// the first commit that has a transform at all, is what keeps the widening
// from being a migration later.

// f64 position. Deliberately not a template over Vec3: the whole point is that
// the two are different types and cannot be assigned to each other by accident.
struct DVec3
{
    f64 x = 0.0;
    f64 y = 0.0;
    f64 z = 0.0;

    [[nodiscard]] constexpr bool operator==(const DVec3&) const noexcept = default;
};

[[nodiscard]] constexpr DVec3 operator+(DVec3 a, DVec3 b) noexcept
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] constexpr DVec3 operator-(DVec3 a, DVec3 b) noexcept
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

// Narrowing is explicit at every call site, so "where did the precision go" has
// a grep-able answer.
[[nodiscard]] constexpr Vec3 toVec3(DVec3 v) noexcept
{
    return {static_cast<f32>(v.x), static_cast<f32>(v.y), static_cast<f32>(v.z)};
}

[[nodiscard]] constexpr DVec3 toDVec3(Vec3 v) noexcept
{
    return {static_cast<f64>(v.x), static_cast<f64>(v.y), static_cast<f64>(v.z)};
}

// Rotation only, f32, column-major and column-vector like `Mat4`: `m[c][r]`,
// so `m[0]` is the right axis, `m[1]` up, and `m[2]` **back** -- the look
// direction is `-m[2]`, matching LookVector in api-design.md §2.3.
struct Mat3
{
    f32 m[3][3]{
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    // Exact, element-wise. This is what a property write compares against to
    // decide whether anything changed, so it must be identity of the stored
    // bits and not an epsilon: "close enough" would swallow a real change and
    // never fire the signal for it.
    [[nodiscard]] constexpr bool operator==(const Mat3&) const noexcept = default;
};

[[nodiscard]] Mat3 operator*(const Mat3& a, const Mat3& b) noexcept;
[[nodiscard]] Vec3 operator*(const Mat3& m, Vec3 v) noexcept;
[[nodiscard]] Mat3 transpose(const Mat3& m) noexcept;

// Re-establishes an orthonormal basis from a possibly-skewed one. The look axis
// is authoritative and up is the hint, which is why a `lookAt` built from a
// nearly-parallel up still produces something usable instead of NaN.
[[nodiscard]] Mat3 orthonormalize(const Mat3& m) noexcept;

// Right-hand-rule rotations about each axis, in radians.
[[nodiscard]] Mat3 rotationX(f32 radians) noexcept;
[[nodiscard]] Mat3 rotationY(f32 radians) noexcept;
[[nodiscard]] Mat3 rotationZ(f32 radians) noexcept;

// The canonical world transform: f64 translation, f32 rotation. Rotation stays
// f32 because a rotation has no magnitude to lose precision in -- the error is
// bounded by the angle, not by the distance from the origin.
struct CFrameD
{
    DVec3 position;
    Mat3 rotation;

    [[nodiscard]] constexpr bool operator==(const CFrameD&) const noexcept = default;
};

// "First `b`, then `a`", the column-vector convention -- so this reads
// backwards relative to the order the transforms happen in.
[[nodiscard]] CFrameD operator*(const CFrameD& a, const CFrameD& b) noexcept;

// Treats `v` as a point: rotates, then translates.
[[nodiscard]] DVec3 transformPoint(const CFrameD& cf, DVec3 v) noexcept;

// Treats `v` as a direction: rotates only.
[[nodiscard]] Vec3 transformDirection(const CFrameD& cf, Vec3 v) noexcept;

[[nodiscard]] CFrameD inverse(const CFrameD& cf) noexcept;

// A degenerate direction or an up hint parallel to it yields the identity
// rotation at `eye` rather than NaN (api-design.md §2.3): a camera that stops
// turning is recoverable, a camera full of NaN is not.
[[nodiscard]] CFrameD lookAtCFrame(DVec3 eye, DVec3 target, Vec3 up) noexcept;

// Renders `cf` relative to `origin` for the f32 world the renderer and physics
// operate in (architecture.md §10). Floating origin itself is M7; this is the
// one operation it will be built out of.
[[nodiscard]] Mat4 toRenderMatrix(const CFrameD& cf, DVec3 origin) noexcept;

// --- Colour ------------------------------------------------------------------

// Linear, not sRGB-encoded, and not clamped: api-design.md §2.3 leaves the
// range open so an HDR value survives a round trip through a property.
struct Color3
{
    f32 r = 0.0f;
    f32 g = 0.0f;
    f32 b = 0.0f;

    [[nodiscard]] constexpr bool operator==(const Color3&) const noexcept = default;
};

[[nodiscard]] Color3 lerp(Color3 a, Color3 b, f32 alpha) noexcept;

// Hue, saturation and value all in [0, 1] -- hue is a turn, not degrees
// (api-design.md §2.3).
[[nodiscard]] Color3 fromHsv(f32 hue, f32 saturation, f32 value) noexcept;
void toHsv(Color3 color, f32& hue, f32& saturation, f32& value) noexcept;

} // namespace luaug::core
