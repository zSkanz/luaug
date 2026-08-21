// The anti-aliasing resolve: FXAA, on the tonemapped sRGB image.
//
// **Spatial rather than temporal, and that is a decision rather than a
// shortcut** (M7.5 brief, Decision 10). The post chain owes anti-aliasing; it
// does not owe a velocity buffer with no consumer. Temporal anti-aliasing needs
// exactly two things a temporal upscaler needs -- a per-pixel motion vector and
// a jitterable projection -- and the roadmap asks for those to be DECLARED as
// renderer outputs rather than built as private state of a pass. The jitter
// ships (`RenderCamera::jitter`, zero everywhere); the velocity target does not,
// because a second render target on every forward draw is renderer-wide
// bandwidth for a consumer that does not exist.
//
// Nothing here forecloses it: FXAA reads the final image and writes the
// swapchain, so a temporal pass would replace this one rather than fight it.
//
// The algorithm is the published FXAA 3.11 console variant, at the quality end:
// find the local contrast, bail where there is none, then step along the edge
// looking for where it ends and blend across it. It works on PERCEPTUAL
// luminance -- this pass runs after `tonemap.hlsl` has encoded sRGB, which is
// exactly where FXAA expects to be, because an edge in linear light is not the
// edge a person sees.

#define LUAUG_UNIFORMS_FXAA
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

// Rec.601 luminance rather than the green channel alone.
//
// The original FXAA uses green, on the argument that it carries most of the
// perceived luminance and costs no dot product. That argument fails on exactly
// the case this engine's first test scene is made of: a pink column against a
// blue floor has almost no GREEN contrast and a great deal of luminance
// contrast, so every edge between them was left untouched. A dot product per
// tap is twenty dot products for a pass that is already twenty texture fetches.
float lumaOf(float3 color)
{
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

// Below this much local contrast there is no edge worth touching, and blending
// anyway is how FXAA gets its reputation for softening texture detail.
static const float LuaugFxaaContrastThreshold = 0.0312f;
// Relative to the brightest neighbour, so a dark region is not smoothed by the
// same absolute amount as a bright one.
static const float LuaugFxaaRelativeThreshold = 0.125f;
// How far the search walks looking for the end of an edge, in texels.
static const int LuaugFxaaSearchSteps = 12;
// How much of the subpixel term reaches the result. One is the reference's own
// maximum, and it is what this renderer wants: with no multisampling anywhere in
// the frozen RHI and no temporal history, this pass is the only thing standing
// between a geometric edge and a staircase.
static const float LuaugFxaaSubpixelQuality = 1.0f;

float4 FragmentMain(Interpolants input) : SV_Target0
{
    const float2 texel = FxaaTexel.xy;
    const float2 uv = input.Uv;

    const float3 centreColor = SourceTexture.SampleLevel(SourceSampler, uv, 0.0f).rgb;
    const float centre = lumaOf(centreColor);
    const float north = lumaOf(SourceTexture.SampleLevel(SourceSampler, uv + float2(0.0f, -texel.y), 0.0f).rgb);
    const float south = lumaOf(SourceTexture.SampleLevel(SourceSampler, uv + float2(0.0f, texel.y), 0.0f).rgb);
    const float west = lumaOf(SourceTexture.SampleLevel(SourceSampler, uv + float2(-texel.x, 0.0f), 0.0f).rgb);
    const float east = lumaOf(SourceTexture.SampleLevel(SourceSampler, uv + float2(texel.x, 0.0f), 0.0f).rgb);

    const float highest = max(centre, max(max(north, south), max(west, east)));
    const float lowest = min(centre, min(min(north, south), min(west, east)));
    const float contrast = highest - lowest;

    if (contrast < max(LuaugFxaaContrastThreshold, highest * LuaugFxaaRelativeThreshold))
    {
        return float4(centreColor, 1.0f);
    }

    const float northWest = lumaOf(SourceTexture.SampleLevel(SourceSampler, uv + float2(-texel.x, -texel.y), 0.0f).rgb);
    const float northEast = lumaOf(SourceTexture.SampleLevel(SourceSampler, uv + float2(texel.x, -texel.y), 0.0f).rgb);
    const float southWest = lumaOf(SourceTexture.SampleLevel(SourceSampler, uv + float2(-texel.x, texel.y), 0.0f).rgb);
    const float southEast = lumaOf(SourceTexture.SampleLevel(SourceSampler, uv + float2(texel.x, texel.y), 0.0f).rgb);

    // Which way the edge runs, from the second derivative along each axis. The
    // corners are weighted once and the edges twice, which is a Sobel in
    // everything but name.
    const float horizontal = abs(north + south - 2.0f * centre) * 2.0f +
                             abs(northEast + southEast - 2.0f * east) + abs(northWest + southWest - 2.0f * west);
    const float vertical = abs(west + east - 2.0f * centre) * 2.0f + abs(northWest + northEast - 2.0f * north) +
                           abs(southWest + southEast - 2.0f * south);
    const bool isHorizontal = horizontal >= vertical;

    // The neighbour across the edge, and the gradient towards it.
    const float positive = isHorizontal ? south : east;
    const float negative = isHorizontal ? north : west;
    const float positiveGradient = abs(positive - centre);
    const float negativeGradient = abs(negative - centre);

    float pixelStep = isHorizontal ? texel.y : texel.x;
    float oppositeLuma = positive;
    float gradient = positiveGradient;
    if (negativeGradient > positiveGradient)
    {
        pixelStep = -pixelStep;
        oppositeLuma = negative;
        gradient = negativeGradient;
    }

    // Walk along the edge in both directions until the local gradient stops
    // matching, which is where the edge ends. The distance to each end is what
    // says how far along the edge this pixel sits, and therefore how much of the
    // neighbour to blend in.
    const float edgeLuma = (centre + oppositeLuma) * 0.5f;
    const float gradientThreshold = gradient * 0.25f;

    float2 edgeStep = isHorizontal ? float2(texel.x, 0.0f) : float2(0.0f, texel.y);
    const float2 edgeUv = isHorizontal ? float2(uv.x, uv.y + pixelStep * 0.5f) : float2(uv.x + pixelStep * 0.5f, uv.y);

    float positiveDistance = 0.0f;
    bool positiveEnded = false;
    [loop]
    for (int i = 1; i <= LuaugFxaaSearchSteps; ++i)
    {
        const float2 probe = edgeUv + edgeStep * float(i);
        const float delta = lumaOf(SourceTexture.SampleLevel(SourceSampler, probe, 0.0f).rgb) - edgeLuma;
        positiveDistance = float(i);
        if (abs(delta) >= gradientThreshold)
        {
            positiveEnded = true;
            break;
        }
    }

    float negativeDistance = 0.0f;
    bool negativeEnded = false;
    [loop]
    for (int j = 1; j <= LuaugFxaaSearchSteps; ++j)
    {
        const float2 probe = edgeUv - edgeStep * float(j);
        const float delta = lumaOf(SourceTexture.SampleLevel(SourceSampler, probe, 0.0f).rgb) - edgeLuma;
        negativeDistance = float(j);
        if (abs(delta) >= gradientThreshold)
        {
            negativeEnded = true;
            break;
        }
    }

    // An edge whose end was never found gets no blend: guessing at the middle of
    // an edge that runs off the search range is how FXAA smears a long thin
    // feature.
    float blend = 0.0f;
    if (positiveEnded || negativeEnded)
    {
        const float shortest = min(positiveDistance, negativeDistance);
        const float total = positiveDistance + negativeDistance;
        blend = saturate(0.5f - shortest / max(total, 1e-4f));
    }

    // And the subpixel term, which is what actually carries this pass on
    // geometric edges: the edge search only moves the pixels next to a step,
    // while this moves every pixel whose luminance differs from its
    // neighbourhood's -- which on a one-pixel staircase is all of them.
    //
    // The double shaping is Lottes's and it is not decoration: a smoothstep and
    // then a square is what keeps a lightly-aliased edge from being softened as
    // hard as a fully aliased one, which is the whole difference between FXAA
    // and a blur.
    const float average = (2.0f * (north + south + west + east) + northWest + northEast + southWest + southEast) /
                          12.0f;
    const float subpixelRaw = saturate(abs(average - centre) / max(contrast, 1e-4f));
    const float subpixelShaped = saturate((-2.0f * subpixelRaw + 3.0f) * subpixelRaw * subpixelRaw);
    const float subpixelBlend = subpixelShaped * subpixelShaped * LuaugFxaaSubpixelQuality;

    const float finalBlend = max(blend, subpixelBlend);
    const float2 resolved = isHorizontal ? float2(uv.x, uv.y + pixelStep * finalBlend)
                                         : float2(uv.x + pixelStep * finalBlend, uv.y);
    return float4(SourceTexture.SampleLevel(SourceSampler, resolved, 0.0f).rgb, 1.0f);
}
