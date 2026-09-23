// Image encode and decode (architecture.md §2 `asset`, ADR 0010).
//
// The first thing in this module, and it arrives by moving rather than by being
// written: `app::writePng` has carried a note since M1 saying it lived in the
// host only until image IO became a real subsystem, and that this milestone was
// when it would move. Decoding is what M4 needs on top of it -- a glTF names
// PNG and JPEG textures, and stb reads both.
//
// KTX2 and basis are NOT here and are not coming in M4: they exist to be
// produced by the offline transcoder ADR 0010 puts in M7, and nothing in this
// repository can currently write one.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace luaug::asset {

using core::u32;

// Decoded pixels, always 8-bit RGBA, top row first, tightly packed.
//
// Always four channels even for a grey PNG: the alternative is every consumer
// branching on channel count, and a GPU upload wants one layout anyway. The
// source's own channel count is kept because it is the difference between a
// texture that meant to be opaque and one that happens to be.
struct Image
{
    u32 width = 0;
    u32 height = 0;
    // 1..4, as the file stored it, before expansion to RGBA.
    u32 sourceChannels = 0;
    std::vector<std::byte> pixels;

    [[nodiscard]] bool valid() const noexcept
    {
        return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width) * height * 4u;
    }
};

// Decodes PNG, JPEG and the rest of stb_image's set from memory.
//
// Takes bytes rather than a path because the caller that matters reads from a
// glTF buffer view -- an image embedded in a `.glb` is a span in the middle of
// a file, never a file of its own.
[[nodiscard]] std::optional<core::EngineError> decodeImage(std::span<const std::byte> encoded, Image& out);

// Writes tightly packed 8-bit RGBA as PNG. `pixels` must hold exactly
// width * height * 4 bytes, top row first.
//
// Returns the error rather than logging it: a screenshot that silently failed
// to write is indistinguishable from one that matched, and the whole point of
// the file is to be evidence.
[[nodiscard]] std::optional<core::EngineError> writePng(const std::filesystem::path& path,
                                                        std::span<const std::byte> pixels, u32 width, u32 height);

// --- Heightmaps ---------------------------------------------------------------

// A heightmap, as elevations from 0 (black) to 1 (white), top row first.
//
// **Its own decode rather than `Image`**, because `Image` is eight bits and a
// heightmap at eight bits is 256 steps: a hundred-metre range in 256 steps is a
// terrace every forty centimetres, which a character walks up as stairs. Every
// terrain tool trades in sixteen-bit greyscale for that reason, so this reads a
// sixteen-bit PNG at its full depth.
struct HeightImage
{
    u32 width = 0;
    u32 height = 0;
    std::vector<float> samples;

    [[nodiscard]] bool valid() const noexcept
    {
        return width > 0 && height > 0 && samples.size() == static_cast<std::size_t>(width) * height;
    }
};

// Decodes a heightmap by its file name's extension.
//
// - `.r16` and `.raw` are headerless little-endian sixteen-bit samples, square,
//   which is what terrain tools export when they export "RAW". The side is the
//   square root of the sample count, and a count that is not a square is
//   refused rather than guessed at.
// - Anything else goes through the image decoder, at sixteen bits where the
//   file has them. A colour file is read as its luminance, which is what a
//   grey one already is.
[[nodiscard]] std::optional<core::EngineError> decodeHeightmap(std::span<const std::byte> encoded,
                                                               std::string_view fileName, HeightImage& out);

// Resamples a heightmap onto `columns` x `rows` terrain columns, bilinearly,
// and maps 0 to `low` and 1 to `high`. Row-major, first row first: the image's
// top-left pixel is the first column of the first row, which is the corner with
// the smallest x and z.
[[nodiscard]] std::vector<float> resampleHeights(const HeightImage& image, u32 columns, u32 rows, float low,
                                                 float high);

} // namespace luaug::asset
