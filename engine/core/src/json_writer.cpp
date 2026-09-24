#include "luaug/core/json_writer.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace luaug::core {
namespace {

// Enough for any double `%.17g` produces, which is the shortest form that round
// trips. 32 leaves room for the sign, the exponent and the terminator.
constexpr usize kNumberBuffer = 32;

} // namespace

std::string jsonQuote(std::string_view text)
{
    static constexpr char kHex[] = "0123456789abcdef";

    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');

    for (const char raw : text) {
        const auto byte = static_cast<u8>(raw);
        switch (raw) {
        case '"':
            out.append("\\\"");
            break;
        case '\\':
            out.append("\\\\");
            break;
        case '\n':
            out.append("\\n");
            break;
        case '\r':
            out.append("\\r");
            break;
        case '\t':
            out.append("\\t");
            break;
        case '\b':
            out.append("\\b");
            break;
        case '\f':
            out.append("\\f");
            break;
        default:
            if (byte < 0x20u) {
                // The only characters JSON forbids raw. Everything above is
                // passed through as-is, which keeps UTF-8 intact byte for byte
                // rather than re-encoding it into escapes nobody asked for.
                out.append("\\u00");
                out.push_back(kHex[(byte >> 4) & 0x0Fu]);
                out.push_back(kHex[byte & 0x0Fu]);
            }
            else {
                out.push_back(raw);
            }
            break;
        }
    }

    out.push_back('"');
    return out;
}

void JsonWriter::newline(usize depth)
{
    m_text.push_back('\n');
    m_text.append(2 * depth, ' ');
}

void JsonWriter::separate()
{
    if (m_expectingValue) {
        m_expectingValue = false;
        return;
    }
    if (!m_populated.empty()) {
        const bool first = m_populated.back() == '\0';
        if (!first)
            m_text.push_back(',');
        m_populated.back() = '\1';
        if (m_layout == JsonLayout::Indented) {
            if (m_inline.back() == '\0')
                newline(m_populated.size());
            else if (!first)
                m_text.push_back(' ');
        }
    }
}

void JsonWriter::beginObject()
{
    separate();
    m_text.push_back('{');
    m_populated.push_back('\0');
    m_inline.push_back('\0');
}

void JsonWriter::endObject()
{
    if (!m_populated.empty()) {
        if (m_layout == JsonLayout::Indented && m_populated.back() != '\0' && m_inline.back() == '\0')
            newline(m_populated.size() - 1);
        m_populated.pop_back();
        m_inline.pop_back();
    }
    m_text.push_back('}');
}

void JsonWriter::beginArray()
{
    separate();
    m_text.push_back('[');
    m_populated.push_back('\0');
    m_inline.push_back('\0');
}

void JsonWriter::beginInlineArray()
{
    separate();
    m_text.push_back('[');
    m_populated.push_back('\0');
    m_inline.push_back('\1');
}

void JsonWriter::endArray()
{
    if (!m_populated.empty()) {
        if (m_layout == JsonLayout::Indented && m_populated.back() != '\0' && m_inline.back() == '\0')
            newline(m_populated.size() - 1);
        m_populated.pop_back();
        m_inline.pop_back();
    }
    m_text.push_back(']');
}

void JsonWriter::key(std::string_view name)
{
    separate();
    m_text.append(jsonQuote(name));
    m_text.push_back(':');
    if (m_layout == JsonLayout::Indented)
        m_text.push_back(' ');
    m_expectingValue = true;
}

void JsonWriter::value(std::string_view text)
{
    separate();
    m_text.append(jsonQuote(text));
}

void JsonWriter::value(bool flag)
{
    separate();
    m_text.append(flag ? "true" : "false");
}

void JsonWriter::value(f64 number)
{
    separate();

    // JSON has no spelling for these, and emitting the C library's `inf` or
    // `nan` would produce a document nothing can read back. Null is the one
    // answer every parser accepts, and it is honest about the value being
    // outside what the format carries.
    if (!std::isfinite(number)) {
        m_text.append("null");
        return;
    }

    std::array<char, kNumberBuffer> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%.17g", number);
    if (written > 0)
        m_text.append(buffer.data(), static_cast<usize>(written));
}

void JsonWriter::value(i64 number)
{
    separate();
    m_text.append(std::to_string(number));
}

void JsonWriter::value(u64 number)
{
    separate();
    m_text.append(std::to_string(number));
}

void JsonWriter::valueFloat(f32 number)
{
    separate();
    if (!std::isfinite(number)) {
        m_text.append("null");
        return;
    }
    // Nine significant digits always round-trip a float; the loop finds the
    // fewest that already do. `%g` rather than a hand-written formatter, so
    // this is the C library's rounding and nobody else's.
    std::array<char, kNumberBuffer> buffer{};
    int written = 0;
    for (int digits = 1; digits <= 9; ++digits) {
        written = std::snprintf(buffer.data(), buffer.size(), "%.*g", digits, static_cast<f64>(number));
        if (written > 0 && std::strtof(buffer.data(), nullptr) == number)
            break;
    }
    if (written > 0)
        m_text.append(buffer.data(), static_cast<usize>(written));
}

void JsonWriter::nullValue()
{
    separate();
    m_text.append("null");
}

void JsonWriter::field(std::string_view name, std::string_view text)
{
    key(name);
    value(text);
}

void JsonWriter::field(std::string_view name, bool flag)
{
    key(name);
    value(flag);
}

void JsonWriter::field(std::string_view name, f64 number)
{
    key(name);
    value(number);
}

void JsonWriter::field(std::string_view name, i64 number)
{
    key(name);
    value(number);
}

void JsonWriter::field(std::string_view name, u64 number)
{
    key(name);
    value(number);
}

void JsonWriter::fieldFloat(std::string_view name, f32 number)
{
    key(name);
    valueFloat(number);
}

void JsonWriter::clear()
{
    m_text.clear();
    m_populated.clear();
    m_inline.clear();
    m_expectingValue = false;
}

} // namespace luaug::core
