// The five solids a `Part` can be, as CPU geometry (roadmap M6, "solid `Part`
// rendering").
//
// **This is M4's first spent constraint.** The roadmap required that
// "engine-generated geometry must be able to reach the renderer" and that "the
// mesh path cannot assume a mesh is a handle to an imported asset". These meshes
// come from arithmetic rather than from a file, go through `MeshCache::create`
// like any other, and the renderer changes not at all -- which is the answer
// that constraint was written to get.
//
// **The tessellation is a permanent decision.** Every capture golden recorded
// after this is baked against these numbers, and changing one re-records all of
// them. They are stated as constants rather than as parameters for exactly that
// reason: a segment count that a caller could pass is a segment count two
// callers will eventually disagree about.
//
// **What `Size` means per shape, and where it differs from the collider.** The
// renderer scales one unit mesh; the physics mirror builds a shape from the same
// `Size` (engine/physics/include/luaug/physics/types.h). They agree exactly for
// `Block`, `Cylinder` and `Ball`, and `Wedge`'s collider is deliberately its
// bounding box. `Capsule` is the one that can diverge: its caps are hemispheres
// the collider never stretches, and a scaled mesh stretches everything. The mesh
// here is built at the character aspect -- radius 0.5, cylinder section 1, total
// height 2 -- so `Size.y == 2 * max(Size.x, Size.z)` is exact and everything
// else is a capsule with slightly oval ends. A mesh per aspect ratio is the fix
// and it belongs to the milestone that measures the cost.
#pragma once

#include "luaug/asset/model.h"

namespace luaug::asset {

// `Enum.PartShape`'s five members, in its own value order -- the enum's numbers
// are the contract (enums.api.luau says so), and an array indexed by them is
// what makes the lookup a subscript rather than a switch.
enum class PrimitiveShape : core::u8
{
    Block = 0,
    Ball = 1,
    Cylinder = 2,
    Capsule = 3,
    Wedge = 4,

    Count,
};

// How many segments go around an axis of revolution. Twenty-four is three
// degrees under a fifteen-degree facet: smooth enough that a ball the size of
// the screen has no visible flat, cheap enough that a world of a thousand of
// them is 288k triangles rather than a profile.
inline constexpr core::u32 kPrimitiveSegments = 24;

// How many rings a sphere has from pole to pole, caps included. Half the
// segment count, which is what keeps a quad square near the equator.
inline constexpr core::u32 kPrimitiveRings = 12;

// How many rings each hemispherical cap of a capsule has.
inline constexpr core::u32 kCapsuleCapRings = 6;

// One unit mesh, in the model space the renderer scales by `Size`. Every shape
// spans [-0.5, 0.5] on each axis except `Capsule`, which spans [-1, 1] on Y for
// the reason this header's opening gives.
//
// Positions, normals, tangents and UVs are all filled: the default `pbr` shader
// reads every one of them, and a primitive that left tangents zeroed would take
// the "no tangents at all" fallback in `tangentFrame` and shade differently from
// an imported mesh for no reason a person could see.
//
// One submesh, material 0, and the caller supplies the material -- a `Part`'s
// look is its own properties, not a file's.
[[nodiscard]] Mesh makePrimitive(PrimitiveShape shape);

} // namespace luaug::asset
