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
    // x: block size in metres.
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
    output.Occlusion = input.Tangent.y;
    const float size = max(VoxelParams.x, 1e-4f);
    // Half a block INTO the face, so every fragment of a face names the block
    // it belongs to rather than the one in front of it.
    output.Block = input.Position / size - input.Normal * 0.5f;
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
    albedo *= 1.0f + (perBlock - 0.5f) * 0.10f + (grain - 0.5f) * 0.10f * grainFade;
    // The corner occlusion: fully open is 1, a corner in a crease is dark. The
    // curve keeps a one-block step readable without making every crease black.
    albedo *= lerp(0.42f, 1.0f, input.Occlusion * input.Occlusion * (3.0f - 2.0f * input.Occlusion));

    // The four material slots, read so the fragment layout matches every other
    // forward shader's -- bound to neutral stand-ins today.
    const float2 uv = input.FaceUv;
    albedo *= BaseColorTexture.Sample(BaseColorSampler, uv).rgb;
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
