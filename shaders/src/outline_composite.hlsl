// The editor's selection outline, drawn from the mask.
//
// A dilate minus the mask itself: a pixel the mask does not cover but one of
// whose neighbours within the outline's width does is an edge pixel, and the
// set of those is the silhouette of everything selected. That is the whole
// mechanism, and it is why this produces an outline around a tree rather than
// around a tree's bounding box.
//
// **Outside rather than on the shape.** Growing outwards leaves the object
// itself untouched, so a selected part still shows its own colour and its own
// shading -- an outline drawn ON the silhouette eats a pixel of whatever it is
// meant to be helping you look at.
//
// Two selected objects that touch on screen produce ONE outline around the
// union, not a seam between them, because the mask does not distinguish them.
// That is the right answer: they are one selection.
//
// The fill is a flat tint over the mask, kept low. It is what tells a selected
// object apart from an unselected one BEHIND a selected one, which an outline
// alone cannot -- and it is the half that has to stay subtle, because a strong
// one hides the colour somebody selected the part to change.

#define LUAUG_UNIFORMS_OUTLINE
#include "luaug_pbr.hlsli"
#include "luaug_fullscreen.hlsli"

Texture2D<float> MaskTexture : register(t0, space2);
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
    const float2 texel = OutlineTexelWidth.xy;
    const float width = OutlineTexelWidth.z;

    const float centre = MaskTexture.SampleLevel(MaskSampler, input.Uv, 0.0f);

    // A square ring at the outline's width rather than a filled disc: only the
    // OUTERMOST taps can decide whether a pixel is within `width` of the shape,
    // so sampling the inside of the neighbourhood is work whose answer the
    // centre tap already gave. Eight taps, which is the ring at radius one --
    // the width scales the step rather than the count, so a thicker outline
    // costs nothing more.
    //
    // The cost of the ring is a corner: at exactly forty-five degrees the
    // diagonal taps reach `width * sqrt(2)`, so the line is a little wider
    // there. At the two-pixel widths an editor uses this is invisible, and the
    // alternative is a tap count that grows with the square of the width.
    float neighbour = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }
            const float2 uv = input.Uv + float2(float(x), float(y)) * texel * width;
            neighbour = max(neighbour, MaskTexture.SampleLevel(MaskSampler, uv, 0.0f));
        }
    }

    // Saturated rather than compared to a threshold: the mask is filtered on the
    // way in, so an edge tap lands between zero and one and the fraction is
    // exactly the coverage an antialiased outline wants.
    const float edge = saturate(neighbour - centre);
    const float fill = centre * OutlineFillColor.a;

    // The outline over the fill, both premultiplied by their own coverage, and
    // the whole thing blended over the frame by the pipeline. Written this way
    // rather than as two passes because an outline and its fill never overlap:
    // `edge` is nonzero only where `centre` is zero.
    const float3 colour = OutlineColor.rgb * edge + OutlineFillColor.rgb * fill;
    const float alpha = edge * OutlineColor.a + fill;
    return float4(colour, alpha);
}
