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
inline constexpr core::u32 TerrainCellFormatVersion = 1;

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
[[nodiscard]] std::vector<std::byte> encodeTerrainCell(const TerrainCell& cell);

// Decodes one, or says why not.
[[nodiscard]] std::optional<core::EngineError> decodeTerrainCell(std::span<const std::byte> bytes, TerrainCell& out);

} // namespace luaug::asset
