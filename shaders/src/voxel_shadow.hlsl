// The sun's and the local lights' shadow pass for a block world's CUTOUT faces
// -- leaves (V1, `VoxelService`).
//
// `shadow_depth` writes every fragment of a face, so a leaf block cast a solid
// square while the block itself drew with holes. This is the same depth-only
// pass with the one test the forward shader makes: the face's image is read at
// the same place in the same atlas, and a pixel whose alpha says "hole" is not
// written. The shadow then has the holes the leaves do.
//
// Only the cutout mesh of a chunk is drawn through this. The opaque faces stay
// on `shadow_depth`, which reads no texture, because every one of their
// fragments is there.
//
// The palette is a vertex-stage block (a fragment stage that reads it is a
// pipeline D3D12 refuses), so the tile's rectangle is worked out per vertex
// and passed down, exactly as `voxel.hlsl` does.

#define LUAUG_UNIFORMS_SHADOW
#include "luaug_pbr.hlsli"

// Mirrors `GpuVoxelPalette`; only the tiles and the params are read here, but
// the layout must be the whole block for the offsets to agree.
cbuffer GpuVoxelPalette : register(b1, space1)
{
    float4 VoxelTop[256];
    float4 VoxelSide[256];
    float4 VoxelBottom[256];
    float4 VoxelTiles[256];
    float4 VoxelParams;
};

Texture2D BlockAtlas : register(t0, space2);
SamplerState BlockAtlasSampler : register(s0, space2);

struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float4 Tangent : TEXCOORD2;
};

struct Interpolants
{
    // Where on the grid this is, in blocks, and the face's normal -- both in
    // the chunk's own space, so the image lands where the forward pass put it.
    float3 Grid : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    // The face's image as a rectangle of the atlas: xy its corner, z its size
    // (zero for a face with no image, which is then solid), w the inset.
    float4 TileRect : TEXCOORD2;
    float4 Position : SV_Position;
};

Interpolants VertexMain(VertexInput input)
{
    Interpolants output;
    output.Position = mul(LightViewProjection, mul(ShadowModel, float4(input.Position, 1.0f)));
    output.Normal = input.Normal;
    output.Grid = input.Position / max(VoxelParams.x, 1e-4f);

    const uint id = uint(input.Tangent.x + 0.5f);
    const uint slot = min(max(id, 1u) - 1u, 255u);
    const float tile = input.Normal.y > 0.5f ? VoxelTiles[slot].x
                       : input.Normal.y < -0.5f ? VoxelTiles[slot].z
                                                : VoxelTiles[slot].y;
    if (tile >= 0.0f) {
        const float row = floor(tile / VoxelParams.y);
        output.TileRect = float4(float2(tile - row * VoxelParams.y, row) * VoxelParams.z, VoxelParams.z, VoxelParams.w);
    }
    else {
        output.TileRect = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    return output;
}

void FragmentMain(Interpolants input)
{
    if (input.TileRect.z <= 0.0f)
        return;
    // The forward pass's own face coordinates (`voxel.hlsl`): a hole there is
    // a hole here, texel for texel.
    const float3 normal = input.Normal;
    const float3 g = input.Grid;
    float2 faceUv;
    if (abs(normal.y) > 0.5f)
        faceUv = float2(frac(g.x), normal.y > 0.0f ? frac(g.z) : 1.0f - frac(g.z));
    else if (abs(normal.x) > 0.5f)
        faceUv = float2(normal.x > 0.0f ? 1.0f - frac(g.z) : frac(g.z), 1.0f - frac(g.y));
    else
        faceUv = float2(normal.z > 0.0f ? frac(g.x) : 1.0f - frac(g.x), 1.0f - frac(g.y));
    faceUv = clamp(faceUv, input.TileRect.w, 1.0f - input.TileRect.w);
    const float2 atlasUv = input.TileRect.xy + faceUv * input.TileRect.z;
    if (BlockAtlas.SampleLevel(BlockAtlasSampler, atlasUv, 0.0f).a < 0.5f)
        discard;
}
