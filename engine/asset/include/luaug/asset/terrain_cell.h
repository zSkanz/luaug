#pragma once

// The `.lterrain` cell format (ADR 0067, F1 B3).
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
// **What is stored is the representation, not just the shape.** Which columns
// carry bricks is part of the field's state -- promotion happens at an edit and
// demotion only at an explicit compaction -- so a save and a reload reproduce
// the same encoding and therefore the same world hash. A format that wrote only
// the heights and let the loader decide where bricks belonged would be a hybrid
// that re-decided its own encoding, and a hash that moved when a project was
// reopened.

#include "luaug/asset/terrain.h"
#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <optional>
#include <span>
#include <vector>

namespace luaug::asset {

// Bumped whenever the layout changes. A hard equality on read, like
// `ChunkFormatVersion`: a reader that tried to be permissive about a format it
// does not know is a reader that produces plausible garbage.
//
// **Version 2 fixed a hole and used the byte version 1 reserved.** Version 1
// wrote `voxelSize` and `giveUpColumns` and dropped `minHeight` and
// `maxHeight` -- so a field came back with the DEFAULT reserved range, and
// ADR 0066 says that range is spread across a collider's precision at
// construction and cannot be widened afterwards. A reloaded world would have
// clamped every later edit to a band it was never sculpted under, silently.
// Nothing had noticed because nothing round-tripped a non-default range.
//
// The same bump carries compression, which version 1 explicitly left for
// version 2 to add with a byte in the header saying which.
inline constexpr core::u32 TerrainCellFormatVersion = 2;

// How a cell's body is coded. The header is never compressed -- a reader has to
// be able to check the counts before it allocates anything to decompress into,
// which is the rule this format's ceilings exist for.
enum class TerrainCellCompression : core::u32
{
    // What version 1 always was.
    None = 0,
    // Run-length, PackBits shape. **Chosen over zstd, which ADR 0067 named and
    // which is already linked inside `basis_universal`.** Reaching into another
    // library's bundled dependency is a dependency decision that deserves an ADR
    // rather than a convenience -- and what this data actually is, is long runs:
    // a brick is mostly saturated distance, a tile's materials are usually one
    // value, and flat ground repeats the same four height bytes across a
    // thousand columns. Measured at better than eight to one on flat ground, in
    // a test that asserts it rather than reporting it. Worst case is one byte
    // per hundred and twenty-eight.
    RunLength = 1,
};

// **Ceilings, checked before anything is reserved.**
//
// A 64 m cell at half a metre is four tiles on a side, so sixteen tiles; the
// ceiling is generous enough for a 512 m cell and small enough that a corrupt
// count cannot ask for a gigabyte. Bricks are bounded by what a cave can
// plausibly occupy in one cell rather than by geometry, because a cell that is
// nothing but cave is a cell somebody should have split.
inline constexpr core::u32 MaxCellTiles = 4096;
inline constexpr core::u32 MaxCellBricks = 32768;

// One cell's worth of field, with the coordinates that place it.
struct TerrainCell
{
    // Which cell, on the streaming grid. No `y`: `asset::ChunkId` has none and
    // a cell is a vertically-infinite column, which is exactly what let the
    // hybrid fit the streaming system unchanged.
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
// giveUpColumns, tileCount, brickCount, compression.
//
// Exposed because three tests poke a header field by offset, and three
// hand-counted offsets is three things that go quietly wrong the next time a
// word is added -- which is exactly what happened when version 2 added two.
inline constexpr core::usize TerrainCellHeaderBytes = 12 * 4;

// Decodes one, or says why not.
[[nodiscard]] std::optional<core::EngineError> decodeTerrainCell(std::span<const std::byte> bytes, TerrainCell& out);

} // namespace luaug::asset
