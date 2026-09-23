// World-space UI (F3): a `BillboardGui` or a `SurfaceGui`'s quads, drawn in the
// world rather than over it.
//
// **The same quads `ui2d.hlsl` draws**, laid out by the same layout and built by
// the same draw list, with one difference that is the whole feature: the host
// has already placed each corner in the world, so this stage projects it like
// any other surface and it is hidden by whatever is in front of it. What comes
// out goes into the HDR target before tone mapping -- it is part of the world's
// picture, which is why it is not the screen pass the frame-generation
// constraint says must be composited last.
//
// A UI colour is written for a screen, in sRGB. Here it is converted to linear
// light and scaled by the gui's `Brightness`, so white at one is about as
// bright as a lit white wall and above one it blooms.

cbuffer UiWorldView : register(b0, space1)
{
    column_major float4x4 ViewProjection;
};

cbuffer UiWorldLook : register(b0, space3)
{
    // x: brightness.
    float4 Look;
};

Texture2D<float4> UiTexture : register(t0, space2);
SamplerState UiSampler : register(s0, space2);

struct VertexInput
{
    // Camera-relative metres, as every f32 position the renderer draws is.
    float3 Position : TEXCOORD0;
    float4 Color : TEXCOORD1;
    // xy: the vertex's offset from its quad's centre; zw: the quad's
    // half-extent -- in canvas pixels, for the rounded corner.
    float4 LocalHalf : TEXCOORD2;
    float Radius : TEXCOORD3;
    float2 Uv : TEXCOORD4;
};

struct Interpolants
{
    float4 Color : TEXCOORD0;
    float4 LocalHalf : TEXCOORD1;
    float Radius : TEXCOORD2;
    float2 Uv : TEXCOORD3;
    float4 Position : SV_Position;
};

Interpolants VertexMain(VertexInput input)
{
    Interpolants output;
    output.Position = mul(ViewProjection, float4(input.Position, 1.0f));
    output.Color = input.Color;
    output.LocalHalf = input.LocalHalf;
    output.Radius = input.Radius;
    output.Uv = input.Uv;
    return output;
}

// `ui2d.hlsl`'s, unchanged: the corner is arithmetic on the fragment.
float roundedRectDistance(float2 local, float2 half, float radius)
{
    const float2 outside = abs(local) - (half - radius);
    return length(max(outside, 0.0f)) + min(max(outside.x, outside.y), 0.0f) - radius;
}

float3 srgbToLinear(float3 color)
{
    return lerp(pow((color + 0.055f) / 1.055f, 2.4f), color / 12.92f, step(color, 0.04045f));
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    float4 color = input.Color * UiTexture.Sample(UiSampler, input.Uv);
    if (input.Radius > 0.0f) {
        const float distance = roundedRectDistance(input.LocalHalf.xy, input.LocalHalf.zw, input.Radius);
        const float edge = fwidth(distance);
        color.a *= 1.0f - smoothstep(-edge, edge, distance);
    }
    // Nothing to blend is nothing to write, which keeps a label's empty corners
    // from greying the depth-sorted surfaces behind them at the edges.
    clip(color.a - 1.0f / 255.0f);
    return float4(srgbToLinear(color.rgb) * Look.x, color.a);
}
