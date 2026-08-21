// The environment every surface is lit against: the sky as a function, and the
// two tables the split-sum approximation reads it through (ADR 0038, M7.5).
//
// **The environment is the sky, and that is the decision this file exists to
// make.** A prefiltered environment normally comes from an artist-placed probe,
// and v1 has no editor to place one with (ADR 0017). But the engine already
// draws a sky analytically from `Lighting`'s own state, so the same function
// evaluated on the CPU produces an environment that changes when `ClockTime`
// does -- a metal reflects the sky at the hour the script set, with no asset
// behind it and nothing to place.
//
// `evaluateSky` and `shaders/src/sky.hlsl` are therefore the same function
// written twice, in two languages, exactly like `shader_types.h` and
// `luaug_pbr.hlsli` are the same bytes written twice. **Change one, change the
// other, in the same commit**: a reflection that disagrees with the sky it
// reflects is worse than no reflection at all, and `luaug_render_tests` asserts
// the pair at a handful of directions rather than trusting the comment.
//
// Three things are baked here rather than on the GPU, and all three for the same
// reason: the frozen RHI (ADR 0037) has no cube texture, no mip-chain
// generation and no compute. It has `uploadTexture(texture, data, mipLevel)`,
// which takes a mip level -- so the prefilter runs on the CPU, into an
// OCTAHEDRAL 2D texture whose levels are uploaded one at a time. ADR 0043
// records what that costs and why it beat widening the interface.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <span>

namespace luaug::render {

using core::Color3;
using core::f32;
using core::u16;
using core::u32;
using core::usize;
using core::Vec3;

// The prefiltered environment's base level, in texels per side. 128 is chosen
// against the content rather than against a habit: the sky is a smooth gradient
// plus a sun disc, and the disc is the only high-frequency thing in it -- at
// 128 the mirror level still resolves it as a disc rather than as a blob.
inline constexpr u32 kEnvironmentBaseSize = 128;

// Levels of the roughness chain, level 0 being a mirror and the last being
// fully rough. Six covers [0, 1] in steps of 0.2, which is finer than the
// roughness a mip index can be interpolated to anyway.
inline constexpr u32 kEnvironmentMipCount = 6;

// The split-sum BRDF table, indexed by (N·V, roughness). 64 is the size Karis
// published and it is enough: both axes are smooth and monotone, and the
// integral's error at 64 is below what an `Rgba16Float` can represent.
inline constexpr u32 kBrdfLutSize = 64;

// The sky, resolved. Held apart from `RenderEnvironment` because two of these
// four are DERIVED from the sun's elevation rather than authored -- the sky
// pass and the environment bake must agree on the derivation, and the only way
// to guarantee that is for both to read one struct filled by one function.
struct SkyParams
{
    // Points from the world towards the sun.
    Vec3 sunDirection{0.0f, 1.0f, 0.0f};
    // The band at the horizon. `Lighting.FogColor`, scaled by the day factor.
    Color3 horizonColor{0.6f, 0.7f, 0.85f};
    // Straight up. Derived: deep blue by day, near black at night.
    Color3 zenithColor{0.15f, 0.3f, 0.7f};
    // The disc, and now also the colour of the light the sun casts. Derived:
    // warm within a few degrees of the horizon, white above.
    Color3 sunColor{1.0f, 0.95f, 0.85f};
    // The disc's angular radius in radians. 0.0047 is the real sun's; 0.02 is
    // what M4 shipped, and it stays, because a disc four times too large is what
    // makes a sunset readable in a 90-second day.
    f32 sunAngularRadius = 0.02f;
    // Zero at night, one in full day. Multiplies the sun's brightness so a
    // scene at midnight is lit by its ambient and its lamps rather than by a sun
    // below the horizon.
    f32 dayFactor = 1.0f;
    // The disc's two edges, precomputed. Derived from `sunAngularRadius` and
    // held here because a full prefilter calls `evaluateSky` a quarter of a
    // million times and two cosines of a constant is two cosines too many.
    f32 discCosOuter = 0.9998f;
    f32 discCosInner = 0.99984f;
};

// The derivation, and the one place it happens.
//
// `fogColor` is `Lighting.FogColor` and keeps its documented meaning -- it is
// what distance fades towards, and the sky borrows it for the horizon band, as
// M4's sky pass already did. What is added here is elevation: everything in the
// sky scales by a day factor, the zenith darkens, and the sun reddens as it
// approaches the horizon. None of that touches a script-authored value.
[[nodiscard]] SkyParams skyParamsFor(Vec3 sunDirection, Color3 fogColor) noexcept;

// Linear HDR radiance arriving from `direction`, which need not be normalised.
// The CPU half of `shaders/src/sky.hlsl`; see this file's header.
[[nodiscard]] Vec3 evaluateSky(const SkyParams& params, Vec3 direction) noexcept;

// Octahedral mapping, y-up: the unit sphere unfolded into the unit square, so a
// single 2D texture with a real mip chain stands in for the cubemap the frozen
// RHI has no type for. `u` and `v` are in [0, 1] and address texel CENTRES.
//
// The seam is the octahedron's outer edge, where texels that are neighbours in
// direction are opposite in uv. `bakeEnvironmentLevel` averages each border
// texel with its seam partner, which is what makes a bilinear tap across the
// edge blend two texels that mean nearly the same direction.
[[nodiscard]] Vec3 octahedralDirection(f32 u, f32 v) noexcept;
void octahedralUv(Vec3 direction, f32& u, f32& v) noexcept;

// How much brighter the sun's disc is than the sky around it. Not decoration:
// it is what puts a highlight in a mirror, and a disc at radiance 1 reflects as
// a pale smudge -- which is the M4 look this milestone exists to replace.
inline constexpr f32 kSunDiscIntensity = 24.0f;

// IEEE 754 binary16, because `Rgba16Float` is the only HDR format in the frozen
// set whose linear filtering every backend guarantees -- `Rgba32Float` filtering
// is an optional Vulkan feature and a prefiltered environment that cannot be
// filtered is not prefiltered.
[[nodiscard]] u16 floatToHalf(f32 value) noexcept;
[[nodiscard]] f32 halfToFloat(u16 value) noexcept;

// One level of the roughness chain. `out` holds `size * size * 4` halves.
//
// `roughness` 0 takes one sample per texel -- a mirror IS the sky -- and every
// other level importance-samples GGX around the texel's own direction under
// Karis's assumption that the view direction equals the normal and the normal
// equals the reflection. That assumption is what makes the integral independent
// of the view and therefore precomputable at all; its known cost is that
// reflections do not stretch with view angle.
void bakeEnvironmentLevel(const SkyParams& params, u32 size, f32 roughness, u32 sampleCount, std::span<u16> out);

// How many GGX samples a level is worth. Cheap at the mirror end because there
// is nothing to integrate, and bounded at the rough end because the levels there
// are tiny.
[[nodiscard]] u32 environmentSampleCount(u32 level) noexcept;

// The diffuse half of the split sum: nine spherical-harmonic coefficients with
// the cosine lobe ALREADY convolved in, so a shader evaluates irradiance with
// nine multiply-adds and no loop over an environment it would otherwise have to
// sample hundreds of times.
//
// Nine rather than sixteen because irradiance is band-limited by the convolution
// itself -- Ramamoorthi and Hanrahan's result is that order two captures over
// 99% of it, and the tenth coefficient is below the noise of the sky model this
// projects.
void bakeIrradianceSh(const SkyParams& params, Vec3 (&out)[9]) noexcept;

// The specular half's second table: the environment BRDF, indexed by N·V on u
// and roughness on v, supplying the Fresnel scale in R and the bias in G.
// Independent of the environment, so it is baked once at startup and never
// again.
void bakeBrdfLut(u32 size, std::span<u16> out);

// How different two skies have to be before the chain is worth rebuilding.
//
// A day/night cycle changes the sky every frame, and a full rebuild every frame
// would put the prefilter in the frame budget -- which, measured rather than
// assumed, is exactly where it was: two and a half milliseconds a frame in
// `examples/02-meshes`, whose ninety-second day moves the sun six degrees a
// second.
//
// About one degree. At half a degree the chain never got ahead of the sun: a
// rebuild every fifth frame marks all six levels dirty and the cursor bakes one
// per frame, so it baked one every frame forever. At one degree it rebuilds
// every tenth, which is six bakes per ten frames instead of ten.
//
// What a degree of staleness looks like is a mirror reflection whose sun steps
// rather than slides, once every tenth of a second, at a size the disc is four
// times too large to make subtle. Nothing rougher than a mirror can show it.
inline constexpr f32 kEnvironmentRebuildCosine = 0.99985f;

} // namespace luaug::render
