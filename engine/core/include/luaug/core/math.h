// Engine math (architecture.md §2, ADR 0013).
//
// The list grows with its consumers: a vector type nobody constructs is a
// vector type nobody has checked the sign conventions of. `Vec2` is still absent
// for that reason. `AABB` and `Frustum` arrived at M4, which is the milestone
// that culls.
//
// **Conventions, stated once because getting them wrong is silent.** Matrices
// are column-major in storage and column-vector in use: a transform applies as
// `v' = M * v`, and composing "first A, then B" is `B * A`. Coordinates are
// right-handed with +Y up and the camera looking down **-Z**, matching the
// LookVector definition in api-design.md. Depth maps to [0, 1], which is what
// Vulkan, D3D12 and Metal all want -- and what SDL_GPU therefore expects.
#pragma once

#include "luaug/core/types.h"

#include <limits>

namespace luaug::core {

// The empty `AABB` below is a default member initializer, so this has to be a
// compile-time constant rather than a call.
static_assert(std::numeric_limits<f32>::has_infinity, "the empty AABB is built from an f32 infinity");
inline constexpr f32 kInfinity = std::numeric_limits<f32>::infinity();

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

// The general inverse, by cofactor expansion. Returns the identity for a
// singular matrix rather than a matrix of infinities: the caller that needs this
// is undoing a projection, and a projection that cannot be undone is a camera
// nobody configured -- which should render badly, not poison every subsequent
// multiply with NaN.
[[nodiscard]] Mat4 inverse(const Mat4& m) noexcept;

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

// Which axis each of the three euler angles turns about, and in what sequence.
// The order is an argument rather than part of a function name because there is
// exactly one euler constructor in the public API (api-design.md §2.3) -- the
// `fromEulerAnglesXYZ`/`YXZ` family is a naming scheme, not six different
// operations.
//
// The letters read left to right in application order, and the rotations are
// **intrinsic**: each turns about the axis the previous ones produced.
enum class RotationOrder : u8
{
    XYZ,
    XZY,
    YXZ,
    YZX,
    ZXY,
    ZYX,
};

// The euler pair, in radians. `angles` is always indexed by axis -- `.x` is the
// rotation about X whatever the order is -- so changing the order changes the
// sequence and never which number means what.
//
// `toEuler` picks the branch with the middle angle in [-pi/2, pi/2]; at the
// poles, where the outer two describe the same rotation, the last is resolved to
// zero. Every euler extraction has to make that choice, and leaving it unstated
// is how two call sites come to disagree.
[[nodiscard]] Mat3 fromEuler(Vec3 radians, RotationOrder order) noexcept;
[[nodiscard]] Vec3 toEuler(const Mat3& rotation, RotationOrder order) noexcept;

// YXZ by name, because it is the default order and `BasePart.Orientation` is
// defined in it (api-design.md §2.2). The two directions are exact inverses of
// each other, or a read of `Orientation` after a write of it would drift.
[[nodiscard]] Mat3 fromEulerYxz(Vec3 radians) noexcept;
[[nodiscard]] Vec3 toEulerYxz(const Mat3& rotation) noexcept;

// Right-hand rule about `axis`, which is normalized on the way in so its length
// carries no meaning (api-design.md §2.3). A zero axis has no direction to turn
// about and yields the identity rather than NaN.
[[nodiscard]] Mat3 fromAxisAngle(Vec3 axis, f32 radians) noexcept;

// The inverse, with a unit axis and an angle in [0, pi]. The identity has no
// axis to report and yields +X with an angle of zero -- an arbitrary choice, so
// it is a documented one.
void toAxisAngle(const Mat3& rotation, Vec3& axis, f32& radians) noexcept;

// Quaternion in (x, y, z, w) order -- w last, matching `ToQuaternion`'s tail,
// which is the half of the convention people get wrong. Normalized on the way
// in; a zero quaternion yields the identity.
[[nodiscard]] Mat3 fromQuaternion(f32 x, f32 y, f32 z, f32 w) noexcept;
void toQuaternion(const Mat3& rotation, f32& x, f32& y, f32& z, f32& w) noexcept;

// Shortest-arc interpolation: the halfway rotation sits at an equal angle from
// each end rather than cutting the corner, which is what component-wise
// interpolation of a basis would do. `alpha` is not clamped.
[[nodiscard]] Mat3 slerp(const Mat3& a, const Mat3& b, f32 alpha) noexcept;

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

// Translation linearly, rotation by `slerp` (api-design.md §2.3, `CFrame:Lerp`).
// The translation stays f64 the whole way: interpolating two positions ten
// million metres out through f32 would quantise the path into steps.
[[nodiscard]] CFrameD lerp(const CFrameD& a, const CFrameD& b, f64 alpha) noexcept;

// A degenerate direction or an up hint parallel to it yields the identity
// rotation at `eye` rather than NaN (api-design.md §2.3): a camera that stops
// turning is recoverable, a camera full of NaN is not.
[[nodiscard]] CFrameD lookAtCFrame(DVec3 eye, DVec3 target, Vec3 up) noexcept;

// Renders `cf` relative to `origin` for the f32 world the renderer and physics
// operate in (architecture.md §10). Floating origin itself is M7; this is the
// one operation it will be built out of.
[[nodiscard]] Mat4 toRenderMatrix(const CFrameD& cf, DVec3 origin) noexcept;

// --- Bounds and culling ------------------------------------------------------
//
// Both live in the f32 space `render::extract` produces -- camera-relative, so
// the coordinates are small whatever the world position was (ADR 0014). Neither
// has an f64 counterpart, and that is the point: a bound is an extent, and an
// extent is exactly what f32 is still good at eight kilometres out.

// Half-open in neither direction: `min` and `max` are both inside. The default
// is the *empty* box rather than a zero-sized one at the origin, so `expand`
// starting from a default is correct and a box nobody filled cannot be mistaken
// for a point at the origin -- which would sit inside every frustum and be
// drawn.
struct AABB
{
    Vec3 min{kInfinity, kInfinity, kInfinity};
    Vec3 max{-kInfinity, -kInfinity, -kInfinity};

    [[nodiscard]] static constexpr AABB fromMinMax(Vec3 min, Vec3 max) noexcept { return AABB{min, max}; }

    // `size` is the full extent, matching `BasePart.Size` (api-design.md §2.2),
    // not a half-extent. Getting that wrong is a factor of two that looks
    // plausible in every screenshot.
    [[nodiscard]] static AABB fromCenterSize(Vec3 center, Vec3 size) noexcept;

    [[nodiscard]] constexpr bool operator==(const AABB&) const noexcept = default;
};

// True when the box holds nothing at all, which is what a default-constructed
// one is. Any inverted axis counts: a box cannot be half-empty.
[[nodiscard]] constexpr bool isEmpty(const AABB& box) noexcept
{
    return box.max.x < box.min.x || box.max.y < box.min.y || box.max.z < box.min.z;
}

// Undefined on an empty box in the sense that the answers are meaningless, not
// in the language sense -- they are computed from the infinities and produce
// NaN. Callers that can see an empty box must check first.
[[nodiscard]] constexpr Vec3 center(const AABB& box) noexcept
{
    return Vec3{(box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f, (box.min.z + box.max.z) * 0.5f};
}

// The full extent, so it pairs with `fromCenterSize`.
[[nodiscard]] constexpr Vec3 size(const AABB& box) noexcept
{
    return Vec3{box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z};
}

void expand(AABB& box, Vec3 point) noexcept;
void expand(AABB& box, const AABB& other) noexcept;

[[nodiscard]] bool contains(const AABB& box, Vec3 point) noexcept;
[[nodiscard]] bool intersects(const AABB& a, const AABB& b) noexcept;

// The axis-aligned bound of the transformed box -- which is a bound of the
// rotated box and not the rotated box itself, so it grows under rotation and
// never shrinks. Transforming an already-transformed bound therefore loses
// tightness each time; transform the local bound, once.
//
// An empty box transforms to an empty box rather than to a point at the
// matrix's translation.
[[nodiscard]] AABB transformed(const Mat4& m, const AABB& box) noexcept;

// `dot(normal, p) + distance` is signed, positive on the side the normal points
// at, and metric -- the plane is normalized on construction, so the value is a
// distance in world units rather than an arbitrary scale.
//
// Metric to about **1e-5 relative**, not absolute, and the far plane is the
// worst of the six: extracted as `row3 - row2`, its coefficients are a
// difference of two nearly equal numbers whose magnitude falls as far/near
// grows, and normalizing divides the f32 error back up by the same factor. Fine
// for culling, which only compares against zero; check the error budget before
// using a far-plane distance for anything that must be exact.
struct Plane
{
    Vec3 normal{0.0f, 1.0f, 0.0f};
    f32 distance = 0.0f;
};

[[nodiscard]] constexpr f32 signedDistance(const Plane& plane, Vec3 point) noexcept
{
    return plane.normal.x * point.x + plane.normal.y * point.y + plane.normal.z * point.z + plane.distance;
}

// Six planes, all pointing **inward**: a point is inside the frustum when it is
// on the positive side of every one. Stated because the opposite convention is
// equally common and the difference is a renderer that draws nothing.
struct Frustum
{
    enum Side : u32
    {
        Left = 0,
        Right,
        Bottom,
        Top,
        Near,
        Far,
        SideCount,
    };

    Plane planes[SideCount]{};
};

// Gribb-Hartmann extraction from a combined view-projection, for this file's
// conventions: column-major storage, column-vector transforms, depth in [0, 1].
// The depth range is why `Near` is a row on its own rather than a difference --
// with OpenGL's [-1, 1] it would be `row3 + row2`, and the mistake shows up as
// geometry clipped at the wrong distance rather than as an error.
[[nodiscard]] Frustum frustumFromViewProjection(const Mat4& viewProjection) noexcept;

// Conservative: false means the box is certainly outside, true means it may be
// inside. The false positives are boxes near a corner that no plane rejects on
// its own, which cost a draw call and never a wrong image -- the trade every
// culler makes, named here so nobody "fixes" it.
//
// An empty box is outside.
[[nodiscard]] bool intersects(const Frustum& frustum, const AABB& box) noexcept;

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
