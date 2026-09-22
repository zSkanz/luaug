// GPU terrain: heights from an atlas, a shared grid, and a morph (ADR 0071).
//
// **Nothing about the ground is a mesh.** Every node of the quadtree
// (`engine/render/include/luaug/render/terrain_lod.h`) draws the same flat 32 by
// 32 grid; this file lifts each vertex to the height the field has at that
// lattice point, and slides it towards the next-coarser level's grid as it nears
// the edge of its level's distance band -- the continuous morph that lets two
// levels meet with no crack and no skirt.
//
// **Where heights live.** Each 32 by 32 tile of the field has a slot in an
// `R32Float` atlas (`HeightAtlas`) and in an `R8Unorm` one (`MaterialAtlas`,
// the tile's material ids). `TileTable` answers which slot a tile key has, or
// -1 for a tile the field does not hold. Every read is a POINT sample at a
// texel centre -- a filtered read would bleed one slot into the next -- and any
// interpolation is done here, by hand. A point sample rather than a `Load`,
// because a texture only ever read by `Load` compiles to a storage texture, and
// the RHI binds sampled ones (the clustered-light tables do the same). Each
// texture is read through ITS OWN sampler register: SDL_GPU pairs `tN` with
// `sN`, and a texture read through another's sampler is not a sampled one.
//
// **A material byte is the id in its low seven bits and, in its high bit, the
// cave flag**: this column carries voxel bricks, and the renderer has a mesh of
// them resident. A triangle touching a flagged vertex is discarded, which opens
// the ground exactly where the cave mesh fills it in.

#ifndef LUAUG_TERRAIN_HLSLI
#define LUAUG_TERRAIN_HLSLI

#define LUAUG_TERRAIN_TILE 32

// The block both stages read, bound per node. Mirrors `GpuTerrainUniforms` in
// `engine/render/include/luaug/render/shader_types.h`.
struct TerrainParams
{
    // xyz: the node's lattice corner, in metres relative to the viewer (y is
    // the terrain's origin height). w: the lattice step in metres.
    float4 NodeRelative;
    // xy: the node's lattice corner, in lattice steps. z: lattice steps per grid
    // step (2^level). w: unused.
    float4 NodeLattice;
    // x: morph start, y: morph end (metres from the viewer), z: 1 / (end -
    // start), w: unused.
    float4 Morph;
    // x: slots per atlas row, y: the tile table's edge, zw: the tile key that
    // table entry (0, 0) holds.
    float4 Atlas;
    // xy: the atlas's size in texels, zw: 1 / that.
    float4 AtlasSize;
};

float terrainFetch(Texture2D<float> texture, SamplerState pointSampler, int2 texel, float2 inverseSize)
{
    return texture.SampleLevel(pointSampler, (float2(texel) + 0.5f) * inverseSize, 0.0f);
}

int2 terrainFloorDivide(int2 value, int divisor)
{
    return int2(floor(float2(value) / float(divisor)));
}

// The atlas texel holding lattice point `lattice`, or false when no tile does.
bool terrainAtlasTexel(Texture2D<float> tileTable, SamplerState pointSampler, TerrainParams params, int2 lattice,
                       out int2 texel)
{
    const int2 tile = terrainFloorDivide(lattice, LUAUG_TERRAIN_TILE);
    const int2 local = lattice - tile * LUAUG_TERRAIN_TILE;
    const int2 entry = tile - int2(params.Atlas.zw);
    const int edge = int(params.Atlas.y);
    texel = int2(0, 0);
    if (any(entry < 0) || any(entry >= edge))
        return false;
    const float slot = terrainFetch(tileTable, pointSampler, entry, 1.0f / float2(edge, edge));
    if (slot < 0.0f)
        return false;
    const uint index = uint(slot + 0.5f);
    const uint perRow = uint(params.Atlas.x);
    texel = int2(int(index % perRow), int(index / perRow)) * LUAUG_TERRAIN_TILE + local;
    return true;
}

// The height at a lattice point, and whether there is ground there at all.
float terrainHeight(Texture2D<float> tileTable, SamplerState tileSampler, Texture2D<float> heights,
                    SamplerState heightSampler, TerrainParams params, int2 lattice, out bool present)
{
    int2 texel;
    present = terrainAtlasTexel(tileTable, tileSampler, params, lattice, texel);
    return present ? terrainFetch(heights, heightSampler, texel, params.AtlasSize.zw) : 0.0f;
}

// The material byte at a lattice point: id | 0x80 when a cave opens here.
uint terrainMaterialByte(Texture2D<float> tileTable, SamplerState tileSampler, Texture2D<float> materials,
                         SamplerState materialSampler, TerrainParams params, int2 lattice)
{
    int2 texel;
    if (!terrainAtlasTexel(tileTable, tileSampler, params, lattice, texel))
        return 0u;
    return uint(terrainFetch(materials, materialSampler, texel, params.AtlasSize.zw) * 255.0f + 0.5f);
}

struct TerrainVertex
{
    // Relative to the viewer, in metres.
    float3 Position;
    // The lattice coordinate the vertex ended at, for the fragment stage.
    float2 Lattice;
    // 1 where the vertex is inside a cave opening or over no ground; any
    // triangle touching one is discarded.
    float Hole;
};

TerrainVertex terrainVertex(Texture2D<float> tileTable, SamplerState tileSampler, Texture2D<float> heights,
                            SamplerState heightSampler, Texture2D<float> materials, SamplerState materialSampler,
                            TerrainParams params, float2 grid)
{
    const float step = params.NodeLattice.z;
    const float voxel = params.NodeRelative.w;
    const int2 corner = int2(params.NodeLattice.xy);

    // Where the vertex would be unmorphed, for the distance the morph is
    // measured by.
    const int2 fine = corner + int2(grid * step);
    bool finePresent;
    const float fineHeight = terrainHeight(tileTable, tileSampler, heights, heightSampler, params, fine, finePresent);
    const float3 unmorphed =
        float3(params.NodeRelative.x + grid.x * step * voxel, params.NodeRelative.y + fineHeight,
               params.NodeRelative.z + grid.y * step * voxel);
    const float morph = saturate((length(unmorphed) - params.Morph.x) * params.Morph.z);

    // Odd vertices slide onto their even neighbour; at `morph == 1` the grid
    // is the next level's, with degenerate triangles where the odd rows were.
    const float2 odd = frac(grid * 0.5f) * 2.0f;
    const float2 morphed = grid - odd * morph;
    const int2 coarse = corner + int2((grid - odd) * step);
    bool coarsePresent;
    const float coarseHeight = terrainHeight(tileTable, tileSampler, heights, heightSampler, params, coarse, coarsePresent);
    const float height = lerp(fineHeight, coarseHeight, morph);

    TerrainVertex vertex;
    vertex.Position = float3(params.NodeRelative.x + morphed.x * step * voxel, params.NodeRelative.y + height,
                             params.NodeRelative.z + morphed.y * step * voxel);
    vertex.Lattice = float2(corner) + morphed * step;

    const int2 settled = morph < 0.5f ? fine : coarse;
    const bool present = morph < 0.5f ? finePresent : coarsePresent;
    const uint material = terrainMaterialByte(tileTable, tileSampler, materials, materialSampler, params, settled);
    // Material zero is a column with no ground in it (D153): dug out to
    // nothing, or never filled in a tile that holds other ground.
    vertex.Hole = (!present || (material & 0x80u) != 0u || (material & 0x7Fu) == 0u) ? 1.0f : 0.0f;
    return vertex;
}

#endif // LUAUG_TERRAIN_HLSLI
