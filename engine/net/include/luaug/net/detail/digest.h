// SHA-1 and base64, needed by exactly one thing: the WebSocket handshake's
// `Sec-WebSocket-Accept` (RFC 6455 §4.1 mandates SHA-1 there, and the value is
// base64).
//
// They live under `detail/` because that is the whole of their remit. SHA-1 is
// not a hash this engine should offer anyone for any other purpose -- world
// state is xxh3 (`core::Hasher`) and content addressing is BLAKE3 (ADR 0010).
// The handshake uses it as a fixed protocol constant, not as security.
#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>

#include "luaug/core/types.h"

namespace luaug::net::detail
{

using Sha1Digest = std::array<core::u8, 20>;

[[nodiscard]] Sha1Digest sha1(std::span<const core::u8> data);
[[nodiscard]] Sha1Digest sha1(std::string_view text);

// Standard alphabet with padding (RFC 4648 §4).
[[nodiscard]] std::string base64Encode(std::span<const core::u8> data);

} // namespace luaug::net::detail
