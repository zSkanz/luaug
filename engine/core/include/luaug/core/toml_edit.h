// Changing one value in a `luaug.toml` without rewriting the file.
//
// **A project file is a document somebody wrote, not a serialised struct.**
// Every `luaug.toml` in this repository opens with a paragraph explaining why
// the settings in it are what they are -- which scene a run starts with and why
// that is not in `init.luau`, what `[graphics] quality` means and what it does
// not. A writer that parsed the file and printed it back would take all of that
// out, and it would do it the first time somebody moved a slider.
//
// So this edits TEXT. It finds the line the key is on, replaces what is to the
// right of the `=`, and leaves every other byte where it was: comments,
// ordering, spacing, blank lines, the keys nobody asked about. A key the file
// does not have is appended under its own table header, and the header is
// created if it is missing -- which is the only case where anything is added.
//
// **It does not validate.** `TomlDocument` is the reader and the judge; this
// answers "what would the file look like with this changed", and a caller that
// wants to know whether the result still parses parses it.
#pragma once

#include "luaug/core/types.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace luaug::core {

// The TOML literal for a value, ready to appear to the right of an `=`.
//
// Separate from the setters below because rendering is the half with rules --
// which characters a basic string has to escape, how a float that happens to be
// integral is printed so it stays a float -- and a caller writing an array wants
// to compose them.
[[nodiscard]] std::string tomlString(std::string_view value);
[[nodiscard]] std::string tomlNumber(f64 value);
[[nodiscard]] std::string tomlBoolean(bool value);
[[nodiscard]] std::string tomlNumberArray(std::span<const f64> values);

// Returns `text` with `key`'s value replaced by `rendered`, or nothing when the
// document is malformed enough that the edit cannot be placed.
//
// `key` is the DOTTED path the reader uses -- `graphics.quality`, `window.size`
// -- and the last segment is the key while everything before it is the table.
// A key with no dot lives at the top level, above every header.
//
// **Nothing is quoted for you.** `rendered` is written literally, so a string
// goes through `tomlString` first; that keeps this one function rather than five
// and keeps an array from needing a variant.
[[nodiscard]] std::optional<std::string> setTomlValue(std::string_view text, std::string_view key,
                                                      std::string_view rendered);

} // namespace luaug::core
