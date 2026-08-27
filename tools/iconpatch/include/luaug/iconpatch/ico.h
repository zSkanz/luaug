#pragma once

// The two byte layouts an icon lives in, and the conversion between them.
//
// **Split out of `main.cpp` so it can be tested** (S7.10). Everything else in
// this tool is `UpdateResourceW` and `LoadLibraryExW`, which need Windows and a
// real PE; this part is arithmetic over bytes and needs neither. It is also the
// part where being wrong is silent: `main.cpp`'s own comment says an executable
// with an icon-shaped hole in it is what a mistake here produces, and nothing
// fails when an icon is wrong -- the program runs and wears the wrong face.
//
// Built on every platform even though the tool is Windows-only, so the Tier-2
// gate reads it too. A parser is a parser.

#include <cstdint>
#include <span>
#include <vector>

namespace luaug::iconpatch {

// One entry of an icon directory.
//
// **The file layout and the resource layout differ by exactly four bytes**: a
// FILE entry ends with a 32-bit offset into the file, a GROUP entry ends with a
// 16-bit id naming another resource. That difference is the whole of the
// conversion, and it is why these are two functions and not one.
struct IconEntry
{
    std::uint8_t width = 0;
    std::uint8_t height = 0;
    std::uint8_t colors = 0;
    std::uint8_t reserved = 0;
    std::uint16_t planes = 0;
    std::uint16_t bitCount = 0;
    std::uint32_t bytes = 0;
    std::uint32_t offset = 0;
};

// Parses an `.ico`. **Empty on anything that is not one** -- a truncated file, a
// cursor, a PNG somebody renamed, an entry pointing past the end -- because a
// build step that half-applied an icon is worse than one that refused.
[[nodiscard]] std::vector<IconEntry> parseIcon(std::span<const std::uint8_t> data);

// The group directory a PE carries: the same six-byte header the file has,
// followed by 14-byte entries naming consecutive resource ids from `firstId`.
[[nodiscard]] std::vector<std::uint8_t> buildGroup(std::span<const IconEntry> entries, std::uint16_t firstId);

} // namespace luaug::iconpatch
