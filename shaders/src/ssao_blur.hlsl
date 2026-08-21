// The depth-aware blur that makes the occlusion term usable.
//
// Separable, so one pipeline is drawn twice with a different direction -- which
// is why `GpuBlurUniforms` carries the direction rather than the shader assuming
// one. Nine taps.
//
// **Depth-aware rather than plain**: a Gaussian across a silhouette drags the
// occlusion of a near object onto a far one, and that reads as a dark halo,
// which is the most recognisable way a first ambient-occlusion pass looks wrong.
// Taps whose depth is far from the centre's are dropped rather than weighted
// down, because a partial contribution from the wrong surface is still the wrong
// surface.

#define LUAUG_UNIFORMS_BLUR
#include "luaug_pbr.hlsli"
#include "luaug_fullscreen.hlsli"

Texture2D<float> OcclusionTexture : register(t0, space2);
SamplerState OcclusionSampler : register(s0, space2);
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
    const float2 step = BlurTexelDirection.xy * BlurTexelDirection.zw;
    const float centreDepth = DepthTexture.SampleLevel(DepthSampler, input.Uv, 0.0f);

    float total = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (int i = -4; i <= 4; ++i)
    {
        const float2 uv = input.Uv + step * float(i);
        const float depth = DepthTexture.SampleLevel(DepthSampler, uv, 0.0f);
        // A tolerance in DEVICE depth rather than in metres: coarse far away and
        // fine close up, which is the same distribution the depth buffer's own
        // precision has and the same one the artifact follows.
        const float weight = abs(depth - centreDepth) < 0.0015f ? 1.0f : 0.0f;
        total += OcclusionTexture.SampleLevel(OcclusionSampler, uv, 0.0f) * weight;
        weightSum += weight;
    }

    const float result = weightSum > 0.0f ? total / weightSum : 1.0f;
    return float4(result, result, result, 1.0f);
}
