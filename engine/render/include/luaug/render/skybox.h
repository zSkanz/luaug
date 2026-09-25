// A sky of six images, as the renderer draws it and as the world is lit by it
// (ADR 0096, `Sky`).
//
// **The frozen RHI has no cube texture** (ADR 0037), so the six faces are
// resampled on the CPU into ONE octahedral image -- the same unfolding of the
// sphere the environment already uses (ADR 0043) -- which the sky pass samples
// by direction. The same image, averaged down and taken into linear light, is
// what the environment prefilter integrates, so reflections and the sky's
// diffuse light come from the pictures rather than from the gradient.
//
// Everything here is a pure function of decoded pixels, so it runs on job
// threads and is tested without a device. `SkyLoader` owns the reading, the
// jobs and the upload.
#pragma once

#include "luaug/asset/image.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"
#include "luaug/render/look.h"

#include <array>
#include <span>
#include <vector>

namespace luaug::render {

// The resampled picture's side, in texels. An octahedral image spends about
// 2.8 times its side on the horizon's full circle, so 2048 is about sixteen
// texels a degree there -- what a 1080-line picture with a seventy-degree lens
// shows -- and 16 MiB of sRGB texels.
inline constexpr core::u32 kSkyboxSize = 2048;

// The linear copy the environment prefilter reads. Small on purpose: the
// sharpest level of the prefiltered chain is 128 texels, and everything
// rougher is a blur of this.
inline constexpr core::u32 kSkyRadianceSize = 128;

// **The linear light of the pictures, octahedral**, for the prefilter. Held by
// a shared pointer in `SkyParams` so a bake can hand it over whole and the
// chain can tell a new one from the old by the pointer.
struct SkyRadiance
{
    core::u32 size = 0;
    std::vector<core::Vec3> texels;

    // Bilinear, by direction; which need not be normalised.
    [[nodiscard]] core::Vec3 sample(core::Vec3 direction) const noexcept;
};

// Where a direction lands in the six pictures: which face, and where on it --
// `u` to the right and `v` down, both 0 to 1, as the picture is stored.
//
// **The faces are seen from inside, upright**: `Front` towards -Z, `Back`
// towards +Z, `Right` towards +X, `Left` towards -X, each with the sky at the
// top. `Up`'s bottom edge meets `Front`'s top edge, and `Down`'s top edge
// meets `Front`'s bottom edge -- tilt your head from the front face and the
// picture continues.
struct SkyFaceHit
{
    SkyFace face = SkyFace::Front;
    core::f32 u = 0.5f;
    core::f32 v = 0.5f;
};
[[nodiscard]] SkyFaceHit skyFaceOf(core::Vec3 direction) noexcept;

// `Sky.SkyboxOrientation` as a rotation: degrees about X, then Y, then Z,
// turning the set of pictures in the world. `toPictures` takes a world
// direction to where in the pictures it looks, which is the inverse.
struct SkyTurn
{
    core::f32 m[3][3]{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
};
[[nodiscard]] SkyTurn skyTurnOf(core::Vec3 degrees) noexcept;
[[nodiscard]] core::Vec3 toPictures(const SkyTurn& turn, core::Vec3 direction) noexcept;

// Resamples rows `[rowBegin, rowEnd)` of the octahedral picture from the six
// faces, in `SkyFace` order. A missing face -- null, or not decoded -- is
// black. `out` holds `size * size * 4` bytes of sRGB, top row first; the rows
// outside the range are not touched, which is what lets bands run at once.
void resampleSkybox(const std::array<const asset::Image*, kSkyFaceCount>& faces, const SkyTurn& turn, core::u32 size,
                    core::u32 rowBegin, core::u32 rowEnd, std::span<std::byte> out) noexcept;

// The resampled picture, averaged down to `radianceSize` and decoded into
// linear light.
void skyRadianceOf(std::span<const std::byte> octahedral, core::u32 size, core::u32 radianceSize, SkyRadiance& out);

} // namespace luaug::render
