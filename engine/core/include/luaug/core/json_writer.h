// A small JSON writer, the counterpart to `json.h`'s reader (ADR 0033 and its
// 2026-08-20 addendum).
//
// The reader's header says a writer would be surface with no caller, and that
// was true until M3: the dev control channel sends JSON messages over a
// WebSocket (ADR 0035), and `luaug test` writes a machine-readable report the
// CLI turns into TAP or JUnit rather than parsing catalog-resolved console
// prose. Two callers, both of them producing bytes another program parses.
//
// Deliberately a *writer* and not a DOM: nothing in the engine needs to build a
// document, mutate it and then serialise it. Every message here is written once
// in the order its fields appear, which is also what keeps the output stable
// enough to compare (R10) without sorting anything.
#pragma once

#include "luaug/core/types.h"

#include <string>
#include <string_view>

namespace luaug::core {

// How a writer lays its text out. `Compact` is every message and every scene:
// bytes another program reads. `Indented` is for a file a PERSON authors and
// reviews in a diff -- a material (ADR 0090) -- where one field per line is
// what makes a change to one field a one-line change.
enum class JsonLayout : u8
{
    Compact,
    Indented,
};

class JsonWriter
{
public:
    JsonWriter() = default;
    explicit JsonWriter(JsonLayout layout) : m_layout(layout) {}

    // Objects and arrays. Each `begin` needs its `end`; the writer inserts the
    // separating commas itself, which is the whole reason it is a writer rather
    // than string concatenation at the call sites.
    void beginObject();
    void endObject();
    void beginArray();
    // An array written on one line even in the indented layout: a colour's
    // three numbers read as one value, and three lines for them would not.
    void beginInlineArray();
    void endArray();

    // Names the next value. Only legal inside an object.
    void key(std::string_view name);

    void value(std::string_view text);
    void value(const char* text) { value(std::string_view(text)); }
    void value(bool flag);
    void value(f64 number);
    void value(i64 number);
    void value(u64 number);
    void nullValue();
    // **The shortest decimal that reads back as the same `f32`**, where
    // `value(f64)` writes seventeen digits. A float field authored as 0.7 is
    // 0.699999988 in single precision, and `%.17g` of that is noise a person
    // would have to read past in every diff.
    void valueFloat(f32 number);

    // `key` + `value`, which is what almost every call site wants.
    void field(std::string_view name, std::string_view text);
    void field(std::string_view name, const char* text) { field(name, std::string_view(text)); }
    void field(std::string_view name, bool flag);
    void field(std::string_view name, f64 number);
    void field(std::string_view name, i64 number);
    void field(std::string_view name, u64 number);
    void fieldFloat(std::string_view name, f32 number);

    [[nodiscard]] const std::string& text() const noexcept { return m_text; }
    void clear();

private:
    void separate();
    void newline(usize depth);

    JsonLayout m_layout = JsonLayout::Compact;
    std::string m_text;
    // One flag per open container: whether it already holds something, which is
    // what decides a comma.
    std::string m_populated;
    // Beside it, one flag per open container: whether it is written on one line.
    std::string m_inline;
    // True immediately after `key`, so the value that follows is not preceded
    // by a comma of its own.
    bool m_expectingValue = false;
};

// Escapes `text` as a JSON string, quotes included. Control characters go to
// `\u00XX`, and the two mandatory escapes are `"` and `\`. Exposed because the
// test report writes keys the same way.
[[nodiscard]] std::string jsonQuote(std::string_view text);

} // namespace luaug::core
