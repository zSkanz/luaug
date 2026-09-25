// A sky of six images, resampled (ADR 0096). See skybox.h.
#include "luaug/render/skybox.h"

#include "luaug/render/environment.h"

#include <algorithm>
#include <cmath>

namespace luaug::render {
namespace {

using core::f32;
using core::u32;
using core::Vec3;

constexpr f32 kDegreesToRadians = 0.017453292519943295f;

// sRGB's transfer function, decoded once per byte value.
[[nodiscard]] const std::array<f32, 256>& decodeTable() noexcept
{
    static const std::array<f32, 256> table = [] {
        std::array<f32, 256> values{};
        for (u32 index = 0; index < 256; ++index) {
            const f32 c = static_cast<f32>(index) / 255.0f;
            values[index] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return values;
    }();
    return table;
}

// And encoded, from linear light quantised to 4096 steps -- finer than eight
// bits of sRGB anywhere on the curve, so no two neighbouring outputs merge.
constexpr u32 kEncodeSteps = 4096;

[[nodiscard]] const std::array<std::byte, kEncodeSteps>& encodeTable() noexcept
{
    static const std::array<std::byte, kEncodeSteps> table = [] {
        std::array<std::byte, kEncodeSteps> values{};
        for (u32 index = 0; index < kEncodeSteps; ++index) {
            const f32 c = static_cast<f32>(index) / static_cast<f32>(kEncodeSteps - 1);
            const f32 encoded = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
            values[index] = static_cast<std::byte>(std::clamp(static_cast<int>(encoded * 255.0f + 0.5f), 0, 255));
        }
        return values;
    }();
    return table;
}

[[nodiscard]] std::byte encode(f32 linear) noexcept
{
    const f32 clamped = std::clamp(linear, 0.0f, 1.0f);
    return encodeTable()[static_cast<u32>(clamped * static_cast<f32>(kEncodeSteps - 1) + 0.5f)];
}

// One face, bilinear in LINEAR light and clamped at its edges: an edge wrapped
// round to the opposite side would draw a line of the wrong sky along every
// seam between two faces.
[[nodiscard]] Vec3 sampleFace(const asset::Image& image, f32 u, f32 v) noexcept
{
    const auto& decode = decodeTable();
    const f32 x = std::clamp(u * static_cast<f32>(image.width) - 0.5f, 0.0f, static_cast<f32>(image.width - 1));
    const f32 y = std::clamp(v * static_cast<f32>(image.height) - 0.5f, 0.0f, static_cast<f32>(image.height - 1));
    const u32 x0 = static_cast<u32>(x);
    const u32 y0 = static_cast<u32>(y);
    const u32 x1 = std::min(x0 + 1, image.width - 1);
    const u32 y1 = std::min(y0 + 1, image.height - 1);
    const f32 fx = x - static_cast<f32>(x0);
    const f32 fy = y - static_cast<f32>(y0);
    const auto texel = [&](u32 tx, u32 ty) {
        const std::byte* pixel = image.pixels.data() + (static_cast<std::size_t>(ty) * image.width + tx) * 4;
        return Vec3{decode[static_cast<u32>(pixel[0])], decode[static_cast<u32>(pixel[1])],
                    decode[static_cast<u32>(pixel[2])]};
    };
    const Vec3 top = texel(x0, y0) * (1.0f - fx) + texel(x1, y0) * fx;
    const Vec3 bottom = texel(x0, y1) * (1.0f - fx) + texel(x1, y1) * fx;
    return top * (1.0f - fy) + bottom * fy;
}

} // namespace

SkyFaceHit skyFaceOf(Vec3 d) noexcept
{
    const f32 ax = std::fabs(d.x);
    const f32 ay = std::fabs(d.y);
    const f32 az = std::fabs(d.z);
    SkyFaceHit hit;
    if (ax >= ay && ax >= az && ax > 0.0f) {
        // Right looks towards +X with +Z to its right; Left the mirror of it.
        hit.face = d.x > 0.0f ? SkyFace::Right : SkyFace::Left;
        hit.u = 0.5f * ((d.x > 0.0f ? d.z : -d.z) / ax + 1.0f);
        hit.v = 0.5f * (1.0f - d.y / ax);
    }
    else if (ay >= az && ay > 0.0f) {
        // Up's bottom and Down's top both meet Front, at -Z.
        hit.face = d.y > 0.0f ? SkyFace::Up : SkyFace::Down;
        hit.u = 0.5f * (d.x / ay + 1.0f);
        hit.v = 0.5f * ((d.y > 0.0f ? -d.z : d.z) / ay + 1.0f);
    }
    else if (az > 0.0f) {
        // Front looks towards -Z with +X to its right; Back the mirror of it.
        hit.face = d.z < 0.0f ? SkyFace::Front : SkyFace::Back;
        hit.u = 0.5f * ((d.z < 0.0f ? d.x : -d.x) / az + 1.0f);
        hit.v = 0.5f * (1.0f - d.y / az);
    }
    return hit;
}

SkyTurn skyTurnOf(Vec3 degrees) noexcept
{
    // R = Rz * Ry * Rx: about X first, then Y, then Z.
    const f32 cx = std::cos(degrees.x * kDegreesToRadians);
    const f32 sx = std::sin(degrees.x * kDegreesToRadians);
    const f32 cy = std::cos(degrees.y * kDegreesToRadians);
    const f32 sy = std::sin(degrees.y * kDegreesToRadians);
    const f32 cz = std::cos(degrees.z * kDegreesToRadians);
    const f32 sz = std::sin(degrees.z * kDegreesToRadians);
    SkyTurn turn;
    turn.m[0][0] = cz * cy;
    turn.m[0][1] = cz * sy * sx - sz * cx;
    turn.m[0][2] = cz * sy * cx + sz * sx;
    turn.m[1][0] = sz * cy;
    turn.m[1][1] = sz * sy * sx + cz * cx;
    turn.m[1][2] = sz * sy * cx - cz * sx;
    turn.m[2][0] = -sy;
    turn.m[2][1] = cy * sx;
    turn.m[2][2] = cy * cx;
    return turn;
}

Vec3 toPictures(const SkyTurn& turn, Vec3 d) noexcept
{
    // The inverse of a rotation is its transpose.
    return Vec3{turn.m[0][0] * d.x + turn.m[1][0] * d.y + turn.m[2][0] * d.z,
                turn.m[0][1] * d.x + turn.m[1][1] * d.y + turn.m[2][1] * d.z,
                turn.m[0][2] * d.x + turn.m[1][2] * d.y + turn.m[2][2] * d.z};
}

void resampleSkybox(const std::array<const asset::Image*, kSkyFaceCount>& faces, const SkyTurn& turn, u32 size,
                    u32 rowBegin, u32 rowEnd, std::span<std::byte> out) noexcept
{
    const f32 texel = 1.0f / static_cast<f32>(size);
    for (u32 row = rowBegin; row < rowEnd && row < size; ++row) {
        for (u32 column = 0; column < size; ++column) {
            const Vec3 direction =
                octahedralDirection((static_cast<f32>(column) + 0.5f) * texel, (static_cast<f32>(row) + 0.5f) * texel);
            const SkyFaceHit hit = skyFaceOf(toPictures(turn, direction));
            const asset::Image* face = faces[static_cast<u32>(hit.face)];
            const Vec3 light = face != nullptr && face->valid() ? sampleFace(*face, hit.u, hit.v) : Vec3{};
            std::byte* pixel = out.data() + (static_cast<std::size_t>(row) * size + column) * 4;
            pixel[0] = encode(light.x);
            pixel[1] = encode(light.y);
            pixel[2] = encode(light.z);
            pixel[3] = std::byte{0xFF};
        }
    }
}

void skyRadianceOf(std::span<const std::byte> octahedral, u32 size, u32 radianceSize, SkyRadiance& out)
{
    const auto& decode = decodeTable();
    out.size = radianceSize;
    out.texels.assign(static_cast<std::size_t>(radianceSize) * radianceSize, Vec3{});
    const u32 step = size / radianceSize;
    if (step == 0)
        return;
    const f32 weight = 1.0f / static_cast<f32>(step * step);
    for (u32 row = 0; row < radianceSize; ++row) {
        for (u32 column = 0; column < radianceSize; ++column) {
            Vec3 sum{};
            for (u32 dy = 0; dy < step; ++dy) {
                for (u32 dx = 0; dx < step; ++dx) {
                    const std::size_t at = (static_cast<std::size_t>(row * step + dy) * size + column * step + dx) * 4;
                    sum = sum + Vec3{decode[static_cast<u32>(octahedral[at])],
                                     decode[static_cast<u32>(octahedral[at + 1])],
                                     decode[static_cast<u32>(octahedral[at + 2])]};
                }
            }
            out.texels[static_cast<std::size_t>(row) * radianceSize + column] = sum * weight;
        }
    }
}

Vec3 SkyRadiance::sample(Vec3 direction) const noexcept
{
    if (size == 0 || texels.empty())
        return Vec3{};
    f32 u = 0.0f;
    f32 v = 0.0f;
    octahedralUv(direction, u, v);
    const f32 x = std::clamp(u * static_cast<f32>(size) - 0.5f, 0.0f, static_cast<f32>(size - 1));
    const f32 y = std::clamp(v * static_cast<f32>(size) - 0.5f, 0.0f, static_cast<f32>(size - 1));
    const u32 x0 = static_cast<u32>(x);
    const u32 y0 = static_cast<u32>(y);
    const u32 x1 = std::min(x0 + 1, size - 1);
    const u32 y1 = std::min(y0 + 1, size - 1);
    const f32 fx = x - static_cast<f32>(x0);
    const f32 fy = y - static_cast<f32>(y0);
    const auto at = [&](u32 tx, u32 ty) { return texels[static_cast<std::size_t>(ty) * size + tx]; };
    const Vec3 top = at(x0, y0) * (1.0f - fx) + at(x1, y0) * fx;
    const Vec3 bottom = at(x0, y1) * (1.0f - fx) + at(x1, y1) * fx;
    return top * (1.0f - fy) + bottom * fy;
}

} // namespace luaug::render
