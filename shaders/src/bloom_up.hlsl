// The bloom chain's upsample: a nine-tap tent filter, added into the level
// below with the pipeline's own blend state rather than by reading it.
//
// Reading the destination and writing it in one pass is what a backend refuses,
// and it is also unnecessary: additive blending is a pipeline state the frozen
// RHI already has (`BlendState`, `One`/`One`), so the level below keeps its own
// downsampled content and gains the blurred one above it.
//
// A tent rather than a box on the way up, and that pairing is the point of the
// dual filter: a box down and a tent up approximates a wide Gaussian with two
// cheap kernels, and the wide, smooth falloff is the difference between bloom
// and a halo.

#define LUAUG_UNIFORMS_BLOOM
#include "luaug_pbr.hlsli"
#include "luaug_fullscreen.hlsli"

Texture2D SourceTexture : register(t0, space2);
SamplerState SourceSampler : register(s0, space2);

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

float3 tap(float2 uv, float2 offset)
{
    return SourceTexture.SampleLevel(SourceSampler, uv + offset, 0.0f).rgb;
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    // The radius is in SOURCE texels, so the kernel's world-space width doubles
    // at every level -- which is what makes the sum across the chain a smooth
    // falloff rather than five separate rings.
    const float2 offset = BloomTexelRadius.xy * BloomTexelRadius.z;
    const float2 uv = input.Uv;

    float3 color = tap(uv, float2(-offset.x, -offset.y)) * 1.0f;
    color += tap(uv, float2(0.0f, -offset.y)) * 2.0f;
    color += tap(uv, float2(offset.x, -offset.y)) * 1.0f;
    color += tap(uv, float2(-offset.x, 0.0f)) * 2.0f;
    color += tap(uv, float2(0.0f, 0.0f)) * 4.0f;
    color += tap(uv, float2(offset.x, 0.0f)) * 2.0f;
    color += tap(uv, float2(-offset.x, offset.y)) * 1.0f;
    color += tap(uv, float2(0.0f, offset.y)) * 2.0f;
    color += tap(uv, float2(offset.x, offset.y)) * 1.0f;

    return float4(color * (1.0f / 16.0f), 1.0f);
}
