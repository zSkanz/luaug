#include "luaug/core/toml.h"

#include <algorithm>
#include <string>

#include "number_parse.h"

namespace luaug::core {
namespace {

constexpr std::string_view kSpaces = " \t";

[[nodiscard]] std::string_view trim(std::string_view text)
{
    const usize first = text.find_first_not_of(kSpaces);
    if (first == std::string_view::npos)
        return {};
    const usize last = text.find_last_not_of(kSpaces);
    return text.substr(first, last - first + 1);
}

// Splits a line at the first `#` that is not inside a string. Quoting rules
// matter here and not only in the value parser: `name = "a # b"` is a string
// with a hash in it, and a naive split would truncate it into an unterminated
// one.
[[nodiscard]] std::string_view stripComment(std::string_view line)
{
    bool inBasic = false;
    bool inLiteral = false;
    for (usize index = 0; index < line.size(); ++index) {
        const char c = line[index];
        if (inBasic) {
            if (c == '\\')
                ++index;
            else if (c == '"')
                inBasic = false;
        }
        else if (inLiteral) {
            if (c == '\'')
                inLiteral = false;
        }
        else if (c == '"') {
            inBasic = true;
        }
        else if (c == '\'') {
            inLiteral = true;
        }
        else if (c == '#') {
            return line.substr(0, index);
        }
    }
    return line;
}

struct ScalarValue
{
    enum class Kind : u8
    {
        String,
        Number,
        Boolean,
    };

    Kind kind = Kind::String;
    std::string text;
    f64 number = 0.0;
    bool boolean = false;
};

struct Parsed
{
    bool ok = false;
    ScalarValue value;
    std::string_view rest;
    std::string error;
};

// Two constructors rather than braces at each return, and Clang is why: a
// designated initializer that omits a trailing field is `-Wmissing-field-
// initializers`, which is an error here, and MSVC says nothing about it. Naming
// every field at twenty return sites would say the same thing twenty times.
[[nodiscard]] Parsed parsedValue(ScalarValue value, std::string_view rest)
{
    return Parsed{.ok = true, .value = std::move(value), .rest = rest, .error = {}};
}

[[nodiscard]] Parsed parsedError(std::string_view rest, std::string error)
{
    return Parsed{.ok = false, .value = {}, .rest = rest, .error = std::move(error)};
}

[[nodiscard]] Parsed parseBasicString(std::string_view text)
{
    std::string out;
    for (usize index = 1; index < text.size(); ++index) {
        const char c = text[index];
        if (c == '\\') {
            if (index + 1 >= text.size())
                return parsedError(text, "unterminated string");
            const char escape = text[index + 1];
            switch (escape) {
            case 'n':
                out.push_back('\n');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case '"':
            case '\\':
                out.push_back(escape);
                break;
            default:
                return parsedError(text, std::string("unsupported escape \\") + escape);
            }
            ++index;
            continue;
        }
        if (c == '"') {
            return parsedValue(
                ScalarValue{.kind = ScalarValue::Kind::String, .text = std::move(out), .number = 0.0, .boolean = false},
                text.substr(index + 1));
        }
        out.push_back(c);
    }
    return parsedError(text, "unterminated string");
}

[[nodiscard]] Parsed parseLiteralString(std::string_view text)
{
    const usize closing = text.find('\'', 1);
    if (closing == std::string_view::npos)
        return parsedError(text, "unterminated literal string");
    return parsedValue(ScalarValue{.kind = ScalarValue::Kind::String,
                                   .text = std::string(text.substr(1, closing - 1)),
                                   .number = 0.0,
                                   .boolean = false},
                       text.substr(closing + 1));
}

[[nodiscard]] Parsed parseScalar(std::string_view text)
{
    const std::string_view body = trim(text);
    if (body.empty())
        return parsedError(body, "expected a value");

    if (body.front() == '"')
        return parseBasicString(body);
    if (body.front() == '\'')
        return parseLiteralString(body);
    if (body.front() == '[')
        return parsedError(body, "a nested array is not part of this subset");

    const usize end = body.find_first_of(",] \t");
    const std::string_view token = body.substr(0, end == std::string_view::npos ? body.size() : end);
    const std::string_view rest = body.substr(token.size());

    if (token == "true")
        return parsedValue(ScalarValue{.kind = ScalarValue::Kind::Boolean, .text = {}, .number = 0.0, .boolean = true},
                           rest);
    if (token == "false")
        return parsedValue(ScalarValue{.kind = ScalarValue::Kind::Boolean, .text = {}, .number = 0.0, .boolean = false},
                           rest);

    // Underscores are legal digit separators in TOML numbers, so the token is
    // copied to strip them before it is read.
    std::string digits;
    digits.reserve(token.size());
    for (const char c : token) {
        if (c != '_')
            digits.push_back(c);
    }
    // A number, if the whole token is one. `decimalToDouble` reads what strtod
    // reads, so a trailing-garbage token like `12abc` would come back as 12 --
    // which is why the token is checked for shape first rather than after.
    if (!digits.empty() && digits.find_first_not_of("+-.eE0123456789") == std::string::npos) {
        f64 parsedNumber = 0.0;
        if (detail::decimalToDouble(digits, parsedNumber))
            return parsedValue(
                ScalarValue{.kind = ScalarValue::Kind::Number, .text = {}, .number = parsedNumber, .boolean = false},
                rest);
    }

    return parsedError(body, "not a value this reader supports: " + std::string(token));
}

// Splits `a.b.c` into a normalised dotted key, honouring quoted segments.
// Returns false when a segment is malformed.
[[nodiscard]] bool splitKey(std::string_view text, std::string& out)
{
    out.clear();
    std::string current;
    bool wroteSegment = false;

    const auto flush = [&]() {
        if (!out.empty())
            out.push_back('.');
        out += current;
        current.clear();
        wroteSegment = true;
    };

    for (usize index = 0; index < text.size();) {
        const char c = text[index];
        if (c == '"' || c == '\'') {
            const Parsed parsed =
                c == '"' ? parseBasicString(text.substr(index)) : parseLiteralString(text.substr(index));
            if (!parsed.ok)
                return false;
            current += parsed.value.text;
            index = text.size() - parsed.rest.size();
            continue;
        }
        if (c == '.') {
            flush();
            ++index;
            continue;
        }
        if (c == ' ' || c == '\t') {
            ++index;
            continue;
        }
        current.push_back(c);
        ++index;
    }

    if (current.empty() && !wroteSegment)
        return false;
    flush();
    return !out.empty() && out.front() != '.' && out.back() != '.';
}

} // namespace

TomlDocument::ParseResult TomlDocument::parse(std::string_view text, std::string_view sourceName)
{
    entries_.clear();

    const auto fail = [&](usize line, const std::string& what) {
        return ParseResult{.ok = false,
                           .diagnostic = std::string(sourceName) + ":" + std::to_string(line) + ": " + what,
                           .line = line};
    };

    std::string section;
    usize lineNumber = 0;
    usize cursor = 0;

    while (cursor <= text.size()) {
        const usize newline = text.find('\n', cursor);
        std::string_view raw =
            text.substr(cursor, newline == std::string_view::npos ? std::string_view::npos : newline - cursor);
        cursor = newline == std::string_view::npos ? text.size() + 1 : newline + 1;
        ++lineNumber;

        if (!raw.empty() && raw.back() == '\r')
            raw.remove_suffix(1);

        const std::string_view line = trim(stripComment(raw));
        if (line.empty())
            continue;

        if (line.front() == '[') {
            if (line.size() > 1 && line[1] == '[')
                return fail(lineNumber, "an array of tables is not part of this subset");
            if (line.back() != ']')
                return fail(lineNumber, "a table header ends with ]");
            if (!splitKey(line.substr(1, line.size() - 2), section))
                return fail(lineNumber, "a table header names a key");
            continue;
        }

        const usize equals = line.find('=');
        if (equals == std::string_view::npos)
            return fail(lineNumber, "a line is a table header or key = value");

        std::string key;
        if (!splitKey(line.substr(0, equals), key))
            return fail(lineNumber, "the key on the left of = is malformed");
        if (!section.empty())
            key = section + "." + key;

        std::string_view rest = trim(line.substr(equals + 1));
        if (rest.empty())
            return fail(lineNumber, "a key with no value");

        Entry entry;
        entry.key = key;

        if (rest.front() == '[') {
            rest = trim(rest.substr(1));
            bool sawNumber = false;
            bool sawString = false;
            while (true) {
                if (rest.empty())
                    return fail(lineNumber, "an array is not closed on its own line");
                if (rest.front() == ']') {
                    rest = trim(rest.substr(1));
                    break;
                }
                const Parsed parsed = parseScalar(rest);
                if (!parsed.ok)
                    return fail(lineNumber, parsed.error);
                switch (parsed.value.kind) {
                case ScalarValue::Kind::Number:
                    entry.numbers.push_back(parsed.value.number);
                    sawNumber = true;
                    break;
                case ScalarValue::Kind::String:
                    entry.strings.push_back(parsed.value.text);
                    sawString = true;
                    break;
                case ScalarValue::Kind::Boolean:
                    return fail(lineNumber, "an array of booleans has no reader here");
                }
                rest = trim(parsed.rest);
                if (!rest.empty() && rest.front() == ',')
                    rest = trim(rest.substr(1));
                else if (rest.empty() || rest.front() != ']')
                    return fail(lineNumber, "expected , or ] in an array");
            }
            if (sawNumber && sawString)
                return fail(lineNumber, "a mixed array has no reader here");
            entry.kind = sawString ? Kind::StringArray : Kind::NumberArray;
        }
        else {
            const Parsed parsed = parseScalar(rest);
            if (!parsed.ok)
                return fail(lineNumber, parsed.error);
            if (!trim(parsed.rest).empty())
                return fail(lineNumber, "trailing text after a value");
            switch (parsed.value.kind) {
            case ScalarValue::Kind::String:
                entry.kind = Kind::String;
                entry.text = parsed.value.text;
                break;
            case ScalarValue::Kind::Number:
                entry.kind = Kind::Number;
                entry.number = parsed.value.number;
                break;
            case ScalarValue::Kind::Boolean:
                entry.kind = Kind::Boolean;
                entry.boolean = parsed.value.boolean;
                break;
            }
        }

        // Last write wins, which is what a duplicate key in a hand-edited file
        // most likely means. Refusing it would be defensible and is not what the
        // CLI's reader does, and the two agreeing matters more.
        const auto existing = std::find_if(entries_.begin(), entries_.end(),
                                           [&](const Entry& candidate) { return candidate.key == entry.key; });
        if (existing != entries_.end())
            *existing = std::move(entry);
        else
            entries_.push_back(std::move(entry));
    }

    return ParseResult{.ok = true, .diagnostic = {}, .line = 0};
}

const TomlDocument::Entry* TomlDocument::find(std::string_view key) const noexcept
{
    for (const Entry& entry : entries_) {
        if (entry.key == key)
            return &entry;
    }
    return nullptr;
}

bool TomlDocument::has(std::string_view key) const noexcept
{
    return find(key) != nullptr;
}

std::optional<std::string_view> TomlDocument::string(std::string_view key) const
{
    const Entry* entry = find(key);
    if (entry == nullptr || entry->kind != Kind::String)
        return std::nullopt;
    return std::string_view{entry->text};
}

std::optional<f64> TomlDocument::number(std::string_view key) const
{
    const Entry* entry = find(key);
    if (entry == nullptr || entry->kind != Kind::Number)
        return std::nullopt;
    return entry->number;
}

std::optional<bool> TomlDocument::boolean(std::string_view key) const
{
    const Entry* entry = find(key);
    if (entry == nullptr || entry->kind != Kind::Boolean)
        return std::nullopt;
    return entry->boolean;
}

std::span<const f64> TomlDocument::numbers(std::string_view key) const
{
    const Entry* entry = find(key);
    if (entry == nullptr || entry->kind != Kind::NumberArray)
        return {};
    return entry->numbers;
}

} // namespace luaug::core
