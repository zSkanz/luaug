// Sun rays, first of two (ADR 0096): what can shine, at half the frame.
//
// **Only the open sky can.** A texel where the depth buffer holds the far plane
// is sky, and anything nearer is something standing in front of it -- so a
// tree, a wall or a pillar is a hole in this mask, and the gather that follows
// streams light past the hole rather than through it. The sky's own light is
// kept, weighted towards the sun, so the shafts are the sun's colour at noon and
// at dusk alike and the rest of the sky adds only a little.

#define LUAUG_UNIFORMS_RAYS
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
    const float depth = DepthTexture.SampleLevel(DepthSampler, input.Uv, 0.0f);
    if (depth < 1.0f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    // Distance from the sun in screen heights, so the falloff is round.
    const float2 away = (input.Uv - RaysSun.xy) * float2(RaysSun.w, 1.0f);
    const float nearSun = exp(-dot(away, away) * 24.0f);

    // Capped, because the sun's disc is many times brighter than white and a
    // shaft that carried all of it would be a second sun smeared across the
    // screen.
    const float3 sky = min(SceneTexture.SampleLevel(SceneSampler, input.Uv, 0.0f).rgb, float3(8.0f, 8.0f, 8.0f));
    return float4(sky * nearSun, 1.0f);
}
