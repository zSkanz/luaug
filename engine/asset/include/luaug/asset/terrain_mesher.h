#pragma once

// Turning a `TerrainField` into triangles (ADR 0082).
//
// **The isosurface is extracted as a SURFACE NET** over occupancy: one vertex
// in each cell of the lattice of voxel centres that the surface passes through,
// at the mean of the crossings on that cell's edges, and one quad around each
// lattice edge the surface crosses, joining the four cells that share it. The
// surface is where occupancy crosses one half.
//
// It replaced marching tetrahedra in 2026-09: six tetrahedra round each cube's
// diagonal put that diagonal into every curved wall as a zig-zag. A surface net
// has no table, no ambiguous case and no preferred direction.
//
// **A region owns lattice points, and two regions side by side agree.** A region
// owns the points `[min, min + n)` on each axis and emits the quads of the edges
// that START on a point it owns. It builds cells from `min - 1`, so every such
// quad has its four cells -- and the ring of cells at `min - 1` is the ring its
// neighbour builds as its last, from the same samples, into the same vertices.
// So meshing the world a region at a time is watertight with no stitching.

#include "luaug/asset/model.h"
#include "luaug/asset/terrain.h"
#include "luaug/core/types.h"

#include <optional>
#include <utility>
#include <vector>

namespace luaug::asset {

// What to mesh, and how finely. Every coordinate is in the voxels of `level`:
// a level-L index is the level-0 index divided by `2^L`.
struct MeshRegion
{
    // The first lattice point the region owns, on each axis.
    core::i32 minX = 0;
    core::i32 minY = 0;
    core::i32 minZ = 0;

    // How many lattice points it owns on each axis.
    core::u32 cellsX = 0;
    core::u32 cellsY = 0;
    core::u32 cellsZ = 0;

    // **The level of detail.** Zero reads the voxels; level L reads each
    // chunk's level-L mip, the mean of `2^L` voxels a side, so a node far away
    // has an eighth of the triangles per level and a thin wall thins rather
    // than vanishing.
    core::u32 level = 0;

    // **How far to hang a skirt from the region's four sides, in metres.** Zero
    // hangs none.
    //
    // Two neighbouring nodes meshed at different levels disagree along their
    // shared side by up to a fraction of the coarse node's cell, and daylight
    // shows through. A strip hung from every boundary edge along the negative
    // normal fills it from behind -- under flat ground and into the rock behind
    // a cliff -- and is invisible from anywhere that can see the surface.
    //
    // **The collider never gets a skirt.** A wall under every edge of collision
    // is something a character could stand on inside a cave.
    float skirt = 0.0f;
};

// The triangles, and what a collider needs from them.
struct TerrainMesh
{
    // Ready for `MeshCache`: 48-byte vertices and u32 indices, in the field's
    // own metres. Each vertex carries its material in the tangent's x and how
    // much sky it sees in its y -- the terrain shader reads both.
    Mesh mesh;

    // What each submesh is made of, parallel to `mesh.submeshes`: one section
    // per material, in id order.
    std::vector<core::u8> sectionMaterials;

    // The same surface as a plain position list and index triples, skirts left
    // out, which is what `ShapeType::TriangleMesh` takes.
    std::vector<core::Vec3> colliderPoints;
    std::vector<core::u32> colliderIndices;
};

// Extracts the surface of `field` over `region`.
[[nodiscard]] TerrainMesh meshField(const TerrainField& field, const MeshRegion& region);

// **The runs of level-0 voxel rows a column of chunks can have a surface in**,
// lowest first and each inclusive, over the chunk columns from (`chunkX`,
// `chunkZ`) `across` wide and a ring of one chunk round them. A layer of chunks
// can when one of them is not all one value, or is solid with something other
// than solid ground against any face: air above, the air past the terrain's
// edge beside it, nothing under it -- which is what gives terrain walls and a
// bottom of one kind all round.
//
// What a renderer meshes a whole column of chunks by, one run at a time,
// without walking the solid rock between its surface and its bottom.
[[nodiscard]] std::vector<std::pair<core::i32, core::i32>> activeRuns(const TerrainField& field, core::i32 chunkX,
                                                                      core::i32 chunkZ, core::i32 across);

// The lowest and highest rows of `activeRuns`, or nothing when there are none.
[[nodiscard]] std::optional<std::pair<core::i32, core::i32>> activeRows(const TerrainField& field, core::i32 chunkX,
                                                                        core::i32 chunkZ, core::i32 across);

// Appends one mesh to another, keeping one section per material in id order.
void appendMesh(TerrainMesh& into, const TerrainMesh& from);

} // namespace luaug::asset
