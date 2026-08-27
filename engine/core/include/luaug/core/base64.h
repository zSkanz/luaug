// Bytes as text, for the two places this repository has to put binary inside
// something that is not binary.
//
// **Standard alphabet with padding, and no line breaks.** MIME's 76-column
// wrapping exists for mail transports that no longer matter here, and a decoder
// that has to skip whitespace is a decoder with a second parser in it.
#pragma once

#include "luaug/core/types.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace luaug::core {

// The encoded form. Deterministic and total: every byte sequence has exactly one
// spelling, which is what lets a scene file stay diffable and a round-trip test
// compare bytes rather than meanings.
[[nodiscard]] std::string base64Encode(std::span<const u8> bytes);

// The bytes back, or nothing when the text is not base64.
//
// **Refused rather than repaired.** A decoder that skipped a stray character
// would turn a corrupt file into a plausible one, and a scene that loads wrong
// is worse than a scene that says it cannot load.
[[nodiscard]] std::optional<std::vector<u8>> base64Decode(std::string_view text);

} // namespace luaug::core
