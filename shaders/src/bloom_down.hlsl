// The bloom chain's downsample: Call of Duty's thirteen-tap filter (Jimenez,
// SIGGRAPH 2014), which is what keeps a single bright pixel from becoming a
// flickering box on the way down the chain.
//
// Twelve taps on two nested squares plus the centre, weighted so the result is a
// proper box average of a 4x4 region rather than the four-tap bilinear reduction
// that aliases. The cost is twelve extra bilinear fetches on targets that are
// each a quarter of the last one, which is a rounding error against the frame.
//
// **Each level is its own texture rather than a mip.** A `ColorAttachment` names
// a texture and not a level, so a chain built out of mips could be sampled and
// not rendered into (ADR 0037). Six textures instead of one, and the same
// pixels.
//
// The threshold is applied on the FIRST pass only. Applying it at every level
// would re-threshold already-blurred light, which makes a surface pop as it
// crosses the line -- and the knee is soft for the same reason: a hard cutoff is
// visible as a bloom that switches on.

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
    const float2 texel = BloomTexelRadius.xy;
    const float2 uv = input.Uv;

    // The inner square, at half a source texel: four bilinear fetches that each
    // already average four texels.
    const float3 inner0 = tap(uv, float2(-texel.x, -texel.y));
    const float3 inner1 = tap(uv, float2(texel.x, -texel.y));
    const float3 inner2 = tap(uv, float2(-texel.x, texel.y));
    const float3 inner3 = tap(uv, float2(texel.x, texel.y));

    // The outer ring, at a full source texel.
    const float3 outer0 = tap(uv, float2(-2.0f * texel.x, -2.0f * texel.y));
    const float3 outer1 = tap(uv, float2(0.0f, -2.0f * texel.y));
    const float3 outer2 = tap(uv, float2(2.0f * texel.x, -2.0f * texel.y));
    const float3 outer3 = tap(uv, float2(-2.0f * texel.x, 0.0f));
    const float3 centre = tap(uv, float2(0.0f, 0.0f));
    const float3 outer5 = tap(uv, float2(2.0f * texel.x, 0.0f));
    const float3 outer6 = tap(uv, float2(-2.0f * texel.x, 2.0f * texel.y));
    const float3 outer7 = tap(uv, float2(0.0f, 2.0f * texel.y));
    const float3 outer8 = tap(uv, float2(2.0f * texel.x, 2.0f * texel.y));

    float3 color = (inner0 + inner1 + inner2 + inner3) * 0.125f;
    color += (outer0 + outer2 + outer6 + outer8) * 0.03125f;
    color += (outer1 + outer3 + outer5 + outer7) * 0.0625f;
    color += centre * 0.125f;

    // Zero on every pass but the first, so the threshold happens once.
    const float threshold = BloomThreshold.x;
    if (threshold > 0.0f)
    {
        const float knee = max(BloomThreshold.y, 1e-4f);
        const float brightness = max(color.r, max(color.g, color.b));
        // A quadratic shoulder between `threshold - knee` and `threshold + knee`,
        // so light fades into the bloom rather than appearing in it.
        const float soft = clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
        const float contribution =
            max(soft * soft / (4.0f * knee), brightness - threshold) / max(brightness, 1e-4f);
        color *= max(contribution, 0.0f);
    }

    return float4(color, 1.0f);
}
