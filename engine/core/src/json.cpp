#include "luaug/core/json.h"

#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <system_error>
#include <utility>

namespace luaug::core {
namespace {

// Bounds the recursion rather than the grammar: the parser descends with the
// document, so a corrupt or hostile file that is nothing but '[' must reach a
// diagnostic long before it reaches the stack guard. Far above anything real --
// the deepest document the engine reads, a shader reflection sidecar, is four
// levels.
constexpr usize kMaxDepth = 128;

constexpr bool isDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

void appendUtf8(std::string& out, u32 codepoint)
{
    if (codepoint < 0x80u) {
        out.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
    else if (codepoint < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
    else {
        out.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
}

// strtod rather than std::from_chars: the floating-point overloads of
// from_chars are still missing from one of the standard libraries the engine
// builds against, and strtod is the correctly-rounded conversion available on
// all of them. It reads the *locale's* decimal separator, so the separator is
// substituted rather than assumed -- nothing here calls setlocale, but a
// dependency that does must not silently turn 1.5 into 1.
//
// False when the magnitude overflows to infinity. JSON cannot express that and
// no caller can use it, so it is a diagnostic rather than a HUGE_VAL that looks
// like a number. Underflow to zero is left alone: it is the value.
bool decimalToDouble(std::string_view token, f64& out)
{
    std::string buffer(token);

    const char* separator = std::localeconv()->decimal_point;
    if (separator != nullptr && std::string_view{separator} != ".") {
        const usize dot = buffer.find('.');
        if (dot != std::string::npos)
            buffer.replace(dot, 1, separator);
    }

    const f64 value = std::strtod(buffer.c_str(), nullptr);
    if (std::isinf(value))
        return false;

    out = value;
    return true;
}

} // namespace

struct JsonValue::Node
{
    // An offset and a size into the document's decoded-text pool, not a
    // string_view: the pool grows as parsing proceeds, and a view into it would
    // dangle the first time it reallocates.
    struct Text
    {
        usize offset = 0;
        usize size = 0;
    };

    JsonType type = JsonType::Null;
    bool boolean = false;
    // Set when `number` is exactly an integer that an i64 can hold; see
    // JsonValue::asInteger.
    bool integral = false;
    f64 number = 0.0;
    i64 integer = 0;
    Text text;
    // An object's `keys` are parallel to `children` and in written order. An
    // array has children and no keys.
    std::vector<Text> keys;
    std::vector<usize> children;
};

struct JsonDocument::Impl
{
    std::vector<JsonValue::Node> nodes;
    // Every decoded string in the document, concatenated. Nodes hold offsets
    // into it, so one growing buffer replaces a std::string per node.
    std::string pool;
    usize rootIndex = 0;
    bool ok = false;

    [[nodiscard]] std::string_view view(const JsonValue::Node::Text& text) const noexcept
    {
        return std::string_view{pool}.substr(text.offset, text.size);
    }

    // Recursive descent, writing straight into the node table. Only the first
    // failure is recorded: everything after it is a consequence of a position
    // the parser no longer trusts.
    struct Parser
    {
        Impl& document;
        std::string_view source;
        usize pos = 0;
        // The initializer is not decoration. This struct is aggregate-
        // initialised with two members named, and Clang's
        // -Wmissing-field-initializers -- an error here -- fires on any trailing
        // member that has neither an explicit initialiser nor a default one.
        // MSVC says nothing, so the Linux tier is what catches it.
        std::string error{};

        bool run()
        {
            usize root = 0;
            if (!parseValue(0, root))
                return false;
            document.rootIndex = root;

            skipSpace();
            if (pos != source.size())
                return fail("trailing content after the document");
            return true;
        }

        [[nodiscard]] char peek() const noexcept { return pos < source.size() ? source[pos] : '\0'; }

        void advance() noexcept
        {
            if (pos < source.size())
                ++pos;
        }

        void skipSpace() noexcept
        {
            while (pos < source.size()) {
                const char c = source[pos];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                    ++pos;
                else
                    break;
            }
        }

        bool failAt(usize offset, std::string_view what)
        {
            if (error.empty())
                error = std::string{what} + " at byte " + std::to_string(offset);
            return false;
        }

        bool fail(std::string_view what) { return failAt(pos, what); }

        bool expect(char c)
        {
            if (peek() != c) {
                char message[16];
                std::snprintf(message, sizeof(message), "expected '%c'", c);
                return fail(message);
            }
            advance();
            return true;
        }

        usize addNode(JsonType type)
        {
            document.nodes.emplace_back();
            document.nodes.back().type = type;
            return document.nodes.size() - 1;
        }

        JsonValue::Node::Text addText(std::string_view decoded)
        {
            const JsonValue::Node::Text text{document.pool.size(), decoded.size()};
            document.pool.append(decoded);
            return text;
        }

        bool parseValue(usize depth, usize& index)
        {
            if (depth >= kMaxDepth)
                return fail("nesting deeper than " + std::to_string(kMaxDepth) + " levels");

            skipSpace();
            const char c = peek();
            switch (c) {
            case '{':
                return parseObject(depth, index);
            case '[':
                return parseArray(depth, index);
            case '"': {
                std::string decoded;
                if (!readString(decoded))
                    return false;
                index = addNode(JsonType::String);
                const JsonValue::Node::Text text = addText(decoded);
                document.nodes[index].text = text;
                return true;
            }
            case 't':
                return parseLiteral("true", JsonType::Boolean, true, index);
            case 'f':
                return parseLiteral("false", JsonType::Boolean, false, index);
            case 'n':
                return parseLiteral("null", JsonType::Null, false, index);
            // The three near-misses that a lenient parser would take, each named
            // rather than reported as "expected a value": every one of them is a
            // file written against a different dialect, and saying which dialect
            // is what turns the diagnostic into a fix.
            case '\'':
                return fail("a JSON string must be double-quoted");
            case '/':
                return fail("comments are not valid JSON");
            case 'N':
                return fail("NaN is not a valid JSON number");
            case 'I':
                return fail("Infinity is not a valid JSON number");
            default:
                break;
            }

            if (c == '-' || isDigit(c))
                return parseNumber(index);
            return fail("expected a value");
        }

        bool parseLiteral(std::string_view word, JsonType type, bool boolean, usize& index)
        {
            if (source.size() - pos < word.size() || source.compare(pos, word.size(), word) != 0)
                return fail("expected '" + std::string{word} + "'");

            pos += word.size();
            index = addNode(type);
            document.nodes[index].boolean = boolean;
            return true;
        }

        bool parseObject(usize depth, usize& index)
        {
            const usize self = addNode(JsonType::Object);
            advance(); // '{'

            std::vector<JsonValue::Node::Text> keys;
            std::vector<usize> children;

            skipSpace();
            if (peek() == '}') {
                advance();
                index = self;
                return true;
            }

            for (;;) {
                skipSpace();
                // Only reachable after a comma: the empty object left above.
                if (peek() == '}')
                    return fail("trailing comma before '}'");
                if (peek() != '"')
                    return fail("an object key must be a double-quoted string");

                std::string key;
                if (!readString(key))
                    return false;

                skipSpace();
                if (!expect(':'))
                    return false;

                usize child = 0;
                if (!parseValue(depth + 1, child))
                    return false;

                keys.push_back(addText(key));
                children.push_back(child);

                skipSpace();
                const char delimiter = peek();
                if (delimiter == ',') {
                    advance();
                    continue;
                }
                if (delimiter == '}') {
                    advance();
                    break;
                }
                return fail("expected ',' or '}' after an object member");
            }

            document.nodes[self].keys = std::move(keys);
            document.nodes[self].children = std::move(children);
            index = self;
            return true;
        }

        bool parseArray(usize depth, usize& index)
        {
            const usize self = addNode(JsonType::Array);
            advance(); // '['

            std::vector<usize> children;

            skipSpace();
            if (peek() == ']') {
                advance();
                index = self;
                return true;
            }

            for (;;) {
                skipSpace();
                if (peek() == ']')
                    return fail("trailing comma before ']'");

                usize child = 0;
                if (!parseValue(depth + 1, child))
                    return false;
                children.push_back(child);

                skipSpace();
                const char delimiter = peek();
                if (delimiter == ',') {
                    advance();
                    continue;
                }
                if (delimiter == ']') {
                    advance();
                    break;
                }
                return fail("expected ',' or ']' after an array element");
            }

            document.nodes[self].children = std::move(children);
            index = self;
            return true;
        }

        bool parseNumber(usize& index)
        {
            const usize start = pos;

            if (peek() == '-')
                advance();

            if (peek() == '0') {
                advance();
                // "01" is a typo in every file that contains it, and a lenient
                // parser would read it as two tokens.
                if (isDigit(peek()))
                    return fail("a number may not have a leading zero");
            }
            else if (isDigit(peek())) {
                while (isDigit(peek()))
                    advance();
            }
            else {
                return fail("expected a digit in a number");
            }

            // Whole syntax means no fraction and no exponent, which is the only
            // form whose digits can be trusted to an i64 without going through
            // a double first.
            bool wholeSyntax = true;

            if (peek() == '.') {
                advance();
                if (!isDigit(peek()))
                    return fail("expected a digit after the decimal point");
                while (isDigit(peek()))
                    advance();
                wholeSyntax = false;
            }

            if (peek() == 'e' || peek() == 'E') {
                advance();
                if (peek() == '+' || peek() == '-')
                    advance();
                if (!isDigit(peek()))
                    return fail("expected a digit in the exponent");
                while (isDigit(peek()))
                    advance();
                wholeSyntax = false;
            }

            const std::string_view token = source.substr(start, pos - start);

            f64 value = 0.0;
            if (!decimalToDouble(token, value))
                return failAt(start, "number is outside the range of a double");

            index = addNode(JsonType::Number);
            JsonValue::Node& node = document.nodes[index];
            node.number = value;

            if (wholeSyntax) {
                i64 whole = 0;
                const std::from_chars_result parsed = std::from_chars(token.data(), token.data() + token.size(), whole);
                if (parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size()) {
                    node.integral = true;
                    node.integer = whole;
                }
            }

            // Otherwise a number is an integer when its *value* is one and fits
            // an i64: a generator that writes 2.0 or 2e3 means two thousand, and
            // refusing that would be pedantry rather than the absence of
            // coercion. An integer too large for an i64 stays a Number and
            // reports the fallback from asInteger(), because truncating it is
            // exactly the plausible-looking wrong answer this reader exists to
            // avoid.
            if (!node.integral) {
                constexpr f64 kLowest = static_cast<f64>(std::numeric_limits<i64>::min()); // exactly -2^63
                if (std::isfinite(value) && value == std::floor(value) && value >= kLowest && value < -kLowest) {
                    node.integral = true;
                    node.integer = static_cast<i64>(value);
                }
            }

            return true;
        }

        bool readHex4(u32& out)
        {
            if (pos + 4 > source.size())
                return fail("truncated \\u escape");

            u32 value = 0;
            for (usize i = 0; i < 4; ++i) {
                const char c = source[pos + i];
                u32 digit = 0;
                if (c >= '0' && c <= '9')
                    digit = static_cast<u32>(c - '0');
                else if (c >= 'a' && c <= 'f')
                    digit = static_cast<u32>(c - 'a') + 10u;
                else if (c >= 'A' && c <= 'F')
                    digit = static_cast<u32>(c - 'A') + 10u;
                else
                    return failAt(pos + i, "invalid hex digit in \\u escape");
                value = (value << 4) | digit;
            }
            pos += 4;
            out = value;
            return true;
        }

        bool readString(std::string& text)
        {
            if (!expect('"'))
                return false;

            text.clear();
            for (;;) {
                if (pos >= source.size())
                    return fail("unterminated string");

                const char c = source[pos];
                if (c == '"') {
                    advance();
                    return true;
                }

                // A raw control character must be escaped (RFC 8259). Accepting
                // one would let a missing closing quote swallow the rest of the
                // file and report the failure a long way from its cause.
                if (static_cast<unsigned char>(c) < 0x20u)
                    return fail("a control character in a string must be escaped");

                if (c != '\\') {
                    text.push_back(c);
                    advance();
                    continue;
                }

                advance();
                if (pos >= source.size())
                    return fail("unterminated escape");

                const char esc = source[pos];
                advance();
                switch (esc) {
                case '"':
                    text.push_back('"');
                    break;
                case '\\':
                    text.push_back('\\');
                    break;
                case '/':
                    text.push_back('/');
                    break;
                case 'b':
                    text.push_back('\b');
                    break;
                case 'f':
                    text.push_back('\f');
                    break;
                case 'n':
                    text.push_back('\n');
                    break;
                case 'r':
                    text.push_back('\r');
                    break;
                case 't':
                    text.push_back('\t');
                    break;
                case 'u': {
                    u32 codepoint = 0;
                    if (!readHex4(codepoint))
                        return false;

                    if (codepoint >= 0xD800u && codepoint <= 0xDBFFu) {
                        // Half a character. UTF-8 has no encoding for a lone
                        // surrogate, so emitting one would produce bytes that
                        // every downstream decoder rejects -- later, and
                        // somewhere else.
                        if (pos + 1 >= source.size() || source[pos] != '\\' || source[pos + 1] != 'u')
                            return fail("a high surrogate must be followed by a low surrogate");
                        pos += 2;

                        u32 low = 0;
                        if (!readHex4(low))
                            return false;
                        if (low < 0xDC00u || low > 0xDFFFu)
                            return fail("invalid low surrogate in \\u escape");
                        codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) + (low - 0xDC00u);
                    }
                    else if (codepoint >= 0xDC00u && codepoint <= 0xDFFFu) {
                        return fail("a low surrogate must follow a high surrogate");
                    }

                    appendUtf8(text, codepoint);
                    break;
                }
                default:
                    return fail("unsupported escape sequence");
                }
            }
        }
    };
};

JsonDocument::JsonDocument() : impl_(std::make_unique<Impl>())
{}

JsonDocument::~JsonDocument() = default;
JsonDocument::JsonDocument(JsonDocument&&) noexcept = default;
JsonDocument& JsonDocument::operator=(JsonDocument&&) noexcept = default;

JsonDocument::ParseResult JsonDocument::parse(std::string_view json, std::string_view sourceName)
{
    // A moved-from document is empty rather than unusable; re-parsing into it
    // is the cheapest way to keep that true.
    if (impl_ == nullptr)
        impl_ = std::make_unique<Impl>();

    impl_->nodes.clear();
    impl_->pool.clear();
    impl_->rootIndex = 0;
    impl_->ok = false;

    Impl::Parser parser{*impl_, json};
    if (!parser.run())
        return ParseResult{false, std::string{sourceName} + ": " + parser.error};

    impl_->ok = true;
    return ParseResult{true, {}};
}

JsonValue JsonDocument::root() const noexcept
{
    if (impl_ == nullptr || !impl_->ok)
        return JsonValue{};
    return JsonValue{&impl_->nodes[impl_->rootIndex], impl_.get()};
}

JsonType JsonValue::type() const noexcept
{
    return node_ != nullptr ? node_->type : JsonType::Null;
}

bool JsonValue::asBool(bool fallback) const noexcept
{
    return type() == JsonType::Boolean ? node_->boolean : fallback;
}

f64 JsonValue::asNumber(f64 fallback) const noexcept
{
    return type() == JsonType::Number ? node_->number : fallback;
}

i64 JsonValue::asInteger(i64 fallback) const noexcept
{
    return type() == JsonType::Number && node_->integral ? node_->integer : fallback;
}

std::string_view JsonValue::asString(std::string_view fallback) const noexcept
{
    if (type() != JsonType::String)
        return fallback;
    return owner_->view(node_->text);
}

usize JsonValue::size() const noexcept
{
    const JsonType kind = type();
    if (kind == JsonType::Array || kind == JsonType::Object)
        return node_->children.size();
    return 0;
}

JsonValue JsonValue::at(usize index) const noexcept
{
    if (index >= size())
        return JsonValue{};
    return JsonValue{&owner_->nodes[node_->children[index]], owner_};
}

bool JsonValue::has(std::string_view key) const noexcept
{
    if (type() != JsonType::Object)
        return false;

    for (const Node::Text& stored : node_->keys) {
        if (owner_->view(stored) == key)
            return true;
    }
    return false;
}

JsonValue JsonValue::operator[](std::string_view key) const noexcept
{
    if (type() != JsonType::Object)
        return JsonValue{};

    // Duplicate keys: the last one wins, which is what every consumer that does
    // not reject them outright does. Both remain visible through keyAt(), so a
    // caller for whom a duplicate is an error -- the i18n catalog is one -- can
    // still see it by iterating.
    for (usize i = node_->keys.size(); i > 0; --i) {
        if (owner_->view(node_->keys[i - 1]) == key)
            return JsonValue{&owner_->nodes[node_->children[i - 1]], owner_};
    }
    return JsonValue{};
}

std::string_view JsonValue::keyAt(usize index) const noexcept
{
    if (type() != JsonType::Object || index >= node_->keys.size())
        return {};
    return owner_->view(node_->keys[index]);
}

} // namespace luaug::core
