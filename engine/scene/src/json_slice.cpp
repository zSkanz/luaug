#include "json_slice.h"

namespace luaug::scene::jsonslice {
namespace {

[[nodiscard]] bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// One past the closing quote of the string starting at `at`, which must be the
// opening quote. A backslash consumes the byte after it whatever that byte is,
// which is all a SCANNER needs: `\"` must not end the string, and whether the
// escape is one this format writes is the decoder's question, not this one's.
[[nodiscard]] usize endOfString(std::string_view text, usize at) noexcept
{
    for (usize i = at + 1; i < text.size(); ++i) {
        if (text[i] == '\\') {
            ++i;
            continue;
        }
        if (text[i] == '"') {
            return i + 1;
        }
    }
    return std::string_view::npos;
}

// The nesting limit `core::json.h` also carries, and for the same reason: a
// corrupt or hostile file must hit a named refusal rather than recurse this
// into the stack guard. This one is a loop rather than a recursion, so the
// limit bounds the counter instead of the stack -- but a document nested two
// hundred deep is a document nothing here should accept either way.
constexpr usize MaxDepth = 128;

} // namespace

usize skipSpace(std::string_view text, usize at) noexcept
{
    while (at < text.size() && isSpace(text[at])) {
        ++at;
    }
    return at;
}

usize endOfValue(std::string_view text, usize at) noexcept
{
    if (at >= text.size()) {
        return std::string_view::npos;
    }
    if (text[at] == '"') {
        return endOfString(text, at);
    }

    if (text[at] != '{' && text[at] != '[') {
        // A number, `true`, `false` or `null`: it ends at the first byte that
        // cannot continue one. Deliberately not validated -- a scanner that
        // rejected `01` would be a second opinion about what JSON is.
        usize i = at;
        while (i < text.size()) {
            const char c = text[i];
            if (c == ',' || c == '}' || c == ']' || isSpace(c)) {
                break;
            }
            ++i;
        }
        return i == at ? std::string_view::npos : i;
    }

    usize depth = 0;
    for (usize i = at; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"') {
            const usize end = endOfString(text, i);
            if (end == std::string_view::npos) {
                return std::string_view::npos;
            }
            // -1 because the loop's own ++i advances past the closing quote.
            i = end - 1;
            continue;
        }
        if (c == '{' || c == '[') {
            if (++depth > MaxDepth) {
                return std::string_view::npos;
            }
            continue;
        }
        if (c == '}' || c == ']') {
            if (--depth == 0) {
                return i + 1;
            }
        }
    }
    return std::string_view::npos;
}

std::optional<std::string_view> member(std::string_view object, std::string_view key)
{
    std::optional<std::string_view> found;
    forEachMember(object, [&](std::string_view name, std::string_view value) {
        if (!found.has_value() && name == key) {
            found = value;
        }
    });
    return found;
}

std::optional<std::string> unquote(std::string_view text)
{
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') {
        return std::nullopt;
    }

    std::string out;
    out.reserve(text.size() - 2);
    for (usize i = 1; i + 1 < text.size(); ++i) {
        const char c = text[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (i + 2 >= text.size()) {
            return std::nullopt;
        }
        const char escape = text[++i];
        switch (escape) {
        case '"':
            out.push_back('"');
            break;
        case '\\':
            out.push_back('\\');
            break;
        case '/':
            out.push_back('/');
            break;
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'u': {
            // `jsonQuote` emits `\u00XX` and nothing else, so this decodes the
            // range it can and refuses the rest rather than guessing at a
            // surrogate pair. A refusal here keeps the instance authored, which
            // is the safe answer; a wrong decode would rename it.
            if (i + 4 >= text.size()) {
                return std::nullopt;
            }
            core::u32 code = 0;
            for (usize digit = 0; digit < 4; ++digit) {
                const char hex = text[i + 1 + digit];
                code <<= 4;
                if (hex >= '0' && hex <= '9') {
                    code |= static_cast<core::u32>(hex - '0');
                }
                else if (hex >= 'a' && hex <= 'f') {
                    code |= static_cast<core::u32>(hex - 'a' + 10);
                }
                else if (hex >= 'A' && hex <= 'F') {
                    code |= static_cast<core::u32>(hex - 'A' + 10);
                }
                else {
                    return std::nullopt;
                }
            }
            i += 4;
            if (code > 0x7F) {
                return std::nullopt;
            }
            out.push_back(static_cast<char>(code));
            break;
        }
        default:
            return std::nullopt;
        }
    }
    return out;
}

} // namespace luaug::scene::jsonslice
