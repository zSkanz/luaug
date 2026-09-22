#pragma once

// Which pieces of a GPU terrain to draw, and at what level (ADR 0071).
//
// **Continuous distance-dependent level of detail (CDLOD), after Strugar.** The
// terrain is a quadtree whose leaves are the field's own 32 by 32 tiles. Every
// node, whatever its level, is drawn with the same 32 by 32 grid -- a node at
// level L simply spaces its vertices 2^L lattice steps apart -- so there is one
// vertex buffer for the whole terrain and nothing is ever meshed on the CPU.
//
// Each level owns a distance band. Towards the outer edge of its band a node's
// vertices MORPH, in the vertex shader, onto the positions the next level's
// grid has: the odd vertices slide onto their even neighbours. By the time a
// vertex reaches the band's edge it sits exactly where the coarser neighbour's
// vertex is, so two nodes of different levels meet with no crack and no skirt,
// and a level change is a continuous slide rather than a pop.
//
// This file is the selection only: pure, deterministic, and testable with no
// device. The morph lives in `shaders/include/luaug_terrain.hlsli`.

#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <span>
#include <vector>

namespace luaug::render {

// The grid every node is drawn with, in quads a side. Equal to a height tile's
// edge, so a leaf node is exactly one tile.
inline constexpr core::u32 TerrainGridQuads = 32;

// The coarsest level. A level-10 node is 32 x 1024 lattice steps a side -- 16
// km at half a metre -- which is past any view distance this engine draws.
inline constexpr core::u32 TerrainMaxLevel = 10;

// What the selection needs to know about one terrain, in the field's own
// lattice. Heights are metres in the field's space; the terrain's origin is
// applied by the caller.
struct TerrainLodSource
{
    // The tile grid the min/max arrays describe, in tile keys.
    core::i32 minTileX = 0;
    core::i32 minTileZ = 0;
    core::u32 tilesX = 0;
    core::u32 tilesZ = 0;
    // `tilesX * tilesZ`, row-major by z. A tile the field does not hold has
    // `min > max`, and a node over nothing but such tiles is not drawn.
    std::span<const float> tileMin;
    std::span<const float> tileMax;

    float voxelSize = 0.5f;
    // The terrain's origin RELATIVE TO THE VIEWER, so every distance below is a
    // distance from the camera.
    core::DVec3 originFromViewer;
};

struct TerrainLodSettings
{
    // The outer edge of level 0's band, in metres. Level L's band ends at
    // `nearRange * 2^L`. Clamped up by the selection to three leaf widths:
    // a band narrower than that cannot contain the morph a leaf needs.
    //
    // Sixty-four at the default half-metre lattice puts one vertex per lattice
    // step out to forty metres, which is where caves are meshed (their holes
    // need the finest grid), and 2^9 of it covers the default view distance.
    double nearRange = 64.0;
    // Where in its band a level starts morphing, as a fraction of the band.
    double morphStart = 0.66;
    // Nothing further than this is drawn.
    double viewDistance = 4096.0;
};

struct TerrainNode
{
    // The node's corner, in lattice steps.
    core::i32 latticeX = 0;
    core::i32 latticeZ = 0;
    // 0 for a leaf; the node spans `TerrainGridQuads << level` lattice steps.
    core::u32 level = 0;
    // The metres, from the viewer, over which this node's vertices morph.
    float morphStart = 0.0f;
    float morphEnd = 0.0f;
    // The node's bounds, relative to the viewer.
    core::Vec3 boundsMin;
    core::Vec3 boundsMax;
};

// Appends the nodes that cover the terrain around the viewer. The order is a
// pure function of the inputs -- a depth-first walk in key order -- so two runs
// of the same frame draw in the same order (R10).
void selectTerrainNodes(const TerrainLodSource& source, const TerrainLodSettings& settings,
                        std::vector<TerrainNode>& out);

// The band edge of `level`, after the clamp described on `nearRange`.
[[nodiscard]] double terrainLevelRange(const TerrainLodSource& source, const TerrainLodSettings& settings,
                                       core::u32 level) noexcept;

} // namespace luaug::render
