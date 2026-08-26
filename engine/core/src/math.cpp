#include "luaug/core/math.h"

#include <algorithm>
#include <cmath>

namespace luaug::core {
namespace {

// Two unit vectors are treated as parallel below this cross-product length,
// which for unit inputs is sin(angle) -- so this is an angle of about 1e-6 rad.
// Deliberately tight: the threshold is the point at which a camera stops
// turning (`lookAtCFrame` falls back to the identity there), and a generous
// epsilon would snap a camera that is merely looking steeply up.
constexpr f32 kParallelEpsilon = 1e-6f;

// Rotates an f64 position by an f32 basis, in f64. Narrowing the position first
// would discard exactly the precision `DVec3` exists to keep (ADR 0014);
// widening the nine rotation entries costs nothing, because a rotation has no
// magnitude for the error to scale with.
[[nodiscard]] DVec3 rotate(const Mat3& m, DVec3 v) noexcept
{
    return {
        static_cast<f64>(m.m[0][0]) * v.x + static_cast<f64>(m.m[1][0]) * v.y + static_cast<f64>(m.m[2][0]) * v.z,
        static_cast<f64>(m.m[0][1]) * v.x + static_cast<f64>(m.m[1][1]) * v.y + static_cast<f64>(m.m[2][1]) * v.z,
        static_cast<f64>(m.m[0][2]) * v.x + static_cast<f64>(m.m[1][2]) * v.y + static_cast<f64>(m.m[2][2]) * v.z,
    };
}

[[nodiscard]] Vec3 column(const Mat3& m, int index) noexcept
{
    return {m.m[index][0], m.m[index][1], m.m[index][2]};
}

void setColumn(Mat3& m, int index, Vec3 v) noexcept
{
    m.m[index][0] = v.x;
    m.m[index][1] = v.y;
    m.m[index][2] = v.z;
}

// Row-then-column, which is how every rotation identity in the literature is
// written and the opposite of how `Mat3` stores it. The euler and quaternion
// derivations below are transcribed from that literature, so they read in its
// indexing and this one function carries the transposition.
[[nodiscard]] f32 element(const Mat3& m, int row, int column) noexcept
{
    return m.m[column][row];
}

// Axes are 0/1/2 so that a rotation order is three indices and the formulas can
// be written once instead of once per order.
struct AxisSequence
{
    int first = 0;
    int second = 1;
    int third = 2;
};

[[nodiscard]] AxisSequence sequenceOf(RotationOrder order) noexcept
{
    switch (order) {
    case RotationOrder::XYZ:
        return {0, 1, 2};
    case RotationOrder::XZY:
        return {0, 2, 1};
    case RotationOrder::YXZ:
        return {1, 0, 2};
    case RotationOrder::YZX:
        return {1, 2, 0};
    case RotationOrder::ZXY:
        return {2, 0, 1};
    case RotationOrder::ZYX:
        return {2, 1, 0};
    }
    return {1, 0, 2};
}

[[nodiscard]] f32 component(Vec3 v, int axis) noexcept
{
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

void setComponent(Vec3& v, int axis, f32 value) noexcept
{
    if (axis == 0)
        v.x = value;
    else if (axis == 1)
        v.y = value;
    else
        v.z = value;
}

[[nodiscard]] Mat3 axisRotation(int axis, f32 radians) noexcept
{
    return axis == 0 ? rotationX(radians) : (axis == 1 ? rotationY(radians) : rotationZ(radians));
}

} // namespace

f32 length(Vec3 v) noexcept
{
    return std::sqrt(dot(v, v));
}

Vec3 normalize(Vec3 v) noexcept
{
    const f32 len = length(v);
    return len > 0.0f ? v * (1.0f / len) : Vec3{};
}

f32 length(Vec2 v) noexcept
{
    return std::sqrt(dot(v, v));
}

Vec2 normalize(Vec2 v) noexcept
{
    const f32 len = length(v);
    return len > 0.0f ? v * (1.0f / len) : Vec2{};
}

Mat4 operator*(const Mat4& a, const Mat4& b) noexcept
{
    Mat4 result;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            f32 sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += a.m[k][row] * b.m[column][k];
            result.m[column][row] = sum;
        }
    }
    return result;
}

Vec3 transformPoint(const Mat4& m, Vec3 v) noexcept
{
    return {
        m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z + m.m[3][0],
        m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z + m.m[3][1],
        m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z + m.m[3][2],
    };
}

Vec3 transformDirection(const Mat4& m, Vec3 v) noexcept
{
    return {
        m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z,
        m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z,
        m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z,
    };
}

Mat4 inverse(const Mat4& m) noexcept
{
    // Column-major storage, so `m.m[c][r]`; the 2x2 sub-determinants below are
    // named for the rows they span, which is the only way this stays readable.
    const f32 a00 = m.m[0][0], a01 = m.m[0][1], a02 = m.m[0][2], a03 = m.m[0][3];
    const f32 a10 = m.m[1][0], a11 = m.m[1][1], a12 = m.m[1][2], a13 = m.m[1][3];
    const f32 a20 = m.m[2][0], a21 = m.m[2][1], a22 = m.m[2][2], a23 = m.m[2][3];
    const f32 a30 = m.m[3][0], a31 = m.m[3][1], a32 = m.m[3][2], a33 = m.m[3][3];

    const f32 b00 = a00 * a11 - a01 * a10;
    const f32 b01 = a00 * a12 - a02 * a10;
    const f32 b02 = a00 * a13 - a03 * a10;
    const f32 b03 = a01 * a12 - a02 * a11;
    const f32 b04 = a01 * a13 - a03 * a11;
    const f32 b05 = a02 * a13 - a03 * a12;
    const f32 b06 = a20 * a31 - a21 * a30;
    const f32 b07 = a20 * a32 - a22 * a30;
    const f32 b08 = a20 * a33 - a23 * a30;
    const f32 b09 = a21 * a32 - a22 * a31;
    const f32 b10 = a21 * a33 - a23 * a31;
    const f32 b11 = a22 * a33 - a23 * a32;

    const f32 determinant = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (determinant == 0.0f)
        return Mat4{};

    const f32 inverseDeterminant = 1.0f / determinant;

    Mat4 result;
    result.m[0][0] = (a11 * b11 - a12 * b10 + a13 * b09) * inverseDeterminant;
    result.m[0][1] = (a02 * b10 - a01 * b11 - a03 * b09) * inverseDeterminant;
    result.m[0][2] = (a31 * b05 - a32 * b04 + a33 * b03) * inverseDeterminant;
    result.m[0][3] = (a22 * b04 - a21 * b05 - a23 * b03) * inverseDeterminant;
    result.m[1][0] = (a12 * b08 - a10 * b11 - a13 * b07) * inverseDeterminant;
    result.m[1][1] = (a00 * b11 - a02 * b08 + a03 * b07) * inverseDeterminant;
    result.m[1][2] = (a32 * b02 - a30 * b05 - a33 * b01) * inverseDeterminant;
    result.m[1][3] = (a20 * b05 - a22 * b02 + a23 * b01) * inverseDeterminant;
    result.m[2][0] = (a10 * b10 - a11 * b08 + a13 * b06) * inverseDeterminant;
    result.m[2][1] = (a01 * b08 - a00 * b10 - a03 * b06) * inverseDeterminant;
    result.m[2][2] = (a30 * b04 - a31 * b02 + a33 * b00) * inverseDeterminant;
    result.m[2][3] = (a21 * b02 - a20 * b04 - a23 * b00) * inverseDeterminant;
    result.m[3][0] = (a11 * b07 - a10 * b09 - a12 * b06) * inverseDeterminant;
    result.m[3][1] = (a00 * b09 - a01 * b07 + a02 * b06) * inverseDeterminant;
    result.m[3][2] = (a31 * b01 - a30 * b03 - a32 * b00) * inverseDeterminant;
    result.m[3][3] = (a20 * b03 - a21 * b01 + a22 * b00) * inverseDeterminant;
    return result;
}

Mat4 translation(Vec3 t) noexcept
{
    Mat4 result;
    result.m[3][0] = t.x;
    result.m[3][1] = t.y;
    result.m[3][2] = t.z;
    return result;
}

Mat4 scaling(Vec3 s) noexcept
{
    Mat4 result;
    result.m[0][0] = s.x;
    result.m[1][1] = s.y;
    result.m[2][2] = s.z;
    return result;
}

Mat4 perspective(f32 fovYRadians, f32 aspect, f32 nearZ, f32 farZ) noexcept
{
    // Depth to [0, 1] rather than OpenGL's [-1, 1]: Vulkan, D3D12 and Metal all
    // want the former, so SDL_GPU does too. Getting this wrong does not produce
    // an error -- it produces a scene where half the depth range is wasted and
    // z-fighting appears at distances that look arbitrary.
    const f32 f = 1.0f / std::tan(fovYRadians * 0.5f);

    Mat4 result;
    result.m[0][0] = f / aspect;
    result.m[1][1] = f;
    result.m[2][2] = farZ / (nearZ - farZ);
    result.m[2][3] = -1.0f;
    result.m[3][2] = (nearZ * farZ) / (nearZ - farZ);
    result.m[3][3] = 0.0f;
    return result;
}

namespace {

// The up vector to actually build a basis from.
//
// **A direction parallel to the requested `up` is an ordinary request, not a
// degenerate one.** Looking straight down is the case, and both `lookAt`
// functions used to fail it: the matrix one normalised a zero cross product, and
// the CFrame one returned the IDENTITY rotation -- so `CFrame.lookAt` from
// directly above a target aimed along -Z instead of down, silently, because the
// rotation it produced was a perfectly valid one that nobody had asked for. It
// aimed every spotlight, camera and turret pointed at something directly above
// or below it in the wrong direction.
//
// The degenerate input the identity IS the answer for is `eye == target`, and
// that is tested separately and before this.
//
// The fallback is +Z unless the direction is itself mostly Z, which is the
// standard choice and the one that keeps the roll continuous everywhere except
// exactly at the pole -- where a roll is not defined by the inputs at all.
[[nodiscard]] Vec3 basisUp(Vec3 back, Vec3 up) noexcept
{
    const Vec3 requested = normalize(up);
    if (length(cross(requested, back)) > kParallelEpsilon)
        return requested;
    return std::abs(back.z) < 0.9f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
}

} // namespace

Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) noexcept
{
    // -Z is forward, matching the LookVector definition in api-design.md, so
    // the basis is built from the vector pointing *back* from the target.
    const Vec3 back = normalize(eye - target);
    const Vec3 right = normalize(cross(basisUp(back, up), back));
    const Vec3 trueUp = cross(back, right);

    Mat4 result;
    result.m[0][0] = right.x;
    result.m[1][0] = right.y;
    result.m[2][0] = right.z;
    result.m[0][1] = trueUp.x;
    result.m[1][1] = trueUp.y;
    result.m[2][1] = trueUp.z;
    result.m[0][2] = back.x;
    result.m[1][2] = back.y;
    result.m[2][2] = back.z;
    result.m[3][0] = -dot(right, eye);
    result.m[3][1] = -dot(trueUp, eye);
    result.m[3][2] = -dot(back, eye);
    return result;
}

// --- World transforms (ADR 0014) -------------------------------------------

Mat3 operator*(const Mat3& a, const Mat3& b) noexcept
{
    Mat3 result;
    for (int c = 0; c < 3; ++c) {
        for (int row = 0; row < 3; ++row) {
            f32 sum = 0.0f;
            for (int k = 0; k < 3; ++k)
                sum += a.m[k][row] * b.m[c][k];
            result.m[c][row] = sum;
        }
    }
    return result;
}

Vec3 operator*(const Mat3& m, Vec3 v) noexcept
{
    return {
        m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z,
        m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z,
        m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z,
    };
}

Mat3 transpose(const Mat3& m) noexcept
{
    Mat3 result;
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row)
            result.m[c][row] = m.m[row][c];
    return result;
}

Mat3 orthonormalize(const Mat3& m) noexcept
{
    // The look axis is authoritative and up is only a hint (api-design.md §2.3,
    // `CFrame:Orthonormalize`). `m[2]` is back and the look direction is its
    // negation, so normalising one normalises the other -- they differ in sign,
    // not in direction.
    const Vec3 back = normalize(column(m, 2));
    if (back == Vec3{})
        return {}; // nothing authoritative survives; identity beats a collapsed basis

    // Normalised before the cross so the parallel test measures an angle rather
    // than the hint's length: a short-but-skewed up must not read as parallel.
    Vec3 upHint = normalize(column(m, 1));
    Vec3 right = cross(upHint, back);
    if (length(right) <= kParallelEpsilon) {
        // The hint carries no roll -- it is parallel to the look axis, or zero.
        // Inventing a hint keeps the axis the caller cared about; discarding
        // both would throw away the one the header calls authoritative. No unit
        // vector is parallel to +Y and +X at once, so one of the two always
        // works.
        upHint = std::fabs(back.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
        right = cross(upHint, back);
    }
    right = normalize(right);

    Mat3 result;
    setColumn(result, 0, right);
    setColumn(result, 1, cross(back, right));
    setColumn(result, 2, back);
    return result;
}

Mat3 rotationX(f32 radians) noexcept
{
    // Right-hand rule about +X: +Y turns toward +Z.
    const f32 s = std::sin(radians);
    const f32 c = std::cos(radians);

    Mat3 result;
    result.m[1][1] = c;
    result.m[1][2] = s;
    result.m[2][1] = -s;
    result.m[2][2] = c;
    return result;
}

Mat3 rotationY(f32 radians) noexcept
{
    // Right-hand rule about +Y: +Z turns toward +X, which is why a +pi/2 yaw
    // takes LookVector from (0, 0, -1) to (-1, 0, 0) (api-design.md §2.3).
    const f32 s = std::sin(radians);
    const f32 c = std::cos(radians);

    Mat3 result;
    result.m[0][0] = c;
    result.m[0][2] = -s;
    result.m[2][0] = s;
    result.m[2][2] = c;
    return result;
}

Mat3 rotationZ(f32 radians) noexcept
{
    // Right-hand rule about +Z: +X turns toward +Y.
    const f32 s = std::sin(radians);
    const f32 c = std::cos(radians);

    Mat3 result;
    result.m[0][0] = c;
    result.m[0][1] = s;
    result.m[1][0] = -s;
    result.m[1][1] = c;
    return result;
}

CFrameD operator*(const CFrameD& a, const CFrameD& b) noexcept
{
    // "First `b`, then `a`": `b`'s translation is expressed in `a`'s basis, so
    // it is rotated before it is added.
    return {a.position + rotate(a.rotation, b.position), a.rotation * b.rotation};
}

DVec3 transformPoint(const CFrameD& cf, DVec3 v) noexcept
{
    return cf.position + rotate(cf.rotation, v);
}

Vec3 transformDirection(const CFrameD& cf, Vec3 v) noexcept
{
    return cf.rotation * v;
}

CFrameD inverse(const CFrameD& cf) noexcept
{
    // Assumes the rotation is orthonormal, which is what makes the transpose its
    // inverse -- this is deliberately NOT a general matrix inverse. A caller who
    // has skewed a basis (`fromMatrix` stores what it is given, api-design.md
    // §2.3) owes it an `orthonormalize` first; inverting a skewed basis by
    // transpose is silently wrong, and paying for a general inverse on every
    // world transform to cover that case is the wrong trade.
    const Mat3 rotation = transpose(cf.rotation);
    const DVec3 rotated = rotate(rotation, cf.position);
    return {DVec3{-rotated.x, -rotated.y, -rotated.z}, rotation};
}

CFrameD lookAtCFrame(DVec3 eye, DVec3 target, Vec3 up) noexcept
{
    // The identity rotation at `eye` is the documented answer for `eye ==
    // target` (api-design.md §2.3): a camera pointed at itself should stop
    // moving, not poison every value it touches for the rest of the run.
    //
    // **It used to be the answer for a second case too, and that was wrong.** A
    // direction parallel to the requested `up` -- looking straight down -- is an
    // ordinary request, and answering it with the identity aimed the thing along
    // -Z instead. `basisUp` below serves it.
    CFrameD result;
    result.position = eye;

    // Resolved in f64 and narrowed only once it is unit length. Narrowing the
    // endpoints first would leave two points a metre apart with no difference to
    // subtract once they are ten million units from the origin (ADR 0014).
    const DVec3 delta = eye - target; // back, not forward: the look axis is -Z
    const f64 lengthSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    if (!(lengthSquared > 0.0))
        return result;

    const f64 scale = 1.0 / std::sqrt(lengthSquared);
    const Vec3 back = toVec3(DVec3{delta.x * scale, delta.y * scale, delta.z * scale});

    // `basisUp` rather than `up`: a direction parallel to the requested up is an
    // ordinary request that this used to answer with the identity. See its own
    // comment; the degenerate input handled above is `eye == target`.
    const Vec3 right = cross(basisUp(back, up), back);
    if (length(right) <= kParallelEpsilon)
        return result;

    const Vec3 unitRight = normalize(right);
    setColumn(result.rotation, 0, unitRight);
    setColumn(result.rotation, 1, cross(back, unitRight));
    setColumn(result.rotation, 2, back);
    return result;
}

Mat4 toRenderMatrix(const CFrameD& cf, DVec3 origin) noexcept
{
    // Subtract in f64, narrow once, and in that order. Narrowing both positions
    // first and subtracting in f32 gives an answer that looks perfectly correct
    // near the origin and quantises to metres at 1e7 -- which is the entire
    // reason the position is f64 (ADR 0014, architecture.md §10).
    const Vec3 t = toVec3(cf.position - origin);

    Mat4 result;
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row)
            result.m[c][row] = cf.rotation.m[c][row];
    result.m[3][0] = t.x;
    result.m[3][1] = t.y;
    result.m[3][2] = t.z;
    return result;
}

CFrameD cframeFromMatrix(const Mat4& m, DVec3 origin) noexcept
{
    CFrameD out;
    // `Mat4` and `Mat3` are both column-major with the same `m[c][r]` layout, so
    // the rotation is the upper 3x3 read straight across and column 3 is the
    // translation.
    Mat3 basis;
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            basis.m[c][r] = m.m[c][r];
    out.rotation = orthonormalize(basis);
    out.position = DVec3{static_cast<f64>(m.m[3][0]) + origin.x, static_cast<f64>(m.m[3][1]) + origin.y,
                         static_cast<f64>(m.m[3][2]) + origin.z};
    return out;
}

// --- Bounds and culling ------------------------------------------------------

AABB AABB::fromCenterSize(Vec3 center, Vec3 size) noexcept
{
    const Vec3 half{size.x * 0.5f, size.y * 0.5f, size.z * 0.5f};
    return AABB{
        Vec3{center.x - half.x, center.y - half.y, center.z - half.z},
        Vec3{center.x + half.x, center.y + half.y, center.z + half.z},
    };
}

void expand(AABB& box, Vec3 point) noexcept
{
    box.min.x = std::min(box.min.x, point.x);
    box.min.y = std::min(box.min.y, point.y);
    box.min.z = std::min(box.min.z, point.z);
    box.max.x = std::max(box.max.x, point.x);
    box.max.y = std::max(box.max.y, point.y);
    box.max.z = std::max(box.max.z, point.z);
}

void expand(AABB& box, const AABB& other) noexcept
{
    // Guarded rather than merged blindly: an empty box's bounds are infinities,
    // and merging them in would leave `box` empty forever.
    if (isEmpty(other)) {
        return;
    }
    expand(box, other.min);
    expand(box, other.max);
}

bool contains(const AABB& box, Vec3 point) noexcept
{
    return point.x >= box.min.x && point.x <= box.max.x && point.y >= box.min.y && point.y <= box.max.y &&
           point.z >= box.min.z && point.z <= box.max.z;
}

bool intersects(const AABB& a, const AABB& b) noexcept
{
    if (isEmpty(a) || isEmpty(b)) {
        return false;
    }
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y && a.min.z <= b.max.z &&
           a.max.z >= b.min.z;
}

AABB transformed(const Mat4& m, const AABB& box) noexcept
{
    if (isEmpty(box)) {
        return AABB{};
    }

    // Arvo's method: the transformed extent along each output axis is the sum
    // of |row entry| * half-extent, which is the eight-corner answer without
    // transforming eight corners.
    const Vec3 c = center(box);
    const Vec3 e = size(box);
    const Vec3 half{e.x * 0.5f, e.y * 0.5f, e.z * 0.5f};

    const Vec3 newCenter = transformPoint(m, c);

    Vec3 newHalf{};
    newHalf.x = std::fabs(m.m[0][0]) * half.x + std::fabs(m.m[1][0]) * half.y + std::fabs(m.m[2][0]) * half.z;
    newHalf.y = std::fabs(m.m[0][1]) * half.x + std::fabs(m.m[1][1]) * half.y + std::fabs(m.m[2][1]) * half.z;
    newHalf.z = std::fabs(m.m[0][2]) * half.x + std::fabs(m.m[1][2]) * half.y + std::fabs(m.m[2][2]) * half.z;

    return AABB{
        Vec3{newCenter.x - newHalf.x, newCenter.y - newHalf.y, newCenter.z - newHalf.z},
        Vec3{newCenter.x + newHalf.x, newCenter.y + newHalf.y, newCenter.z + newHalf.z},
    };
}

namespace {

// A plane straight out of the matrix has a normal whose length is arbitrary, so
// `signedDistance` would return a scaled value rather than a distance. Every
// caller either wants metres or wants a comparison against zero that behaves the
// same for all six planes, so normalization is not optional here.
Plane normalizedPlane(f32 a, f32 b, f32 c, f32 d) noexcept
{
    const f32 length = std::sqrt(a * a + b * b + c * c);
    if (length <= 0.0f) {
        return Plane{Vec3{0.0f, 1.0f, 0.0f}, 0.0f};
    }
    const f32 inverse = 1.0f / length;
    return Plane{Vec3{a * inverse, b * inverse, c * inverse}, d * inverse};
}

} // namespace

Frustum frustumFromViewProjection(const Mat4& vp) noexcept
{
    // Row `i` of a column-major matrix is (m[0][i], m[1][i], m[2][i], m[3][i]).
    // Naming the four rows is worth the lines: every sign error in this function
    // comes from indexing a column while thinking of a row.
    const f32 r0[4]{vp.m[0][0], vp.m[1][0], vp.m[2][0], vp.m[3][0]};
    const f32 r1[4]{vp.m[0][1], vp.m[1][1], vp.m[2][1], vp.m[3][1]};
    const f32 r2[4]{vp.m[0][2], vp.m[1][2], vp.m[2][2], vp.m[3][2]};
    const f32 r3[4]{vp.m[0][3], vp.m[1][3], vp.m[2][3], vp.m[3][3]};

    Frustum frustum;
    frustum.planes[Frustum::Left] = normalizedPlane(r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3]);
    frustum.planes[Frustum::Right] = normalizedPlane(r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3]);
    frustum.planes[Frustum::Bottom] = normalizedPlane(r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3]);
    frustum.planes[Frustum::Top] = normalizedPlane(r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3]);
    // Depth is [0, 1], so the near plane is `z >= 0` -- row 2 alone. Under
    // OpenGL's [-1, 1] it would be `row3 + row2`, and the difference is
    // geometry clipped at the wrong distance rather than an error.
    frustum.planes[Frustum::Near] = normalizedPlane(r2[0], r2[1], r2[2], r2[3]);
    frustum.planes[Frustum::Far] = normalizedPlane(r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3]);
    return frustum;
}

bool intersects(const Frustum& frustum, const AABB& box) noexcept
{
    if (isEmpty(box)) {
        return false;
    }

    for (const Plane& plane : frustum.planes) {
        // The corner furthest along the plane's normal. If even that one is on
        // the negative side, every corner is, and the box is outside this plane
        // -- which is enough to reject it entirely.
        const Vec3 positive{
            plane.normal.x >= 0.0f ? box.max.x : box.min.x,
            plane.normal.y >= 0.0f ? box.max.y : box.min.y,
            plane.normal.z >= 0.0f ? box.max.z : box.min.z,
        };
        if (signedDistance(plane, positive) < 0.0f) {
            return false;
        }
    }
    return true;
}

// --- Colour ------------------------------------------------------------------

Color3 lerp(Color3 a, Color3 b, f32 alpha) noexcept
{
    // Neither the channels nor `alpha` are clamped: api-design.md §2.3 leaves
    // the channel range open for HDR, and an alpha outside [0, 1] extrapolates.
    // Written as a weighted sum rather than `a + (b - a) * alpha` so both
    // endpoints come back bit-exact.
    const f32 inverseAlpha = 1.0f - alpha;
    return {
        a.r * inverseAlpha + b.r * alpha,
        a.g * inverseAlpha + b.g * alpha,
        a.b * inverseAlpha + b.b * alpha,
    };
}

Color3 fromHsv(f32 hue, f32 saturation, f32 value) noexcept
{
    // Hue is a turn, not degrees (api-design.md §2.3), so it wraps: 1.0 is the
    // same red as 0.0 and an animated hue can keep counting up without a seam.
    f32 turns = hue - std::floor(hue);
    if (!(turns < 1.0f))
        turns = 0.0f; // a hue of -1e-9 wraps to exactly 1.0f in f32; also catches NaN

    const f32 sector = turns * 6.0f;
    const f32 sextant = std::floor(sector);
    const f32 fraction = sector - sextant;

    // Saturation and value are not clamped either -- an HDR value above 1 has to
    // survive the round trip the same way a raw channel does.
    const f32 p = value * (1.0f - saturation);
    const f32 q = value * (1.0f - saturation * fraction);
    const f32 t = value * (1.0f - saturation * (1.0f - fraction));

    switch (static_cast<int>(sextant)) {
    case 0:
        return {value, t, p};
    case 1:
        return {q, value, p};
    case 2:
        return {p, value, t};
    case 3:
        return {p, q, value};
    case 4:
        return {t, p, value};
    default:
        return {value, p, q};
    }
}

void toHsv(Color3 color, f32& hue, f32& saturation, f32& value) noexcept
{
    const f32 maximum = std::fmax(color.r, std::fmax(color.g, color.b));
    const f32 minimum = std::fmin(color.r, std::fmin(color.g, color.b));
    const f32 chroma = maximum - minimum;

    value = maximum;
    saturation = maximum > 0.0f ? chroma / maximum : 0.0f;

    if (!(chroma > 0.0f)) {
        // Achromatic. Every hue names this colour, so there is nothing to
        // report; 0 is the answer that round-trips, because `fromHsv(0, 0, v)`
        // reproduces the grey whatever hue we had claimed.
        hue = 0.0f;
        return;
    }

    f32 sextants = 0.0f;
    if (maximum == color.r)
        sextants = (color.g - color.b) / chroma; // negative below red; wrapped at the end
    else if (maximum == color.g)
        sextants = 2.0f + (color.b - color.r) / chroma;
    else
        sextants = 4.0f + (color.r - color.g) / chroma;

    hue = sextants / 6.0f;
    if (hue < 0.0f)
        hue += 1.0f;
}

Mat3 fromEuler(Vec3 radians, RotationOrder order) noexcept
{
    const AxisSequence axes = sequenceOf(order);
    // Intrinsic, read left to right: the first turn happens about a world axis
    // and each later one about the axis its predecessors produced. Under the
    // column-vector convention that composes right to left, which is why the
    // three factors appear in the order the letters are written.
    return axisRotation(axes.first, component(radians, axes.first)) *
           axisRotation(axes.second, component(radians, axes.second)) *
           axisRotation(axes.third, component(radians, axes.third));
}

Vec3 toEuler(const Mat3& rotation, RotationOrder order) noexcept
{
    const AxisSequence axes = sequenceOf(order);
    const int i = axes.first;
    const int j = axes.second;
    const int k = axes.third;
    // +1 when (i, j, k) is a cyclic permutation of (X, Y, Z) and -1 otherwise.
    // Every sign below is this one factor: the six Tait-Bryan orders are two
    // formulas, not six, and writing them out six times is how one of them comes
    // to disagree with the others.
    const f32 parity = ((j - i + 3) % 3 == 1) ? 1.0f : -1.0f;

    const f32 sinMiddle = std::clamp(parity * element(rotation, i, k), -1.0f, 1.0f);
    Vec3 result;

    // Gimbal lock: |sin| == 1 collapses the outer two angles into one degree of
    // freedom, so the pair is not recoverable. The last is resolved to zero and
    // the whole rotation is expressed in the first, which is the convention that
    // keeps a round trip stable instead of splitting the angle arbitrarily.
    constexpr f32 lockEpsilon = 1.0f - 1e-6f;
    if (std::abs(sinMiddle) > lockEpsilon) {
        const f32 sign = sinMiddle < 0.0f ? -1.0f : 1.0f;
        setComponent(result, i, std::atan2(sign * element(rotation, j, i), element(rotation, j, j)));
        setComponent(result, j, std::asin(sinMiddle));
        setComponent(result, k, 0.0f);
        return result;
    }

    setComponent(result, i, std::atan2(-parity * element(rotation, j, k), element(rotation, k, k)));
    setComponent(result, j, std::asin(sinMiddle));
    setComponent(result, k, std::atan2(-parity * element(rotation, i, j), element(rotation, i, i)));
    return result;
}

Mat3 fromEulerYxz(Vec3 radians) noexcept
{
    return fromEuler(radians, RotationOrder::YXZ);
}

Vec3 toEulerYxz(const Mat3& rotation) noexcept
{
    return toEuler(rotation, RotationOrder::YXZ);
}

Mat3 fromAxisAngle(Vec3 axis, f32 radians) noexcept
{
    const Vec3 unit = normalize(axis);
    if (unit == Vec3{})
        return {}; // no direction to turn about; the identity beats NaN

    const f32 half = radians * 0.5f;
    const f32 s = std::sin(half);
    return fromQuaternion(unit.x * s, unit.y * s, unit.z * s, std::cos(half));
}

void toAxisAngle(const Mat3& rotation, Vec3& axis, f32& radians) noexcept
{
    // Through the quaternion rather than from the trace directly. Near a half
    // turn the skew-symmetric part of the matrix goes to zero and the axis it
    // encodes becomes numerically meaningless, while the quaternion's vector
    // part is at its largest there -- the two failure regions are opposite, and
    // `toQuaternion` already picks the well-conditioned branch.
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 1.0f;
    toQuaternion(rotation, x, y, z, w);

    // The sign convention that puts the angle in [0, pi]: q and -q are the same
    // rotation, so flipping to w >= 0 chooses the short way round.
    if (w < 0.0f) {
        x = -x;
        y = -y;
        z = -z;
        w = -w;
    }

    const f32 sinHalf = length(Vec3{x, y, z});
    if (sinHalf <= kParallelEpsilon) {
        // The identity, or near enough that no axis is recoverable. +X with a
        // zero angle is arbitrary, which is why the header says so.
        axis = Vec3{1.0f, 0.0f, 0.0f};
        radians = 0.0f;
        return;
    }

    axis = Vec3{x / sinHalf, y / sinHalf, z / sinHalf};
    radians = 2.0f * std::atan2(sinHalf, std::clamp(w, -1.0f, 1.0f));
}

Mat3 fromQuaternion(f32 x, f32 y, f32 z, f32 w) noexcept
{
    const f32 norm = std::sqrt(x * x + y * y + z * z + w * w);
    if (!(norm > 0.0f))
        return {};

    const f32 s = 1.0f / norm;
    const f32 qx = x * s;
    const f32 qy = y * s;
    const f32 qz = z * s;
    const f32 qw = w * s;

    Mat3 result;
    setColumn(result, 0,
              Vec3{1.0f - 2.0f * (qy * qy + qz * qz), 2.0f * (qx * qy + qz * qw), 2.0f * (qx * qz - qy * qw)});
    setColumn(result, 1,
              Vec3{2.0f * (qx * qy - qz * qw), 1.0f - 2.0f * (qx * qx + qz * qz), 2.0f * (qy * qz + qx * qw)});
    setColumn(result, 2,
              Vec3{2.0f * (qx * qz + qy * qw), 2.0f * (qy * qz - qx * qw), 1.0f - 2.0f * (qx * qx + qy * qy)});
    return result;
}

void toQuaternion(const Mat3& rotation, f32& x, f32& y, f32& z, f32& w) noexcept
{
    // Shepperd's method: pick whichever of the four components is largest and
    // solve for the rest from it. The naive trace formula divides by a quantity
    // that vanishes at a half turn, and the failure is a silently wrong axis
    // rather than a NaN, which is worse.
    const f32 m00 = element(rotation, 0, 0);
    const f32 m11 = element(rotation, 1, 1);
    const f32 m22 = element(rotation, 2, 2);
    const f32 trace = m00 + m11 + m22;

    if (trace > 0.0f) {
        const f32 s = std::sqrt(trace + 1.0f) * 2.0f;
        w = 0.25f * s;
        x = (element(rotation, 2, 1) - element(rotation, 1, 2)) / s;
        y = (element(rotation, 0, 2) - element(rotation, 2, 0)) / s;
        z = (element(rotation, 1, 0) - element(rotation, 0, 1)) / s;
    }
    else if (m00 > m11 && m00 > m22) {
        const f32 s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        w = (element(rotation, 2, 1) - element(rotation, 1, 2)) / s;
        x = 0.25f * s;
        y = (element(rotation, 0, 1) + element(rotation, 1, 0)) / s;
        z = (element(rotation, 0, 2) + element(rotation, 2, 0)) / s;
    }
    else if (m11 > m22) {
        const f32 s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        w = (element(rotation, 0, 2) - element(rotation, 2, 0)) / s;
        x = (element(rotation, 0, 1) + element(rotation, 1, 0)) / s;
        y = 0.25f * s;
        z = (element(rotation, 1, 2) + element(rotation, 2, 1)) / s;
    }
    else {
        const f32 s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        w = (element(rotation, 1, 0) - element(rotation, 0, 1)) / s;
        x = (element(rotation, 0, 2) + element(rotation, 2, 0)) / s;
        y = (element(rotation, 1, 2) + element(rotation, 2, 1)) / s;
        z = 0.25f * s;
    }
}

Mat3 slerp(const Mat3& a, const Mat3& b, f32 alpha) noexcept
{
    f32 ax = 0.0f;
    f32 ay = 0.0f;
    f32 az = 0.0f;
    f32 aw = 1.0f;
    toQuaternion(a, ax, ay, az, aw);

    f32 bx = 0.0f;
    f32 by = 0.0f;
    f32 bz = 0.0f;
    f32 bw = 1.0f;
    toQuaternion(b, bx, by, bz, bw);

    // q and -q are the same rotation but opposite ends of the arc; without this
    // flip an interpolation between two nearby frames can take the long way
    // round, which reads as the object spinning 350 degrees to move 10.
    f32 cosine = ax * bx + ay * by + az * bz + aw * bw;
    if (cosine < 0.0f) {
        bx = -bx;
        by = -by;
        bz = -bz;
        bw = -bw;
        cosine = -cosine;
    }

    f32 weightA = 1.0f - alpha;
    f32 weightB = alpha;
    // Below this the arc is short enough that sin(theta) has lost most of its
    // significant bits; the linear blend and the spherical one agree to well
    // under an f32 ulp there, and only one of them divides by nearly zero.
    constexpr f32 linearThreshold = 1.0f - 1e-6f;
    if (cosine < linearThreshold) {
        const f32 theta = std::acos(std::clamp(cosine, -1.0f, 1.0f));
        const f32 sinTheta = std::sin(theta);
        weightA = std::sin((1.0f - alpha) * theta) / sinTheta;
        weightB = std::sin(alpha * theta) / sinTheta;
    }

    return fromQuaternion(weightA * ax + weightB * bx, weightA * ay + weightB * by, weightA * az + weightB * bz,
                          weightA * aw + weightB * bw);
}

CFrameD lerp(const CFrameD& a, const CFrameD& b, f64 alpha) noexcept
{
    const DVec3 position{
        a.position.x + (b.position.x - a.position.x) * alpha,
        a.position.y + (b.position.y - a.position.y) * alpha,
        a.position.z + (b.position.z - a.position.z) * alpha,
    };
    return {position, slerp(a.rotation, b.rotation, static_cast<f32>(alpha))};
}

} // namespace luaug::core
