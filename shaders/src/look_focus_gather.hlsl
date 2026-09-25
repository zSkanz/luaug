// Depth of field, second of three (ADR 0096): each half-resolution texel
// gathers the texels whose circles reach it.
//
// A blur by distance is a SCATTER -- every point spreads into its own circle --
// and a fragment shader can only gather, so each texel asks its neighbours
// whether their circles cover it. Thirty-two of them, on a sunflower spiral out
// to the widest circle there is, so the disc is filled evenly at every radius.
//
// **What is behind may not blur over what is in front.** A neighbour further
// away than this texel contributes only as far as THIS texel's own circle
// reaches: a blurred distance does not bleed across a sharp post standing in
// front of it. A NEARER neighbour contributes as far as its own circle reaches,
// which is what lets a blurred foreground spread over what is behind it -- and
// how much of that happened is written out beside the colour, so the composite
// knows to take the blur there even where this texel itself is sharp.

#define LUAUG_UNIFORMS_FOCUS
#include "luaug_look.hlsli"
#include "luaug_fullscreen.hlsli"

Texture2D PreparedTexture : register(t0, space2);
SamplerState PreparedSampler : register(s0, space2);

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

static const int FocusTaps = 32;
// The golden angle, which is what spreads a spiral's points without rings.
static const float GoldenAngle = 2.39996323f;

float4 FragmentMain(Interpolants input) : SV_Target0
{
    const float4 centre = PreparedTexture.SampleLevel(PreparedSampler, input.Uv, 0.0f);
    const float widest = max(FocusLens.z, 1e-3f);
    const float own = centre.a;

    float3 sum = centre.rgb;
    float total = 1.0f;
    float nearCover = 0.0f;
    [loop]
    for (int tap = 0; tap < FocusTaps; ++tap)
    {
        const float radius = widest * sqrt((tap + 0.5f) / FocusTaps);
        const float angle = tap * GoldenAngle;
        const float2 offset = float2(cos(angle), sin(angle)) * radius;
        const float4 neighbour = PreparedTexture.SampleLevel(PreparedSampler, input.Uv + offset * FocusTexel.zw, 0.0f);

        // Whether this neighbour's circle reaches back to here, softened over
        // one texel so a circle's edge is not a hard ring.
        float weight = saturate(abs(neighbour.a) - radius + 1.0f);
        if (neighbour.a > own)
            weight = min(weight, saturate(abs(own) - radius + 1.0f));
        sum += neighbour.rgb * weight;
        total += weight;
        if (neighbour.a < 0.0f)
            nearCover = max(nearCover, weight * saturate(-neighbour.a / widest));
    }
    return float4(sum / total, max(abs(own) / widest, nearCover));
}
