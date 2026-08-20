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
// The most lights one draw is shaded by. A forward pass pays for every light in
// its block on every fragment, so this is a budget rather than a limit of the
// design: the clustered pass ADR 0027 describes is what removes it, and that is
// a later milestone. `extract` sorts nothing by light yet, so the first eight in
// dense order are what a draw gets -- documented because "the lights past eight
// silently stopped mattering" is otherwise a very confusing afternoon.
inline constexpr u32 kMaxForwardLights = 8;

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
    // xyz points from the world TOWARDS the sun, w is its brightness.
    f32 sunDirectionBrightness[4]{0.0f, 1.0f, 0.0f, 2.0f};
    f32 ambient[4]{0.15f, 0.16f, 0.2f, 0.0f};
    f32 fogColor[4]{0.6f, 0.7f, 0.85f, 0.0f};
    // x start, y end, z one over (end - start) precomputed because a fragment
    // shader would otherwise divide per pixel, w unused. When fog is off
    // (`end <= start`) z is zero, which makes the fog factor zero without the
    // shader needing to know why.
    f32 fogRange[4]{200.0f, 0.0f, 0.0f, 0.0f};
    // x is how many entries of `lights` are live. The shader loops to it rather
    // than to kMaxForwardLights, so an unlit scene costs nothing.
    f32 lightCountUnused[4]{0.0f, 0.0f, 0.0f, 0.0f};
    core::Mat4 sunViewProjection;
    GpuLight lights[kMaxForwardLights];
};

static_assert(sizeof(GpuFrameUniforms) == 80 + 64 + 48 * kMaxForwardLights, "GpuFrameUniforms is a cbuffer layout");

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

// Vertex stage, `b0 space1`, for the shadow pass. Depth only, so there is
// nothing else it needs.
struct GpuShadowUniforms
{
    core::Mat4 lightViewProjection;
    core::Mat4 model;
};

static_assert(sizeof(GpuShadowUniforms) == 128, "GpuShadowUniforms is a cbuffer layout");

// Fragment stage, `b0 space3`, for the tonemap pass.
struct GpuTonemapUniforms
{
    // x exposure, rest unused. A whole 16-byte row for one float because that is
    // the smallest a constant buffer row is.
    f32 exposureUnused[4]{1.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(GpuTonemapUniforms) == 16, "GpuTonemapUniforms is a cbuffer layout");

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
