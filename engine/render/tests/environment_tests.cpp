#include "luaug/jobs/jobs.h"
#include "luaug/render/environment.h"

#include <cmath>
#include <doctest/doctest.h>
#include <vector>

using luaug::core::Color3;
using luaug::core::f32;
using luaug::core::u16;
using luaug::core::u32;
using luaug::core::usize;
using luaug::core::Vec3;
using luaug::render::bakeBrdfLut;
using luaug::render::bakeEnvironmentLevel;
using luaug::render::bakeIrradianceSh;
using luaug::render::evaluateSky;
using luaug::render::floatToHalf;
using luaug::render::halfToFloat;
using luaug::render::kBrdfLutSize;
using luaug::render::kSunDiscIntensity;
using luaug::render::octahedralDirection;
using luaug::render::octahedralUv;
using luaug::render::SkyParams;
using luaug::render::skyParamsFor;

namespace {

bool near(f32 a, f32 b, f32 epsilon) noexcept
{
    return std::fabs(a - b) <= epsilon;
}

// A sky that is one colour in every direction, so an integral over it has a
// closed form and the conventions can be checked against arithmetic rather than
// against another implementation of the same mistake.
SkyParams uniformSky(f32 level) noexcept
{
    SkyParams params;
    params.sunDirection = Vec3{0.0f, 1.0f, 0.0f};
    params.horizonColor = Color3{level, level, level};
    params.zenithColor = Color3{level, level, level};
    // No disc: it is the one high-frequency feature and it would defeat the
    // closed form this whole fixture exists to compare against.
    params.sunColor = Color3{0.0f, 0.0f, 0.0f};
    params.dayFactor = 0.0f;
    return params;
}

} // namespace

TEST_CASE("floatToHalf: the round trip, and the two saturating ends")
{
    for (const f32 value : {0.0f, 1.0f, 0.5f, 0.125f, 3.75f, 1024.0f, -2.5f})
        CHECK(near(halfToFloat(floatToHalf(value)), value, 1e-3f));

    // Binary16 tops out at 65504. Saturating rather than producing an infinity
    // is deliberate: the environment is radiance, and an infinity in it
    // propagates through every filter that reads it.
    CHECK(near(halfToFloat(floatToHalf(1.0e30f)), 65504.0f, 1.0f));
    CHECK(near(halfToFloat(floatToHalf(1.0e-30f)), 0.0f, 1e-6f));
}

TEST_CASE("octahedral: the mapping round-trips, and its landmarks are where they should be")
{
    // The centre is up and the corners are down: that is the y-up unfolding, and
    // getting it wrong puts every reflection on its head with no other symptom.
    CHECK(near(octahedralDirection(0.5f, 0.5f).y, 1.0f, 1e-5f));
    CHECK(near(octahedralDirection(0.0f, 0.0f).y, -1.0f, 1e-5f));
    CHECK(near(octahedralDirection(1.0f, 1.0f).y, -1.0f, 1e-5f));
    CHECK(near(octahedralDirection(1.0f, 0.5f).x, 1.0f, 1e-5f));
    CHECK(near(octahedralDirection(0.5f, 1.0f).z, 1.0f, 1e-5f));

    // The round trip is what `luaug_brdf.hlsli`'s copy of this mapping has to
    // agree with. A direction that encodes to a uv that decodes to a different
    // direction is a reflection that lands somewhere else.
    for (u32 i = 1; i < 16; ++i) {
        for (u32 j = 1; j < 16; ++j) {
            const f32 u = static_cast<f32>(i) / 16.0f;
            const f32 v = static_cast<f32>(j) / 16.0f;
            const Vec3 direction = octahedralDirection(u, v);
            f32 backU = 0.0f;
            f32 backV = 0.0f;
            octahedralUv(direction, backU, backV);
            CHECK(near(backU, u, 1e-4f));
            CHECK(near(backV, v, 1e-4f));
        }
    }
}

TEST_CASE("the mirror level IS the sky, which is what makes roughness zero mean anything")
{
    SkyParams params = skyParamsFor(Vec3{0.3f, 0.8f, 0.5f}, Color3{0.6f, 0.7f, 0.85f});
    constexpr u32 kSize = 16;
    std::vector<u16> texels(static_cast<usize>(kSize) * kSize * 4, 0);
    bakeEnvironmentLevel(params, kSize, 0.0f, 1, texels);

    // An interior texel, away from the seam: the border is deliberately averaged
    // with its partner, so it is the one place the bake does NOT equal the sky.
    const u32 x = 7;
    const u32 y = 9;
    const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(kSize);
    const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(kSize);
    const Vec3 expected = evaluateSky(params, octahedralDirection(u, v));

    const usize index = (static_cast<usize>(y) * kSize + x) * 4;
    CHECK(near(halfToFloat(texels[index + 0]), expected.x, 1e-2f));
    CHECK(near(halfToFloat(texels[index + 1]), expected.y, 1e-2f));
    CHECK(near(halfToFloat(texels[index + 2]), expected.z, 1e-2f));
}

TEST_CASE("prefiltering a flat sky returns that flat value, which is the normalisation check")
{
    // The GGX prefilter is a weighted AVERAGE of directions, so over a region
    // where the sky is constant its integral is that constant. A normalisation
    // error here shows up as "rough surfaces are mysteriously darker", which is
    // indistinguishable from a lighting preference until something asserts it.
    //
    // Only well above the horizon, and only at a roughness whose lobe is narrow:
    // `evaluateSky` darkens the lower hemisphere by design, so the sky is flat
    // above y = 0 and is not flat across it.
    const SkyParams params = uniformSky(0.4f);
    constexpr u32 kSize = 16;
    std::vector<u16> texels(static_cast<usize>(kSize) * kSize * 4, 0);
    bakeEnvironmentLevel(params, kSize, 0.25f, 128, texels);

    u32 checked = 0;
    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(kSize);
            const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(kSize);
            if (octahedralDirection(u, v).y < 0.75f)
                continue;
            const usize index = (static_cast<usize>(y) * kSize + x) * 4;
            CHECK(near(halfToFloat(texels[index + 0]), 0.4f, 8e-3f));
            ++checked;
        }
    }
    // A loop that checked nothing is a test that cannot fail.
    CHECK(checked >= 8);
}

TEST_CASE("irradiance reconstructs the cosine integral, which is what pins the 1/pi convention")
{
    // A surface under radiance L(d) reflects `albedo/pi * integral L(d) (n.d) dw`.
    // The coefficients carry the cosine convolution AND the 1/pi already
    // (environment.h), so their reconstruction must equal that integral divided
    // by pi -- not pi times it, and not it divided by pi twice.
    //
    // Compared against the DEFINITION, computed here, rather than against a
    // fixture: a factor of pi is "the scene looks a bit flat", which is
    // indistinguishable from a lighting preference until something asserts it.
    // The sky used is deliberately not uniform -- it darkens below the horizon,
    // as `evaluateSky` does by design -- so a check that only worked for a
    // constant would not pass.
    const SkyParams params = uniformSky(0.25f);
    Vec3 coefficients[9]{};
    bakeIrradianceSh(params, coefficients);

    const auto reconstruct = [&](Vec3 n) {
        const f32 basis[9]{0.282095f,
                           0.488603f * n.y,
                           0.488603f * n.z,
                           0.488603f * n.x,
                           1.092548f * n.x * n.y,
                           1.092548f * n.y * n.z,
                           0.315392f * (3.0f * n.z * n.z - 1.0f),
                           1.092548f * n.x * n.z,
                           0.546274f * (n.x * n.x - n.y * n.y)};
        Vec3 total{};
        for (u32 i = 0; i < 9; ++i)
            total = total + coefficients[i] * basis[i];
        return total;
    };

    const auto integrate = [&](Vec3 n) {
        constexpr u32 kThetaSteps = 128;
        constexpr u32 kPhiSteps = 256;
        constexpr f32 kPi = 3.14159265358979323846f;
        const f32 dTheta = kPi / static_cast<f32>(kThetaSteps);
        const f32 dPhi = 2.0f * kPi / static_cast<f32>(kPhiSteps);
        f32 total = 0.0f;
        for (u32 t = 0; t < kThetaSteps; ++t) {
            const f32 theta = (static_cast<f32>(t) + 0.5f) * dTheta;
            for (u32 p = 0; p < kPhiSteps; ++p) {
                const f32 phi = (static_cast<f32>(p) + 0.5f) * dPhi;
                const Vec3 d{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
                const f32 cosine = luaug::core::dot(n, d);
                if (cosine <= 0.0f)
                    continue;
                total += evaluateSky(params, d).x * cosine * std::sin(theta) * dTheta * dPhi;
            }
        }
        return total / kPi;
    };

    // Three percent, because order two is a TRUNCATION and this sky has a step
    // at the horizon. Ramamoorthi and Hanrahan's result is that nine
    // coefficients carry over 99% of a smooth irradiance signal; a step costs
    // the rest of the budget and nothing more.
    for (const Vec3 normal :
         {Vec3{0.0f, 1.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, -1.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}}) {
        const f32 expected = integrate(normal);
        CHECK(near(reconstruct(normal).x, expected, 0.03f * expected + 2e-3f));
    }
}

TEST_CASE("the BRDF table's ends are the ones the split sum's algebra fixes")
{
    std::vector<u16> lut(static_cast<usize>(kBrdfLutSize) * kBrdfLutSize * 4, 0);
    bakeBrdfLut(kBrdfLutSize, lut);

    const auto at = [&](u32 x, u32 y, u32 channel) {
        return halfToFloat(lut[(static_cast<usize>(y) * kBrdfLutSize + x) * 4 + channel]);
    };

    // A mirror seen head-on: all the energy is in the scale term and none in the
    // Fresnel bias, so specular is exactly F0.
    CHECK(near(at(kBrdfLutSize - 1, 0, 0), 1.0f, 2e-2f));
    CHECK(near(at(kBrdfLutSize - 1, 0, 1), 0.0f, 2e-2f));

    // Both terms are energy fractions, so neither may exceed one anywhere. A
    // table that does is a table that makes a rough metal brighter than the sky
    // it reflects.
    for (u32 y = 0; y < kBrdfLutSize; ++y) {
        for (u32 x = 0; x < kBrdfLutSize; ++x) {
            CHECK(at(x, y, 0) <= 1.001f);
            CHECK(at(x, y, 1) <= 1.001f);
            CHECK(at(x, y, 0) >= -0.001f);
            CHECK(at(x, y, 1) >= -0.001f);
        }
    }
}

TEST_CASE("the sky derivation: a sun below the horizon lights nothing, and a low one is warm")
{
    const Color3 fog{0.6f, 0.7f, 0.85f};

    const SkyParams night = skyParamsFor(Vec3{0.0f, -1.0f, 0.0f}, fog);
    CHECK(near(night.dayFactor, 0.0f, 1e-6f));

    const SkyParams noon = skyParamsFor(Vec3{0.0f, 1.0f, 0.0f}, fog);
    CHECK(near(noon.dayFactor, 1.0f, 1e-6f));
    // At midday the tint is white, so `Lighting.FogColor` reaches the horizon
    // unchanged -- the property keeps meaning exactly what it documents.
    CHECK(near(noon.horizonColor.r, fog.r, 1e-3f));
    CHECK(near(noon.horizonColor.b, fog.b, 1e-3f));

    // A sun a few degrees up has crossed enough atmosphere to have lost most of
    // its blue, which is the whole reason a sunset reads as a sunset.
    const SkyParams dawn = skyParamsFor(Vec3{1.0f, 0.05f, 0.0f}, fog);
    CHECK(dawn.sunColor.r > dawn.sunColor.b * 2.0f);
    CHECK(noon.sunColor.b > dawn.sunColor.b);
}

TEST_CASE("the sun's disc is far brighter than the sky, which is what puts it in a mirror")
{
    const SkyParams params = skyParamsFor(Vec3{0.0f, 1.0f, 0.0f}, Color3{0.6f, 0.7f, 0.85f});
    const Vec3 atSun = evaluateSky(params, params.sunDirection);
    const Vec3 asideOfSun = evaluateSky(params, Vec3{1.0f, 0.2f, 0.0f});
    CHECK(atSun.x > asideOfSun.x * (kSunDiscIntensity * 0.5f));
}

TEST_CASE("the BRDF table is the same bytes however many workers baked it")
{
    // **The whole reason this bake could be parallelised at all.** 512
    // importance samples per texel over a 256-square table is 33 million of
    // them, on the first frame that renders -- and the capture goldens carry a
    // content HASH of the result, so one ULP of drift reddens four gates that
    // run in CI.
    //
    // The partition is a function of the data (`jobs.h`), so the answer must not
    // depend on how many workers this machine has. That is asserted here the
    // only way it can be: bake it with the pool in its documented SERIAL mode --
    // an uninitialised pool walks the ranges in order on the calling thread --
    // and again with real workers, and require the bytes to agree.
    constexpr u32 kSize = 64;
    const usize texels = static_cast<usize>(kSize) * kSize * 4;

    const bool poolWasUp = luaug::jobs::initialized();
    if (poolWasUp)
        luaug::jobs::shutdown();

    std::vector<u16> serial(texels, 0);
    bakeBrdfLut(kSize, serial);

    luaug::jobs::init(4);
    std::vector<u16> parallel(texels, 0);
    bakeBrdfLut(kSize, parallel);
    luaug::jobs::shutdown();

    if (poolWasUp)
        luaug::jobs::init();

    // Byte-identical, not near: a golden compares a hash.
    CHECK(serial == parallel);

    // And it baked something rather than agreeing about zeroes, which a
    // comparison alone would happily do.
    bool anyNonZero = false;
    for (const u16 value : serial)
        anyNonZero = anyNonZero || value != 0;
    CHECK(anyNonZero);
}
