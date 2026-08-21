// The uniform blocks, in HLSL, byte for byte as
// `engine/render/include/luaug/render/shader_types.h` declares them in C++.
//
// That header names this file, and the rule it states binds both ways: SDL_GPU
// hands a uniform block to a shader as an opaque span, so a disagreement about
// an offset is not a build error, it is a scene lit by garbage. **Change one,
// change the other, in the same commit.**
//
// Register assignment is fixed per stage by SDL_GPU and is not a style choice
// (third_party/sdl3/include/SDL3/SDL_gpu.h:2699-2730, SDL_CreateGPUShader):
//
//   vertex   -- t[n]/s[n] in space0, uniform buffers b[n] in space1
//   fragment -- t[n]/s[n] in space2, uniform buffers b[n] in space3
//
// A shader that ignores it compiles and then binds nothing.
//
// Every block is behind an opt-in macro because the register namespace is per
// stage rather than per block: `GpuObjectUniforms` and `GpuShadowUniforms` both
// want b0 space1, and all three fragment blocks want b0 space3. Declaring the
// whole set unconditionally would collide, and a block a shader never reads
// would still be counted in the reflection sidecar -- which is the number
// `SDL_CreateGPUShader` is given and rejects the shader over.
//
// Matrices are `column_major` explicitly rather than by default: `core::Mat4`
// stores `m[column][row]` (engine/core/src/math.cpp:114-128), so column n of
// the matrix is 16-byte row n of the buffer, and `mul(M, v)` is then the
// column-vector transform `v' = M * v` the engine's math means. Spelling the
// qualifier out makes the shader immune to a compiler flag that flips the
// default.
#ifndef LUAUG_PBR_HLSLI
#define LUAUG_PBR_HLSLI

// Mirrors `render/clusters.h`. Macros rather than `static const` because they
// are array and loop bounds, and because the cluster lookup below indexes
// textures with them.
#define LUAUG_CLUSTER_TILES_X 16
#define LUAUG_CLUSTER_TILES_Y 9
#define LUAUG_CLUSTER_SLICES 24
#define LUAUG_CLUSTER_GRID_WIDTH (LUAUG_CLUSTER_TILES_X * LUAUG_CLUSTER_SLICES)
#define LUAUG_CLUSTER_GRID_HEIGHT LUAUG_CLUSTER_TILES_Y
#define LUAUG_MAX_LIGHTS_PER_CLUSTER 64
#define LUAUG_MAX_CLUSTERED_LIGHTS 256
#define LUAUG_LIGHT_INDEX_WIDTH 128
#define LUAUG_LIGHT_INDEX_HEIGHT 128
// `offset * 256 + count`, which a float carries exactly.
#define LUAUG_CLUSTER_OFFSET_SHIFT 256.0f

// Mirrors `render::GpuLight`, 48 bytes. Four-float rows because a constant
// buffer packs to 16 and the array stride has to come out at 48 on every
// backend.
struct GpuLight
{
    // xyz position in the shading space (camera-relative), w range.
    float4 PositionRange;
    // rgb colour premultiplied by brightness, w unused.
    float4 Color;
    // xyz spot direction, w cosine of the half angle. See
    // `luaug_brdf.hlsli`'s `evaluatePunctualLight` for what w == 1 means and
    // why the shader has to remap it.
    float4 DirectionCosAngle;
};

#if defined(LUAUG_UNIFORMS_OBJECT)
// `render::GpuObjectUniforms`, 208 bytes.
cbuffer GpuObjectUniforms : register(b0, space1)
{
    column_major float4x4 ViewProjection;
    column_major float4x4 Model;
    // The cofactor matrix of Model's rotation-scale block, so a normal survives
    // a non-uniform scale. A float4x4 because a float3x3 in a constant buffer
    // occupies three float4 rows anyway.
    column_major float4x4 NormalMatrix;
    // x is 1 - BasePart.Transparency for this draw. Per draw rather than per
    // material: see the C++ header. This block is vertex-stage only, so a
    // fragment shader that needs it takes it through an interpolant.
    float4 InstanceAlphaUnused;
};
#endif

#if defined(LUAUG_UNIFORMS_FRAME)
// `render::GpuFrameUniforms`, 608 bytes: 208 of float4 rows, 256 of cascade
// matrices, then 144 of irradiance.
cbuffer GpuFrameUniforms : register(b0, space3)
{
    // xyz points from the world TOWARDS the sun, w is its brightness -- with
    // the day factor already in it, so a sun below the horizon lights nothing
    // and this shader needs no test for that.
    float4 SunDirectionBrightness;
    // rgb the sun's own colour, derived from its elevation. Before M7.5 the sun
    // cast white light while the sky's disc was tinted, so a sunset lit a scene
    // exactly like noon did.
    float4 SunColorUnused;
    float4 Ambient;
    float4 FogColor;
    // x start, y end, z one over (end - start), w unused. z is zero when fog is
    // off, which zeroes the fog factor without the shader testing for it.
    float4 FogRange;
    // x is how many lights the frame carries at all; a scene with none skips the
    // cluster lookup entirely.
    float4 LightCountUnused;
    // x how many mip levels the prefiltered environment has, y how strongly it
    // contributes, z and w unused.
    float4 EnvironmentParams;
    // x and y are the exponential depth slicing's scale and bias.
    float4 ClusterParams;
    // x width in pixels, y height, z 1/width, w 1/height.
    float4 ViewportParams;
    // One lane per cascade: where it stops in view-space metres, one of its
    // shadow texels in world metres, and its orthographic depth range in metres.
    float4 CascadeFar;
    float4 CascadeTexelWorld;
    float4 CascadeDepthRange;
    // x filter radius in world metres, y normal offset in shadow texels, z the
    // fraction of a cascade that blends into the next, w depth bias in metres.
    float4 ShadowParams;
    column_major float4x4 CascadeViewProjection[4];
    // Irradiance as nine SH coefficients, cosine-convolved and divided by pi on
    // the CPU, so `evaluateIrradiance` multiplies straight by the albedo.
    float4 IrradianceSh[9];
};
#endif

#if defined(LUAUG_UNIFORMS_MATERIAL)
// `render::GpuMaterialUniforms`, 64 bytes.
cbuffer GpuMaterialUniforms : register(b1, space3)
{
    // rgb base colour factor, w alpha.
    float4 BaseColorFactor;
    // rgb emissive factor, w unused.
    float4 EmissiveFactor;
    // x metallic, y roughness, z normal-map scale, w alpha cutoff.
    float4 MetallicRoughnessNormalCutoff;
    // x base colour, y normal, z metallic-roughness, w emissive: 1 when the
    // material has that texture. Floats because the shader multiplies by them
    // instead of branching -- which means **every slot must always have a
    // texture bound**, a 1x1 default where the material has none, or the
    // sample reads an unbound descriptor.
    float4 TextureFlags;
};
#endif

#if defined(LUAUG_UNIFORMS_SHADOW)
// `render::GpuShadowUniforms`, 128 bytes. Its second matrix is `model` in C++
// and `ShadowModel` here because a cbuffer's members land in HLSL's global
// scope, where it would otherwise collide with `GpuObjectUniforms::Model`. Only
// the offsets have to match, and they do.
cbuffer GpuShadowUniforms : register(b0, space1)
{
    column_major float4x4 LightViewProjection;
    column_major float4x4 ShadowModel;
};
#endif

#if defined(LUAUG_UNIFORMS_SKIN)
// Mirrors `render::kMaxSkinJoints`. A macro rather than a `static const` because
// it is an array bound.
#define LUAUG_MAX_SKIN_JOINTS 64

// `render::GpuSkinUniforms`, 4096 bytes: one model-space matrix per joint, with
// the inverse bind already folded in. The palette is what a vertex multiplies
// by, so it is the only form worth uploading -- sending joint transforms and
// inverse binds separately would send twice the bytes to do the same multiply
// on the GPU.
cbuffer GpuSkinUniforms : register(b1, space1)
{
    column_major float4x4 JointMatrices[LUAUG_MAX_SKIN_JOINTS];
};

// The linear blend, glTF's own: up to four joints per vertex, weighted, with the
// weights already normalised by the importer. A vertex whose weights sum to zero
// -- which a broken export can produce -- keeps its bind position rather than
// collapsing to the origin, and the `identity` fallback is what does that.
float4x4 skinMatrix(uint4 joints, float4 weights)
{
    const float total = weights.x + weights.y + weights.z + weights.w;
    if (total <= 0.0f) {
        return float4x4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                        1.0f);
    }
    return JointMatrices[joints.x] * weights.x + JointMatrices[joints.y] * weights.y +
           JointMatrices[joints.z] * weights.z + JointMatrices[joints.w] * weights.w;
}
#endif

#if defined(LUAUG_UNIFORMS_TONEMAP)
// `render::GpuTonemapUniforms`, 16 bytes -- a whole row for one float, because
// that is the smallest a constant buffer row is.
cbuffer GpuTonemapUniforms : register(b0, space3)
{
    // x exposure, rest unused.
    float4 ExposureUnused;
};
#endif

#if defined(LUAUG_UNIFORMS_SKY)
// `render::GpuSkyUniforms`, 128 bytes.
cbuffer GpuSkyUniforms : register(b0, space3)
{
    column_major float4x4 InverseViewProjection;
    // xyz points from the world towards the sun, w the disc's angular size as a
    // fraction (0.02 is roughly the real sun seen from Earth).
    float4 SunDirectionSize;
    float4 HorizonColor;
    float4 ZenithColor;
    float4 SunColor;
};
#endif

#endif // LUAUG_PBR_HLSLI
