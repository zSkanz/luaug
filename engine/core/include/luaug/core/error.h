// Structured engine errors (ADR 0019, api-design.md §6).
//
// An engine error is identified by its TextKey, not by its prose. The
// user-visible message is prefixed with the key -- `[engine.assets.err.not_found]
// ...` -- so conformance tests and CI assert on a stable identifier while the
// text stays free to be translated or reworded.
#pragma once

#include <span>
#include <string>

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"

namespace luaug::core
{

struct EngineError
{
    TextKey key;
    // Key-prefixed, catalog-formatted, ready to cross into Luau or a log.
    std::string message;
    // Traceback, source location, or other developer context. Never localised.
    std::string detail;
};

[[nodiscard]] EngineError makeError(TextKey key, std::span<const I18nArg> args = {}, std::string detail = {});

// The `[key]` prefix used by makeError, exposed so tests and the script host
// can match on it without duplicating the format.
[[nodiscard]] std::string formatKeyPrefixed(TextKey key, std::span<const I18nArg> args = {});

} // namespace luaug::core
