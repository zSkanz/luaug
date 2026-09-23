// GPU terrain into a depth-only target: the sun's cascades, the local shadow
// atlas and the camera's depth prepass (ADR 0071).
//
// The same vertex stage as `terrain.hlsl`, so a shadow is cast by exactly the
// surface that is drawn -- the same morph, the same level, the same holes. The
// fragment stage exists only to discard the holes and the cave openings: a cave
// opening that still cast a shadow would be a patch of darkness over a hole in
// the ground, and one still in the prepass would hide the cave behind it.

#include "luaug_terrain.hlsli"

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

// Fragment resources: what a cave opening is read from. Only `Atlas` and
// `AtlasSize` of the block are read.
Texture2D<float> TileTable : register(t0, space2);
SamplerState TileTableSampler : register(s0, space2);
Texture2D<float> Materials : register(t1, space2);
SamplerState MaterialsSampler : register(s1, space2);

cbuffer GpuTerrainFieldUniforms : register(b0, space3)
{
    TerrainParams Field;
};

struct VertexInput
{
    float2 Grid : TEXCOORD0;
};

struct Interpolants
{
    float Hole : TEXCOORD0;
    float2 Lattice : TEXCOORD1;
    float4 Position : SV_Position;
};

Interpolants VertexMain(VertexInput input)
{
    const TerrainVertex vertex = terrainVertex(VertexTileTable, VertexTileTableSampler, VertexHeights, VertexHeightsSampler,
                                                VertexMaterials, VertexMaterialsSampler, Node, input.Grid);
    Interpolants output;
    // `precise`: the colour pass is tested against this one's depth.
    precise const float4 clipPosition = mul(ViewProjection, float4(vertex.Position, 1.0f));
    output.Position = clipPosition;
    // Away from the light, in a cascade (see the renderer's cascade loop); zero
    // in the camera's prepass. Scaled by w so it is the same depth offset for a
    // perspective projection as for an orthographic one.
    output.Position.z += Node.Morph.w * output.Position.w;
    output.Hole = vertex.Hole;
    output.Lattice = vertex.Lattice;
    return output;
}

void FragmentMain(Interpolants input)
{
    clip(0.001f - input.Hole);
    if (terrainCaveAt(TileTable, TileTableSampler, Materials, MaterialsSampler, Field, input.Lattice))
        discard;
}
