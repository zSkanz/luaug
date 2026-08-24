// Walking JSON without parsing it, for the one caller that must not.
//
// `core::json.h` is a DOM: it parses a document into nodes and owns them. That
// is exactly right for a manifest, a chunk index or a stamp, and exactly wrong
// for the partitioner, whose whole contract is that it never holds the world it
// is reading (ADR 0053). Parsing a scene into a DOM to decide which parts of it
// to keep is the same mistake as building the world to decide what to stream,
// one layer down.
//
// So this walks the TEXT. It hands out slices of the original string and never
// copies anything: a member's value is a `string_view` into the document, and a
// caller that wants the value parses that slice with the DOM reader -- which is
// what the partitioner does for the small leaf objects and never for a subtree.
//
// **The second thing the slices buy is a splice.** What a partition leaves
// behind is the same scene minus what left, and building it by copying the
// verbatim text of every node that stays means no number is ever reformatted:
// a scene with nothing streamable in it partitions to itself, byte for byte.
// Reserialising through a writer could not promise that -- `1e-7` and
// `0.0000001` are the same number and not the same file.
//
// Deliberately NOT a validator. It assumes well-formed JSON and reports failure
// only where it cannot make progress; the strict reader is what says whether a
// document is legal, and running two validators over one file would mean two
// answers to that question.
#pragma once

#include "luaug/core/types.h"

#include <optional>
#include <string>
#include <string_view>

namespace luaug::scene::jsonslice {

using core::usize;

// The first byte of the next non-space character at or after `at`.
[[nodiscard]] usize skipSpace(std::string_view text, usize at) noexcept;

// One past the last byte of the JSON value that starts at `at`, which must be
// that value's first byte. `npos` when the text ends inside the value.
[[nodiscard]] usize endOfValue(std::string_view text, usize at) noexcept;

// The value of `key` in the object `object` is, as a slice of it. Nothing when
// the object does not have that member or `object` is not an object.
//
// Keys are compared as RAW text, escapes included, which is sound for every key
// this format uses: they are property names out of the IDL and the four
// structural words, none of which contains a character `jsonQuote` escapes.
[[nodiscard]] std::optional<std::string_view> member(std::string_view object, std::string_view key);

// Calls `fn(key, value)` for each member of `object`, in written order, where
// `key` is the raw text between the quotes. Returns false when `object` is not
// an object or the walk could not finish.
template <typename Fn>
bool forEachMember(std::string_view object, Fn&& fn)
{
    usize at = skipSpace(object, 0);
    if (at >= object.size() || object[at] != '{') {
        return false;
    }
    at = skipSpace(object, at + 1);
    if (at < object.size() && object[at] == '}') {
        return true;
    }

    while (at < object.size()) {
        if (object[at] != '"') {
            return false;
        }
        const usize keyEnd = endOfValue(object, at);
        if (keyEnd == std::string_view::npos) {
            return false;
        }
        const std::string_view key = object.substr(at + 1, keyEnd - at - 2);

        at = skipSpace(object, keyEnd);
        if (at >= object.size() || object[at] != ':') {
            return false;
        }
        at = skipSpace(object, at + 1);
        const usize valueEnd = endOfValue(object, at);
        if (valueEnd == std::string_view::npos) {
            return false;
        }
        fn(key, object.substr(at, valueEnd - at));

        at = skipSpace(object, valueEnd);
        if (at >= object.size()) {
            return false;
        }
        if (object[at] == '}') {
            return true;
        }
        if (object[at] != ',') {
            return false;
        }
        at = skipSpace(object, at + 1);
    }
    return false;
}

// Calls `fn(element)` for each element of the array `array` is.
template <typename Fn>
bool forEachElement(std::string_view array, Fn&& fn)
{
    usize at = skipSpace(array, 0);
    if (at >= array.size() || array[at] != '[') {
        return false;
    }
    at = skipSpace(array, at + 1);
    if (at < array.size() && array[at] == ']') {
        return true;
    }

    while (at < array.size()) {
        const usize valueEnd = endOfValue(array, at);
        if (valueEnd == std::string_view::npos) {
            return false;
        }
        fn(array.substr(at, valueEnd - at));

        at = skipSpace(array, valueEnd);
        if (at >= array.size()) {
            return false;
        }
        if (array[at] == ']') {
            return true;
        }
        if (array[at] != ',') {
            return false;
        }
        at = skipSpace(array, at + 1);
    }
    return false;
}

// The text a JSON string slice -- quotes included -- stands for. Nothing when
// the slice is not a string or carries an escape this decoder cannot read.
[[nodiscard]] std::optional<std::string> unquote(std::string_view text);

} // namespace luaug::scene::jsonslice
