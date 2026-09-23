// Terrain and block worlds cut into streaming cells (ADR 0075).
//
// **A cell is a vertical column of the world about 64 metres square**, the size
// `Terrain.CellSize` has always named. The ground and the blocks inside it are
// one file each, keyed by an `asset::ChunkId` on a grid of their own -- not the
// parts' 256 m one -- so a cell of ground is something a streaming manager can
// score, load, materialise and evict with the same policy a cell of parts gets.
//
// Everything here is a pure function of its input, so a partition run twice
// writes the same bytes, and in `ChunkId` order, so what it writes is a fact
// about the world rather than about a walk (R10).
#pragma once

#include "luaug/asset/chunk.h"
#include "luaug/asset/terrain.h"
#include "luaug/asset/terrain_cell.h"
#include "luaug/asset/voxel.h"
#include "luaug/core/error.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace luaug::asset {

// The side of a field cell, in metres, before it is rounded to whole tiles or
// whole chunks.
inline constexpr core::f64 FieldCellMetres = 64.0;

// The `ChunkId::layer` each kind of field cell is filed under, in the field
// index. Two kinds on one grid, told apart the way the parts' size classes are.
inline constexpr core::i32 FieldLayerTerrain = 0;
inline constexpr core::i32 FieldLayerVoxels = 1;

// --- Terrain ---------------------------------------------------------------

// How many chunk columns on a side a cell holds at this voxel size: the whole
// count nearest `cellMetres`, never fewer than one. A 64 m cell is four columns
// of chunks at half a metre and two at a metre; at two metres a chunk is
// already 64 m, and the cell is the chunk column.
[[nodiscard]] core::u32 terrainCellChunks(core::f32 voxelSize, core::f64 cellMetres = FieldCellMetres) noexcept;

// Which cell a chunk belongs to: every chunk of its column, whatever its `y`,
// so a cave is never split from the ground it is dug into.
[[nodiscard]] ChunkId terrainCellOf(ChunkKey key, core::u32 cellChunks) noexcept;

// The whole field cut into cells, in `ChunkId` order. The field inside each
// cell holds absolute keys and SHARES the chunks: this runs once, at a
// partition, and what it produces is written to disk and dropped.
[[nodiscard]] std::vector<TerrainCell> splitTerrain(const TerrainField& field, core::f64 cellMetres = FieldCellMetres);

// The world-space box a cell covers: its square, and the field's whole height
// range, both offset by where the terrain sits.
[[nodiscard]] core::DAABB terrainCellBounds(const TerrainCell& cell, core::u32 cellChunks, core::DVec3 origin) noexcept;

// **Whether `field` still holds exactly what `cell` put into it**, within the
// cell's footprint: every chunk the same one `cell` shares, and nothing
// there that `cell` did not bring. False is a cell somebody edited, which an
// evicting streamer must keep -- dropping it would drop the edit.
[[nodiscard]] bool terrainCellUntouched(const TerrainField& field, const TerrainCell& cell,
                                        core::u32 cellChunks) noexcept;

// Takes a cell's chunks back out of `field`.
void removeTerrainCell(TerrainField& field, const TerrainCell& cell);

// --- Block worlds ------------------------------------------------------------

// How many 16-block chunks on a side a cell holds at this block size, on
// `terrainCellChunks`' terms.
[[nodiscard]] core::u32 voxelCellChunks(core::f32 blockSize, core::f64 cellMetres = FieldCellMetres) noexcept;

// Which cell a chunk belongs to: every chunk of the column, whatever its `y`.
[[nodiscard]] ChunkId voxelCellOf(VoxelChunkKey key, core::u32 cellChunks) noexcept;

// One cell's worth of blocks.
struct VoxelCell
{
    core::i32 x = 0;
    core::i32 z = 0;
    core::f32 blockSize = 1.0f;
    VoxelGrid grid;
};

// The whole grid cut into cells, in `ChunkId` order, on `splitTerrain`'s terms.
[[nodiscard]] std::vector<VoxelCell> splitVoxels(const VoxelGrid& grid, core::f32 blockSize,
                                                 core::f64 cellMetres = FieldCellMetres);

// Its square, and the height its chunks actually span.
[[nodiscard]] core::DAABB voxelCellBounds(const VoxelCell& cell, core::u32 cellChunks) noexcept;

// On `terrainCellUntouched`'s terms.
[[nodiscard]] bool voxelCellUntouched(const VoxelGrid& grid, const VoxelCell& cell, core::u32 cellChunks) noexcept;
void removeVoxelCell(VoxelGrid& grid, const VoxelCell& cell);

// **The `.lvoxel` cell file.** Little-endian words: magic `LGVC`, version,
// x, z, the block size's bits and a chunk count; then per chunk its key's three
// words, a byte count and `encodeVoxelChunk`'s run-coded bytes. Keys in sorted
// order, which the decoder requires, so a file has one reading.
inline constexpr core::u32 VoxelCellFormatVersion = 1;
// A corrupt count must not be able to ask for a gigabyte: a 64 m cell of
// one-metre blocks is sixteen columns of chunks, and 65,536 is room for every
// one of them to be 4,096 chunks tall.
inline constexpr core::u32 MaxVoxelCellChunks = 65536;

[[nodiscard]] std::vector<std::byte> encodeVoxelCell(const VoxelCell& cell);
[[nodiscard]] std::optional<core::EngineError> decodeVoxelCell(std::span<const std::byte> bytes, VoxelCell& out);

} // namespace luaug::asset
