#pragma once

// The `.lterrain` cell format (ADR 0067, F1 B3; version 3 by ADR 0082).
//
// **A sculpted terrain has to be saved with the project and is far too large for
// JSON.** A scene file is text so that a change to it is reviewed as a diff; a
// terrain cell is tens of thousands of samples that no human reads, and encoding
// them as decimal numbers would be an order of magnitude of waste for a review
// nobody performs.
//
// So this is binary, little-endian, and it follows the rules `chunk.cpp`
// already established -- with one of them stated here because it is the one that
// matters most: **every count is checked against a named ceiling BEFORE it is
// allocated against.** M7's own finding, in its own words: "a bounds-checked
// reader is not a safe reader, because the allocation happens first."
//
// **What is stored is the chunks exactly as the field holds them**, keys and
// voxels, so a save and a reload reproduce the same field and therefore the
// same world hash.

#include "luaug/asset/terrain.h"
#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <optional>
#include <span>
#include <vector>

namespace luaug::asset {

// Bumped whenever the layout changes. A hard equality on read, like
// `ChunkFormatVersion`, with one exception: version 2 -- the hybrid of height
// tiles and voxel bricks (ADR 0067) -- is still read, and resampled into voxels
// as it loads (ADR 0082). It is never written. Every world saved before the
// voxel grid opens; none is saved in the old shape again.
//
// **Version 3 is chunks of voxels.** After the header, a sorted directory of
// chunk keys, then each chunk as a byte count and its voxels run-length coded
// in storage order: pairs of little-endian u16, a packed voxel
// (`packVoxel`) and how many times it repeats. A uniform chunk is one pair.
inline constexpr core::u32 TerrainCellFormatVersion = 3;
inline constexpr core::u32 TerrainCellLegacyVersion = 2;

// How a cell's body is coded. The header is never compressed -- a reader has to
// be able to check the counts before it allocates anything to decompress into,
// which is the rule this format's ceilings exist for.
enum class TerrainCellCompression : core::u32
{
    None = 0,
    // PackBits over the whole body, on top of the voxel runs. Chosen over zstd
    // for the reason version 2 gave: reaching into another library's bundled
    // copy is a dependency decision, and what is left after the voxel runs is
    // mostly repeated keys and counts, which this codes well enough.
    RunLength = 1,
};

// **Ceilings, checked before anything is reserved.**
//
// A 64 m cell at half a metre is sixteen columns of chunks; a 512 m band of
// height at that size is thirty-two chunks a column, 512 in all. The ceiling is
// room for a quarter-metre voxel over a kilometre of height, and small enough
// that a corrupt count cannot ask for a gigabyte.
inline constexpr core::u32 MaxCellChunks = 16384;

// **The whole field, as a scene carries it** (D159): a scene writes its terrain
// as ONE cell at the origin, and held to a streamed cell's ceiling a large
// world saved and then refused to load. Still a bound, and the bytes actually
// present bound it again (see `decodeTerrainCell`).
inline constexpr core::u32 MaxFieldChunks = 1u << 22;

struct TerrainCellLimits
{
    core::u32 chunks = MaxCellChunks;
};

inline constexpr TerrainCellLimits WholeFieldLimits{MaxFieldChunks};

// One cell's worth of field, with the coordinates that place it.
struct TerrainCell
{
    // Which cell, on the streaming grid. No `y`: a field cell is a column of
    // chunks from floor to ceiling, because the ground over a cave is its roof
    // (ADR 0086), and its `ChunkId` is always band zero.
    core::i32 x = 0;
    core::i32 z = 0;

    FieldSettings settings;
    TerrainField field;
};

// Encodes a cell. A pure function of its input: the same cell encodes to the
// same bytes on every machine, which is what makes a content hash over one mean
// anything.
//
// `compression` is a parameter rather than a fixed choice because the format
// carries both and a decoder that only ever met one of them is a decoder with an
// untested branch in it. Callers want the default; the uncompressed form is for
// tests that need a known byte offset, and for anything that would rather spend
// bytes than cycles.
[[nodiscard]] std::vector<std::byte>
encodeTerrainCell(const TerrainCell& cell, TerrainCellCompression compression = TerrainCellCompression::RunLength);

// Where a cell's body begins, in bytes. The header is twelve little-endian
// words: magic, version, flags, x, z, voxelSize, minHeight, maxHeight,
// chunkCount, compression, the plain body's byte count, and a reserved zero.
//
// Exposed because tests poke a header field by offset, and a hand-counted
// offset is something that goes quietly wrong the next time a word is added.
inline constexpr core::usize TerrainCellHeaderBytes = 12 * 4;

// Decodes one, or says why not.
// `limits` is a streamed cell's by default; a scene's whole field passes
// `WholeFieldLimits`. A version 2 cell is converted into voxels on the way in.
[[nodiscard]] std::optional<core::EngineError> decodeTerrainCell(std::span<const std::byte> bytes, TerrainCell& out,
                                                                 TerrainCellLimits limits = {});

} // namespace luaug::asset
