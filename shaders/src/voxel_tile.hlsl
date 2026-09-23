// One block image into its tile of the block atlas (V1).
//
// **The atlas is filled by drawing, not by copying**, and that is what lets it
// hold COMPILED images: a texture the asset compiler turned into BC7 has no
// pixels the CPU could pack, but it can be sampled -- so each one is drawn,
// once, into its square of an ordinary colour target, and the block shader
// reads the atlas. The viewport is the tile; the triangle covers it.
//
// Sampled without smoothing, because a block image is usually a handful of
// pixels meant to stay crisp: a sixteen-pixel grass top drawn into a sixty-four
// pixel tile is four copies of each pixel, not a blur.

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
    return SourceTexture.SampleLevel(SourceSampler, input.Uv, 0.0f);
}
