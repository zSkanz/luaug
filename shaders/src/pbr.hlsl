// The forward pass for a STATIC mesh: metallic-roughness PBR, one shadowed
// directional sun, up to `kMaxForwardLights` punctual lights, image-based
// lighting, then distance fog. It is the default shader every material names
// unless it names another (`MaterialDef::shader`).
//
// **One shader, two pipelines.** The opaque and the blended passes compile the
// same code and differ only in their pipeline state -- depth-write and blending
// -- because a fragment's colour does not depend on which pass drew it. What
// differs is the order, and `extract` owns that (M4's third design constraint).
//
// **The fragment stage lives in `luaug_forward.hlsli`** and is shared with
// `pbr_skinned.hlsl`. It was a copy in both files until M7.5 gave it enough to
// do that two copies became two shaders waiting to disagree; that header says
// so at length. What is left here is the one thing that genuinely differs
// between the two: the vertex input layout and the vertex stage.
//
// It writes **linear HDR** into an `Rgba16Float` target. Nothing here tonemaps
// and nothing here encodes sRGB -- `tonemap.hlsl` does both, once, on the way to
// the swapchain.
//
// Register spaces are fixed per stage by SDL_GPU and a shader that ignores them
// binds nothing (SDL_gpu.h:2699-2730). Vertex inputs use TEXCOORDn semantics
// because that is what SDL_shadercross maps to SPIR-V input locations, and the
// declaration order below is the location order.
//
// The vertex layout is `asset::Vertex` (engine/asset/include/luaug/asset/
// model.h): 48 bytes interleaved, position float3 / normal float3 / tangent
// float4 / uv float2, with `tangent.w` the bitangent handedness sign that glTF
// defines.

#define LUAUG_UNIFORMS_OBJECT
#define LUAUG_UNIFORMS_FRAME
#define LUAUG_UNIFORMS_MATERIAL
#include "luaug_forward.hlsli"

struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float4 Tangent : TEXCOORD2;
    float2 Uv : TEXCOORD3;
};

Interpolants VertexMain(VertexInput input)
{
    Interpolants output;

    const float4 shadingPosition = mul(Model, float4(input.Position, 1.0f));
    output.ShadingPosition = shadingPosition.xyz;
    output.Position = mul(ViewProjection, shadingPosition);
    // For `core::perspective` the clip w IS the view-space distance in front of
    // the camera, which is the space the cascade splits are stated in.
    output.ViewDepth = output.Position.w;

    // The cofactor matrix, so a non-uniform scale tilts the normal correctly
    // instead of shearing it. Renormalised in the fragment stage, after
    // interpolation has shortened it.
    output.Normal = mul((float3x3)NormalMatrix, input.Normal);
    // A tangent is a direction along the surface, not a covector, so it rides on
    // the model matrix rather than on the cofactor one. `w` is a sign and is
    // carried through untouched.
    output.Tangent = float4(mul((float3x3)Model, input.Tangent.xyz), input.Tangent.w);
    output.Uv = input.Uv;
    output.InstanceAlpha = InstanceAlphaUnused.x;

    return output;
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    return shadeForward(input);
}
