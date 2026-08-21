// The engine's content address (architecture.md §2): BLAKE3, truncated to 128
// bits.
//
// **This is a NAME, not a checksum**, and the distinction decides the
// algorithm. `WorldHash` is xxh3 because it is compared against itself -- a
// replay's hash against the same replay's hash -- so a collision between two
// unrelated worlds means nothing. A content address is the opposite: two
// distinct assets that hash alike are one asset as far as the pack, the cache
// and the manifest are concerned, and the failure looks exactly like a cache
// hit. 128 bits of a cryptographic hash is what makes "the same hash means the
// same bytes" a statement rather than a hope.
//
// Truncation to 128 bits is BLAKE3's own documented shape -- its output is an
// extendable stream and any prefix is a valid digest of that length -- and it
// halves what every TOC entry, manifest row and URI has to carry.
#pragma once

#include "luaug/core/types.h"

#include <array>
#include <compare>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace luaug::core {

struct ContentHash
{
    // Big-endian halves of the 16-byte digest: `high` is bytes 0..7, `low` is
    // bytes 8..15. Stated because the hex form and the byte order have to
    // agree across the writer, the reader and every log line, and "whatever
    // memcpy did on this machine" is not an agreement.
    u64 high = 0;
    u64 low = 0;

    [[nodiscard]] constexpr bool operator==(const ContentHash&) const noexcept = default;

    // Ordering exists so a pack's table of contents can be SORTED by hash --
    // which is what makes a lookup a binary search and, more to the point, what
    // makes the file itself deterministic.
    [[nodiscard]] constexpr std::strong_ordering operator<=>(const ContentHash&) const noexcept = default;

    [[nodiscard]] constexpr bool isZero() const noexcept { return high == 0 && low == 0; }

    // 32 lowercase hex digits, `high` first. The canonical text form: it is
    // what a manifest row carries, what a cache file is named, and what a log
    // line prints.
    [[nodiscard]] std::string toHex() const;
};

// Exactly 16 bytes, `high` first. For writing a hash into a binary format.
[[nodiscard]] std::array<std::byte, 16> toBytes(const ContentHash& hash) noexcept;
[[nodiscard]] ContentHash fromBytes(std::span<const std::byte, 16> bytes) noexcept;

// False -- and `out` untouched -- for anything that is not exactly 32 hex
// digits. Case-insensitive on input; `toHex` always emits lowercase, so a hash
// that round-trips through text compares equal as text too.
[[nodiscard]] bool parseHex(std::string_view text, ContentHash& out);

[[nodiscard]] ContentHash hashBytes(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] ContentHash hashText(std::string_view text) noexcept;

// Incremental, for content that is not in one buffer: a file read in chunks, a
// mesh whose streams are hashed in sequence, a directory hashed entry by entry.
//
// The state is held inline rather than behind a pointer, so hashing ten
// thousand files costs no allocations at all -- which is what an asset build
// spends most of its time doing.
class ContentHasher
{
public:
    ContentHasher() noexcept;

    void update(std::span<const std::byte> bytes) noexcept;
    void update(std::string_view text) noexcept;

    // Repeatable: finishing does not consume the state, so a caller may take a
    // hash of a prefix and keep appending.
    [[nodiscard]] ContentHash finish() const noexcept;

private:
    // Sized for BLAKE3's `blake3_hasher`, asserted against the real type where
    // the header is included. Opaque here so that nothing above `core` acquires
    // an include path into a vendored tree.
    static constexpr usize StateBytes = 2048;

    alignas(8) std::array<std::byte, StateBytes> m_state{};
};

} // namespace luaug::core

template <>
struct std::hash<luaug::core::ContentHash>
{
    [[nodiscard]] std::size_t operator()(const luaug::core::ContentHash& value) const noexcept
    {
        // The input is already a uniformly distributed cryptographic digest, so
        // there is nothing left to mix: taking half of it IS the hash.
        return static_cast<std::size_t>(value.high ^ value.low);
    }
};
