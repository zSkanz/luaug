// The forward pass for terrain (ADR 0082): every node of every terrain's
// level-of-detail quadtree, meshed from the voxels on the CPU.
//
// **The material comes per VERTEX** -- the mesher writes its id into the
// tangent's x, and how much sky the vertex sees into its y -- and is turned
// into a palette colour in the vertex stage, so it blends across a triangle
// rather than changing at its edge. The variation, the rock and the grain are
// procedural (`luaug_terrain_surface.hlsli`), pinned to the field's own
// coordinates.
//
// The vertex layout is `asset::Vertex`, the same 48 bytes every static mesh
// uses, so a node travels through `MeshCache` like any other mesh.

#define LUAUG_UNIFORMS_OBJECT
#define LUAUG_UNIFORMS_FRAME
#include "luaug_forward.hlsli"
#include "luaug_terrain_surface.hlsli"

// Per frame: a colour per material id.
cbuffer GpuTerrainSurfaceUniforms : register(b1, space3)
{
    float4 Palette[32];
};

// **The same block again, for the vertex stage**, which is where a vertex's
// material becomes a colour. SDL_GPU gives each stage its own uniform space, so
// a vertex shader cannot read the fragment stage's block; the renderer pushes
// the one block to both.
cbuffer GpuTerrainVertexPalette : register(b1, space1)
{
    float4 VertexPalette[32];
};

struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float4 Tangent : TEXCOORD2;
};

struct TerrainInterpolants
{
    float3 ShadingPosition : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float3 Albedo : TEXCOORD2;
    // Field space: the coordinate the ground's noise is pinned to.
    float3 Ground : TEXCOORD3;
    float ViewDepth : TEXCOORD4;
    // 1 where the vertex is rock already, so the slope rule leaves it alone.
    float Rock : TEXCOORD5;
    // How much of the sky the vertex sees (`skyVisibility` in the mesher).
    float Sky : TEXCOORD6;
    float4 Position : SV_Position;
};

TerrainInterpolants VertexMain(VertexInput input)
{
    TerrainInterpolants output;
    const float4 shadingPosition = mul(Model, float4(input.Position, 1.0f));
    output.ShadingPosition = shadingPosition.xyz;
    output.Position = mul(ViewProjection, shadingPosition);
    output.ViewDepth = output.Position.w;
    output.Normal = mul((float3x3)NormalMatrix, input.Normal);
    const uint material = uint(input.Tangent.x + 0.5f) & 31u;
    output.Albedo = VertexPalette[material].rgb;
    output.Rock = (material == LUAUG_TERRAIN_ROCK || material == LUAUG_TERRAIN_BASALT) ? 1.0f : 0.0f;
    // The mesher works in the field's own space, so the untransformed
    // position IS the field coordinate.
    output.Ground = input.Position.xyz;
    // The mesher's sky visibility rides in the tangent's y.
    output.Sky = saturate(input.Tangent.y);
    return output;
}

float4 FragmentMain(TerrainInterpolants input) : SV_Target0
{
    const float3 normal = normalize(input.Normal);
    const TerrainDetail surfaceDetail =
        terrainDetail(input.Albedo, input.Ground, normal, input.Rock > 0.5f, Palette[LUAUG_TERRAIN_ROCK].rgb);

    // The four material slots, read as the ground reads them.
    const float2 uv = input.Ground.xz * 0.25f;
    const float3 albedo = surfaceDetail.Albedo * BaseColorTexture.Sample(BaseColorSampler, uv).rgb;
    const float roughness = 0.92f * MetallicRoughnessTexture.Sample(MetallicRoughnessSampler, uv).g;
    float3 detail = NormalTexture.Sample(NormalSampler, uv).xyz * 2.0f - 1.0f;
    detail.xy += surfaceDetail.NormalNudge;
    const float3x3 frame = tangentFrame(normal, float4(1.0f, 0.0f, 0.0f, 1.0f));
    const float3 shadingNormal = normalize(mul(normalize(detail), frame));

    Surface surface = makeSurface(input.ShadingPosition, shadingNormal, albedo, 0.0f, roughness);
    float3 color =
        lightSurface(surface, input.ShadingPosition, shadingNormal, input.ViewDepth, input.Position.xy, input.Sky);
    color += EmissiveTexture.Sample(EmissiveSampler, uv).rgb;
    color = applyFog(color, FogColor.rgb, FogRange, length(input.ShadingPosition));
    return float4(color, 1.0f);
}
