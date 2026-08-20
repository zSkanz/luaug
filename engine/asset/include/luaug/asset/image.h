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

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "luaug/core/error.h"
#include "luaug/core/types.h"

namespace luaug::asset
{

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
[[nodiscard]] std::optional<core::EngineError> writePng(
    const std::filesystem::path& path, std::span<const std::byte> pixels, u32 width, u32 height);

} // namespace luaug::asset
