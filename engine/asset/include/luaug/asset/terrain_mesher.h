#pragma once

// Turning a `TerrainField` into triangles (ADR 0067, F1 B2).
//
// **The isosurface is extracted as a SURFACE NET** (since 2026-09-22): one
// vertex in each cell the surface passes through, at the mean of the crossings
// on that cell's edges, and one quad around each lattice edge the surface
// crosses, joining the four cells that share it.
//
// It replaced marching tetrahedra, which the first version used for three good
// reasons -- no 256-entry table to vendor or derive, no ambiguous faces, and
// winding taken from the field -- and one bad result nobody weighed until a cave
// was looked at: six tetrahedra around each cube's main diagonal put that
// diagonal into the surface, and every curved wall became a zig-zag of long thin
// triangles leaning one way. A surface net keeps all three good reasons (it has
// no table, no ambiguous case, and its winding still comes from the gradient)
// and has no preferred direction, with about a third of the triangles.
//
// **What it does not do is put vertices on lattice edges.** `sd = y - H` no
// longer lands a vertex at exactly `y = H`; it lands at the mean of the heights
// round the cell, which is the same surface to within the lattice's own
// resolution. Nothing depends on the exact equality any more: the height layer
// is drawn from its atlas on the GPU (ADR 0071), and this meshes caves, which
// overlap the ground they replace by a cell.

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

    // **How far below the region's side edges to hang a skirt, in metres.**
    // Zero, the default, hangs none.
    //
    // A skirt is the standard answer to the crack between two neighbouring
    // chunks meshed at different strides -- chunked LOD, HTerrain and most voxel
    // terrains ship it. The coarse chunk's edge vertices interpolate the field at
    // half the density its fine neighbour does, so the two edges disagree by up
    // to a fraction of a cell and daylight shows through the seam. A strip of
    // wall hung straight down from every boundary edge fills that gap from
    // below, and because it is vertical it is invisible from anywhere that can
    // see the surface.
    //
    // The alternative that removes the crack rather than covering it is
    // Transvoxel's transition cells, which stitch a half-resolution face to a
    // full-resolution one exactly. It is the right answer for a pure voxel
    // terrain and the expensive one; skirts cost two triangles an edge and work
    // identically over the height layer and the bricks.
    //
    // **The collider never sees a skirt.** It is a rendering device, and a wall
    // of collision hanging under every tile edge would be something a
    // character could stand on inside a cave.
    float skirt = 0.0f;

    // **Which of the region's four sides meet a height field drawn elsewhere**,
    // as bits: 1 low x, 2 high x, 4 low z, 8 high z. On such a side the
    // outermost ring of cells puts its vertex ON the lattice point just inside
    // the region's edge, at the height layer's own height there -- exactly where
    // the height field's last vertex is. The two surfaces then meet vertex for
    // vertex instead of overlapping, which a surface net (whose vertices sit at
    // cell centres) cannot do on its own. A cave's mesh uses this on every side
    // that borders ground drawn from the atlas (ADR 0071).
    core::u8 snapSides = 0;
};

// The triangles, and what a collider needs from them.
struct TerrainMesh
{
    // Ready for `MeshCache`: 48-byte vertices and u32 indices, the layout every
    // world shader hardcodes.
    Mesh mesh;

    // What each submesh is made of, parallel to `mesh.submeshes`.
    //
    // **The material does not live in the vertex**, because `Vertex` is a GPU
    // buffer layout whose size is asserted and every world shader hardcodes it.
    // It lives here, one per section, which is the same shape a compiled mesh
    // already uses for its own materials -- so the renderer resolves a colour
    // exactly as it does for a `MeshPart`.
    std::vector<core::u8> sectionMaterials;

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
