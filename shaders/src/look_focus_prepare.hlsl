// Depth of field, first of three (ADR 0096): the frame at half resolution,
// with each texel's circle of confusion beside its colour.
//
// Half, because the gather that follows reads thirty-two texels for every one
// it writes, and a blur has no detail a quarter of the pixels would lose. The
// depth is the NEAREST of the four it covers: a thin post in front of a blurred
// distance keeps its edge rather than being averaged into what is behind it.

#define LUAUG_UNIFORMS_FOCUS
#include "luaug_look.hlsli"
#include "luaug_fullscreen.hlsli"

Texture2D SceneTexture : register(t0, space2);
SamplerState SceneSampler : register(s0, space2);
Texture2D<float> DepthTexture : register(t1, space2);
SamplerState DepthSampler : register(s1, space2);

struct Interpolants
{
    float2 Uv : TEXCOORD0;
    float4 Position : SV_Position;
};

Interpolants VertexMain(uint vertexId : SV_VertexID)
{
    Interpolants output;
    fullscreenTriangle(vertexId, 0.0f, output.Position, output.Uv);
    return output;
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    // One bilinear read in the middle of the four full-resolution texels is
    // their average.
    const float3 color = SceneTexture.SampleLevel(SceneSampler, input.Uv, 0.0f).rgb;

    const float2 corner = FocusTexel.xy * 0.5f;
    float nearest = DepthTexture.SampleLevel(DepthSampler, input.Uv + float2(-corner.x, -corner.y), 0.0f);
    nearest = min(nearest, DepthTexture.SampleLevel(DepthSampler, input.Uv + float2(corner.x, -corner.y), 0.0f));
    nearest = min(nearest, DepthTexture.SampleLevel(DepthSampler, input.Uv + float2(-corner.x, corner.y), 0.0f));
    nearest = min(nearest, DepthTexture.SampleLevel(DepthSampler, input.Uv + float2(corner.x, corner.y), 0.0f));

    return float4(color, focusCircle(nearest) * FocusLens.z);
}
