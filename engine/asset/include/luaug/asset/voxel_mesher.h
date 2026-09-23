#pragma once

// Turning a chunk of blocks into triangles (V1).
//
// **Greedy meshing with hidden faces culled** -- the standard answer for block
// worlds, and the opposite problem to the terrain's mesher. A block world's
// surface is quads on lattice planes; the interesting part is merging coplanar
// faces so a flat wall of a thousand blocks is a handful of triangles rather
// than two thousand. For each axis and each direction, every slice of the chunk
// becomes a 16 x 16 mask of visible faces, and the mask is swept into the
// largest rectangles of identical faces.
//
// **Faces are identical only when their block AND their ambient occlusion
// match.** Each face corner is darkened by the blocks around it (the "smooth
// lighting" every block game has, computed from the three neighbours a corner
// touches), and merging two faces with different corner shading would smear one
// face's shadow across the other. So a merge keeps the look exact, and a flat,
// unoccluded floor still merges into one quad.

#include "luaug/asset/model.h"
#include "luaug/asset/voxel.h"
#include "luaug/core/types.h"

#include <span>
#include <vector>

namespace luaug::asset {

// How much of what is behind a block shows through it.
enum class BlockOpacity : core::u8
{
    // Nothing: a face against it is hidden, and it darkens the corners it
    // touches.
    Opaque = 0,
    // Holes, from the image's alpha: leaves, a fence, a grate. Drawn with the
    // opaque blocks, a pixel either there or not.
    Cutout = 1,
    // Blended: glass, water, ice. Drawn afterwards, over what is behind it.
    Translucent = 2,
};

// What the mesher needs to know about a block type. Indexed by `BlockId`; an id
// past the end is treated as a plain opaque block.
struct BlockLook
{
    BlockOpacity opacity = BlockOpacity::Opaque;
};

struct VoxelMesh
{
    // Positions in the grid's own space, in metres -- block coordinates times
    // the block size -- so the owner's origin is the only transform a chunk
    // needs.
    //
    // **What rides in the tangent**: x is the block id (the shader turns it into
    // a colour), y the corner's ambient occlusion from 0 (fully occluded) to 1.
    // The UV is the face's position in blocks along its two axes, for a texture
    // that tiles once per block. z is 1 on a cutout block's face, whose image
    // alpha the shader tests.
    //
    // Opaque faces.
    Mesh mesh;
    // Cutout faces -- a third mesh because the depth prepass must not draw
    // them: it has no image to test, and would write a leaf's holes as solid.
    Mesh cutout;
    // Translucent faces, for the blended pass -- a second mesh because it is a
    // second draw, after every opaque surface and without writing depth.
    Mesh translucent;

    // Every face for the collider, translucent ones included: glass is a wall.
    // Positions only; one index list.
    std::vector<core::Vec3> colliderPoints;
    std::vector<core::u32> colliderIndices;
};

[[nodiscard]] VoxelMesh meshVoxelChunk(const VoxelGrid& grid, VoxelChunkKey key, std::span<const BlockLook> looks,
                                       float blockSize);

} // namespace luaug::asset
