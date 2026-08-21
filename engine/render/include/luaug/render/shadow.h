// The sun's shadow: four cascades, where they are cut, and the matrices that
// fit each one into a quarter of one atlas.
//
// Split out of `renderer_default.cpp` when M4.5 made the matrix answer a
// question a test has to be able to ask: *does a fixed world point stay on the
// same shadow texel when the camera moves a fraction of one?* It did not, and
// the crawling edges a human reported were the consequence. That question is
// still what this file is for; there are now four of it.
//
// **Four cascades in ONE texture, as a 2x2 atlas.** `TextureDesc` has a `layers`
// field and neither attachment struct has any way to name a layer, so a
// four-layer array can be created and cannot be rendered into. An atlas needs
// nothing the frozen RHI does not have (ADR 0037): one target, four viewports,
// one bind, one `GetDimensions`. Its whole tax is that a filter tap near a tile
// edge would read the neighbouring cascade, so every sample is clamped to its
// own tile -- four lines in `luaug_brdf.hlsli`.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

namespace luaug::render {

using core::f32;
using core::u32;

// Four, which is where the reference implementations settle (ADR 0038).
inline constexpr u32 kShadowCascadeCount = 4;

// One cascade's tile. **1024 rather than the 2048 M4 shipped for its single
// cascade**, and that is not a downgrade: four tiles at 1024 make a 2048 atlas
// at 16 MiB, where four at 2048 would be a 4096 atlas at 64 MiB -- and 4096 is
// exactly the 2D texture size the weakest conforming device is required to
// support, which is not a floor to build a default on (R16). The near cascade
// still lands at about 1.2 cm per texel against M4's 5.9, and shadow resolution
// becomes an engine setting at M8 (ADR 0038 §3).
inline constexpr u32 kShadowTileResolution = 1024;
inline constexpr u32 kShadowAtlasResolution = kShadowTileResolution * 2;

// How far from the camera the sun casts. Shorter than any example's far plane on
// purpose: a cascade set stretched to a 400-metre far plane spends its whole
// resolution on geometry nobody is looking at.
inline constexpr f32 kShadowDistance = 120.0f;

// GPU Gems 3 chapter 10's practical split scheme: 0 is uniform partitioning, 1
// is logarithmic, and the useful values are in between (ADR 0038).
//
// 0.85 leans logarithmic, which is where the near cascade's quality lives: at
// 0.6 the first cascade reached twelve metres and its texel was 2.8 cm, barely
// better than the 5.9 M4 shipped everywhere. At 0.85 it reaches five metres at
// 1.1 cm. The bill is the ratio between the ends of the chain -- about
// twenty-three to one -- which is what the filter radius has to be clamped
// against.
inline constexpr f32 kShadowSplitLambda = 0.85f;

// How far above a cascade's sphere a caster can be and still reach into it. A
// constant because the snapshot carries no world bounds; it buys correctness at
// the cost of depth range, and the depth bias below is expressed in metres
// precisely so that cost does not turn into peter-panning.
inline constexpr f32 kShadowCasterMargin = 25.0f;

// What `extract` culls against: geometry further than this from the camera can
// neither be seen nor cast into view.
//
// Larger than `kShadowDistance` and not by a little, which is a consequence of
// the sphere fit rather than a safety margin: the far cascade's slice is wide,
// so the sphere that contains it has a radius of about 134 metres centred 75
// metres ahead, and its casters sit above that again. Cheap to be generous
// here -- it is one distance test per draw at extraction, and each cascade then
// rejects what it does not cover.
inline constexpr f32 kShadowRadius = 220.0f;

// The filter's radius in WORLD metres, which is the roadmap's requirement rather
// than a preference: each cascade has its own world-units-per-texel, so a kernel
// fixed in texels makes shadow softness change as an object crosses a split, and
// a viewer reads that as a seam.
//
// It is CLAMPED to a texel range per cascade, and that clamp is the honest part
// of this: a 3x3 kernel spread over twenty texels is not a filter, it is four
// samples of a large region, and it bands. With the splits above the clamp binds
// only at the two ends of the chain and the residual difference in softness is
// what the blend band covers.
inline constexpr f32 kShadowFilterWorldRadius = 0.05f;
inline constexpr f32 kShadowFilterMinTexels = 0.8f;
inline constexpr f32 kShadowFilterMaxTexels = 3.0f;

// Normal-offset bias, replacing M4's depth-only one (ADR 0038). The sample is
// displaced along the surface normal by this many shadow texels, scaled by the
// sine of the light angle -- so it grows exactly where a grazing receiver needs
// it and vanishes where a face-on one does not.
inline constexpr f32 kShadowNormalOffsetTexels = 2.0f;

// The residual depth bias, in METRES rather than in depth units. Depth units
// mean different things in different cascades, because `kShadowCasterMargin`
// makes each cascade's depth range depend on its radius -- a constant in that
// space is a bias that detaches contact shadows in the near cascade or lets acne
// through in the far one.
inline constexpr f32 kShadowDepthBiasMetres = 0.02f;

// The fraction of a cascade's depth range over which it blends into the next.
// The roadmap names a hard handover as one of the two tells of a first cascaded
// implementation, and it is right: a switch at a plane is visible for the same
// reason a filter that changes width is.
inline constexpr f32 kShadowCascadeBlend = 0.12f;

// What the fit produces, and what the frame uniforms carry.
struct ShadowCascades
{
    // In the snapshot's camera-relative space, like everything else the GPU
    // sees.
    core::Mat4 viewProjection[kShadowCascadeCount];
    // The view-space distance at which each cascade stops. A fragment picks the
    // first cascade whose value exceeds its own depth.
    f32 farDistance[kShadowCascadeCount]{};
    // One texel of this cascade, in world metres. The filter radius and the
    // normal offset are both expressed against it.
    f32 texelWorld[kShadowCascadeCount]{};
    // The cascade's ortho depth range in metres, so a bias in metres can be
    // turned into one in depth units.
    f32 depthRange[kShadowCascadeCount]{};
};

// Everything the fit needs, and nothing about a renderer.
//
// The camera basis is in camera-relative space, where the camera sits at the
// origin -- so there is no eye position here and there does not need to be.
// `origin` is the f64 world position that space is measured from, and it is the
// one field whose absence makes the texel grid crawl: a world point's light-space
// coordinate is `L * (world - origin)`, so without rounding against the world
// position every point slides continuously across the map as the camera moves,
// and a binary depth comparison then flips it between lit and shadowed from
// frame to frame.
struct ShadowFit
{
    core::Vec3 sunDirection{0.0f, 1.0f, 0.0f};
    core::Vec3 forward{0.0f, 0.0f, -1.0f};
    core::Vec3 right{1.0f, 0.0f, 0.0f};
    core::Vec3 up{0.0f, 1.0f, 0.0f};
    // `1 / projection.m[0][0]` and `1 / projection.m[1][1]`, which is what
    // `core::perspective` puts there.
    f32 tanHalfFovX = 0.5f;
    f32 tanHalfFovY = 0.3f;
    f32 nearPlane = 0.1f;
    core::DVec3 origin;
};

[[nodiscard]] ShadowCascades fitShadowCascades(const ShadowFit& fit) noexcept;

// The practical split scheme itself, exposed because it is a claim a test can
// check directly: `out[0]` is the near plane, `out[kShadowCascadeCount]` is
// `kShadowDistance`, and the values in between are monotone.
void shadowSplits(f32 nearPlane, f32 farDistance, f32 lambda, f32 (&out)[kShadowCascadeCount + 1]) noexcept;

} // namespace luaug::render
