#pragma once

// Turning a `TerrainField` into triangles (ADR 0067, F1 B2).
//
// **The isosurface is extracted by marching TETRAHEDRA, not marching cubes, and
// that is a deviation from the plan with a reason.** Everything ADR 0067 claims
// is preserved -- vertices land on edge crossings by linear interpolation, so
// `sd(p) = p.y - H(x, z)` still puts the vertical crossing at exactly `y = H`,
// and the boundary between the two encodings is still an equality rather than a
// stitch. What changes is how a cell is subdivided before that happens.
//
// Three reasons, in the order they decided it:
//
//   * **Marching cubes needs a 256-entry triangulation table**, and that table
//     is somebody else's work to vendor -- which R5 and R6 make a decision with
//     an ADR rather than an `#include`. Deriving it from the fifteen base cases
//     under the cube's symmetry group is real work with a silent failure mode.
//     A tetrahedron has sixteen cases and every one of them is derivable in a
//     few lines, which is clean-room by construction.
//   * **A tetrahedron has no ambiguous face.** Marching cubes' ambiguous cases
//     are the reason two neighbouring cells can disagree about whether a surface
//     connects, which is a crack -- and a crack in terrain is a hole somebody
//     falls through. There is no such case here.
//   * **The winding is derived from the field rather than from a table.** A
//     triangle's normal is flipped to agree with the gradient, so "which way
//     does this face" stops being a thing a table can get backwards. That class
//     of bug already cost this milestone one failing test in the physics seam.
//
// **The cost is honest and recorded**: roughly twice the triangles of marching
// cubes, and worse-shaped ones. The A3 bench puts a 32,000-triangle collider at
// 12 ms to build, so this matters for a bricked cell and not for a
// height-encoded one, whose collider is a height field. If the count becomes the
// problem, the fix is the derived marching-cubes table, and it is a change to
// this file alone.

#include "luaug/asset/model.h"
#include "luaug/asset/terrain.h"
#include "luaug/core/types.h"

namespace luaug::asset {

// What to mesh, and how finely.
struct MeshRegion
{
    // The lattice point the region starts at, inclusive.
    core::i32 minX = 0;
    core::i32 minY = 0;
    core::i32 minZ = 0;

    // How many lattice CELLS on each axis. A cell spans one lattice step, so
    // meshing `n` cells reads `n + 1` samples.
    core::u32 cellsX = 0;
    core::u32 cellsY = 0;
    core::u32 cellsZ = 0;

    // **The LOD stride, in lattice steps.** One is full detail; two reads every
    // other sample and produces a quarter of the triangles.
    //
    // A residency decision rather than a renderer one: `selectMeshLod` picks per
    // draw from camera distance, and two neighbouring cells picking different
    // levels on different frames is a crack that appears and disappears. So the
    // level is baked into what was meshed.
    core::u32 stride = 1;
};

// The triangles, and what a collider needs from them.
struct TerrainMesh
{
    // Ready for `MeshCache`: 48-byte vertices and u32 indices, the layout every
    // world shader hardcodes.
    Mesh mesh;

    // The same surface as a plain position list and index triples, which is what
    // `ShapeType::TriangleMesh` takes. Shared with `mesh` rather than duplicated
    // would mean the collider tracked the render mesh's LOD, and a collider that
    // gets coarser as you walk away is a character that falls through the world.
    std::vector<core::Vec3> colliderPoints;
    std::vector<core::u32> colliderIndices;
};

// Extracts the isosurface of `field` over `region`.
//
// Watertight within the region by construction: two tetrahedra sharing a face
// see the same two samples on every edge of it, so they place the same vertex,
// and the vertex cache below makes it literally the same index.
[[nodiscard]] TerrainMesh meshField(const TerrainField& field, const MeshRegion& region);

} // namespace luaug::asset
