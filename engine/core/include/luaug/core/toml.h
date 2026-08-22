// A small, strict TOML subset reader for `luaug.toml` (api-design.md §4).
//
// `tools/cli/toml.luau` said the engine would get one "in the milestone that
// first needs engine-side configuration, and not before". This is that
// milestone: M8's graphics settings, its window title and its application
// identity all live in the project file, and two of the three are read by the
// host rather than by the CLI -- a packaged player has no Lute in it.
//
// **The subset is the same one the CLI reads**, deliberately, because a project
// file that `luaug dev` accepts and the shipped player rejects is worse than
// either being stricter. Supported: comments, bare and quoted keys, `[table]`
// and `[nested.table]` headers, basic and literal strings, integers, floats,
// booleans, and single-line arrays of those. Everything else -- multi-line
// strings, inline tables, arrays of tables, dates -- is an explicit error and
// never a silent misread, for the reason the Luau reader gives: a config format
// that quietly ignores what it does not understand is how a setting ends up not
// applying.
//
// Values are addressed by their DOTTED path (`graphics.quality`), because
// nothing that reads this file walks it; every caller knows the key it wants.
#pragma once

#include "luaug/core/types.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::core {

class TomlDocument
{
public:
    struct ParseResult
    {
        bool ok = false;
        // Developer-facing: a malformed project file is a startup failure and
        // never something a player sees, so this is exempt from R3 the same way
        // `JsonDocument::ParseResult::diagnostic` is.
        std::string diagnostic;
        usize line = 0;

        explicit operator bool() const noexcept { return ok; }
    };

    // Replaces whatever was parsed before. `sourceName` appears in diagnostics
    // only.
    ParseResult parse(std::string_view text, std::string_view sourceName = "<toml>");

    [[nodiscard]] bool has(std::string_view key) const noexcept;

    // Nothing is coerced: asking a string for a number answers nothing rather
    // than zero, which is the property that makes a typo in the project file
    // visible instead of silently meaning "default".
    [[nodiscard]] std::optional<std::string_view> string(std::string_view key) const;
    [[nodiscard]] std::optional<f64> number(std::string_view key) const;
    [[nodiscard]] std::optional<bool> boolean(std::string_view key) const;

    // The numbers in an array, or empty when the key is absent or holds
    // anything else. `[window] size = [1280, 720]` is the whole of why this
    // exists.
    [[nodiscard]] std::span<const f64> numbers(std::string_view key) const;

private:
    enum class Kind : u8
    {
        String,
        Number,
        Boolean,
        NumberArray,
        StringArray,
    };

    struct Entry
    {
        std::string key;
        Kind kind = Kind::String;
        std::string text;
        f64 number = 0.0;
        bool boolean = false;
        std::vector<f64> numbers;
        std::vector<std::string> strings;
    };

    [[nodiscard]] const Entry* find(std::string_view key) const noexcept;

    std::vector<Entry> entries_;
};

} // namespace luaug::core
