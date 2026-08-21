// Automatic exposure, pass three of three: the last reduction to one texel, and
// the adaptation that keeps it from snapping.
//
// **This pass carries state**, which is what makes it the odd one in the chain:
// it samples LAST frame's exposure from one 1x1 texture and writes this frame's
// into another, and the renderer swaps the two. That ping-pong is how a value
// survives a frame in a renderer with no compute and no readback a frame can
// afford.
//
// **The rate is per FRAME rather than per second.** A rate driven by elapsed
// wall-clock time would make a screenshot at frame thirty a different picture on
// a fast machine and a slow one, and the goldens are the reason that matters
// (R10 in spirit, and the observation rule in practice). The cost is honest and
// small: adaptation feels slightly quicker at a high frame rate.

#define LUAUG_UNIFORMS_LUMINANCE
#include "luaug_pbr.hlsli"
#include "luaug_fullscreen.hlsli"

Texture2D<float> SourceTexture : register(t0, space2);
SamplerState SourceSampler : register(s0, space2);
Texture2D<float> PreviousExposure : register(t1, space2);
SamplerState PreviousSampler : register(s1, space2);

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
    const float2 texel = LuminanceTexelRate.xy;

    float total = 0.0f;
    [unroll]
    for (int y = 0; y < 8; ++y)
    {
        [unroll]
        for (int x = 0; x < 8; ++x)
        {
            const float2 offset = (float2(float(x), float(y)) - 3.5f) * texel;
            total += SourceTexture.SampleLevel(SourceSampler, float2(0.5f, 0.5f) + offset, 0.0f);
        }
    }

    // Back out of the logarithm: the average of logs is the log of the geometric
    // mean, and the geometric mean is the number an exposure should follow.
    const float measured = clamp(exp2(total * (1.0f / 64.0f)), LuminanceRange.x, LuminanceRange.y);

    // The previous value, or the measurement itself on the first frame -- a zero
    // there would otherwise make the first frames of every run a black screen
    // fading in, which a golden recorded at frame two and a screenshot taken at
    // frame thirty would disagree about.
    const float previous = PreviousExposure.SampleLevel(PreviousSampler, float2(0.5f, 0.5f), 0.0f);
    const float from = previous > 0.0f ? previous : measured;

    // Exponential approach, in the log domain, so the same rate takes the same
    // number of frames to cross a stop whichever stop it is.
    const float rate = saturate(LuminanceTexelRate.z);
    const float adapted = exp2(lerp(log2(from), log2(measured), rate));
    return float4(adapted, 0.0f, 0.0f, 1.0f);
}
