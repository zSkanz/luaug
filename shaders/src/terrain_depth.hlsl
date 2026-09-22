// GPU terrain into a depth-only target: the sun's cascades, the local shadow
// atlas and the camera's depth prepass (ADR 0071).
//
// The same vertex stage as `terrain.hlsl`, so a shadow is cast by exactly the
// surface that is drawn -- the same morph, the same level, the same holes. The
// fragment stage exists only to discard the holes: a cave opening that still
// cast a shadow would be a patch of darkness over a hole in the ground.

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

struct VertexInput
{
    float2 Grid : TEXCOORD0;
};

struct Interpolants
{
    float Hole : TEXCOORD0;
    float4 Position : SV_Position;
};

Interpolants VertexMain(VertexInput input)
{
    const TerrainVertex vertex = terrainVertex(VertexTileTable, VertexTileTableSampler, VertexHeights, VertexHeightsSampler,
                                                VertexMaterials, VertexMaterialsSampler, Node, input.Grid);
    Interpolants output;
    output.Position = mul(ViewProjection, float4(vertex.Position, 1.0f));
    // Away from the light, in a cascade (see the renderer's cascade loop); zero
    // in the camera's prepass. Scaled by w so it is the same depth offset for a
    // perspective projection as for an orthographic one.
    output.Position.z += Node.Morph.w * output.Position.w;
    output.Hole = vertex.Hole;
    return output;
}

void FragmentMain(Interpolants input)
{
    clip(0.001f - input.Hole);
}
