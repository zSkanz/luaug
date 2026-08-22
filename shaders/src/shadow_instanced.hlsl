// The depth-only pass for a run of objects that share a mesh: the shadow
// cascades and the depth prepass, instanced (ADR 0043).
//
// `shadow_depth.hlsl`'s shader with the model matrix coming from a per-INSTANCE
// vertex stream instead of from the uniform block, which is the same difference
// `pbr_instanced.hlsl` has from `pbr.hlsl`. `ShadowModel` is left unread rather
// than removed: the block is `GpuShadowUniforms` either way, and what the
// instanced path binds in it is the view-projection alone.
//
// There is no colour target, so the fragment stage writes nothing. It exists
// because `SDL_CreateGPUGraphicsPipeline` requires both stages, and a `void`
// entry point is also the correct one -- a pipeline with zero colour targets
// whose fragment shader declares `SV_Target0` is a mismatch the D3D12 debug
// layer objects to.

#define LUAUG_UNIFORMS_SHADOW
#include "luaug_pbr.hlsli"

struct VertexInput
{
    float3 Position : TEXCOORD0;
    // Slot 1, per instance. The alpha row of `render::GpuInstance` is not
    // declared: a depth pass has no use for it, and an attribute a shader does
    // not consume is one more location the pipeline has to describe for no
    // result. The stride still steps over it, which is what the layout is for.
    float4 ModelColumn0 : TEXCOORD1;
    float4 ModelColumn1 : TEXCOORD2;
    float4 ModelColumn2 : TEXCOORD3;
    float4 ModelColumn3 : TEXCOORD4;
};

struct Interpolants
{
    float4 Position : SV_Position;
};

Interpolants VertexMain(VertexInput input)
{
    Interpolants output;
    // Transposed, and see `pbr_instanced.hlsl` for why: the attributes are the
    // matrix's columns and HLSL's constructor fills rows (D043).
    const float4x4 model =
        transpose(float4x4(input.ModelColumn0, input.ModelColumn1, input.ModelColumn2, input.ModelColumn3));
    output.Position = mul(LightViewProjection, mul(model, float4(input.Position, 1.0f)));
    return output;
}

void FragmentMain()
{
}
