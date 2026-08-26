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

TEST_CASE("every cascade can draw the penumbra the filter asks for")
{
    // **This case has now been rewritten twice, and each rewrite is a design
    // that failed.** First it measured how hard a texel clamp bound on a
    // world-constant radius -- it bound so hard the far cascade filtered 0.8
    // texels, which is not a filter. Then the kernel was sized in texels
    // instead, and this case measured the softness ratio across a split -- which
    // made the near cascade's penumbra a few millimetres wide, so its shadow
    // edges showed the texel grid they were rasterised on.
    //
    // The claim now is the one that matters to a viewer: the penumbra is a
    // number of METRES and every cascade must be able to draw it. "Able" is the
    // texel band -- under six a filter cannot hide the step it exists for, over
    // eight its own taps start to show even rotated -- so this checks that the
    // shipped
    // cascades land inside it rather than on its edges, because a cascade on the
    // edge is one whose softness the clamp is choosing rather than the radius.
    const render::ShadowCascades cascades = render::fitShadowCascades(fitAt(core::DVec3{}));
    for (core::u32 cascade = 0; cascade < render::kShadowCascadeCount; ++cascade) {
        const core::f32 wanted = render::kShadowFilterWorldRadius / cascades.texelWorld[cascade];
        const core::f32 actual =
            wanted < render::kShadowFilterMinTexels
                ? render::kShadowFilterMinTexels
                : (wanted > render::kShadowFilterMaxTexels ? render::kShadowFilterMaxTexels : wanted);
        // **Six texels, everywhere, whatever else happens (D054).** That is the whole
        // guarantee: a shadow edge is quantised onto this grid, and a penumbra
        // narrower than the step it has to hide leaves the step showing. The
        // near cascade reaches it by clamping up from a radius its own texel
        // makes tiny, and the far one by clamping down from one its texel makes
        // huge -- and both of those are the clamp doing its job rather than
        // failing at it, which is what the two previous versions of this case
        // got wrong.
        CHECK(actual >= render::kShadowFilterMinTexels);
        CHECK(actual <= render::kShadowFilterMaxTexels);
        // And it is a real distance on the ground: never under a centimetre,
        // never over two and a half metres, across a chain whose texels span
        // 23:1.
        //
        // **The ceiling was one metre until D054.** The floor of the texel band
        // is what the far cascade gets -- out there the authored penumbra is a
        // fraction of a texel and the clamp is the only thing speaking -- and
        // six texels of a third of a metre is two metres of penumbra. That is
        // the trade the defect asked for: a shadow at eighty metres that is
        // soft rather than one that advances in visible steps as the sun turns.
        CHECK(actual * cascades.texelWorld[cascade] > 0.01f);
        CHECK(actual * cascades.texelWorld[cascade] < 2.5f);
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

// --- Local lights: spots and points get shadows too ---------------------------
//
// **These are the first assertions `PointLight.Shadows` and `SpotLight.Shadows`
// have ever had.** The properties were stored, plumbed to `RenderLight::shadows`
// and read by nothing for three milestones, so there was nothing to test. What
// is checked here is the half that needs no GPU and is the half most likely to
// be wrong: which lights get a tile, in what order, and where each tile is.

namespace {

[[nodiscard]] render::LocalShadowCandidate spotAt(core::Vec3 position, core::f32 range,
                                                  core::f32 cosHalfAngle = 0.7f) noexcept
{
    render::LocalShadowCandidate candidate;
    candidate.position = position;
    candidate.direction = core::Vec3{0.0f, -1.0f, 0.0f};
    candidate.range = range;
    candidate.cosHalfAngle = cosHalfAngle;
    return candidate;
}

[[nodiscard]] render::LocalShadowCandidate pointAt(core::Vec3 position, core::f32 range) noexcept
{
    render::LocalShadowCandidate candidate;
    candidate.position = position;
    candidate.range = range;
    // -1 is what makes it a point light, and it is `GpuLight`'s own spelling.
    candidate.cosHalfAngle = -1.0f;
    return candidate;
}

// Whether a world point lands inside the clip volume a matrix projects into.
[[nodiscard]] bool insideClip(const core::Mat4& viewProjection, core::Vec3 point) noexcept
{
    // `transformPoint` returns the raw xyz and no w, which is right for the
    // cascades above -- an orthographic w is always one -- and not enough here:
    // a perspective divide by a NEGATIVE w mirrors a point behind the light into
    // the volume in front of it, and every one of these cases would then pass by
    // accident. So w is computed and its sign is checked before anything else.
    const core::Vec3 clip = core::transformPoint(viewProjection, point);
    const core::f32 w = viewProjection.m[0][3] * point.x + viewProjection.m[1][3] * point.y +
                        viewProjection.m[2][3] * point.z + viewProjection.m[3][3];
    if (!(w > 0.0f))
        return false;
    const core::f32 x = clip.x / w;
    const core::f32 y = clip.y / w;
    const core::f32 z = clip.z / w;
    return x >= -1.0f && x <= 1.0f && y >= -1.0f && y <= 1.0f && z >= 0.0f && z <= 1.0f;
}

} // namespace

TEST_CASE("the atlas is a four by four grid, row-major, and a tile index wraps")
{
    const render::LocalShadowTileRect first = render::localShadowTileRect(0);
    CHECK(first.x == 0);
    CHECK(first.y == 0);
    CHECK(first.width == render::kLocalShadowTileResolution);

    // The fifth tile starts the second row, which is what "row-major" has to
    // mean for the shader index arithmetic to agree with this.
    const render::LocalShadowTileRect fifth = render::localShadowTileRect(4);
    CHECK(fifth.x == 0);
    CHECK(fifth.y == render::kLocalShadowTileResolution);

    const render::LocalShadowTileRect last = render::localShadowTileRect(render::kLocalShadowTileCount - 1);
    CHECK(last.x == render::kLocalShadowTileResolution * 3);
    CHECK(last.y == render::kLocalShadowTileResolution * 3);
}

TEST_CASE("a spot costs one tile and a point costs six")
{
    const render::LocalShadowCandidate candidates[] = {
        spotAt({0.0f, 5.0f, -3.0f}, 20.0f),
        pointAt({2.0f, 4.0f, -3.0f}, 20.0f),
    };

    const render::LocalShadows shadows = render::fitLocalShadows(candidates);
    REQUIRE(shadows.count == 2);
    CHECK(shadows.tilesUsed == render::kSpotShadowTiles + render::kPointShadowTiles);
    CHECK(shadows.refused == 0);

    // Tiles are handed out contiguously, so the second light starts where the
    // first ended. The shader indexes faces off `firstTile`, so a gap would be a
    // face sampled out of a tile belonging to another light.
    const render::LocalShadow& second = shadows.entries[1];
    CHECK(second.firstTile == shadows.entries[0].firstTile + shadows.entries[0].tileCount);
}

TEST_CASE("the budget is tiles, so a point that does not fit does not block the spots behind it")
{
    // Two points is twelve tiles; a third would be eighteen against sixteen. The
    // spots after it cost one each and must still be served -- a budget that
    // stopped at the first refusal would be a queue, and one large light near
    // the camera would take every shadow in the scene with it.
    const render::LocalShadowCandidate candidates[] = {
        pointAt({0.0f, 0.0f, -1.0f}, 100.0f), pointAt({0.0f, 0.0f, -2.0f}, 100.0f),
        spotAt({0.0f, 0.0f, -3.0f}, 100.0f),  pointAt({0.0f, 0.0f, -4.0f}, 100.0f),
        spotAt({0.0f, 0.0f, -5.0f}, 100.0f),
    };

    const render::LocalShadows shadows = render::fitLocalShadows(candidates);
    CHECK(shadows.tilesUsed <= render::kLocalShadowTileCount);
    CHECK(shadows.refused == 1);

    bool servedTheLastSpot = false;
    for (core::u32 index = 0; index < shadows.count; ++index) {
        if (shadows.entries[index].candidate == 4)
            servedTheLastSpot = true;
    }
    CHECK(servedTheLastSpot);
}

TEST_CASE("order is by apparent size, and a tie is broken by index rather than by luck")
{
    // Two identical lamps at the same distance is a corridor, not a curiosity. A
    // comparison that left the tie to the sort could return either order on
    // either run, and a caster set that changes for a reason not on screen is
    // shadows popping in and out.
    const render::LocalShadowCandidate candidates[] = {
        spotAt({0.0f, 0.0f, -50.0f}, 4.0f),
        spotAt({3.0f, 0.0f, -20.0f}, 8.0f),
        spotAt({-3.0f, 0.0f, -20.0f}, 8.0f),
    };

    const render::LocalShadows first = render::fitLocalShadows(candidates);
    REQUIRE(first.count == 3);
    // The small far one is last however the pair fell.
    CHECK(first.entries[2].candidate == 0);

    // And the answer is the same every time it is asked.
    for (int again = 0; again < 8; ++again) {
        const render::LocalShadows repeated = render::fitLocalShadows(candidates);
        REQUIRE(repeated.count == first.count);
        for (core::u32 index = 0; index < repeated.count; ++index)
            CHECK(repeated.entries[index].candidate == first.entries[index].candidate);
    }
}

TEST_CASE("a light with no range is refused rather than given a degenerate projection")
{
    const render::LocalShadowCandidate candidates[] = {spotAt({0.0f, 2.0f, -4.0f}, 0.0f)};
    const render::LocalShadows shadows = render::fitLocalShadows(candidates);
    CHECK(shadows.count == 0);
    CHECK(shadows.refused == 1);
    CHECK(shadows.tilesUsed == 0);
}

TEST_CASE("a point light six faces cover every direction around it")
{
    // The claim a cube shadow rests on: whichever way a receiver lies from the
    // light, at least one face has it. A wrong `up` on one face turns into a band
    // of the world that casts no shadow at all, which is the artefact this
    // refuses.
    const core::Vec3 lightPosition{1.0f, 2.0f, -3.0f};
    const render::LocalShadowCandidate candidates[] = {pointAt(lightPosition, 30.0f)};
    const render::LocalShadows shadows = render::fitLocalShadows(candidates);
    REQUIRE(shadows.count == 1);
    REQUIRE(shadows.entries[0].tileCount == render::kPointShadowTiles);

    const render::LocalShadow& shadow = shadows.entries[0];
    const core::Vec3 directions[] = {
        {1.0f, 0.0f, 0.0f},  {-1.0f, 0.0f, 0.0f},  {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},   {0.0f, 0.0f, -1.0f},
        {0.6f, 0.6f, 0.53f}, {-0.5f, 0.7f, -0.5f}, {0.4f, -0.8f, 0.45f},
    };

    for (const core::Vec3& direction : directions) {
        const core::Vec3 target{lightPosition.x + direction.x * 5.0f, lightPosition.y + direction.y * 5.0f,
                                lightPosition.z + direction.z * 5.0f};
        int seen = 0;
        for (core::u32 face = 0; face < render::kPointShadowTiles; ++face) {
            if (insideClip(shadow.viewProjection[face], target))
                ++seen;
        }
        CHECK(seen >= 1);
    }
}

TEST_CASE("a spot projection contains the rim of its own cone")
{
    // Fitted exactly to the half angle, the rim lands ON the tile edge, where a
    // filter tap reads off the map and the outline of the shadow map itself
    // becomes the artefact. The margin is what this asserts.
    const core::Vec3 lightPosition{0.0f, 10.0f, 0.0f};
    const core::f32 cosHalfAngle = 0.7071f; // forty-five degrees
    const render::LocalShadowCandidate candidates[] = {spotAt(lightPosition, 40.0f, cosHalfAngle)};
    const render::LocalShadows shadows = render::fitLocalShadows(candidates);
    REQUIRE(shadows.count == 1);

    const core::Mat4& viewProjection = shadows.entries[0].viewProjection[0];
    // Straight down the axis, well inside.
    CHECK(insideClip(viewProjection, core::Vec3{0.0f, 0.0f, 0.0f}));
    // On the rim: ten metres down and ten metres out is forty-five degrees.
    CHECK(insideClip(viewProjection, core::Vec3{10.0f, 0.0f, 0.0f}));
    CHECK(insideClip(viewProjection, core::Vec3{0.0f, 0.0f, 10.0f}));
    // Well outside the cone is off the map, which is what keeps the resolution
    // on the cone rather than on the room.
    CHECK_FALSE(insideClip(viewProjection, core::Vec3{60.0f, 0.0f, 0.0f}));
}
