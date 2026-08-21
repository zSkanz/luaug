// Clustered forward shading: which lights reach which piece of the view frustum
// (ADR 0038, ADR 0027's "clustered pass", M7.5).
//
// Olsson and Assarsson's clustering with the grid Doom 2016 used -- 16 by 9 by
// 24 -- and exponential depth slicing, `slice = max(log2(linearDepth) * scale +
// bias, 0)`. The logarithm is the whole reason one grid serves a near plane at
// 0.1 and a far plane in the thousands: a uniform slicing would spend
// twenty-three of its twenty-four slices past the point anything is lit.
//
// **The assignment is CPU-side, and it travels as textures.** There is no
// compute in the frozen RHI (ADR 0037) and no storage buffer, so a fragment
// shader's only bulk-data route is a sampled texture -- which is fine, because
// `Load`-style point fetches need no filtering and the three tables together are
// under a hundred kilobytes a frame.
//
// What this replaces: eight lights per draw, unculled, iterated by every
// fragment (`kMaxForwardLights`, gone). The number that matters is not the new
// total but the per-cluster bound: a fragment pays for the lights that reach its
// own cluster and for no others.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"
#include "luaug/render/render_world.h"

#include <span>
#include <vector>

namespace luaug::render {

using core::f32;
using core::u32;

// Doom 2016's grid. Not tuned here: it is the published one, and a grid whose
// dimensions were guessed would be a parameter nobody could defend.
inline constexpr u32 kClusterTilesX = 16;
inline constexpr u32 kClusterTilesY = 9;
inline constexpr u32 kClusterSlices = 24;
inline constexpr u32 kClusterCount = kClusterTilesX * kClusterTilesY * kClusterSlices;

// The grid texture's shape: one texel per cluster, slices laid out along x so a
// lookup is two integer multiplies.
inline constexpr u32 kClusterGridWidth = kClusterTilesX * kClusterSlices;
inline constexpr u32 kClusterGridHeight = kClusterTilesY;

// **A cluster's index IS its texel's row-major position in that texture**, and
// this function exists because the two were once different: the assignment
// numbered clusters slice-major while the shader read them row-major, so every
// fragment looked up a cluster that belonged to some other part of the screen.
// Nothing in the unit tests could see it -- both sides were internally
// consistent -- and it showed up as light clipped to hard horizontal bands.
[[nodiscard]] constexpr u32 clusterIndexOf(u32 tileX, u32 tileY, u32 slice) noexcept
{
    return tileY * kClusterGridWidth + slice * kClusterTilesX + tileX;
}

// How many lights one frame may carry. A budget rather than a limit of the
// design, like `kMaxSkinJoints`: the light table is a texture and a texture has
// a size. Two hundred and fifty-six is thirty-two times what M4 shipped.
inline constexpr u32 kMaxClusteredLights = 256;

// How many may reach ONE cluster, which is the number a fragment actually pays
// for. Sixty-four is generous: a cluster is a sixteenth of the screen by a
// twenty-fourth of the depth range, and a scene where that many lights overlap
// one is a scene with a lighting problem rather than a renderer problem.
inline constexpr u32 kMaxLightsPerCluster = 64;

// The flattened index list's capacity, and its texture's width. Sixteen
// thousand entries is 64 KiB a frame; the worst case a grid this size could
// demand is thirteen times that, so the list can fill -- `ClusterGrid::overflow`
// is what says it did, rather than the lights past the end silently stopping
// mattering.
inline constexpr u32 kLightIndexCapacity = 16384;
inline constexpr u32 kLightIndexTextureWidth = 128;
inline constexpr u32 kLightIndexTextureHeight = kLightIndexCapacity / kLightIndexTextureWidth;

// How the grid texel packs its two numbers: `offset * kClusterOffsetShift +
// count`. A float carries integers exactly to 2^24 and the largest value this
// can produce is under 2^22, so the packing is lossless rather than nearly so.
inline constexpr f32 kClusterOffsetShift = 256.0f;

// What one frame's assignment produces, in the exact layouts the three textures
// take. Vectors rather than fixed arrays because this is uploaded from and the
// upload wants a contiguous span.
struct ClusterGrid
{
    // One texel per cluster, `offset * 256 + count`.
    std::vector<f32> grid;
    // The flattened per-cluster light index runs.
    std::vector<f32> indices;
    // Three RGBA texels per light: position and range, colour, direction and
    // cone cosine -- `GpuLight`'s own three rows, in the same order.
    std::vector<f32> lightData;

    u32 lightCount = 0;
    u32 indexCount = 0;
    // Clusters that wanted more lights than they were allowed, and lights
    // dropped because the flat list filled. Both are reported rather than
    // silently absorbed: "the lights past eight stopped mattering" was an
    // afternoon nobody should have to repeat.
    u32 overflowClusters = 0;

    // The exponential slicing's two constants, which the shader needs to invert
    // the mapping. Derived from the camera and carried here so exactly one place
    // computes them.
    f32 sliceScale = 0.0f;
    f32 sliceBias = 0.0f;

    void clear() noexcept;
};

// Assigns `lights` to clusters for `camera`.
//
// Order is a pure function of the inputs: lights are visited in index order in
// both passes, so a cluster's run is ascending by light index and the flat list
// is ascending by cluster. Nothing here depends on iteration order of anything
// unordered (R10) even though this is render-side work.
void buildClusters(const RenderCamera& camera, std::span<const RenderLight> lights, ClusterGrid& out);

// The slice a view-space depth falls in, which the shader computes the same way.
// Exposed because it is the claim a test can check directly: the mapping must be
// monotone, must put the near plane in slice zero and the far plane in the last
// one, and must be the exact inverse of the slice's own depth bounds.
[[nodiscard]] u32 clusterSliceOf(f32 viewDepth, f32 sliceScale, f32 sliceBias) noexcept;

// The two constants, from a camera's depth range.
void clusterSliceConstants(f32 nearPlane, f32 farPlane, f32& outScale, f32& outBias) noexcept;

} // namespace luaug::render
