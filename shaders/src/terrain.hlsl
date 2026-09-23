// The forward pass for GPU terrain (ADR 0071).
//
// The vertex stage is `luaug_terrain.hlsli`'s: a shared grid lifted to the
// field's heights and morphed between levels. The fragment stage builds the
// surface from the atlases at the fragment's own lattice position -- so the
// NORMAL and the MATERIAL BLEND are per pixel, at full resolution, however
// coarse the level the vertices came from -- and then hands it to the same
// `lightSurface` every other forward shader uses.
//
// **Per-pixel normals are the reason far terrain looks like terrain.** A normal
// carried per vertex would be the normal of whatever level drew it: a hill two
// kilometres away, drawn at level 6, would shade as a handful of flat facets.
// Computed here from the height atlas it shades as the field it is.
//
// **Per-pixel material blending is the reason a painted edge is a blend and
// not a staircase.** The four lattice points around the fragment each name a
// material, and their colours are mixed by the fragment's position between
// them. A vertex colour could only ever change at a triangle edge.

#define LUAUG_UNIFORMS_FRAME
#include "luaug_forward.hlsli"
#include "luaug_terrain.hlsli"
#include "luaug_terrain_surface.hlsli"

// Vertex resources. SDL_GPU puts vertex textures in space0 and vertex uniforms
// in space1 (SDL_gpu.h, SDL_CreateGPUShader).
Texture2D<float> VertexTileTable : register(t0, space0);
SamplerState VertexTileTableSampler : register(s0, space0);
Texture2D<float> VertexHeights : register(t1, space0);
SamplerState VertexHeightsSampler : register(s1, space0);
Texture2D<float> VertexMaterials : register(t2, space0);
SamplerState VertexMaterialsSampler : register(s2, space0);

cbuffer GpuTerrainUniforms : register(b0, space1)
{
    column_major float4x4 ViewProjection;
    TerrainParams Node;
};

// Fragment resources, after the thirteen `luaug_forward.hlsli` declares.
Texture2D<float> TileTable : register(t13, space2);
SamplerState TileTableSampler : register(s13, space2);
Texture2D<float> Heights : register(t14, space2);
SamplerState HeightsSampler : register(s14, space2);
Texture2D<float> Materials : register(t15, space2);
SamplerState MaterialsSampler : register(s15, space2);

// Per terrain rather than per node. Mirrors `GpuTerrainSurfaceUniforms`.
cbuffer GpuTerrainSurfaceUniforms : register(b1, space3)
{
    // Linear colour per material id. 32 entries; an id past them wraps.
    float4 Palette[32];
    // Only `Atlas` and `NodeRelative.w` (the lattice step) are read.
    TerrainParams Field;
};

struct VertexInput
{
    float2 Grid : TEXCOORD0;
};

struct TerrainInterpolants
{
    float3 ShadingPosition : TEXCOORD0;
    float2 Lattice : TEXCOORD1;
    float Hole : TEXCOORD2;
    float ViewDepth : TEXCOORD3;
    // The height in field space, for the surface detail.
    float Height : TEXCOORD4;
    float4 Position : SV_Position;
};

TerrainInterpolants VertexMain(VertexInput input)
{
    const TerrainVertex vertex = terrainVertex(VertexTileTable, VertexTileTableSampler, VertexHeights, VertexHeightsSampler,
                                                VertexMaterials, VertexMaterialsSampler, Node, input.Grid);

    TerrainInterpolants output;
    output.ShadingPosition = vertex.Position;
    // `precise` for the reason `terrainVertex` gives: `terrain_depth.hlsl`
    // computes the same position, and this one is tested against it.
    precise const float4 clipPosition = mul(ViewProjection, float4(vertex.Position, 1.0f));
    output.Position = clipPosition;
    output.ViewDepth = output.Position.w;
    output.Lattice = vertex.Lattice;
    output.Hole = vertex.Hole;
    output.Height = vertex.Height;
    return output;
}

// The height at a lattice point, falling back to `fallback` where the field
// has no ground -- no tile, or a column with nothing in it -- so a normal at the
// edge of the ground is the edge's own slope rather than a cliff down to
// whatever an empty column holds.
float heightOr(int2 lattice, float fallback)
{
    bool present;
    const float height = terrainHeight(TileTable, TileTableSampler, Heights, HeightsSampler, Field, lattice, present);
    if (!present)
        return fallback;
    const uint id = terrainMaterialByte(TileTable, TileTableSampler, Materials, MaterialsSampler, Field, lattice) & 0x7Fu;
    return id != 0u ? height : fallback;
}

// The material id at a lattice point, or `fallback` where there is none.
uint materialOr(int2 lattice, uint fallback)
{
    const uint id = terrainMaterialByte(TileTable, TileTableSampler, Materials, MaterialsSampler, Field, lattice) & 0x7Fu;
    return id != 0u ? id : fallback;
}

// The normal at a lattice point, by forward differences to its +x and +z
// neighbours.
float3 latticeNormal(int2 lattice, float here, float step)
{
    const float east = heightOr(lattice + int2(1, 0), here);
    const float north = heightOr(lattice + int2(0, 1), here);
    return normalize(float3(here - east, step, here - north));
}

float4 FragmentMain(TerrainInterpolants input) : SV_Target0
{
    // Any triangle with a hole corner is gone, where the field ends; and the
    // ground opens for a cave, pixel by pixel.
    clip(0.001f - input.Hole);
    if (terrainCaveAt(TileTable, TileTableSampler, Materials, MaterialsSampler, Field, input.Lattice))
        discard;

    const float step = Field.NodeRelative.w;
    const float2 cell = floor(input.Lattice);
    const float2 f = input.Lattice - cell;
    const int2 base = int2(cell);

    // The four lattice points around the fragment, and a normal at each,
    // blended bilinearly -- so the surface's shading is continuous across
    // every cell rather than faceted per triangle.
    const float h00 = heightOr(base, 0.0f);
    const float h10 = heightOr(base + int2(1, 0), h00);
    const float h01 = heightOr(base + int2(0, 1), h00);
    const float h11 = heightOr(base + int2(1, 1), h00);
    const float3 n00 = latticeNormal(base, h00, step);
    const float3 n10 = latticeNormal(base + int2(1, 0), h10, step);
    const float3 n01 = latticeNormal(base + int2(0, 1), h01, step);
    const float3 n11 = latticeNormal(base + int2(1, 1), h11, step);
    const float3 normal = normalize(lerp(lerp(n00, n10, f.x), lerp(n01, n11, f.x), f.y));

    // The material, the same way. A cave's flag bit is not a colour, and a
    // lattice point with no ground -- past the edge of the world, or dug out --
    // takes its neighbour's material rather than id zero's, or the last cell
    // before every edge would blend towards a colour nothing is made of.
    const uint m00 = materialOr(base, 1u);
    const uint m10 = materialOr(base + int2(1, 0), m00);
    const uint m01 = materialOr(base + int2(0, 1), m00);
    const uint m11 = materialOr(base + int2(1, 1), m00);
    const float3 c00 = Palette[m00 & 31u].rgb;
    const float3 c10 = Palette[m10 & 31u].rgb;
    const float3 c01 = Palette[m01 & 31u].rgb;
    const float3 c11 = Palette[m11 & 31u].rgb;
    float3 albedo = lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);

    // Colour variation, rock on steep ground and grain: the shared look
    // (`luaug_terrain_surface.hlsli`), so a cave mesh beside this matches it.
    const uint dominant = f.x < 0.5f ? (f.y < 0.5f ? m00 : m01) : (f.y < 0.5f ? m10 : m11);
    const float3 ground = float3(input.Lattice.x * step, input.Height, input.Lattice.y * step);
    const TerrainDetail surfaceDetail = terrainDetail(
        albedo, ground, normal, dominant == LUAUG_TERRAIN_ROCK || dominant == LUAUG_TERRAIN_BASALT,
        Palette[LUAUG_TERRAIN_ROCK].rgb);
    albedo = surfaceDetail.Albedo;

    // The four material slots are bound -- to white, flat, white and black
    // today -- and read, so the terrain has the same fragment layout as every
    // other forward shader and a texture set can arrive without one.
    const float2 uv = input.Lattice * step * 0.25f;
    albedo *= BaseColorTexture.Sample(BaseColorSampler, uv).rgb;
    const float roughness = 0.92f * MetallicRoughnessTexture.Sample(MetallicRoughnessSampler, uv).g;
    float3 detail = NormalTexture.Sample(NormalSampler, uv).xyz * 2.0f - 1.0f;
    detail.xy += surfaceDetail.NormalNudge;
    const float3x3 frame = tangentFrame(normal, float4(1.0f, 0.0f, 0.0f, 1.0f));
    const float3 shadingNormal = normalize(mul(normalize(detail), frame));

    Surface surface = makeSurface(input.ShadingPosition, shadingNormal, albedo, 0.0f, roughness);
    float3 color = lightSurface(surface, input.ShadingPosition, shadingNormal, input.ViewDepth, input.Position.xy);
    color += EmissiveTexture.Sample(EmissiveSampler, uv).rgb;
    color = applyFog(color, FogColor.rgb, FogRange, length(input.ShadingPosition));
    return float4(color, 1.0f);
}
