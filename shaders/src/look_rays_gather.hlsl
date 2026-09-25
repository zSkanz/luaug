// Sun rays, second of two (ADR 0096): every texel gathers the mask along the
// line from itself towards the sun.
//
// Light that reaches the camera past an edge travels along that line, so
// walking it and summing what shines is a shaft: bright where the line crosses
// open sky near the sun, dark where it crosses something that blocks it. Each
// step counts a little less than the one before, which is what makes a shaft
// fade with its length instead of ending in a cut. The sun itself may be off
// the screen; the line still points at it, and the renderer has already faded
// the whole effect by how far off it is.

#define LUAUG_UNIFORMS_RAYS
#include "luaug_look.hlsli"
#include "luaug_fullscreen.hlsli"

Texture2D MaskTexture : register(t0, space2);
SamplerState MaskSampler : register(s0, space2);

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
    const int taps = (int)RaysGather.y;
    const float2 stride = (RaysSun.xy - input.Uv) * (RaysGather.x / max(RaysGather.y, 1.0f));
    float2 uv = input.Uv;
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float weight = 1.0f;
    [loop]
    for (int tap = 0; tap < taps; ++tap)
    {
        sum += MaskTexture.SampleLevel(MaskSampler, uv, 0.0f).rgb * weight;
        weight *= RaysGather.z;
        uv += stride;
    }
    // Presence and intensity, already one number: zero when the sun is well off
    // the screen or below the horizon, which is how the shafts fade away.
    return float4(sum * (RaysSun.z / max(RaysGather.y, 1.0f)), 1.0f);
}
