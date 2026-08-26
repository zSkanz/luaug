#include "luaug/core/toml_edit.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace luaug::core {

namespace {

[[nodiscard]] std::string_view trim(std::string_view text) noexcept
{
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!text.empty() && space(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && space(text.back()))
        text.remove_suffix(1);
    return text;
}

// A bare key as the reader accepts it, or the contents of a quoted one. Quoted
// keys are unwrapped so that `"quality"` and `quality` name the same setting --
// the reader treats them as one, and a writer that did not would append a second
// line beside the first.
[[nodiscard]] std::string_view unquoteKey(std::string_view key) noexcept
{
    if (key.size() >= 2 && (key.front() == '"' || key.front() == '\'') && key.back() == key.front())
        return key.substr(1, key.size() - 2);
    return key;
}

// The offset of the first `=` that is not inside a string, or npos.
//
// **Scanned rather than split**, because a quoted key may contain one. What is
// to the left of it is the key and what is to the right is the value.
[[nodiscard]] usize assignmentAt(std::string_view line) noexcept
{
    bool inBasic = false;
    bool inLiteral = false;
    for (usize at = 0; at < line.size(); ++at) {
        const char c = line[at];
        if (c == '"' && !inLiteral)
            inBasic = !inBasic;
        else if (c == '\'' && !inBasic)
            inLiteral = !inLiteral;
        else if (c == '=' && !inBasic && !inLiteral)
            return at;
    }
    return std::string_view::npos;
}

// Where a `#` starts a comment, or npos. Same scan and same reason: a `#` inside
// a string is part of the value.
[[nodiscard]] usize commentAt(std::string_view text) noexcept
{
    bool inBasic = false;
    bool inLiteral = false;
    for (usize at = 0; at < text.size(); ++at) {
        const char c = text[at];
        if (c == '"' && !inLiteral)
            inBasic = !inBasic;
        else if (c == '\'' && !inBasic)
            inLiteral = !inLiteral;
        else if (c == '#' && !inBasic && !inLiteral)
            return at;
    }
    return std::string_view::npos;
}

// The table a `[header]` line names, or nothing when the line is not one.
[[nodiscard]] std::optional<std::string_view> headerOnLine(std::string_view line) noexcept
{
    const std::string_view text = trim(line);
    if (text.size() < 2 || text.front() != '[' || text.back() != ']')
        return std::nullopt;
    // `[[array of tables]]` is outside the subset the reader accepts, and
    // reading it as a table named `[x]` would put a key somewhere it does not
    // belong.
    if (text.size() >= 4 && text[1] == '[')
        return std::nullopt;
    return trim(text.substr(1, text.size() - 2));
}

struct Split
{
    std::string_view table;
    std::string_view key;
};

[[nodiscard]] Split splitKey(std::string_view dotted) noexcept
{
    const usize dot = dotted.rfind('.');
    if (dot == std::string_view::npos)
        return Split{std::string_view{}, dotted};
    return Split{dotted.substr(0, dot), dotted.substr(dot + 1)};
}

} // namespace

std::string tomlString(std::string_view value)
{
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (const char c : value) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    out.push_back('"');
    return out;
}

std::string tomlNumber(f64 value)
{
    // **An integral float is written as an integer**, because that is what the
    // file already says: `size = [1600, 900]` round-tripped as `1600.0` would be
    // a diff on every save for a value nobody changed. The reader coerces
    // neither way -- it answers `number` for both -- so nothing downstream can
    // tell them apart.
    std::array<char, 40> buffer{};
    if (std::isfinite(value) && value == std::floor(value) && std::abs(value) < 1.0e15) {
        (void)std::snprintf(buffer.data(), buffer.size(), "%lld", static_cast<long long>(value));
        return std::string(buffer.data());
    }

    (void)std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
    return std::string(buffer.data());
}

std::string tomlBoolean(bool value)
{
    return value ? "true" : "false";
}

std::string tomlNumberArray(std::span<const f64> values)
{
    std::string out = "[";
    for (usize index = 0; index < values.size(); ++index) {
        if (index > 0)
            out += ", ";
        out += tomlNumber(values[index]);
    }
    out += "]";
    return out;
}

std::optional<std::string> setTomlValue(std::string_view text, std::string_view key, std::string_view rendered)
{
    const Split wanted = splitKey(key);
    const std::string_view bareKey = unquoteKey(wanted.key);
    if (bareKey.empty())
        return std::nullopt;

    // Walked as lines so that every byte not on the key's own line survives
    // untouched, which is the whole contract.
    std::string_view current;
    usize lineStart = 0;
    usize insertAt = std::string_view::npos;
    bool sawTable = wanted.table.empty();

    while (lineStart <= text.size()) {
        usize lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string_view::npos)
            lineEnd = text.size();
        const std::string_view line = text.substr(lineStart, lineEnd - lineStart);

        if (const std::optional<std::string_view> header = headerOnLine(line); header.has_value()) {
            // Leaving the key's table: this is where an appended key goes, at
            // the END of the table it belongs to rather than at the end of the
            // file. A key under the wrong header is a key the reader never finds.
            if (current == wanted.table && insertAt == std::string_view::npos)
                insertAt = lineStart;
            current = *header;
            if (current == wanted.table)
                sawTable = true;
        }
        else if (current == wanted.table) {
            const std::string_view bare = trim(line);
            if (!bare.empty() && bare.front() != '#') {
                if (const usize equals = assignmentAt(line);
                    equals != std::string_view::npos && unquoteKey(trim(line.substr(0, equals))) == bareKey) {
                    // **Only the value.** A trailing comment on the line is part
                    // of what somebody wrote about the setting, so it stays --
                    // which means the replacement ends where the comment starts
                    // rather than at the end of the line.
                    const std::string_view rest = line.substr(equals + 1);
                    const usize comment = commentAt(rest);
                    const usize valueBegin = lineStart + equals + 1;
                    const usize valueEnd =
                        comment == std::string_view::npos ? lineStart + line.size() : valueBegin + comment;

                    std::string out;
                    out.reserve(text.size() + rendered.size());
                    out.append(text.substr(0, valueBegin));
                    out.push_back(' ');
                    out.append(rendered);
                    if (comment != std::string_view::npos)
                        out.push_back(' ');
                    out.append(text.substr(valueEnd));
                    return out;
                }
            }
        }

        if (lineEnd == text.size())
            break;
        lineStart = lineEnd + 1;
    }

    // --- Not there, so it is added ------------------------------------------
    //
    // The only case in which this makes the file longer, and it is still
    // surgical: one line under the right header, or a new header at the end when
    // the table does not exist at all.
    const std::string line = std::string(bareKey) + " = " + std::string(rendered) + "\n";

    if (!sawTable) {
        std::string out(text);
        if (!out.empty() && out.back() != '\n')
            out.push_back('\n');
        if (!out.empty())
            out.push_back('\n');
        out += "[" + std::string(wanted.table) + "]\n";
        out += line;
        return out;
    }

    if (insertAt == std::string_view::npos) {
        // The key's table is the last one in the file, so its end is the file's.
        std::string out(text);
        if (!out.empty() && out.back() != '\n')
            out.push_back('\n');
        out += line;
        return out;
    }

    std::string out;
    out.reserve(text.size() + line.size());
    out.append(text.substr(0, insertAt));
    out += line;
    out.append(text.substr(insertAt));
    return out;
}

} // namespace luaug::core
