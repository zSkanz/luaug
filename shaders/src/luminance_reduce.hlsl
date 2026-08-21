// Automatic exposure, pass two of three: an 8x8 box reduction of the log
// luminance grid.
//
// Sixty-four taps per output texel, over sixty-four output texels. Four thousand
// bilinear fetches for the whole pass, which is less than one row of the frame
// it is measuring.

#define LUAUG_UNIFORMS_LUMINANCE
#include "luaug_pbr.hlsli"
#include "luaug_fullscreen.hlsli"

Texture2D<float> SourceTexture : register(t0, space2);
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
            total += SourceTexture.SampleLevel(SourceSampler, input.Uv + offset, 0.0f);
        }
    }

    return float4(total * (1.0f / 64.0f), 0.0f, 0.0f, 1.0f);
}
