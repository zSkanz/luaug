#include "luaug/core/math.h"

#include <algorithm>
#include <cmath>

namespace luaug::core
{
namespace
{

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

Mat4 operator*(const Mat4& a, const Mat4& b) noexcept
{
    Mat4 result;
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
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

Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) noexcept
{
    // -Z is forward, matching the LookVector definition in api-design.md, so
    // the basis is built from the vector pointing *back* from the target.
    const Vec3 back = normalize(eye - target);
    const Vec3 right = normalize(cross(up, back));
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
    for (int c = 0; c < 3; ++c)
    {
        for (int row = 0; row < 3; ++row)
        {
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
    if (length(right) <= kParallelEpsilon)
    {
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
    // The identity rotation at `eye` is the documented answer for both
    // degenerate inputs (api-design.md §2.3): a camera pointed at itself should
    // stop moving, not poison every value it touches for the rest of the run.
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

    const Vec3 right = cross(normalize(up), back);
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

    switch (static_cast<int>(sextant))
    {
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

    if (!(chroma > 0.0f))
    {
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

Mat3 fromEulerYxz(Vec3 radians) noexcept
{
    // Intrinsic YXZ read left to right: yaw, then pitch about the axis the yaw
    // produced, then roll about the axis those two produced. Under the
    // column-vector convention that composes right to left.
    return rotationY(radians.y) * rotationX(radians.x) * rotationZ(radians.z);
}

Vec3 toEulerYxz(const Mat3& rotation) noexcept
{
    // For R = Ry(y) * Rx(x) * Rz(z), the second row is
    // [ cos(x)sin(z), cos(x)cos(z), -sin(x) ], which gives pitch directly and
    // roll from the pair beside it. Yaw comes from the third column, which the
    // pitch rotation leaves in the XZ plane.
    const f32 sinPitch = std::clamp(-rotation.m[2][1], -1.0f, 1.0f);
    const f32 pitch = std::asin(sinPitch);

    // Gimbal lock: |sin(pitch)| == 1 collapses yaw and roll into one degree of
    // freedom, so the pair is not recoverable. Roll is resolved to zero and the
    // whole rotation is expressed as yaw, which is the convention that keeps a
    // round trip stable instead of splitting the angle arbitrarily.
    constexpr f32 lockEpsilon = 1.0f - 1e-6f;
    if (std::abs(sinPitch) > lockEpsilon)
        return {pitch, std::atan2(-rotation.m[0][2], rotation.m[0][0]), 0.0f};

    const f32 yaw = std::atan2(rotation.m[2][0], rotation.m[2][2]);
    const f32 roll = std::atan2(rotation.m[0][1], rotation.m[1][1]);
    return {pitch, yaw, roll};
}

} // namespace luaug::core
