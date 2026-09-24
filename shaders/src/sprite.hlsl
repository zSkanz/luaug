// Sprites (the 2D layer): a `Part2D`, or one tile of a `Tilemap2D`, as a
// textured rectangle on the world's z = 0 plane.
//
// **Instances only**, like the particles: the six corners come from the vertex
// index and each instance is one rectangle. What is drawn is the picture times
// its colour, unlit -- a sprite is art, and lighting it would repaint it --
// into the HDR target before tone mapping, with the same sRGB decode the world
// UI gives a colour written for a screen.
//
// A tile's corners are placed exactly as given. Everything else is turned
// about its middle, and a circle or a capsule is cut out of the rectangle with
// an anti-aliased edge, so what is drawn is the outline it collides as.

cbuffer SpriteView : register(b0, space1)
{
    column_major float4x4 ViewProjection;
};

Texture2D<float4> SpriteTexture : register(t0, space2);
SamplerState SpriteSampler : register(s0, space2);

struct VertexInput
{
    // Camera-relative low x, low y, high x, high y.
    float4 Rect : TEXCOORD0;
    // x cosine, y sine, z the plane's depth, w the shape.
    float4 Turn : TEXCOORD1;
    // Left, top, right, bottom in texture space.
    float4 Uv : TEXCOORD2;
    // sRGB colour and opacity.
    float4 Color : TEXCOORD3;
    uint Vertex : SV_VertexID;
};

struct Interpolants
{
    float4 Color : TEXCOORD0;
    float2 Uv : TEXCOORD1;
    // Metres from the middle, in the sprite's own frame, and its half-extent.
    float2 Local : TEXCOORD2;
    float2 Half : TEXCOORD3;
    nointerpolation float Shape : TEXCOORD4;
    float4 Position : SV_Position;
};

float3 decodeSrgb(float3 color)
{
    return lerp(pow((color + 0.055f) / 1.055f, 2.4f), color / 12.92f, step(color, 0.04045f));
}

Interpolants VertexMain(VertexInput input)
{
    // Two triangles, in the order a square is cut: 0 1 2, 0 2 3.
    const float2 corners[6] = {float2(-1.0f, -1.0f), float2(1.0f, -1.0f), float2(1.0f, 1.0f),
                               float2(-1.0f, -1.0f), float2(1.0f, 1.0f), float2(-1.0f, 1.0f)};
    const float2 corner = corners[input.Vertex % 6u];

    float2 position =
        float2(corner.x < 0.0f ? input.Rect.x : input.Rect.z, corner.y < 0.0f ? input.Rect.y : input.Rect.w);
    const float2 middle = (input.Rect.xy + input.Rect.zw) * 0.5f;
    const float2 extent = (input.Rect.zw - input.Rect.xy) * 0.5f;
    // Unturned -- every tile -- keeps its corner bit for bit, so neighbours meet.
    if (input.Turn.x != 1.0f || input.Turn.y != 0.0f) {
        const float2 offset = position - middle;
        position = middle + float2(offset.x * input.Turn.x - offset.y * input.Turn.y,
                                   offset.x * input.Turn.y + offset.y * input.Turn.x);
    }

    Interpolants output;
    output.Position = mul(ViewProjection, float4(position, input.Turn.z, 1.0f));
    output.Color = float4(decodeSrgb(input.Color.rgb), input.Color.a);
    // The top of the picture is the top of the sprite: y grows upwards in the
    // world and downwards in an image.
    output.Uv = float2(corner.x < 0.0f ? input.Uv.x : input.Uv.z, corner.y < 0.0f ? input.Uv.w : input.Uv.y);
    output.Local = corner * extent;
    output.Half = extent;
    output.Shape = input.Turn.w;
    return output;
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    // Distance outside the outline, in metres: a box has none, a circle is its
    // inscribed disc, and a capsule is a segment up the middle swept by half
    // the width.
    // Evaluated for every shape and then chosen, because a derivative inside a
    // branch is a derivative some drivers do not have.
    const uint shape = uint(input.Shape + 0.5f);
    const float radius = min(input.Half.x, input.Half.y);
    const float reach = shape == 2u ? max(input.Half.y - input.Half.x, 0.0f) : 0.0f;
    const float2 nearest = float2(input.Local.x, max(abs(input.Local.y) - reach, 0.0f));
    const float outside = length(nearest) - radius;
    const float edge = saturate(0.5f - outside / max(fwidth(outside), 1e-5f));
    const float coverage = shape == 0u ? 1.0f : edge;

    const float4 picture = SpriteTexture.Sample(SpriteSampler, input.Uv);
    const float alpha = picture.a * input.Color.a * coverage;
    if (alpha <= 0.002f)
        discard;
    return float4(picture.rgb * input.Color.rgb, alpha);
}
