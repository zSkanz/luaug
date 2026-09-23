// The forward pass for the block world (V1, `VoxelService`).
//
// A chunk's mesh arrives with, in each vertex's tangent, the block id (x) and
// the corner's ambient occlusion (y) -- baked by the greedy mesher, which only
// merges faces whose corners agree, so the occlusion interpolated across a
// merged quad is exact. The id becomes a colour here, from the registry the
// renderer pushes as a palette; the occlusion darkens the corner; and two kinds
// of procedural variation stop a wall of one block type reading as a single
// flat colour: a per-BLOCK tint, and a fine grain within each face.
//
// Lit by `lightSurface`, the same code as every other forward shader.

#define LUAUG_UNIFORMS_OBJECT
#define LUAUG_UNIFORMS_FRAME
#include "luaug_forward.hlsli"
#include "luaug_terrain_surface.hlsli"

// The registry's colours, by id minus one, and the block size. Vertex stage
// only: a block's colour is decided per vertex. Mirrors `GpuVoxelPalette`.
cbuffer GpuVoxelPalette : register(b1, space1)
{
    float4 VoxelTop[256];
    float4 VoxelSide[256];
    float4 VoxelBottom[256];
    // The atlas tile each face's image is in: x top, y sides, z bottom, and -1
    // for a face with no image.
    float4 VoxelTiles[256];
    // x: block size in metres; y: tiles per atlas row; z: one tile's size in
    // atlas UV; w: half a texel of a tile, in the tile's own UV, for the inset.
    float4 VoxelParams;
};

struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float4 Tangent : TEXCOORD2;
    float2 Uv : TEXCOORD3;
};

struct VoxelInterpolants
{
    float3 ShadingPosition : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float3 Albedo : TEXCOORD2;
    // The block the face belongs to, in block units, and where on the face the
    // fragment is -- both in the grid's own space, so nothing swims.
    float3 Block : TEXCOORD3;
    float2 FaceUv : TEXCOORD4;
    float Occlusion : TEXCOORD5;
    float ViewDepth : TEXCOORD6;
    // Where on the grid this is, in blocks, for the image's coordinates.
    float3 Grid : TEXCOORD7;
    // The face's image as a rectangle of the atlas, worked out here from the
    // palette: xy the tile's corner in atlas UV, z its size (zero when the face
    // has no image), w half a texel of inset. **Passed rather than read in the
    // fragment stage**, because the palette is a vertex-stage block and a
    // fragment stage that touches it is a pipeline D3D12 refuses. The same at
    // every corner of a face, so it interpolates to itself.
    float4 TileRect : TEXCOORD8;
    float4 Position : SV_Position;
};

VoxelInterpolants VertexMain(VertexInput input)
{
    VoxelInterpolants output;
    const float4 shadingPosition = mul(Model, float4(input.Position, 1.0f));
    output.ShadingPosition = shadingPosition.xyz;
    output.Position = mul(ViewProjection, shadingPosition);
    output.ViewDepth = output.Position.w;
    output.Normal = mul((float3x3)NormalMatrix, input.Normal);

    const uint id = uint(input.Tangent.x + 0.5f);
    // Which of the block's three colours this face wears, from its normal: a
    // face is axis-aligned, so its normal is exactly one of six.
    const uint slot = min(max(id, 1u) - 1u, 255u);
    output.Albedo = input.Normal.y > 0.5f ? VoxelTop[slot].rgb
                    : input.Normal.y < -0.5f ? VoxelBottom[slot].rgb
                                             : VoxelSide[slot].rgb;
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
    output.Occlusion = input.Tangent.y;
    const float size = max(VoxelParams.x, 1e-4f);
    // Half a block INTO the face, so every fragment of a face names the block
    // it belongs to rather than the one in front of it.
    output.Block = input.Position / size - input.Normal * 0.5f;
    output.Grid = input.Position / size;
    output.FaceUv = input.Uv;
    return output;
}

float4 FragmentMain(VoxelInterpolants input) : SV_Target0
{
    const float3 normal = normalize(input.Normal);
    const float3 block = floor(input.Block);

    // A tint per block, so a wall of one type is a wall of blocks.
    const float perBlock = terrainHash(block.xz + block.y * 17.31f);
    // A grain within each face: eight cells a side, like a low-resolution
    // texture, faded out where a cell is smaller than a pixel.
    const float2 grainCell = floor(frac(input.FaceUv) * 8.0f);
    const float grain = terrainHash(grainCell + block.xz * 7.13f + block.y * 3.7f);
    const float footprint = max(fwidth(input.FaceUv.x), fwidth(input.FaceUv.y)) * 8.0f;
    const float grainFade = saturate(1.0f - footprint);

    float3 albedo = input.Albedo;
    const bool imaged = input.TileRect.z > 0.0f;
    if (imaged) {
        // **The image's own coordinates, from the grid and the face**, rather
        // than from the mesher's quad: a merged quad's UV runs along whichever
        // axes its sweep used, so a side image would lie on its back on two
        // sides and be mirrored on two. Here every side stands upright, seen
        // from outside, and a top is laid out on the ground's own axes.
        const float3 g = input.Grid;
        float2 faceUv;
        if (abs(normal.y) > 0.5f)
            faceUv = float2(frac(g.x), normal.y > 0.0f ? frac(g.z) : 1.0f - frac(g.z));
        else if (abs(normal.x) > 0.5f)
            faceUv = float2(normal.x > 0.0f ? 1.0f - frac(g.z) : frac(g.z), 1.0f - frac(g.y));
        else
            faceUv = float2(normal.z > 0.0f ? frac(g.x) : 1.0f - frac(g.x), 1.0f - frac(g.y));
        // Inset by half a texel, so the tile next door never bleeds in.
        faceUv = clamp(faceUv, input.TileRect.w, 1.0f - input.TileRect.w);
        const float2 atlasUv = input.TileRect.xy + faceUv * input.TileRect.z;
        albedo *= BaseColorTexture.SampleLevel(BaseColorSampler, atlasUv, 0.0f).rgb;
    }
    // The procedural variation is for colour-only blocks: an image already has
    // its own, and a tint laid over it reads as dirt on the texture.
    if (!imaged)
        albedo *= 1.0f + (perBlock - 0.5f) * 0.10f + (grain - 0.5f) * 0.10f * grainFade;
    // The corner occlusion: fully open is 1, a corner in a crease is dark. The
    // curve keeps a one-block step readable without making every crease black.
    albedo *= lerp(0.42f, 1.0f, input.Occlusion * input.Occlusion * (3.0f - 2.0f * input.Occlusion));

    // The four material slots, read so the fragment layout matches every other
    // forward shader's -- bound to neutral stand-ins today.
    const float2 uv = input.FaceUv;
    const float roughness = 0.9f * MetallicRoughnessTexture.Sample(MetallicRoughnessSampler, uv).g;
    const float3 detail = NormalTexture.Sample(NormalSampler, uv).xyz * 2.0f - 1.0f;
    const float3x3 frame = tangentFrame(normal, float4(1.0f, 0.0f, 0.0f, 1.0f));
    const float3 shadingNormal = normalize(mul(normalize(detail), frame));

    Surface surface = makeSurface(input.ShadingPosition, shadingNormal, albedo, 0.0f, roughness);
    float3 color = lightSurface(surface, input.ShadingPosition, shadingNormal, input.ViewDepth, input.Position.xy);
    color += EmissiveTexture.Sample(EmissiveSampler, uv).rgb;
    color = applyFog(color, FogColor.rgb, FogRange, length(input.ShadingPosition));
    return float4(color, 1.0f);
}
