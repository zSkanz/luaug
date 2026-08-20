#include <doctest/doctest.h>

#include "luaug_test_nearly.h"

#include <cmath>

#include "luaug/render/shadow.h"

using namespace luaug;
using luaug::testing::nearly;

namespace
{

// Where a fixed WORLD point lands on the shadow map, in texels, for a camera at
// `origin`. This is the quantity the crawl is about: the snapshot is
// camera-relative, so the point handed to the shader is `world - origin`, and
// the question is whether moving the camera moves the point across the grid.
[[nodiscard]] core::Vec3 shadowTexelOf(core::Vec3 sun, core::DVec3 origin, core::DVec3 world)
{
    const core::Mat4 matrix = render::sunViewProjection(sun, origin);
    const core::Vec3 relative{
        static_cast<core::f32>(world.x - origin.x),
        static_cast<core::f32>(world.y - origin.y),
        static_cast<core::f32>(world.z - origin.z),
    };
    const core::Vec3 clip = core::transformPoint(matrix, relative);
    const auto resolution = static_cast<core::f32>(render::kShadowResolution);
    return core::Vec3{(clip.x * 0.5f + 0.5f) * resolution, (clip.y * 0.5f + 0.5f) * resolution, clip.z};
}

[[nodiscard]] core::f32 distanceToNearestInteger(core::f32 value) noexcept
{
    const core::f32 rounded = std::round(value);
    const core::f32 difference = value - rounded;
    return difference < 0.0f ? -difference : difference;
}

// A sun low enough that the shadow is long and the light-space basis is not
// degenerate -- an overhead sun takes the alternate `up` branch, which is
// covered separately below.
constexpr core::Vec3 kMorningSun{0.6f, 0.35f, 0.72f};

} // namespace

TEST_CASE("a world point keeps its shadow texel when the camera moves under one")
{
    const core::DVec3 world{3.0, 1.5, -2.0};

    // Sub-texel steps, which is the case that produced the reported flicker: at
    // the M4 example's orbit speed the camera moves 0.42 of a texel per frame,
    // so no step here ever crosses a boundary on its own.
    const core::Vec3 reference = shadowTexelOf(kMorningSun, core::DVec3{}, world);
    for (int step = 1; step <= 8; ++step)
    {
        const auto offset = static_cast<core::f64>(step) * 0.4 * static_cast<core::f64>(render::kShadowTexel);
        const core::Vec3 moved = shadowTexelOf(kMorningSun, core::DVec3{offset, 0.0, offset * 0.5}, world);

        // Whole texels, not "no movement": the grid is allowed to jump by a
        // texel when the camera crosses a boundary, and that is exactly what
        // makes a point flip at most once instead of every frame. Asserting
        // equality would be asserting something the design does not promise.
        CHECK(distanceToNearestInteger(moved.x - reference.x) < 0.02f);
        CHECK(distanceToNearestInteger(moved.y - reference.y) < 0.02f);
    }
}

TEST_CASE("without the snap the same movement lands between texels")
{
    // The control for the case above, so that "within 0.02 of an integer" is
    // known to be a claim the arithmetic can fail. It is the same computation
    // with the camera offset applied to the point instead of to the matrix,
    // which is what the unsnapped renderer effectively did.
    const core::DVec3 world{3.0, 1.5, -2.0};
    const core::Mat4 matrix = render::sunViewProjection(kMorningSun, core::DVec3{});
    const auto resolution = static_cast<core::f32>(render::kShadowResolution);

    const auto offset = static_cast<core::f32>(0.4 * static_cast<core::f64>(render::kShadowTexel));
    const core::Vec3 a = core::transformPoint(matrix,
        core::Vec3{static_cast<core::f32>(world.x), static_cast<core::f32>(world.y), static_cast<core::f32>(world.z)});
    const core::Vec3 b = core::transformPoint(matrix,
        core::Vec3{static_cast<core::f32>(world.x) - offset, static_cast<core::f32>(world.y),
            static_cast<core::f32>(world.z) - offset * 0.5f});

    const core::f32 slide = ((b.x - a.x) * 0.5f) * resolution;
    CHECK(distanceToNearestInteger(slide) > 0.05f);
}

TEST_CASE("snapping never moves the grid by more than half a texel")
{
    // The residual is what gets added to the view, and a residual larger than
    // half a texel would mean the rounding went the wrong way -- which would
    // shift the whole map rather than align it.
    const core::Mat4 unsnapped = render::sunViewProjection(kMorningSun, core::DVec3{});
    for (int step = 0; step < 16; ++step)
    {
        const auto offset = static_cast<core::f64>(step) * 7.31;
        const core::Mat4 snapped = render::sunViewProjection(kMorningSun, core::DVec3{offset, 0.0, -offset});
        // Column 3 of the projection*view product carries the translation; the
        // projection scales light-space metres by 1/extent, so the comparison
        // is made in those units.
        const core::f32 scale = 1.0f / render::kShadowExtent;
        CHECK(std::fabs(snapped.m[3][0] - unsnapped.m[3][0]) <= 0.5f * render::kShadowTexel * scale + 1e-6f);
        CHECK(std::fabs(snapped.m[3][1] - unsnapped.m[3][1]) <= 0.5f * render::kShadowTexel * scale + 1e-6f);
    }
}

TEST_CASE("a sun straight overhead still produces a usable basis")
{
    // `lookAt` returns the identity for a degenerate basis, so the alternate
    // `up` branch is what keeps noon from flattening the shadow map. Checked by
    // asserting the matrix actually maps a point somewhere, rather than by
    // reading which branch ran.
    const core::Mat4 matrix = render::sunViewProjection(core::Vec3{0.0f, 1.0f, 0.0f}, core::DVec3{});
    const core::Vec3 clip = core::transformPoint(matrix, core::Vec3{10.0f, 0.0f, 0.0f});
    // Ten metres east maps ten metres off centre in the map. Which side depends
    // on the handedness of the basis `lookAt` builds from the alternate `up`,
    // and that is not what this case is about -- what it is about is that the
    // matrix is not the identity `lookAt` returns for a degenerate one, which
    // would put the point at 10/1 rather than 10/60 and outside the map.
    CHECK(nearly(std::fabs(clip.x), 10.0f / render::kShadowExtent, 1e-4f));
    CHECK(nearly(clip.y, 0.0f, 1e-4f));
    CHECK(clip.z > 0.0f);
    CHECK(clip.z < 1.0f);
}
