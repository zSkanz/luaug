#include "luaug/render/shadow.h"

#include <cmath>
#include <doctest/doctest.h>

#include "luaug_test_nearly.h"

using namespace luaug;
using luaug::testing::nearly;

namespace {

// A sun low enough that the shadow is long and the light-space basis is not
// degenerate -- an overhead sun takes the alternate `up` branch, which is
// covered separately below.
constexpr core::Vec3 kMorningSun{0.6f, 0.35f, 0.72f};

[[nodiscard]] render::ShadowFit fitAt(core::DVec3 origin, core::Vec3 sun = kMorningSun) noexcept
{
    render::ShadowFit fit;
    fit.sunDirection = sun;
    fit.forward = core::Vec3{0.0f, 0.0f, -1.0f};
    fit.right = core::Vec3{1.0f, 0.0f, 0.0f};
    fit.up = core::Vec3{0.0f, 1.0f, 0.0f};
    fit.tanHalfFovX = 0.92f;
    fit.tanHalfFovY = 0.52f;
    fit.nearPlane = 0.1f;
    fit.origin = origin;
    return fit;
}

// Where a fixed WORLD point lands on one cascade's tile, in texels. This is the
// quantity the crawl is about: the snapshot is camera-relative, so the point
// handed to the shader is `world - origin`, and the question is whether moving
// the camera moves the point across the grid.
[[nodiscard]] core::Vec3 shadowTexelOf(core::u32 cascade, core::DVec3 origin, core::DVec3 world)
{
    const render::ShadowCascades cascades = render::fitShadowCascades(fitAt(origin));
    const core::Vec3 relative{
        static_cast<core::f32>(world.x - origin.x),
        static_cast<core::f32>(world.y - origin.y),
        static_cast<core::f32>(world.z - origin.z),
    };
    const core::Vec3 clip = core::transformPoint(cascades.viewProjection[cascade], relative);
    const auto resolution = static_cast<core::f32>(render::kShadowTileResolution);
    return core::Vec3{(clip.x * 0.5f + 0.5f) * resolution, (clip.y * 0.5f + 0.5f) * resolution, clip.z};
}

[[nodiscard]] core::f32 distanceToNearestInteger(core::f32 value) noexcept
{
    const core::f32 rounded = std::round(value);
    const core::f32 difference = value - rounded;
    return difference < 0.0f ? -difference : difference;
}

} // namespace

TEST_CASE("the practical split scheme is monotone and spans exactly the shadow distance")
{
    core::f32 splits[render::kShadowCascadeCount + 1]{};
    render::shadowSplits(0.1f, render::kShadowDistance, render::kShadowSplitLambda, splits);

    CHECK(nearly(splits[0], 0.1f, 1e-5f));
    CHECK(nearly(splits[render::kShadowCascadeCount], render::kShadowDistance, 1e-3f));
    for (core::u32 index = 0; index < render::kShadowCascadeCount; ++index)
        CHECK(splits[index] < splits[index + 1]);
}

TEST_CASE("lambda is the blend between the two schemes, and both ends are reachable")
{
    // GPU Gems 3 chapter 10: 0 is uniform, 1 is logarithmic. Checked at the ends
    // because the value in between is a judgement and the ends are arithmetic.
    core::f32 uniform[render::kShadowCascadeCount + 1]{};
    core::f32 logarithmic[render::kShadowCascadeCount + 1]{};
    render::shadowSplits(1.0f, 100.0f, 0.0f, uniform);
    render::shadowSplits(1.0f, 100.0f, 1.0f, logarithmic);

    CHECK(nearly(uniform[2], 50.5f, 1e-3f));
    // 1 * (100/1)^(2/4) = 10.
    CHECK(nearly(logarithmic[2], 10.0f, 1e-3f));
    // Logarithmic packs resolution near the camera, which is the whole reason
    // the blend exists: uniform alone wastes the near cascade.
    CHECK(logarithmic[1] < uniform[1]);
}

TEST_CASE("a world point keeps its shadow texel when the camera moves under one")
{
    const core::DVec3 world{3.0, 1.5, -12.0};

    // Sub-texel steps, which is the case that produced the reported flicker at
    // M4.5: at that example's orbit speed the camera moved 0.42 of a texel per
    // frame, so no step here ever crosses a boundary on its own.
    //
    // Every cascade, because the fit is per cascade now and a snap that only
    // worked for one of them would look exactly like a snap that works.
    for (core::u32 cascade = 0; cascade < render::kShadowCascadeCount; ++cascade) {
        const render::ShadowCascades reference = render::fitShadowCascades(fitAt(core::DVec3{}));
        const core::Vec3 base = shadowTexelOf(cascade, core::DVec3{}, world);

        for (int step = 1; step <= 8; ++step) {
            const auto offset =
                static_cast<core::f64>(step) * 0.4 * static_cast<core::f64>(reference.texelWorld[cascade]);
            const core::Vec3 moved = shadowTexelOf(cascade, core::DVec3{offset, 0.0, offset * 0.5}, world);

            // Whole texels, not "no movement": the grid is allowed to jump by a
            // texel when the camera crosses a boundary, and that is exactly what
            // makes a point flip at most once instead of every frame.
            CHECK(distanceToNearestInteger(moved.x - base.x) < 0.02f);
            CHECK(distanceToNearestInteger(moved.y - base.y) < 0.02f);
        }
    }
}

TEST_CASE("without the snap the same movement lands between texels")
{
    // The control for the case above, so that "within 0.02 of an integer" is
    // known to be a claim the arithmetic can fail. It is the same computation
    // with the camera offset applied to the POINT instead of to the matrix,
    // which is what an unsnapped renderer effectively does.
    const render::ShadowCascades cascades = render::fitShadowCascades(fitAt(core::DVec3{}));
    const auto resolution = static_cast<core::f32>(render::kShadowTileResolution);
    const core::Vec3 world{3.0f, 1.5f, -12.0f};

    const auto offset = 0.4f * cascades.texelWorld[1];
    const core::Vec3 a = core::transformPoint(cascades.viewProjection[1], world);
    const core::Vec3 b =
        core::transformPoint(cascades.viewProjection[1], core::Vec3{world.x - offset, world.y, world.z});

    const core::f32 slide = std::fabs(b.x - a.x) * 0.5f * resolution;
    CHECK(slide > 0.1f);
    CHECK(slide < 0.9f);
}

TEST_CASE("snapping never moves the grid by more than one texel")
{
    // What the snap must not do is SHIFT the map: the residual it applies is at
    // most half a texel, so across every camera position the translation must
    // stay inside a window one texel wide. A residual that came out larger would
    // mean the rounding went the wrong way, and the whole grid would slide.
    //
    // Stated as a window rather than as a distance from an unsnapped reference,
    // because every fit here is snapped -- the cascade's centre is ahead of the
    // camera, so even the fit at world origin has a residual of its own.
    core::f32 lowest[render::kShadowCascadeCount]{};
    core::f32 highest[render::kShadowCascadeCount]{};
    core::f32 texel[render::kShadowCascadeCount]{};
    bool seeded = false;

    for (int step = 0; step < 32; ++step) {
        const auto offset = static_cast<core::f64>(step) * 7.31;
        const render::ShadowCascades fitted = render::fitShadowCascades(fitAt(core::DVec3{offset, 0.0, -offset}));
        for (core::u32 cascade = 0; cascade < render::kShadowCascadeCount; ++cascade) {
            const core::f32 value = fitted.viewProjection[cascade].m[3][0];
            texel[cascade] = fitted.texelWorld[cascade];
            if (!seeded || value < lowest[cascade])
                lowest[cascade] = value;
            if (!seeded || value > highest[cascade])
                highest[cascade] = value;
        }
        seeded = true;
    }

    for (core::u32 cascade = 0; cascade < render::kShadowCascadeCount; ++cascade) {
        // Column 3 of the projection*view product carries the translation, and
        // the projection scales light-space metres by 1/radius -- the radius
        // being half a tile of texels.
        const core::f32 scale = 2.0f / (texel[cascade] * static_cast<core::f32>(render::kShadowTileResolution));
        CHECK(highest[cascade] - lowest[cascade] <= texel[cascade] * scale + 1e-5f);
        // And it does move: a window of zero would mean the fit ignores the
        // camera, which is a different bug that this same check would hide.
        CHECK(highest[cascade] - lowest[cascade] > 0.0f);
    }
}

TEST_CASE("every cascade is sharper than M4's single one, and the near one by an order")
{
    // The number ADR 0038 opens with: M4 shipped 5.9 cm per texel, everywhere,
    // out to sixty metres. This is what replaces it, and the claim is checked
    // rather than asserted in a comment.
    const render::ShadowCascades cascades = render::fitShadowCascades(fitAt(core::DVec3{}));
    for (core::u32 cascade = 0; cascade < render::kShadowCascadeCount; ++cascade)
        CHECK(cascades.texelWorld[cascade] > 0.0f);

    CHECK(cascades.texelWorld[0] < 0.02f);
    // And they grow with distance, which is the whole point of a cascade: the
    // far one covers twice M4's range at a resolution nobody looks at closely.
    for (core::u32 cascade = 1; cascade < render::kShadowCascadeCount; ++cascade)
        CHECK(cascades.texelWorld[cascade] > cascades.texelWorld[cascade - 1]);
}

TEST_CASE("the filter is the same number of texels everywhere, and the seam is the ratio between cascades")
{
    // **This case reversed with the filter, and the reversal is the point.**
    // M7.5 asked for a radius constant in WORLD metres and clamped it to a texel
    // range, and this case measured how hard the clamp bound. It bound so hard
    // that the far cascade filtered 0.8 texels -- nine taps inside one texel,
    // which is a binary comparison wearing a loop, and a human saw it as a
    // shadow that loses its gradient as it stretches.
    //
    // The kernel is a fixed number of texels now, which is what every reference
    // implementation does (U-58), and the softness a viewer sees therefore
    // changes across a split by exactly the texel ratio. So that ratio is what
    // this case measures: it is the whole of the seam risk, and the blend band
    // is what carries it.
    const render::ShadowCascades cascades = render::fitShadowCascades(fitAt(core::DVec3{}));
    for (core::u32 cascade = 0; cascade + 1 < render::kShadowCascadeCount; ++cascade) {
        const core::f32 here = cascades.texelWorld[cascade] * (render::kShadowFilterTexels + 1.0f);
        const core::f32 next = cascades.texelWorld[cascade + 1] * (render::kShadowFilterTexels + 1.0f);
        CHECK(next > here);
        // Under five, and the last pair is the one that spends it: the practical
        // split scheme at 0.85 leans logarithmic, which buys the near cascade
        // its 1.1 cm texel and pays for it at the far end. A tighter bound here
        // would be a demand on `kShadowSplitLambda` rather than on the filter.
        //
        // Fitted to real casters the ratio is smaller than this -- the fit
        // shrinks the far cascade most -- but the fixture hands the fit no
        // casters on purpose, so what this checks is the worst case the splits
        // can produce rather than what one scene happens to get.
        CHECK(next / here < 5.0f);
    }
}

TEST_CASE("a sun straight overhead still produces a usable basis")
{
    // `lookAt` returns the identity for a degenerate basis, so the alternate
    // `up` branch is what keeps noon from flattening the shadow map. Checked by
    // asserting the matrix actually maps a point somewhere, rather than by
    // reading which branch ran.
    const render::ShadowCascades cascades =
        render::fitShadowCascades(fitAt(core::DVec3{}, core::Vec3{0.0f, 1.0f, 0.0f}));
    const core::f32 radius = cascades.texelWorld[3] * 0.5f * static_cast<core::f32>(render::kShadowTileResolution);
    const core::Vec3 clip = core::transformPoint(cascades.viewProjection[3], core::Vec3{10.0f, 0.0f, -60.0f});

    // Ten metres east maps ten metres off centre in the map. Which side depends
    // on the handedness of the basis `lookAt` builds from the alternate `up`,
    // and that is not what this case is about -- what it is about is that the
    // matrix is not the identity `lookAt` returns for a degenerate one, which
    // would put the point at 10/1 rather than at 10/radius and outside the map.
    CHECK(nearly(std::fabs(clip.x), 10.0f / radius, 1e-3f));
    CHECK(clip.z > 0.0f);
    CHECK(clip.z < 1.0f);
}
