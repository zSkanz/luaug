// A `Sky`'s six pictures, resampled (ADR 0096): which picture a direction
// looks at, how `SkyboxOrientation` turns them, and that the octahedral image
// and its linear copy carry what the pictures held.
#include "luaug/render/environment.h"
#include "luaug/render/skybox.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <doctest/doctest.h>

#include "luaug_test_nearly.h"

using namespace luaug;
using luaug::testing::nearly;

namespace {

// A picture of one flat colour.
[[nodiscard]] asset::Image flat(core::u32 size, std::byte r, std::byte g, std::byte b)
{
    asset::Image image;
    image.width = size;
    image.height = size;
    image.sourceChannels = 4;
    image.pixels.resize(static_cast<std::size_t>(size) * size * 4);
    for (std::size_t at = 0; at < image.pixels.size(); at += 4) {
        image.pixels[at] = r;
        image.pixels[at + 1] = g;
        image.pixels[at + 2] = b;
        image.pixels[at + 3] = std::byte{0xFF};
    }
    return image;
}

} // namespace

TEST_CASE("each axis looks at the middle of its own picture")
{
    const auto check = [](core::Vec3 direction, render::SkyFace face) {
        const render::SkyFaceHit hit = render::skyFaceOf(direction);
        CHECK(hit.face == face);
        CHECK(nearly(hit.u, 0.5f));
        CHECK(nearly(hit.v, 0.5f));
    };
    check(core::Vec3{0.0f, 0.0f, -1.0f}, render::SkyFace::Front);
    check(core::Vec3{0.0f, 0.0f, 1.0f}, render::SkyFace::Back);
    check(core::Vec3{1.0f, 0.0f, 0.0f}, render::SkyFace::Right);
    check(core::Vec3{-1.0f, 0.0f, 0.0f}, render::SkyFace::Left);
    check(core::Vec3{0.0f, 1.0f, 0.0f}, render::SkyFace::Up);
    check(core::Vec3{0.0f, -1.0f, 0.0f}, render::SkyFace::Down);
}

TEST_CASE("the pictures are upright and continue across their edges")
{
    // A little above the front picture's middle is above its middle row.
    CHECK(render::skyFaceOf(core::Vec3{0.0f, 0.3f, -1.0f}).v < 0.5f);
    // And to the right of the front is towards +X.
    CHECK(render::skyFaceOf(core::Vec3{0.3f, 0.0f, -1.0f}).u > 0.5f);

    // **Across the front's top edge into the up picture's bottom edge**: just
    // below the diagonal is the front at its top, just above it is up at its
    // bottom -- and the same column either side.
    const render::SkyFaceHit front = render::skyFaceOf(core::Vec3{0.2f, 0.999f, -1.0f});
    const render::SkyFaceHit up = render::skyFaceOf(core::Vec3{0.2f, 1.0f, -0.999f});
    CHECK(front.face == render::SkyFace::Front);
    CHECK(up.face == render::SkyFace::Up);
    CHECK(front.v < 0.01f);
    CHECK(up.v > 0.99f);
    CHECK(nearly(front.u, up.u, 1e-3f));

    // And the front's bottom edge into the down picture's top edge.
    const render::SkyFaceHit low = render::skyFaceOf(core::Vec3{0.2f, -0.999f, -1.0f});
    const render::SkyFaceHit down = render::skyFaceOf(core::Vec3{0.2f, -1.0f, -0.999f});
    CHECK(low.face == render::SkyFace::Front);
    CHECK(down.face == render::SkyFace::Down);
    CHECK(low.v > 0.99f);
    CHECK(down.v < 0.01f);
    CHECK(nearly(low.u, down.u, 1e-3f));

    // Round the side: the front's right edge meets the right picture's left.
    const render::SkyFaceHit edge = render::skyFaceOf(core::Vec3{0.999f, 0.1f, -1.0f});
    const render::SkyFaceHit side = render::skyFaceOf(core::Vec3{1.0f, 0.1f, -0.999f});
    CHECK(edge.face == render::SkyFace::Front);
    CHECK(side.face == render::SkyFace::Right);
    CHECK(edge.u > 0.99f);
    CHECK(side.u < 0.01f);
    CHECK(nearly(edge.v, side.v, 1e-3f));
}

TEST_CASE("an orientation turns the pictures in the world")
{
    // None turns nothing.
    const render::SkyTurn none = render::skyTurnOf(core::Vec3{0.0f, 0.0f, 0.0f});
    const core::Vec3 same = render::toPictures(none, core::Vec3{0.3f, 0.4f, -0.5f});
    CHECK(nearly(same.x, 0.3f));
    CHECK(nearly(same.y, 0.4f));
    CHECK(nearly(same.z, -0.5f));

    // A quarter turn about Y carries the back picture (+Z) round to +X: looking
    // towards +X then shows what faced +Z.
    const render::SkyTurn quarter = render::skyTurnOf(core::Vec3{0.0f, 90.0f, 0.0f});
    const core::Vec3 looked = render::toPictures(quarter, core::Vec3{1.0f, 0.0f, 0.0f});
    CHECK(nearly(looked.x, 0.0f, 1e-5f));
    CHECK(nearly(looked.z, 1.0f, 1e-5f));
}

TEST_CASE("the octahedral picture holds the six faces, and its linear copy their light")
{
    // Six colours, one per face, so every texel says where it came from.
    const std::array<asset::Image, render::kSkyFaceCount> faces{
        flat(8, std::byte{255}, std::byte{0}, std::byte{0}),     // Back
        flat(8, std::byte{0}, std::byte{255}, std::byte{0}),     // Down
        flat(8, std::byte{0}, std::byte{0}, std::byte{255}),     // Front
        flat(8, std::byte{255}, std::byte{255}, std::byte{0}),   // Left
        flat(8, std::byte{0}, std::byte{255}, std::byte{255}),   // Right
        flat(8, std::byte{128}, std::byte{128}, std::byte{128}), // Up
    };
    std::array<const asset::Image*, render::kSkyFaceCount> pointers{};
    for (core::u32 face = 0; face < render::kSkyFaceCount; ++face)
        pointers[face] = &faces[face];

    constexpr core::u32 size = 64;
    std::vector<std::byte> picture(static_cast<std::size_t>(size) * size * 4);
    // Two bands, as the loader runs them: rows are independent.
    render::resampleSkybox(pointers, render::skyTurnOf(core::Vec3{}), size, 0, size / 2, picture);
    render::resampleSkybox(pointers, render::skyTurnOf(core::Vec3{}), size, size / 2, size, picture);

    const auto at = [&](core::Vec3 direction) {
        core::f32 u = 0.0f;
        core::f32 v = 0.0f;
        render::octahedralUv(direction, u, v);
        // Straight down is the octahedron's corner, u and v of 1: the last texel.
        const auto column = std::min(static_cast<core::u32>(u * static_cast<core::f32>(size)), size - 1);
        const auto row = std::min(static_cast<core::u32>(v * static_cast<core::f32>(size)), size - 1);
        const std::byte* pixel = picture.data() + (static_cast<std::size_t>(row) * size + column) * 4;
        return std::array<int, 3>{static_cast<int>(pixel[0]), static_cast<int>(pixel[1]), static_cast<int>(pixel[2])};
    };
    CHECK(at(core::Vec3{0.0f, 1.0f, 0.0f}) == std::array<int, 3>{128, 128, 128});
    CHECK(at(core::Vec3{0.0f, 0.0f, -1.0f}) == std::array<int, 3>{0, 0, 255});
    CHECK(at(core::Vec3{1.0f, 0.1f, 0.0f}) == std::array<int, 3>{0, 255, 255});
    CHECK(at(core::Vec3{0.0f, -1.0f, 0.0f}) == std::array<int, 3>{0, 255, 0});

    // The linear copy: the up picture's grey, 128 in sRGB, is about 0.216 of
    // white in light.
    render::SkyRadiance radiance;
    render::skyRadianceOf(picture, size, 16, radiance);
    const core::Vec3 up = radiance.sample(core::Vec3{0.0f, 1.0f, 0.0f});
    CHECK(nearly(up.x, 0.2158f, 0.01f));
    CHECK(nearly(up.y, up.x, 1e-4f));
}

TEST_CASE("a missing picture is black, not the neighbour's")
{
    std::array<const asset::Image*, render::kSkyFaceCount> none{};
    constexpr core::u32 size = 16;
    std::vector<std::byte> picture(static_cast<std::size_t>(size) * size * 4, std::byte{0x7F});
    render::resampleSkybox(none, render::skyTurnOf(core::Vec3{}), size, 0, size, picture);
    for (std::size_t at = 0; at < picture.size(); at += 4) {
        CHECK(picture[at] == std::byte{0});
        CHECK(picture[at + 3] == std::byte{0xFF});
    }
}

TEST_CASE("a sky of pictures replaces the gradient in the environment, and keeps the sun")
{
    render::SkyParams params = render::skyParamsFor(core::Vec3{0.0f, 1.0f, 0.0f}, core::Color3{0.6f, 0.7f, 0.85f});
    auto radiance = std::make_shared<render::SkyRadiance>();
    radiance->size = 2;
    radiance->texels.assign(4, core::Vec3{0.25f, 0.5f, 0.75f});
    params.skybox = radiance;

    // Away from the sun: the pictures' light exactly.
    const core::Vec3 side = render::evaluateSky(params, core::Vec3{1.0f, 0.0f, 0.0f});
    CHECK(nearly(side.x, 0.25f));
    CHECK(nearly(side.z, 0.75f));
    // Towards it: brighter, because the sun is still drawn on top.
    CHECK(render::evaluateSky(params, core::Vec3{0.0f, 1.0f, 0.0f}).x > 1.0f);
    // And `CelestialBodiesShown` off takes the sun out of the reflection too.
    params.celestial = false;
    CHECK(nearly(render::evaluateSky(params, core::Vec3{0.0f, 1.0f, 0.0f}).x, 0.25f));
}
