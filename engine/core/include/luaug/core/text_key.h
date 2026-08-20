// Interned identity of a translatable message (ADR 0019, rule R3).
//
// Engine code never holds user-facing prose. It holds a TextKey, and the
// catalog turns that into text for the active locale. A TextKey is four bytes
// so it is free to pass around and store in error records.
//
// The key *string* deliberately lives in exactly one place -- the catalog
// (`i18n/en.json`) -- rather than being carried alongside the hash. The
// catalog therefore doubles as the hash -> name registry used for diagnostics,
// and the i18n lint guarantees every LUAUG_TR() literal in the source has a
// catalog entry, so "hash with no known name" is a CI failure rather than a
// runtime mystery.
#pragma once

#include "luaug/core/types.h"

#include <string_view>

namespace luaug::core {

// FNV-1a (32-bit). Chosen for being constexpr-trivial and stable across
// compilers -- the value must not vary between the engine and the tools that
// generate the key inventory. Collisions are detected at catalog load rather
// than assumed away.
constexpr u32 hashTextKey(std::string_view key) noexcept
{
    u32 hash = 0x811C9DC5u;
    for (const char c : key) {
        hash ^= static_cast<u32>(static_cast<unsigned char>(c));
        hash *= 0x01000193u;
    }
    return hash;
}

struct TextKey
{
    u32 hash = 0;

    friend constexpr bool operator==(TextKey, TextKey) noexcept = default;
};

} // namespace luaug::core

// The one sanctioned way to name a message. Takes a string literal so the
// i18n lint can find every key by scanning source (roadmap M0/M3).
#define LUAUG_TR(keyLiteral) (::luaug::core::TextKey{::luaug::core::hashTextKey(keyLiteral)})
