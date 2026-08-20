#include "luaug/render/lighting.h"

#include <cmath>
#include <doctest/doctest.h>

using luaug::core::f32;
using luaug::core::Vec3;
using luaug::render::sunDirection;

namespace {

constexpr f32 kEpsilon = 1e-5f;

bool near(f32 a, f32 b) noexcept
{
    return std::fabs(a - b) <= kEpsilon;
}

bool near(Vec3 a, Vec3 b) noexcept
{
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

} // namespace

TEST_CASE("sunDirection: the equator's day, written out rather than derived")
{
    // Each of these is a fact about the sky, not a number read off a run. If
    // the axis convention is ever changed, these are what say so.
    CHECK(near(sunDirection(12.0f, 0.0f), Vec3{0.0f, 1.0f, 0.0f}));  // noon: overhead
    CHECK(near(sunDirection(0.0f, 0.0f), Vec3{0.0f, -1.0f, 0.0f}));  // midnight: underfoot
    CHECK(near(sunDirection(6.0f, 0.0f), Vec3{1.0f, 0.0f, 0.0f}));   // sunrise: due east, +X
    CHECK(near(sunDirection(18.0f, 0.0f), Vec3{-1.0f, 0.0f, 0.0f})); // sunset: due west, -X
}

TEST_CASE("sunDirection: north of the equator the noon sun is towards the south, which is +Z")
{
    // The sign that is easiest to get backwards, and the one whose only symptom
    // is a scene lit from the wrong side -- which looks fine in a screenshot.
    const Vec3 noon = sunDirection(12.0f, 45.0f);
    CHECK(noon.y > 0.0f);
    CHECK(noon.z > 0.0f);
    CHECK(near(noon.y, noon.z));
    CHECK(near(noon.x, 0.0f));

    // And south of it, towards the north, which is -Z.
    const Vec3 southern = sunDirection(12.0f, -45.0f);
    CHECK(southern.z < 0.0f);
    CHECK(southern.y > 0.0f);

    // Sunrise stays due east at every latitude, because v1 has no declination.
    CHECK(near(sunDirection(6.0f, 60.0f), Vec3{1.0f, 0.0f, 0.0f}));
}

TEST_CASE("sunDirection: always unit length, at every hour and latitude")
{
    // Unit by construction rather than by normalizing, so this is the assertion
    // that the construction is right. A stepped sweep rather than a spot check,
    // because the failure would be a wrong term in one octant.
    for (int hour = 0; hour < 48; ++hour) {
        for (int latitude = -90; latitude <= 90; latitude += 15) {
            const Vec3 direction = sunDirection(static_cast<f32>(hour) * 0.5f, static_cast<f32>(latitude));
            const f32 length =
                std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
            CHECK(near(length, 1.0f));
        }
    }
}

TEST_CASE("sunDirection: the clock wraps, and is a pure function of its inputs")
{
    // Wrapping is what lets `ClockTime += dt` run forever without a check.
    CHECK(near(sunDirection(25.0f, 30.0f), sunDirection(1.0f, 30.0f)));
    CHECK(near(sunDirection(-1.0f, 30.0f), sunDirection(23.0f, 30.0f)));
    CHECK(near(sunDirection(48.0f, 30.0f), sunDirection(0.0f, 30.0f)));

    // R10, and the reason SunDirection is derived rather than stored: the same
    // clock time gives the same sun in a replay as in the run it replays, with
    // no state in between to drift.
    for (int hour = 0; hour < 24; ++hour) {
        const auto value = static_cast<f32>(hour);
        CHECK(sunDirection(value, 51.5f) == sunDirection(value, 51.5f));
    }
}
