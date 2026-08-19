#include "luaug/core/math.h"

#include <cmath>

namespace luaug::core
{

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

} // namespace luaug::core
