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
// them resident. The ground is opened there PER PIXEL (`terrainCaveAt`), which
// opens it exactly where the cave mesh fills it in at every level of detail --
// a per-vertex test only lines up with the columns at the finest level, which
// is what kept caves from being drawn more than a stone's throw away.

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
    // start), w: how far to push clip depth away from the light, in a shadow
    // pass; zero everywhere else.
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

// **Whether a cave opens the ground at this lattice position**, for the
// fragment stages. Exactly the triangles the finest level's grid would lose to
// a flagged corner -- the grid splits each cell along the diagonal from (1, 0)
// to (0, 1), so the position picks one of the two and its three corners are
// asked -- which is the footprint every cave mesh was built to cover.
bool terrainCaveAt(Texture2D<float> tileTable, SamplerState tileSampler, Texture2D<float> materials,
                   SamplerState materialSampler, TerrainParams params, float2 lattice)
{
    const float2 cellCorner = floor(lattice);
    const int2 cell = int2(cellCorner);
    const float2 f = lattice - cellCorner;
    // The corner the fragment's triangle has and the other one lacks.
    const int2 own = (f.x + f.y < 1.0f) ? cell : cell + int2(1, 1);
    const uint a = terrainMaterialByte(tileTable, tileSampler, materials, materialSampler, params, own);
    const uint b = terrainMaterialByte(tileTable, tileSampler, materials, materialSampler, params, cell + int2(1, 0));
    const uint c = terrainMaterialByte(tileTable, tileSampler, materials, materialSampler, params, cell + int2(0, 1));
    return ((a | b | c) & 0x80u) != 0u;
}

struct TerrainVertex
{
    // Relative to the viewer, in metres.
    float3 Position;
    // The lattice coordinate the vertex ended at, for the fragment stage.
    float2 Lattice;
    // Its height in the field's own space, metres: with `Lattice`, the
    // position the surface detail is pinned to.
    float Height;
    // 1 where the vertex is over no ground; any triangle touching one is
    // discarded. Caves are not holes here: they open per pixel.
    float Hole;
};

TerrainVertex terrainVertex(Texture2D<float> tileTable, SamplerState tileSampler, Texture2D<float> heights,
                            SamplerState heightSampler, Texture2D<float> materials, SamplerState materialSampler,
                            TerrainParams params, float2 grid)
{
    const float step = params.NodeLattice.z;
    const float voxel = params.NodeRelative.w;
    const int2 corner = int2(params.NodeLattice.xy);

    // The two lattice points this vertex lies between: its own, and the even
    // one it slides onto as it morphs to the next level's grid.
    const float2 odd = frac(grid * 0.5f) * 2.0f;
    const int2 fine = corner + int2(grid * step);
    const int2 coarse = corner + int2((grid - odd) * step);
    bool finePresent;
    bool coarsePresent;
    float fineHeight = terrainHeight(tileTable, tileSampler, heights, heightSampler, params, fine, finePresent);
    float coarseHeight = terrainHeight(tileTable, tileSampler, heights, heightSampler, params, coarse, coarsePresent);
    const uint fineMaterial = terrainMaterialByte(tileTable, tileSampler, materials, materialSampler, params, fine);
    const uint coarseMaterial =
        terrainMaterialByte(tileTable, tileSampler, materials, materialSampler, params, coarse);
    const bool fineGround = finePresent && (fineMaterial & 0x7Fu) != 0u;
    const bool coarseGround = coarsePresent && (coarseMaterial & 0x7Fu) != 0u;
    // **Heights are only taken from ground.** A lattice point with none -- no
    // tile, or a column dug to nothing -- holds a height that means
    // nothing: zero, or the world's floor. A vertex just past the edge of the
    // ground stops being a hole once it has slid more than halfway onto an even
    // neighbour that is ground; had it kept its own height it would hang a sheer
    // sliver down to the floor, and every edge of the ground drew a curtain
    // wherever a level of detail was mid-morph. So each end borrows the other's
    // height when it has no ground of its own.
    if (!fineGround && coarseGround)
        fineHeight = coarseHeight;
    if (!coarseGround)
        coarseHeight = fineHeight;

    // **Everything from here to the position is `precise`.** Two shaders run
    // this function -- the colour pass and the depth prepass it is tested
    // against -- and the compiler is otherwise free to fuse and reorder it
    // differently in each, because what else each shader does differs. A
    // position a rounding behind the prepass's fails `LessOrEqual`, and the
    // ground showed the sky through in specks wherever it did.
    //
    // Where the vertex would be unmorphed, for the distance the morph is
    // measured by.
    precise const float3 unmorphed =
        float3(params.NodeRelative.x + grid.x * step * voxel, params.NodeRelative.y + fineHeight,
               params.NodeRelative.z + grid.y * step * voxel);
    precise const float morph = saturate((length(unmorphed) - params.Morph.x) * params.Morph.z);

    // Odd vertices slide onto their even neighbour; at `morph == 1` the grid
    // is the next level's, with degenerate triangles where the odd rows were.
    precise const float2 morphed = grid - odd * morph;
    precise const float height = lerp(fineHeight, coarseHeight, morph);

    TerrainVertex vertex;
    precise const float3 position = float3(params.NodeRelative.x + morphed.x * step * voxel, params.NodeRelative.y + height,
                             params.NodeRelative.z + morphed.y * step * voxel);
    vertex.Position = position;
    vertex.Lattice = float2(corner) + morphed * step;
    vertex.Height = height;

    // Material zero is a column with no ground in it (D153): dug out to
    // nothing, or never filled in a tile that holds other ground.
    vertex.Hole = (morph < 0.5f ? fineGround : coarseGround) ? 0.0f : 1.0f;
    return vertex;
}

#endif // LUAUG_TERRAIN_HLSLI
