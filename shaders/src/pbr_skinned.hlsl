// The forward pass for a SKINNED mesh: `pbr.hlsl`'s shading, reached through a
// linear blend of up to four joints per vertex (roadmap M6, "minimal skeletal
// animation").
//
// **A separate entry file rather than a `#define` inside `pbr.hlsl`.** The two
// differ in their vertex INPUT layout, which is part of the pipeline description
// and not of the code -- one shader with a branch would still have to declare
// the joint and weight attributes, and every static mesh in every world would
// then carry two more vertex attributes it never reads (M6 brief, Decision 11).
//
// **The fragment stage is genuinely shared now** rather than pasted: both files
// include `luaug_forward.hlsli`, which owns the texture slots, the interpolants
// and `shadeForward`. Until M7.5 it was a copy, which was fine while it was one
// screen and stopped being fine when it grew image-based lighting.
//
// The extra vertex buffer is `asset::SkinVertex` (engine/asset/include/luaug/
// asset/model.h) at slot 1: four `uint16` joint indices then four `float`
// weights, 24 bytes, parallel to the position stream by construction.
//
// Register spaces are fixed per stage by SDL_GPU and a shader that ignores them
// binds nothing (SDL_gpu.h:2699-2730).

#define LUAUG_UNIFORMS_OBJECT
#define LUAUG_UNIFORMS_FRAME
#define LUAUG_UNIFORMS_MATERIAL
#define LUAUG_UNIFORMS_SKIN
#include "luaug_forward.hlsli"

struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float4 Tangent : TEXCOORD2;
    float2 Uv : TEXCOORD3;
    // Slot 1, the skin stream. `uint4` rather than `uint16_t4`: the vertex
    // format widens the two 16-bit pairs on the way in, which every backend
    // does for free, and 16-bit shader types need a capability not every one of
    // them has.
    uint4 Joints : TEXCOORD4;
    float4 Weights : TEXCOORD5;
};

Interpolants VertexMain(VertexInput input)
{
    Interpolants output;

    // Model space to posed model space, then the object's own transform. Two
    // multiplies rather than one premultiplied palette: the palette is per
    // SKELETON and `Model` is per draw, so folding them would mean re-uploading
    // the whole palette for every draw that shares a rig.
    const float4x4 skin = skinMatrix(input.Joints, input.Weights);
    const float4 posed = mul(skin, float4(input.Position, 1.0f));
    const float4 shadingPosition = mul(Model, posed);
    output.ShadingPosition = shadingPosition.xyz;
    output.Position = mul(ViewProjection, shadingPosition);
    // For `core::perspective` the clip w IS the view-space distance in front of
    // the camera, which is the space the cascade splits are stated in.
    output.ViewDepth = output.Position.w;

    // The skin's rotation-scale block first, then the object's cofactor matrix.
    // Not the skin's own cofactor: a joint matrix is rigid plus whatever scale
    // the exporter baked into the inverse bind, and computing a 3x3 cofactor per
    // vertex to catch that is a cost the case does not justify. A rig with a
    // non-uniform joint scale shades slightly wrong, and that is written down
    // here rather than discovered.
    output.Normal = mul((float3x3)NormalMatrix, mul((float3x3)skin, input.Normal));
    // A tangent is a direction along the surface, not a covector, so it rides on
    // the model matrix rather than on the cofactor one. `w` is a sign and is
    // carried through untouched.
    output.Tangent = float4(mul((float3x3)Model, mul((float3x3)skin, input.Tangent.xyz)), input.Tangent.w);
    output.Uv = input.Uv;
    output.InstanceAlpha = InstanceAlphaUnused.x;

    return output;
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    return shadeForward(input);
}
