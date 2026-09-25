// A picture into a target of another size, filtered: the last step of a blur
// that ran at a fraction of the frame, bringing it back to the frame's own size
// so every pass after it reads a full-resolution image as it always has
// (ADR 0096).
//
// Bilinear is enough here and that is not a shortcut: what it reads is already
// a Gaussian at least two texels wide, so the step between two of its texels is
// smaller than the blur itself.

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

float4 FragmentMain(Interpolants input) : SV_Target0
{
    return float4(SourceTexture.SampleLevel(SourceSampler, input.Uv, 0.0f).rgb, 1.0f);
}
