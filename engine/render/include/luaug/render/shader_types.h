// The uniform layouts the renderer and its shaders both agree on (ADR 0006).
//
// This file and `shaders/include/luaug_pbr.hlsli` describe the same bytes twice,
// in two languages, and nothing in the toolchain checks that they still match:
// SDL_GPU takes a uniform block as an opaque span and a shader that disagrees
// about the layout reads whatever happens to be there. The failure is not a
// build error, it is a scene lit by garbage.
//
// So the rule is: **change one, change the other, in the same commit**, and the
// `static_assert`s below are what turn a size mismatch into a compile error on
// this side at least.
//
// **SDL_GPU fixes the register spaces per stage** and a shader that ignores the
// contract binds nothing at all (SDL_gpu.h:2702-2730, read at the pinned
// version):
//
//   vertex   -- textures and samplers in `space0`, uniform buffers `b[n] space1`
//   fragment -- textures and samplers in `space2`, uniform buffers `b[n] space3`
//
// And vertex inputs use `TEXCOORDn` semantics, because that is what
// SDL_shadercross maps to SPIR-V input locations; `POSITION`/`NORMAL` compile
// and then bind nothing.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

namespace luaug::render {

using core::f32;
using core::u32;

// **The shading space is camera-relative, and nothing in these blocks says so
// except this paragraph.** There is no camera position anywhere below, because
// the eye is at the origin: `extract` subtracts the camera's world position from
// every transform and every light before the snapshot is built (ADR 0014, the
// M4 brief's Decision 8). Two consequences a shader author cannot see from the
// struct definitions, and which silently produce a wrong image rather than an
// error if they are broken: `GpuObjectUniforms::model` must carry a
// camera-relative translation, and `sunViewProjection` must be built in the same
// space.
//
// The clustered pass replaced `kMaxForwardLights` at M7.5 (clusters.h). What is
// left of the old shape is `GpuLight` itself, which the shader now rebuilds from
// three texels of a light table instead of reading out of this block -- the
// frozen RHI has no storage buffer, so a fragment shader's only route to bulk
// data is a sampled texture.
//
// A light as the fragment shader reads it. Four-float rows because a constant
// buffer packs to 16 bytes and a struct that ignores that is a struct whose
// second element is at the wrong offset on some backend.
struct GpuLight
{
    // xyz position (camera-relative), w range.
    f32 positionRange[4]{0.0f, 0.0f, 0.0f, 0.0f};
    // rgb colour premultiplied by brightness, w unused.
    f32 color[4]{0.0f, 0.0f, 0.0f, 0.0f};
    // xyz spot direction, w cosine of the half angle. **-1 for a point light**,
    // which is the value that admits every direction and so costs no branch; 1
    // would be the narrowest cone expressible, which is the opposite.
    f32 directionCosAngle[4]{0.0f, -1.0f, 0.0f, -1.0f};
};

static_assert(sizeof(GpuLight) == 48, "GpuLight is a cbuffer layout; see luaug_pbr.hlsli");

// Vertex stage, `b0 space1`.
struct GpuObjectUniforms
{
    core::Mat4 viewProjection;
    core::Mat4 model;
    // The cofactor matrix of `model`'s rotation-scale block, so normals survive
    // a non-uniform scale. Stored as a Mat4 rather than a Mat3 because a
    // float3x3 in a constant buffer is three float4 rows anyway, and spelling
    // that out is how the two sides stop disagreeing about the padding.
    core::Mat4 normalMatrix;
    // x is `1 - BasePart.Transparency` for this draw, and it is HERE rather than
    // in `GpuMaterialUniforms` for a reason the material block's own comment
    // gives: materials are deduplicated per frame precisely so the sort key can
    // group draws that share a bind set, and a per-instance alpha written there
    // would split one material into as many as there are distinct values.
    //
    // This block is per draw and already bound per draw, so carrying it costs
    // one more 16-byte row and no RHI call -- which is what keeps it possible
    // after ADR 0037 froze the interface.
    //
    // The fragment stage cannot read this block (SDL_GPU gives the vertex stage
    // space1 and the fragment stage space3), so the vertex shader passes it down
    // as an interpolant. That is not an optimization, it is the only route.
    f32 instanceAlphaUnused[4]{1.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(GpuObjectUniforms) == 208, "GpuObjectUniforms is a cbuffer layout");

// Fragment stage, `b0 space3`.
struct GpuFrameUniforms
{
    // xyz points from the world TOWARDS the sun, w is its brightness. The
    // brightness already carries the day factor (`SkyParams::dayFactor`), so a
    // sun below the horizon lights nothing and the shader needs no test for it.
    f32 sunDirectionBrightness[4]{0.0f, 1.0f, 0.0f, 2.0f};
    // rgb the sun's OWN colour, w unused. Derived from its elevation rather
    // than authored (environment.h), and new at M7.5: until then the sun cast
    // white light while the sky's disc was tinted, so a sunset lit a scene
    // exactly like noon did.
    f32 sunColorUnused[4]{1.0f, 0.96f, 0.9f, 0.0f};
    f32 ambient[4]{0.15f, 0.16f, 0.2f, 0.0f};
    f32 fogColor[4]{0.6f, 0.7f, 0.85f, 0.0f};
    // x start, y end, z one over (end - start) precomputed because a fragment
    // shader would otherwise divide per pixel, w unused. When fog is off
    // (`end <= start`) z is zero, which makes the fog factor zero without the
    // shader needing to know why.
    f32 fogRange[4]{200.0f, 0.0f, 0.0f, 0.0f};
    // x is how many lights the frame carries at all. The shader does not loop to
    // it -- it loops to its own cluster's count -- but a scene with none skips
    // the cluster lookup entirely.
    f32 lightCountUnused[4]{0.0f, 0.0f, 0.0f, 0.0f};
    // x how many mip levels the prefiltered environment has, y how strongly it
    // contributes, z how strongly ambient occlusion darkens it, w unused. `y`
    // and `z` are multipliers rather than switches so that a scene can dial
    // either down without the shader gaining a branch nothing else needs.
    f32 environmentParams[4]{6.0f, 1.0f, 1.0f, 0.0f};
    // x and y are the exponential depth slicing's scale and bias, so a fragment
    // turns its own view depth into a cluster slice with one multiply-add and a
    // logarithm (clusters.h). z and w unused.
    f32 clusterParams[4]{};
    // x width in pixels, y height, z 1/width, w 1/height. The fragment stage
    // needs it to turn `SV_Position` into a cluster tile, and it is the first
    // time this block has carried anything about the target's size.
    f32 viewportParams[4]{};
    // Per cascade, one lane each: where the cascade stops in view-space metres,
    // one of its shadow texels in world metres, and its orthographic depth range
    // in metres. The last two are what let a filter radius and a depth bias be
    // stated in metres and mean the same thing in every cascade.
    f32 cascadeFar[4]{};
    f32 cascadeTexelWorld[4]{};
    f32 cascadeDepthRange[4]{};
    // x the filter radius in world metres, y the normal offset in shadow texels,
    // z the fraction of a cascade that blends into the next, w the residual
    // depth bias in world metres.
    f32 shadowParams[4]{};
    core::Mat4 cascadeViewProjection[4];
    // Irradiance as nine spherical-harmonic coefficients, cosine-convolved and
    // already divided by pi, so the shader multiplies the evaluation straight
    // by the diffuse albedo (environment.h). This is what replaces a flat
    // ambient on the diffuse lobe; `ambient` above is ADDED to it rather than
    // replaced by it, so `Lighting.Ambient` still means what it documents.
    f32 irradianceSh[9][4]{};
};

static_assert(sizeof(GpuFrameUniforms) == 208 + 256 + 144, "GpuFrameUniforms is a cbuffer layout");

// Fragment stage, `b1 space3`. Per material rather than per frame, because it
// changes with the bind set and the sort key already groups draws by material.
struct GpuMaterialUniforms
{
    // rgb base colour factor, w alpha.
    f32 baseColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
    // rgb emissive factor, w unused.
    f32 emissive[4]{0.0f, 0.0f, 0.0f, 0.0f};
    // x metallic, y roughness, z normal-map scale, w alpha cutoff.
    f32 metallicRoughnessNormalCutoff[4]{1.0f, 1.0f, 1.0f, 0.5f};
    // x is 1 when the material has a base-colour texture, y normal, z
    // metallic-roughness, w emissive. Floats rather than a bitmask because a
    // shader multiplies by them and a branch per texture per fragment is worse
    // than a multiply by one.
    //
    // **Because they are multipliers rather than branches, the sample happens
    // either way** -- so every one of the five fragment texture slots must have
    // something bound, and a material with no base-colour map gets a 1x1
    // default rather than an invalid handle. An unbound descriptor read is not
    // a black pixel, it is whatever the backend last left there.
    f32 textureFlags[4]{0.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(GpuMaterialUniforms) == 64, "GpuMaterialUniforms is a cbuffer layout");

// One entry of the per-INSTANCE vertex stream (ADR 0043), at slot 1.
//
// The model matrix as four columns and the instance's own alpha, and NOT the
// cofactor normal matrix: that would be four more `float4` attributes, and a
// vertex layout is capped at sixteen on the weakest conforming device. The
// instanced shaders derive it with three cross products per vertex instead,
// which is the cheaper half of the trade.
struct GpuInstance
{
    core::Mat4 model;
    // x is `1 - BasePart.Transparency`, for the same reason
    // `GpuObjectUniforms` carries it: a per-instance alpha written into the
    // material block would split one material into as many as there are values.
    f32 alphaUnused[4]{1.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(GpuInstance) == 80, "GpuInstance is a vertex stride; see pbr_instanced.hlsl");

// Vertex stage, `b0 space1`, for the shadow pass. Depth only, so there is
// nothing else it needs.
struct GpuShadowUniforms
{
    core::Mat4 lightViewProjection;
    core::Mat4 model;
};

static_assert(sizeof(GpuShadowUniforms) == 128, "GpuShadowUniforms is a cbuffer layout");

// The most joints one skinned draw can be posed by. A budget rather than a limit
// of the design, like `kMaxForwardLights`: the palette is a constant buffer and
// a constant buffer has a size, so a rig with more joints than this draws in
// bind pose for the ones past it. Sixty-four is generous for a humanoid -- a
// game character is typically thirty to fifty -- and 4 KB is a comfortable push
// on every backend. Raising it is one number here and one in `luaug_pbr.hlsli`.
inline constexpr u32 kMaxSkinJoints = 64;

// Vertex stage, `b1 space1`, for both skinned passes. `joint * inverseBind`
// already combined, because that product is what a vertex multiplies by and
// sending the two halves separately would send twice the bytes to do the same
// multiply on the GPU.
struct GpuSkinUniforms
{
    core::Mat4 jointMatrices[kMaxSkinJoints];
};

static_assert(sizeof(GpuSkinUniforms) == 64 * 64, "GpuSkinUniforms is a cbuffer layout; see luaug_pbr.hlsli");

// Fragment stage, `b0 space3`, for the tonemap pass.
struct GpuTonemapUniforms
{
    // x exposure compensation in EV stops -- the artist control, on top of the
    // automatic exposure the 1x1 target carries. y how much bloom is mixed in.
    // z and w unused.
    f32 exposureBloom[4]{0.0f, 0.04f, 0.0f, 0.0f};
};

static_assert(sizeof(GpuTonemapUniforms) == 16, "GpuTonemapUniforms is a cbuffer layout");

// Fragment stage, `b0 space3`, for the screen-space ambient occlusion pass.
//
// It reads the depth the PREPASS wrote and nothing else: a forward renderer that
// grew a normal target would have grown half a deferred one, so the normal is
// reconstructed from depth derivatives (M7.5 brief, Decision 13).
struct GpuSsaoUniforms
{
    // x tan of half the horizontal field of view, y vertical, z near plane,
    // w far plane -- everything needed to turn a depth sample back into a
    // view-space position.
    f32 projection[4]{1.0f, 0.5f, 0.1f, 400.0f};
    // x width in pixels, y height, z 1/width, w 1/height, of the AO target.
    f32 viewport[4]{};
    // x the sampling radius in world metres, y the depth bias that keeps a
    // surface from occluding itself, z the strength, w unused.
    f32 params[4]{0.5f, 0.02f, 1.0f, 0.0f};
};

static_assert(sizeof(GpuSsaoUniforms) == 48, "GpuSsaoUniforms is a cbuffer layout");

// Fragment stage, `b0 space3`, for the depth-aware blur that follows it.
struct GpuBlurUniforms
{
    // x and y are one texel of the source, z and w the blur direction in texels
    // -- so one pipeline serves both the horizontal and the vertical pass.
    f32 texelDirection[4]{};
};

static_assert(sizeof(GpuBlurUniforms) == 16, "GpuBlurUniforms is a cbuffer layout");

// Fragment stage, `b0 space3`, for the editor's selection outline.
struct GpuOutlineUniforms
{
    // x and y are one mask texel, z the outline's half-width in texels, w
    // unused. The width scales the tap step rather than the tap count, so a
    // thicker line costs nothing more.
    f32 texelWidth[4]{};
    // The line. Alpha is how opaque it is over the frame.
    f32 color[4]{};
    // The tint over the selected shape, and its alpha is how much of it there
    // is. Small on purpose -- see the shader.
    f32 fillColor[4]{};
};

static_assert(sizeof(GpuOutlineUniforms) == 48, "GpuOutlineUniforms is a cbuffer layout");

// Fragment stage, `b0 space3`, shared by the bloom chain's two pipelines.
struct GpuBloomUniforms
{
    // x and y are one texel of the SOURCE, z the filter radius in source texels,
    // w unused.
    f32 texelRadius[4]{};
    // x the threshold in scene-referred luminance, y the soft knee's width, and
    // both are zero on every pass but the first: the threshold is applied once,
    // on the way into the chain, or a surface pops as it crosses it at every
    // level. z and w unused.
    f32 threshold[4]{};
};

static_assert(sizeof(GpuBloomUniforms) == 32, "GpuBloomUniforms is a cbuffer layout");

// Fragment stage, `b0 space3`, for the three passes that measure the frame's
// brightness and adapt to it.
struct GpuLuminanceUniforms
{
    // x and y are one texel of the source, z how far towards the measured value
    // one frame moves, w unused.
    //
    // **`z` is per FRAME rather than per second**, and that is deliberate: a
    // rate driven by elapsed wall-clock time would make a screenshot at frame
    // thirty a different picture on a fast machine and a slow one, and the
    // goldens are the reason that matters (R10 in spirit).
    f32 texelRate[4]{};
    // x the lowest and y the highest average luminance the automatic exposure
    // will accept.
    //
    // **The lower bound is what keeps midnight from looking like noon**, and it
    // is the whole reason this is a clamp rather than a guard. A meter with no
    // floor exposes a night scene up until it matches a day one, which removes
    // the day/night cycle the automatic exposure was added to serve -- the exact
    // failure the day strip showed on its first run. Against a key of 0.45 this
    // allows a gain between 0.15 and 3, so a night scene lands about a stop and
    // a half below a day one instead of level with it.
    f32 range[4]{0.15f, 3.0f, 0.0f, 0.0f};
};

static_assert(sizeof(GpuLuminanceUniforms) == 32, "GpuLuminanceUniforms is a cbuffer layout");

// Fragment stage, `b0 space3`, for the anti-aliasing resolve.
struct GpuFxaaUniforms
{
    // x and y are one texel of the source; z and w unused.
    f32 texel[4]{};
};

static_assert(sizeof(GpuFxaaUniforms) == 16, "GpuFxaaUniforms is a cbuffer layout");

// Fragment stage, `b0 space3`, for the sky pass. The sky is drawn as a
// fullscreen triangle before any geometry, so it needs the inverse view
// projection to turn a screen position back into a direction.
struct GpuSkyUniforms
{
    core::Mat4 inverseViewProjection;
    f32 sunDirectionSize[4]{0.0f, 1.0f, 0.0f, 0.02f};
    f32 horizonColor[4]{0.6f, 0.7f, 0.85f, 0.0f};
    f32 zenithColor[4]{0.15f, 0.3f, 0.7f, 0.0f};
    f32 sunColor[4]{1.0f, 0.95f, 0.85f, 0.0f};
};

static_assert(sizeof(GpuSkyUniforms) == 64 + 64, "GpuSkyUniforms is a cbuffer layout");

} // namespace luaug::render
