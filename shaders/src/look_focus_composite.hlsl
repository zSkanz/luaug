// Depth of field, last of three (ADR 0096): the sharp frame and the gathered
// blur, mixed by how out of focus each full-resolution pixel is.
//
// The circle is recomputed here at full resolution rather than read from the
// half-resolution pass, so the line where a sharp object meets its blurred
// background is drawn at the frame's own resolution. Where a blurred foreground
// has spread over this pixel, the gather said so, and that wins.

#define LUAUG_UNIFORMS_FOCUS
#include "luaug_look.hlsli"
#include "luaug_fullscreen.hlsli"

Texture2D SceneTexture : register(t0, space2);
SamplerState SceneSampler : register(s0, space2);
Texture2D GatheredTexture : register(t1, space2);
SamplerState GatheredSampler : register(s1, space2);
Texture2D<float> DepthTexture : register(t2, space2);
SamplerState DepthSampler : register(s2, space2);

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
    const float3 sharp = SceneTexture.SampleLevel(SceneSampler, input.Uv, 0.0f).rgb;
    const float4 gathered = GatheredTexture.SampleLevel(GatheredSampler, input.Uv, 0.0f);
    const float own = abs(focusCircle(DepthTexture.SampleLevel(DepthSampler, input.Uv, 0.0f)));

    // In pixels of this frame: under half a pixel of blur is sharp, and the
    // mix is whole by a pixel and a half.
    const float pixels = max(own, gathered.a) * FocusLens.w;
    const float mixed = saturate(pixels - 0.5f);
    return float4(lerp(sharp, gathered.rgb, mixed), 1.0f);
}
