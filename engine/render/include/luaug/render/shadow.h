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

#include <span>

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

// The penumbra, in WORLD metres, and the texel band the kernel will accept for
// it.
//
// **This is the third answer to the same question and the first one that can be
// true, because the kernel changed underneath it.** M7.5 asked for a
// world-constant radius and clamped it to [0.8, 3.0] texels; the clamp was
// forced because a 3x3 grid cannot span more than a few texels without banding,
// and the chain's 23:1 texel ratio pushed both ends into it -- the far cascade
// ended up filtering 0.8 texels, which is nine taps inside one texel and is not
// a filter at all. Sizing the kernel in texels instead fixed that end and made
// the near end worse: with the cascade fitted to its casters a texel is about a
// millimetre and a half, so a few texels of kernel is a few millimetres of
// penumbra, and a shadow edge with no penumbra shows the texel grid it was
// rasterised on. A human saw that as dashes that crawl.
//
// What lets the radius be world-constant now is that the kernel is 5x5 with a
// variable step rather than 3x3 with a fixed one: twenty-five bilinear taps can
// cover eight texels without the gaps that made three bands.
//
// The band is still a band because the two ends are still real, and **both of
// its numbers moved at D054**, which is the third report about the same edge.
//
// The floor is what a FAR cascade gets, because out there the authored
// penumbra is a fraction of a texel and the clamp is the only thing speaking.
// It is what decides whether the one-texel step a rotating light forces is
// visible, and two texels was not enough: a human watching from a distance
// still saw the edge advance in discrete jumps. Six is, and the measurement is
// in `docs/perf-baselines.md` -- on a still scene at forty-five metres, pixels
// changing by more than four levels between consecutive frames fell from 62 to
// 14, and the worst single change from 37 to 14.
//
// What made six affordable is that the kernel is now ROTATED PER PIXEL
// (`luaug_brdf.hlsli`). The old ceiling of four existed because twenty-five
// taps spread wider than that sample the same five rings in every pixel of a
// region, which is banding; turning each pixel's grid by its own angle spends
// the same taps on different rings and the banding becomes fine noise. The
// ceiling is eight now for the same reason.
//
// The NEAR cascade is barely touched by any of this: its texel is a millimetre
// and a half, so `kShadowFilterWorldRadius` divided by it lands inside the band
// on its own and the world-space penumbra is still the five centimetres this
// file authored. Contact was re-measured at three sun elevations on
// `tests/screenshots/contact` after the change, because a wider filter scales
// the normal offset with it and that is exactly what D051 was about.
inline constexpr f32 kShadowFilterWorldRadius = 0.05f;
inline constexpr f32 kShadowFilterMinTexels = 6.0f;
inline constexpr f32 kShadowFilterMaxTexels = 8.0f;

// Normal-offset bias, replacing M4's depth-only one (ADR 0038). The sample is
// displaced along the surface normal, scaled by the sine of the light angle --
// so it grows exactly where a grazing receiver needs it and vanishes where a
// face-on one does not.
//
// **In units of the FILTER's own reach**, because a kernel that reads depths
// four texels away needs an offset that reaches as far as it does, or its widest
// taps land back on the surface that cast them.
//
// **Half a filter rather than the diagonal, and D051 is why.** M7.5 set this to
// `sqrt(2)` -- a square kernel's corner is further away than its edge, and Unity
// scales its normal bias the same way (U-58). What that ignores is which acne
// this configuration can actually suffer: the shadow pass culls FRONT faces, so
// what a receiver compares against is the far side of a solid object and there
// is nothing for a lit surface to shadow itself with. The offset was therefore
// paying for a problem the cull mode already prevents, and its only remaining
// effect was to push every shadow away from the object casting it. A human sent
// a close-up of a capsule and a boulder hovering over their own shadows.
//
// Measured on `tests/screenshots/contact` at three sun elevations: at a quarter
// the contact is perfect and the capsule's own base begins to speckle; at half
// it is attached at every hour with no speckle anywhere; and
// `examples/02-meshes` at a 24-degree sun -- D044's own probe frame -- is clean
// at both.
inline constexpr f32 kShadowNormalOffsetFilters = 0.5f;

// The residual depth bias, in METRES rather than in depth units. Depth units
// mean different things in different cascades, because `kShadowCasterMargin`
// makes each cascade's depth range depend on its radius -- a constant in that
// space is a bias that detaches contact shadows in the near cascade or lets acne
// through in the far one.
//
// **Eight millimetres rather than twenty (D051)**, for the same reason as the
// offset above and measured the same way: with front faces culled in the shadow
// pass there is very little for it to correct, and what it does instead is lift
// every shadow off the thing that casts it. Two centimetres is a visible band of
// lit floor under a character.
inline constexpr f32 kShadowDepthBiasMetres = 0.008f;

// The fraction of a cascade's depth range over which it blends into the next.
// The roadmap names a hard handover as one of the two tells of a first cascaded
// implementation, and it is right: a switch at a plane is visible for the same
// reason a filter that changes width is.
inline constexpr f32 kShadowCascadeBlend = 0.12f;

// One thing that can cast a shadow, as the fit sees it: a bounding sphere in the
// snapshot's camera-relative space. The same two numbers `DrawItem` already
// carries, handed over so the fit can size a cascade to what is really there.
struct ShadowCasterBounds
{
    core::Vec3 centre;
    f32 radius = 0.0f;
};

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
    // The sphere a caster is culled against, in camera-relative space. Returned
    // rather than recovered from the texel size: the box is no longer the
    // slice's own sphere, so the two numbers stopped being the same thing.
    core::Vec3 cullCentre[kShadowCascadeCount];
    f32 cullRadius[kShadowCascadeCount]{};

    // **The fit's own memory, and it is what stops shadow edges crawling
    // (D048).** A box refitted to its contents every frame changes its texel
    // size and its lattice every frame, so every shadow edge in the picture
    // re-quantises -- and a character jittering by micrometres is enough to do
    // it. Fed back into the next fit, which keeps this answer for as long as it
    // still covers what casts.
    //
    // In WORLD space, unlike everything above, because the camera moves: a
    // centre remembered in camera-relative space would mean a different place
    // one frame later, which is the crawl it exists to prevent.
    core::DVec3 boxCentreWorld[kShadowCascadeCount];
    f32 boxExtent[kShadowCascadeCount]{};
    // False until a fit has run. The first frame of a run has nothing to keep.
    bool fitted = false;
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
    // Everything that can cast into this frame. **What this buys is the whole of
    // the fix**: a cascade fitted to the camera's frustum slice is a box sized
    // by what the camera might see, and a directional light near the horizon
    // projects a flat world into a thin band inside it. Measured on a sunrise
    // frame of `examples/02-meshes` by binding the atlas to the resolve pass and
    // looking at it: the entire scene landed in 500 by 90 texels of a 1024 by
    // 1024 tile, so a one-metre object's shadow was drawn with a handful of them
    // and came out as dashes that crawled as the sun moved.
    //
    // Fitting the box to the casters recovers that, and it is exact rather than
    // approximate for the reason that makes shadow maps work at all: the
    // projection IS along the light, so a caster's shadow lands at its own
    // light-space x and y. A receiver outside those bounds cannot be shadowed by
    // anything, and the "outside the cascade is lit" answer the sampler already
    // gives is the right one there.
    //
    // Empty is a legal input and means "fit the slice", which is what every unit
    // test hands it and what a frame with no geometry gets.
    std::span<const ShadowCasterBounds> casters;

    // The two numbers M8 turned from constants into settings (ADR 0044). They
    // default to what the constants above say, so every existing caller and
    // every unit test still describes the shipped configuration by saying
    // nothing.
    f32 distance = kShadowDistance;
    u32 tileResolution = kShadowTileResolution;
};

// `previous` is the last frame's result, or null on the first frame. It is a
// HINT and never a constraint: a cascade keeps its box while the box still
// covers what casts into it, and refits the moment it does not.
[[nodiscard]] ShadowCascades fitShadowCascades(const ShadowFit& fit, const ShadowCascades* previous = nullptr) noexcept;

// The practical split scheme itself, exposed because it is a claim a test can
// check directly: `out[0]` is the near plane, `out[kShadowCascadeCount]` is
// `kShadowDistance`, and the values in between are monotone.
void shadowSplits(f32 nearPlane, f32 farDistance, f32 lambda, f32 (&out)[kShadowCascadeCount + 1]) noexcept;

} // namespace luaug::render
