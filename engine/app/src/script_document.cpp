#include "luaug/app/script_document.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <regex>
#include <string>
#include <utility>

namespace luaug::app {
namespace {

using core::f32;
using core::u32;

[[nodiscard]] bool isWordByte(char c) noexcept
{
    const auto value = static_cast<unsigned char>(c);
    return value == '_' || std::isalnum(value) != 0 || value >= 0x80;
}

// A UTF-8 continuation byte, which is never a place a caret may sit.
[[nodiscard]] bool isContinuation(char c) noexcept
{
    return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u;
}

[[nodiscard]] char lowerByte(char c) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// Where `text` ends if it is inserted at `at`. The one piece of arithmetic both
// the edit and its inverse need, so it is written once.
[[nodiscard]] Position positionAfter(Position at, std::string_view text) noexcept
{
    Position end = at;
    std::size_t lineStart = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '\n')
            continue;
        ++end.line;
        end.column = 0;
        lineStart = index + 1;
    }
    if (lineStart == 0)
        end.column = at.column + static_cast<u32>(text.size());
    else
        end.column = static_cast<u32>(text.size() - lineStart);
    return end;
}

// `\r\n` and a lone `\r` both become `\n`, so a file written on Windows and
// edited here comes back with the endings every other file this engine writes
// already has.
[[nodiscard]] std::string normalizeNewlines(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n')
                ++index;
            out.push_back('\n');
            continue;
        }
        out.push_back(text[index]);
    }
    return out;
}

} // namespace

ScriptDocument::ScriptDocument()
{
    m_lines.emplace_back();
}

ScriptDocument::ScriptDocument(std::string_view text)
{
    if (!setText(text))
        m_lines.emplace_back();
}

bool ScriptDocument::setText(std::string_view text)
{
    m_lines.clear();
    clearHistory();
    m_revision = 0;
    m_diagnosticsRevision = ~0ull;
    m_diagnostics.clear();

    if (text.size() > kMaxDocumentBytes) {
        m_lines.emplace_back();
        return false;
    }

    const std::string normalized = normalizeNewlines(text);
    std::size_t start = 0;
    for (;;) {
        const std::size_t newline = normalized.find('\n', start);
        const std::size_t end = newline == std::string::npos ? normalized.size() : newline;
        if (end - start > kMaxLineBytes) {
            m_lines.clear();
            m_lines.emplace_back();
            return false;
        }
        Line line;
        line.text = normalized.substr(start, end - start);
        m_lines.push_back(std::move(line));
        if (newline == std::string::npos)
            break;
        start = newline + 1;
    }

    // Lexing the whole document once, at load. See the note on `Line::tokens`.
    propagate(0, static_cast<u32>(m_lines.size()) - 1);
    return true;
}

std::string ScriptDocument::text() const
{
    std::size_t total = 0;
    for (const Line& line : m_lines)
        total += line.text.size() + 1;

    std::string out;
    out.reserve(total);
    for (std::size_t index = 0; index < m_lines.size(); ++index) {
        if (index != 0)
            out.push_back('\n');
        out += m_lines[index].text;
    }
    return out;
}

std::string_view ScriptDocument::line(u32 index) const noexcept
{
    return index < m_lines.size() ? std::string_view(m_lines[index].text) : std::string_view{};
}

u32 ScriptDocument::lineLength(u32 index) const noexcept
{
    return index < m_lines.size() ? static_cast<u32>(m_lines[index].text.size()) : 0u;
}

std::span<const Token> ScriptDocument::tokens(u32 index) const
{
    return index < m_lines.size() ? std::span<const Token>(m_lines[index].tokens) : std::span<const Token>{};
}

Position ScriptDocument::clamp(Position at) const noexcept
{
    Position out = at;
    if (out.line >= m_lines.size())
        out.line = static_cast<u32>(m_lines.size()) - 1;
    const std::string& text = m_lines[out.line].text;
    if (out.column > text.size())
        out.column = static_cast<u32>(text.size());
    // Never inside a codepoint: a caret there would split a glyph and an edit
    // there would produce invalid UTF-8.
    while (out.column > 0 && out.column < text.size() && isContinuation(text[out.column]))
        --out.column;
    return out;
}

Position ScriptDocument::nextColumn(Position at) const noexcept
{
    Position from = clamp(at);
    const std::string& text = m_lines[from.line].text;
    if (from.column >= text.size())
        return from.line + 1 < m_lines.size() ? Position{from.line + 1, 0} : from;
    ++from.column;
    while (from.column < text.size() && isContinuation(text[from.column]))
        ++from.column;
    return from;
}

Position ScriptDocument::prevColumn(Position at) const noexcept
{
    Position from = clamp(at);
    if (from.column == 0)
        return from.line == 0 ? from : Position{from.line - 1, lineLength(from.line - 1)};
    const std::string& text = m_lines[from.line].text;
    --from.column;
    while (from.column > 0 && isContinuation(text[from.column]))
        --from.column;
    return from;
}

namespace {

[[nodiscard]] std::string_view trimmed(std::string_view text) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

// A literal number and nothing else, or nothing.
[[nodiscard]] std::optional<double> literalNumber(std::string_view text)
{
    text = trimmed(text);
    if (text.empty() || text.size() > 32)
        return std::nullopt;
    const std::string copy(text);
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value))
        return std::nullopt;
    return value;
}

[[nodiscard]] int hexDigit(char c) noexcept
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

} // namespace

std::optional<ColorLiteral> findColorLiteral(std::string_view line, u32 lineIndex)
{
    struct Method
    {
        std::string_view name;
        ColorLiteralKind kind;
    };
    constexpr Method kMethods[]{{"Color3.new(", ColorLiteralKind::New},
                                {"Color3.fromRGB(", ColorLiteralKind::FromRgb},
                                {"Color3.fromHex(", ColorLiteralKind::FromHex}};
    std::size_t from = 0;
    while (from < line.size()) {
        std::size_t best = std::string_view::npos;
        const Method* found = nullptr;
        for (const Method& method : kMethods) {
            const std::size_t at = line.find(method.name, from);
            if (at != std::string_view::npos && at < best) {
                best = at;
                found = &method;
            }
        }
        if (found == nullptr)
            return std::nullopt;
        const std::size_t open = best + found->name.size();
        const std::size_t close = line.find(')', open);
        from = open;
        if (close == std::string_view::npos)
            return std::nullopt;
        const std::string_view inside = line.substr(open, close - open);
        if (inside.find('(') != std::string_view::npos)
            continue;

        ColorLiteral literal;
        literal.kind = found->kind;
        literal.args = Range{Position{lineIndex, static_cast<u32>(open)}, Position{lineIndex, static_cast<u32>(close)}};
        literal.call =
            Range{Position{lineIndex, static_cast<u32>(best)}, Position{lineIndex, static_cast<u32>(close + 1)}};
        if (found->kind == ColorLiteralKind::FromHex) {
            std::string_view text = trimmed(inside);
            if (text.size() < 2 || (text.front() != '"' && text.front() != '\'') || text.back() != text.front())
                continue;
            text = text.substr(1, text.size() - 2);
            if (!text.empty() && text.front() == '#')
                text.remove_prefix(1);
            if (text.size() != 6)
                continue;
            int channels[3]{};
            bool ok = true;
            for (int channel = 0; channel < 3; ++channel) {
                const int high = hexDigit(text[static_cast<std::size_t>(channel) * 2]);
                const int low = hexDigit(text[static_cast<std::size_t>(channel) * 2 + 1]);
                ok = ok && high >= 0 && low >= 0;
                channels[channel] = high * 16 + low;
            }
            if (!ok)
                continue;
            literal.color = core::Color3{static_cast<f32>(channels[0]) / 255.0f, static_cast<f32>(channels[1]) / 255.0f,
                                         static_cast<f32>(channels[2]) / 255.0f};
            return literal;
        }

        const std::size_t firstComma = inside.find(',');
        const std::size_t secondComma =
            firstComma == std::string_view::npos ? firstComma : inside.find(',', firstComma + 1);
        if (secondComma == std::string_view::npos || inside.find(',', secondComma + 1) != std::string_view::npos)
            continue;
        const std::optional<double> r = literalNumber(inside.substr(0, firstComma));
        const std::optional<double> g = literalNumber(inside.substr(firstComma + 1, secondComma - firstComma - 1));
        const std::optional<double> b = literalNumber(inside.substr(secondComma + 1));
        if (!r || !g || !b)
            continue;
        const double scale = found->kind == ColorLiteralKind::FromRgb ? 1.0 / 255.0 : 1.0;
        literal.color =
            core::Color3{static_cast<f32>(*r * scale), static_cast<f32>(*g * scale), static_cast<f32>(*b * scale)};
        return literal;
    }
    return std::nullopt;
}

std::string formatColorLiteral(ColorLiteralKind kind, core::Color3 color)
{
    const auto clamp01 = [](f32 value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); };
    const auto byte = [&clamp01](f32 value) { return static_cast<int>(std::lround(clamp01(value) * 255.0f)); };
    char buffer[64]{};
    switch (kind) {
    case ColorLiteralKind::FromRgb:
        (void)std::snprintf(buffer, sizeof(buffer), "%d, %d, %d", byte(color.r), byte(color.g), byte(color.b));
        return buffer;
    case ColorLiteralKind::FromHex:
        (void)std::snprintf(buffer, sizeof(buffer), "\"#%02X%02X%02X\"", byte(color.r), byte(color.g), byte(color.b));
        return buffer;
    case ColorLiteralKind::New:
        break;
    }
    // Three decimals at most, and no trailing zeros: `0.5`, not `0.500`.
    const auto decimal = [&clamp01](f32 value) {
        char text[16]{};
        (void)std::snprintf(text, sizeof(text), "%.3f", static_cast<double>(clamp01(value)));
        std::string out(text);
        while (out.size() > 1 && out.back() == '0')
            out.pop_back();
        if (!out.empty() && out.back() == '.')
            out.pop_back();
        return out;
    };
    return decimal(color.r) + ", " + decimal(color.g) + ", " + decimal(color.b);
}

bool ScriptDocument::indentLines(u32 first, u32 last, bool outdent)
{
    if (first > last || first >= lineCount())
        return false;
    last = std::min(last, lineCount() - 1);
    std::string rewritten;
    bool changed = false;
    for (u32 index = first; index <= last; ++index) {
        std::string row(line(index));
        if (outdent) {
            std::size_t drop = 0;
            if (!row.empty() && row[0] == '\t')
                drop = 1;
            else
                while (drop < 4 && drop < row.size() && row[drop] == ' ')
                    ++drop;
            row.erase(0, drop);
            changed = changed || drop > 0;
        }
        else if (!row.empty()) {
            row.insert(0, "\t");
            changed = true;
        }
        rewritten += row;
        if (index < last)
            rewritten.push_back('\n');
    }
    if (!changed)
        return false;
    (void)replace(Range{Position{first, 0}, Position{last, lineLength(last)}}, rewritten);
    return true;
}

bool ScriptDocument::duplicateLines(u32 first, u32 last)
{
    if (first > last || first >= lineCount())
        return false;
    last = std::min(last, lineCount() - 1);
    const std::string block = textIn(Range{Position{first, 0}, Position{last, lineLength(last)}});
    (void)insert(Position{last, lineLength(last)}, "\n" + block);
    return true;
}

bool ScriptDocument::deleteLines(u32 first, u32 last)
{
    if (first > last || first >= lineCount())
        return false;
    last = std::min(last, lineCount() - 1);
    // The newline that ends the block goes with it; the last line of the file
    // has none after it, so it takes the one before instead.
    if (last + 1 < lineCount())
        (void)erase(Range{Position{first, 0}, Position{last + 1, 0}});
    else if (first > 0)
        (void)erase(Range{Position{first - 1, lineLength(first - 1)}, Position{last, lineLength(last)}});
    else if (lineLength(last) > 0 || last > first)
        (void)erase(Range{Position{first, 0}, Position{last, lineLength(last)}});
    else
        return false;
    return true;
}

bool ScriptDocument::toggleComment(u32 first, u32 last)
{
    if (first > last || first >= lineCount())
        return false;
    last = std::min(last, lineCount() - 1);

    bool any = false;
    bool allCommented = true;
    u32 indent = ~0u;
    for (u32 index = first; index <= last; ++index) {
        const std::string_view text = line(index);
        const u32 at = indentOf(index);
        if (at >= text.size())
            continue;
        any = true;
        indent = std::min(indent, at);
        if (text.substr(at, 2) != "--")
            allCommented = false;
    }
    if (!any)
        return false;

    std::string rewritten;
    for (u32 index = first; index <= last; ++index) {
        std::string row(line(index));
        const u32 at = indentOf(index);
        if (at < row.size()) {
            if (allCommented) {
                const std::size_t drop = row.size() > at + 2 && row[at + 2] == ' ' ? 3 : 2;
                row.erase(at, drop);
            }
            else {
                row.insert(indent, "-- ");
            }
        }
        rewritten += row;
        if (index < last)
            rewritten.push_back('\n');
    }
    (void)replace(Range{Position{first, 0}, Position{last, lineLength(last)}}, rewritten);
    return true;
}

bool ScriptDocument::moveLines(u32 first, u32 last, int delta)
{
    if (delta == 0 || first > last || last >= lineCount())
        return false;
    if (delta < 0 && first == 0)
        return false;
    if (delta > 0 && last + 1 >= lineCount())
        return false;

    // The block and the single line it swaps with, rewritten in the other
    // order.
    const u32 from = delta < 0 ? first - 1 : first;
    const u32 to = delta < 0 ? last : last + 1;

    std::string rebuilt;
    // **Counted rather than asked of the string**, because an empty line is a
    // line: `rebuilt.empty()` cannot tell "nothing appended yet" from "appended
    // a blank line", and the blank last line of a file is the common case. With
    // the string asked, moving a line into it swallowed the final newline.
    bool wroteAny = false;
    const auto append = [this, &rebuilt, &wroteAny](u32 index) {
        if (wroteAny)
            rebuilt.push_back('\n');
        wroteAny = true;
        rebuilt.append(line(index));
    };
    if (delta < 0) {
        for (u32 index = first; index <= last; ++index)
            append(index);
        append(first - 1);
    }
    else {
        append(last + 1);
        for (u32 index = first; index <= last; ++index)
            append(index);
    }

    (void)replace(Range{Position{from, 0}, Position{to, lineLength(to)}}, rebuilt);
    return true;
}

Range ScriptDocument::wordAt(Position at) const noexcept
{
    const Position here = clamp(at);
    const std::string& text = m_lines[here.line].text;
    if (here.column >= text.size() || !isWordByte(text[here.column]))
        return Range{here, here};

    u32 begin = here.column;
    while (begin > 0 && isWordByte(text[begin - 1]))
        --begin;
    u32 end = here.column;
    while (end < text.size() && isWordByte(text[end]))
        ++end;
    return Range{Position{here.line, begin}, Position{here.line, end}};
}

u32 ScriptDocument::indentOf(u32 index) const noexcept
{
    const std::string_view text = line(index);
    u32 column = 0;
    while (column < text.size() && (text[column] == ' ' || text[column] == '\t'))
        ++column;
    return column;
}

namespace {

// The cell after a character that starts at `cells`: a tab runs to its stop.
[[nodiscard]] u32 advanceCell(u32 cells, char c) noexcept
{
    return c == '\t' ? (cells / kTabWidth + 1) * kTabWidth : cells + 1;
}

} // namespace

std::string indentWithTabs(std::string_view source)
{
    std::string out;
    out.reserve(source.size());
    std::size_t start = 0;
    while (start <= source.size()) {
        const std::size_t newline = source.find('\n', start);
        const std::string_view row =
            source.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);
        std::size_t body = 0;
        u32 cells = 0;
        while (body < row.size() && (row[body] == ' ' || row[body] == '\t')) {
            cells = advanceCell(cells, row[body]);
            ++body;
        }
        // A line of nothing but whitespace keeps nothing: an indent with
        // nothing after it is the trailing space every editor strips.
        if (body < row.size()) {
            out.append(cells / kTabWidth, '\t');
            out.append(cells % kTabWidth, ' ');
            out.append(row.substr(body));
        }
        else {
            out.append(row);
        }
        if (newline == std::string_view::npos)
            break;
        out.push_back('\n');
        start = newline + 1;
    }
    return out;
}

u32 ScriptDocument::cellOf(u32 index, u32 column) const noexcept
{
    const std::string_view text = line(index);
    const std::size_t limit = std::min<std::size_t>(column, text.size());
    u32 cells = 0;
    for (std::size_t at = 0; at < limit; ++at) {
        if (!isContinuation(text[at]))
            cells = advanceCell(cells, text[at]);
    }
    return cells;
}

u32 ScriptDocument::columnOfCell(u32 index, u32 cell) const noexcept
{
    const std::string_view text = line(index);
    u32 cells = 0;
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (isContinuation(text[at]))
            continue;
        const u32 next = advanceCell(cells, text[at]);
        // A cell inside a tab's span is the tab when it is nearer its start,
        // and what follows it when nearer its end -- which is where a click
        // there puts the caret in every editor.
        if (cell < next) {
            if (text[at] == '\t' && cell - cells >= (next - cells + 1) / 2)
                return static_cast<u32>(at + 1);
            return static_cast<u32>(at);
        }
        cells = next;
    }
    // Past the end is the end, which is where a click to the right of the last
    // character should land.
    return static_cast<u32>(text.size());
}

u32 ScriptDocument::cellCount(u32 index) const noexcept
{
    return cellOf(index, lineLength(index));
}

std::string ScriptDocument::textIn(Range range) const
{
    const Range span = ordered(clamp(range.begin), clamp(range.end));
    if (span.empty())
        return {};

    if (span.begin.line == span.end.line)
        return m_lines[span.begin.line].text.substr(span.begin.column, span.end.column - span.begin.column);

    std::string out = m_lines[span.begin.line].text.substr(span.begin.column);
    for (u32 index = span.begin.line + 1; index < span.end.line; ++index) {
        out.push_back('\n');
        out += m_lines[index].text;
    }
    out.push_back('\n');
    out += m_lines[span.end.line].text.substr(0, span.end.column);
    return out;
}

Position ScriptDocument::applyEdit(Range range, std::string_view inserted)
{
    const Range span = ordered(clamp(range.begin), clamp(range.end));

    // The whole splice, as one string: everything before the range on its line,
    // the new text, and everything after the range on its line. Splitting that
    // back into lines is what makes a multi-line paste and a single keystroke
    // the same code path.
    std::string combined = m_lines[span.begin.line].text.substr(0, span.begin.column);
    combined += inserted;
    combined += m_lines[span.end.line].text.substr(span.end.column);

    std::vector<Line> replacement;
    std::size_t start = 0;
    for (;;) {
        const std::size_t newline = combined.find('\n', start);
        const std::size_t end = newline == std::string::npos ? combined.size() : newline;
        Line line;
        line.text = combined.substr(start, end - start);
        replacement.push_back(std::move(line));
        if (newline == std::string::npos)
            break;
        start = newline + 1;
    }

    const auto first = static_cast<std::ptrdiff_t>(span.begin.line);
    const auto lastPlusOne = static_cast<std::ptrdiff_t>(span.end.line) + 1;
    // The entry state of the first replaced line is inherited from above and the
    // edit cannot have changed it, so it is carried onto the line that takes its
    // place -- `propagate` starts from it rather than recomputing it.
    replacement.front().entry = m_lines[span.begin.line].entry;

    m_lines.erase(m_lines.begin() + first, m_lines.begin() + lastPlusOne);
    m_lines.insert(m_lines.begin() + first, std::make_move_iterator(replacement.begin()),
                   std::make_move_iterator(replacement.end()));

    const Position after = positionAfter(span.begin, inserted);
    ++m_revision;
    propagate(span.begin.line, after.line);
    // Bounded: a panel that never takes the log must not grow it forever.
    if (m_editLog.size() < 4096)
        m_editLog.push_back(EditSpan{span.begin, span.end, after});
    return after;
}

Position ScriptDocument::shifted(Position at, const EditSpan& edit) noexcept
{
    if (at < edit.begin || (at == edit.begin && !(edit.begin == edit.oldEnd)))
        return at;
    if (at < edit.oldEnd)
        return edit.newEnd;
    if (at.line == edit.oldEnd.line)
        return Position{edit.newEnd.line, edit.newEnd.column + (at.column - edit.oldEnd.column)};
    return Position{at.line + edit.newEnd.line - edit.oldEnd.line, at.column};
}

void ScriptDocument::beginGroup() noexcept
{
    m_group = ++m_groups;
    m_coalescing = false;
}

void ScriptDocument::propagate(u32 first, u32 last)
{
    if (m_lines.empty()) {
        m_lastRelexed = 0;
        return;
    }
    first = std::min<u32>(first, static_cast<u32>(m_lines.size()) - 1);

    u32 relexed = 0;
    LineState state = first == 0 ? LineState{} : m_lines[first].entry;
    for (u32 index = first; index < m_lines.size(); ++index) {
        // Past the edited span and inheriting exactly what it already did:
        // nothing below this line can have changed, so the walk stops. This is
        // the whole of "an edit costs the lines it reached".
        if (index > last && m_lines[index].entry == state)
            break;
        m_lines[index].entry = state;
        state = m_language == ScriptLanguage::Hlsl ? lexHlslLine(m_lines[index].text, state, m_lines[index].tokens)
                                                   : lexLine(m_lines[index].text, index, state, m_lines[index].tokens);
        ++relexed;
    }
    m_lastRelexed = relexed;
}

Position ScriptDocument::insert(Position at, std::string_view text)
{
    if (text.empty())
        return clamp(at);

    const std::string normalized = normalizeNewlines(text);
    const Position begin = clamp(at);
    const Position after = applyEdit(Range{begin, begin}, normalized);

    // Only a run of ordinary typing coalesces: no newline, nothing removed. A
    // paste is one step of its own, which is what somebody pressing Ctrl+Z after
    // one expects.
    const bool coalescable = normalized.find('\n') == std::string::npos && normalized.size() <= 4;
    record(Edit{.begin = begin, .removed = {}, .inserted = normalized, .caretBefore = begin}, coalescable);
    return after;
}

Position ScriptDocument::erase(Range range)
{
    const Range span = ordered(clamp(range.begin), clamp(range.end));
    if (span.empty())
        return span.begin;

    std::string removed = textIn(span);
    const Position caretBefore = range.begin;
    applyEdit(span, {});
    record(Edit{.begin = span.begin, .removed = std::move(removed), .inserted = {}, .caretBefore = caretBefore}, false);
    return span.begin;
}

Position ScriptDocument::replace(Range range, std::string_view text)
{
    const Range span = ordered(clamp(range.begin), clamp(range.end));
    const std::string normalized = normalizeNewlines(text);
    if (span.empty() && normalized.empty())
        return span.begin;

    std::string removed = textIn(span);
    const Position caretBefore = range.begin;
    const Position after = applyEdit(span, normalized);
    record(Edit{.begin = span.begin, .removed = std::move(removed), .inserted = normalized, .caretBefore = caretBefore},
           false);
    return after;
}

void ScriptDocument::record(Edit edit, bool coalescable)
{
    m_redo.clear();
    edit.group = m_group;

    if (m_group == 0 && coalescable && m_coalescing && !m_undo.empty()) {
        Edit& top = m_undo.back();
        if (top.removed.empty() && positionAfter(top.begin, top.inserted) == edit.begin) {
            m_undoBytes += edit.inserted.size();
            top.inserted += edit.inserted;
            trimHistory();
            return;
        }
    }

    m_coalescing = m_group == 0 && coalescable;
    m_undoBytes += edit.removed.size() + edit.inserted.size();
    m_undo.push_back(std::move(edit));
    trimHistory();
}

void ScriptDocument::trimHistory()
{
    while (m_undoBytes > MaxUndoBytes && m_undo.size() > 1) {
        m_undoBytes -= m_undo.front().removed.size() + m_undo.front().inserted.size();
        m_undo.erase(m_undo.begin());
    }
}

void ScriptDocument::clearHistory() noexcept
{
    m_undo.clear();
    m_redo.clear();
    m_undoBytes = 0;
    m_coalescing = false;
}

bool ScriptDocument::undo(Position& caret)
{
    if (m_undo.empty())
        return false;

    // A group comes back whole, newest edit first -- the reverse of the order
    // it was made in, which is what keeps each edit's positions true.
    const core::u64 group = m_undo.back().group;
    do {
        Edit edit = std::move(m_undo.back());
        m_undo.pop_back();
        m_undoBytes -= edit.removed.size() + edit.inserted.size();
        applyEdit(Range{edit.begin, positionAfter(edit.begin, edit.inserted)}, edit.removed);
        caret = clamp(edit.caretBefore);
        m_redo.push_back(std::move(edit));
    } while (group != 0 && !m_undo.empty() && m_undo.back().group == group);
    m_coalescing = false;
    return true;
}

bool ScriptDocument::redo(Position& caret)
{
    if (m_redo.empty())
        return false;

    const core::u64 group = m_redo.back().group;
    do {
        Edit edit = std::move(m_redo.back());
        m_redo.pop_back();
        caret = applyEdit(Range{edit.begin, positionAfter(edit.begin, edit.removed)}, edit.inserted);
        m_undoBytes += edit.removed.size() + edit.inserted.size();
        m_undo.push_back(std::move(edit));
    } while (group != 0 && !m_redo.empty() && m_redo.back().group == group);
    m_coalescing = false;
    trimHistory();
    return true;
}

void ScriptDocument::refreshDiagnostics()
{
    if (m_language == ScriptLanguage::Hlsl)
        m_diagnostics = m_external;
    else
        parseDiagnostics(text(), m_diagnostics);
    m_diagnosticsRevision = m_revision;
}

void ScriptDocument::setExternalDiagnostics(std::vector<Diagnostic> diagnostics)
{
    m_external = std::move(diagnostics);
    if (m_language == ScriptLanguage::Hlsl)
        m_diagnostics = m_external;
}

void ScriptDocument::setLanguage(ScriptLanguage language)
{
    if (language == m_language)
        return;
    m_language = language;
    m_diagnostics.clear();
    m_diagnosticsRevision = ~0ull;
    propagate(0, static_cast<u32>(m_lines.size()));
}

ScriptLanguage scriptLanguageOf(std::string_view fileName) noexcept
{
    const auto endsWith = [&](std::string_view suffix) {
        if (fileName.size() < suffix.size())
            return false;
        for (std::size_t index = 0; index < suffix.size(); ++index) {
            const char c = fileName[fileName.size() - suffix.size() + index];
            if ((c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c) != suffix[index])
                return false;
        }
        return true;
    };
    return endsWith(".hlsl") || endsWith(".hlsli") ? ScriptLanguage::Hlsl : ScriptLanguage::Luau;
}

// --- Searching ---------------------------------------------------------------

namespace {

// One line, one needle, from `from`. Answers npos when there is no match.
[[nodiscard]] std::size_t findInLine(std::string_view haystack, std::string_view needle, std::size_t from,
                                     bool matchCase, bool wholeWord) noexcept
{
    if (needle.empty() || needle.size() > haystack.size())
        return std::string_view::npos;

    for (std::size_t start = from; start + needle.size() <= haystack.size(); ++start) {
        bool same = true;
        for (std::size_t index = 0; index < needle.size(); ++index) {
            const char a = haystack[start + index];
            const char b = needle[index];
            if (matchCase ? a != b : lowerByte(a) != lowerByte(b)) {
                same = false;
                break;
            }
        }
        if (!same)
            continue;
        if (wholeWord) {
            const bool leftOk = start == 0 || !isWordByte(haystack[start - 1]);
            const std::size_t end = start + needle.size();
            const bool rightOk = end >= haystack.size() || !isWordByte(haystack[end]);
            if (!leftOk || !rightOk)
                continue;
        }
        return start;
    }
    return std::string_view::npos;
}

// A line's matches, as byte spans, and the find that makes them.
struct Span
{
    std::size_t begin = 0;
    std::size_t end = 0;
};

// **A find compiled once per operation**, plain text or a regular expression:
// compiling a pattern for every line of a file is the cost that makes regex
// search feel slow, and none of it is per line.
class Matcher
{
public:
    Matcher(std::string_view needle, ScriptDocument::SearchOptions options) : m_needle(needle), m_options(options)
    {
        if (!options.regex || needle.empty())
            return;
        std::string pattern(needle);
        if (options.wholeWord)
            pattern = "\\b(?:" + pattern + ")\\b";
        auto flags = std::regex::ECMAScript;
        if (!options.matchCase)
            flags |= std::regex::icase;
        try {
            m_regex.emplace(pattern, flags);
        } catch (const std::regex_error&) {
            m_invalid = true;
        }
    }

    [[nodiscard]] bool usable() const noexcept { return !m_needle.empty() && !m_invalid; }

    void matches(std::string_view line, std::vector<Span>& out) const
    {
        out.clear();
        if (!usable())
            return;
        if (!m_regex.has_value()) {
            std::size_t scan = 0;
            for (;;) {
                const std::size_t hit = findInLine(line, m_needle, scan, m_options.matchCase, m_options.wholeWord);
                if (hit == std::string_view::npos)
                    return;
                out.push_back(Span{hit, hit + m_needle.size()});
                scan = hit + m_needle.size();
            }
        }
        // An empty match marks a place and not a piece of text: it cannot be
        // highlighted or replaced, so it is stepped over.
        for (auto it = std::cregex_iterator(line.data(), line.data() + line.size(), *m_regex);
             it != std::cregex_iterator(); ++it) {
            if (it->length(0) == 0)
                continue;
            const auto begin = static_cast<std::size_t>(it->position(0));
            out.push_back(Span{begin, begin + static_cast<std::size_t>(it->length(0))});
        }
    }

    // What `with` becomes at the match starting at `begin`: itself for a plain
    // find, and with `$1`..`$9` and `$&` filled in for a pattern.
    [[nodiscard]] std::string replacement(std::string_view line, std::size_t begin, std::string_view with) const
    {
        if (!m_regex.has_value())
            return std::string(with);
        for (auto it = std::cregex_iterator(line.data(), line.data() + line.size(), *m_regex);
             it != std::cregex_iterator(); ++it) {
            if (static_cast<std::size_t>(it->position(0)) == begin)
                return it->format(std::string(with));
        }
        return std::string(with);
    }

private:
    std::string_view m_needle;
    ScriptDocument::SearchOptions m_options;
    std::optional<std::regex> m_regex;
    bool m_invalid = false;
};

} // namespace

ScriptDocument::BlockBreak ScriptDocument::blockBreakAt(Position caret) const
{
    BlockBreak out;
    // An `end` is Luau's; HLSL's blocks are braces, which the pairing closes.
    if (m_language != ScriptLanguage::Luau)
        return out;
    caret = clamp(caret);
    const std::string_view text = m_lines[caret.line].text;
    const std::vector<Token>& tokens = m_lines[caret.line].tokens;
    const auto word = [&](const Token& token) { return text.substr(token.column, token.length); };

    // The last thing that means anything before the caret, and the first on
    // the line.
    const Token* last = nullptr;
    const Token* first = nullptr;
    for (const Token& token : tokens) {
        if (token.column + token.length > caret.column)
            break;
        if (token.kind == TokenKind::Text || token.kind == TokenKind::Comment)
            continue;
        if (first == nullptr)
            first = &token;
        last = &token;
    }
    if (last == nullptr)
        return out;

    // A function's parameter list closes with `)` on a line with `function`
    // on it, and how many `(` were still open at that keyword is how many `)`
    // follow its `end`: `x:Connect(function()` closes with `end)`.
    //
    // **And a return type after it still opens the block** (the owner: "the
    // automatic end does not happen when I type a function's return"):
    // `function Snake.new(at: vector): Snake` ends in a TYPE, not in `)`. So the
    // `)` that closes the parameters is remembered, and what follows it counts
    // when it is an annotation -- a `:` and then only type tokens.
    std::optional<core::u32> functionOpenParens;
    const Token* paramsClose = nullptr;
    bool awaitingParams = false;
    bool annotated = false;
    bool annotationOnly = true;
    core::u32 depth = 0;
    for (const Token& token : tokens) {
        if (token.column + token.length > caret.column)
            break;
        if (token.kind == TokenKind::Text || token.kind == TokenKind::Comment)
            continue;
        const std::string_view w = word(token);
        if (paramsClose != nullptr && &token != paramsClose) {
            // Everything after the parameters: the first must be the `:`, and
            // the rest must be a type -- names, `nil`, and the punctuation a
            // type is written with.
            if (!annotated) {
                annotated = token.kind == TokenKind::Operator && w == ":";
                annotationOnly = annotationOnly && annotated;
            }
            else {
                const bool typePunctuation =
                    token.kind == TokenKind::Operator &&
                    (w == "(" || w == ")" || w == "{" || w == "}" || w == "<" || w == ">" || w == "?" || w == "|" ||
                     w == "&" || w == "," || w == "->" || w == ":" || w == "..." || w == "[" || w == "]");
                const bool typeWord = token.kind == TokenKind::Type || token.kind == TokenKind::Identifier ||
                                      token.kind == TokenKind::String ||
                                      (token.kind == TokenKind::Keyword && (w == "nil" || w == "true" || w == "false"));
                annotationOnly = annotationOnly && (typePunctuation || typeWord);
            }
        }
        if (token.kind == TokenKind::Keyword && w == "function") {
            functionOpenParens = depth;
            paramsClose = nullptr;
            awaitingParams = true;
            annotated = false;
            annotationOnly = true;
        }
        else if (token.kind == TokenKind::Operator && w == "(") {
            ++depth;
        }
        else if (token.kind == TokenKind::Operator && w == ")" && depth > 0) {
            --depth;
            if (awaitingParams && functionOpenParens.has_value() && depth == *functionOpenParens) {
                paramsClose = &token;
                awaitingParams = false;
            }
        }
    }
    const bool returnType = paramsClose != nullptr && last != paramsClose && annotated && annotationOnly &&
                            functionOpenParens.has_value() && depth == *functionOpenParens;

    const std::string_view tail = word(*last);
    std::string closer;
    if (last->kind == TokenKind::Keyword && (tail == "then" || tail == "do"))
        closer = "end";
    else if (last->kind == TokenKind::Keyword && tail == "repeat")
        closer = "until ";
    else if (last->kind == TokenKind::Keyword && tail == "else")
        out.opens = true;
    else if ((last == paramsClose || returnType) && functionOpenParens.has_value() && depth == *functionOpenParens)
        closer = "end" + std::string(*functionOpenParens, ')');
    if (closer.empty())
        return out;
    out.opens = true;

    // `elseif ... then` continues a block and never needs a closer of its own.
    if (first != nullptr && first->kind == TokenKind::Keyword && word(*first) == "elseif")
        return out;

    // **Is THIS block closed already?** (the owner: Enter after the `then` of
    // an `if` that already had its `end` wrote a second one, and `else` has to
    // be respected too.) Asked of the block itself rather than of the file:
    // a count over the whole document said "open" whenever ANY block anywhere
    // was -- a function still being written, or an `if` expression, which has
    // no `end` at all. So the scan starts at the caret, nests, and stops at the
    // closer that matches this line's opener. That closer settles it when it
    // is indented at least as far as this line: less, and it is an OUTER
    // block's, reached because this one was never closed.
    const auto keywordAt = [&](const Line& line, const Token& token) {
        return token.kind == TokenKind::Keyword ? std::string_view(line.text).substr(token.column, token.length)
                                                : std::string_view{};
    };
    // `if` in an expression -- `local x = if a then b else c` -- opens nothing.
    const auto expressionIf = [&](const Line& line, const Token* before) {
        if (before == nullptr)
            return false;
        const std::string_view w = std::string_view(line.text).substr(before->column, before->length);
        if (before->kind == TokenKind::Operator)
            return w != ")" && w != "]" && w != "}";
        return before->kind == TokenKind::Keyword &&
               (w == "return" || w == "and" || w == "or" || w == "not" || w == "in" || w == "until");
    };
    const u32 indent = cellOf(caret.line, indentOf(caret.line));
    int nesting = 1;
    const Token* before = last;
    const Line* beforeLine = &m_lines[caret.line];
    for (u32 index = caret.line; index < m_lines.size(); ++index) {
        const Line& line = m_lines[index];
        bool firstOnLine = true;
        for (const Token& token : line.tokens) {
            if (index == caret.line && token.column < caret.column)
                continue;
            if (token.kind == TokenKind::Text || token.kind == TokenKind::Comment)
                continue;
            const std::string_view w = keywordAt(line, token);
            const bool opener =
                w == "function" || w == "do" || w == "repeat" || (w == "if" && !expressionIf(*beforeLine, before));
            if (opener) {
                ++nesting;
            }
            else if (w == "end" || w == "until") {
                if (--nesting == 0) {
                    // A closer that starts its line is judged by where it
                    // stands; one after code on the same line closes where it
                    // is written.
                    const bool closes = !firstOnLine || index == caret.line || cellOf(index, token.column) >= indent;
                    if (!closes)
                        out.closer = std::move(closer);
                    return out;
                }
            }
            before = &token;
            beforeLine = &line;
            firstOnLine = false;
        }
    }
    out.closer = std::move(closer);
    return out;
}

std::vector<ScriptDocument::FoldRange> ScriptDocument::foldRanges() const
{
    struct Open
    {
        u32 line = 0;
        bool brace = false;
    };
    std::vector<Open> open;
    std::vector<FoldRange> out;
    const Token* before = nullptr;
    const Line* beforeLine = nullptr;
    const auto close = [&](u32 line, bool brace) {
        // A closer pops to its own kind: an `end` does not close a table left
        // open inside the block, and a stray one closes nothing.
        while (!open.empty()) {
            const Open top = open.back();
            open.pop_back();
            if (top.brace == brace) {
                if (line > top.line + 1)
                    out.push_back(FoldRange{top.line, line});
                return;
            }
        }
    };
    for (u32 index = 0; index < m_lines.size(); ++index) {
        const Line& line = m_lines[index];
        for (const Token& token : line.tokens) {
            if (token.kind == TokenKind::Text || token.kind == TokenKind::Comment)
                continue;
            const std::string_view w = std::string_view(line.text).substr(token.column, token.length);
            if (token.kind == TokenKind::Keyword) {
                bool expression = false;
                if (w == "if" && before != nullptr) {
                    const std::string_view b =
                        std::string_view(beforeLine->text).substr(before->column, before->length);
                    expression = (before->kind == TokenKind::Operator && b != ")" && b != "]" && b != "}") ||
                                 (before->kind == TokenKind::Keyword && (b == "return" || b == "and" || b == "or" ||
                                                                         b == "not" || b == "in" || b == "until"));
                }
                if (w == "function" || w == "do" || w == "repeat" || (w == "if" && !expression))
                    open.push_back(Open{index, false});
                else if (w == "end" || w == "until")
                    close(index, false);
            }
            else if (token.kind == TokenKind::Operator && w == "{") {
                open.push_back(Open{index, true});
            }
            else if (token.kind == TokenKind::Operator && w == "}") {
                close(index, true);
            }
            before = &token;
            beforeLine = &line;
        }
    }
    // Outer before inner, as a reader meets them.
    std::sort(out.begin(), out.end(), [](const FoldRange& a, const FoldRange& b) {
        return a.first != b.first ? a.first < b.first : a.last > b.last;
    });
    return out;
}

bool ScriptDocument::searchable(std::string_view needle, SearchOptions options)
{
    return Matcher(needle, options).usable();
}

std::vector<Range> ScriptDocument::findAll(std::string_view needle, SearchOptions options) const
{
    std::vector<Range> out;
    const Matcher matcher(needle, options);
    if (!matcher.usable())
        return out;
    std::vector<Span> spans;
    for (u32 index = 0; index < m_lines.size(); ++index) {
        matcher.matches(m_lines[index].text, spans);
        for (const Span& span : spans) {
            out.push_back(
                Range{Position{index, static_cast<u32>(span.begin)}, Position{index, static_cast<u32>(span.end)}});
        }
    }
    return out;
}

Range ScriptDocument::findNext(std::string_view needle, Position from, SearchOptions options) const
{
    const Position start = clamp(from);
    const std::vector<Range> all = findAll(needle, options);
    if (all.empty())
        return Range{start, start};
    // The first at or after the caret, wrapping to the top once.
    for (const Range& match : all) {
        if (!(match.begin < start))
            return match;
    }
    return all.front();
}

Range ScriptDocument::findPrevious(std::string_view needle, Position from, SearchOptions options) const
{
    const Position start = clamp(from);
    const std::vector<Range> all = findAll(needle, options);
    if (all.empty())
        return Range{start, start};
    // The last that ends at or before the caret, wrapping to the bottom once.
    for (auto match = all.rbegin(); match != all.rend(); ++match) {
        if (!(start < match->end))
            return *match;
    }
    return all.back();
}

u32 ScriptDocument::countMatches(std::string_view needle, SearchOptions options) const
{
    return static_cast<u32>(findAll(needle, options).size());
}

Range ScriptDocument::replaceMatch(Range match, std::string_view needle, std::string_view with, SearchOptions options)
{
    // Only a real match: a stale range -- the text moved since it was found --
    // replaces nothing rather than something else.
    const std::vector<Range> all = findAll(needle, options);
    if (std::find(all.begin(), all.end(), match) == all.end())
        return Range{match.begin, match.begin};
    const Matcher matcher(needle, options);
    const std::string text = matcher.replacement(m_lines[match.begin.line].text, match.begin.column, with);
    const Position end = replace(match, text);
    return Range{match.begin, end};
}

u32 ScriptDocument::replaceAll(std::string_view needle, std::string_view with, SearchOptions options)
{
    const Matcher matcher(needle, options);
    if (!matcher.usable())
        return 0;

    // **Rebuilt whole, recorded as one step.** A loop of `replace` calls would
    // be N undo steps for one action, and every one after the first would be
    // computed against positions the previous had already moved.
    const std::string before = text();
    std::string after;
    after.reserve(before.size());

    u32 replaced = 0;
    std::vector<Span> spans;
    for (const Line& line : m_lines) {
        if (&line != &m_lines.front())
            after.push_back('\n');
        const std::string_view source = line.text;
        matcher.matches(source, spans);
        std::size_t scan = 0;
        for (const Span& span : spans) {
            after.append(source.substr(scan, span.begin - scan));
            after.append(matcher.replacement(source, span.begin, with));
            scan = span.end;
            ++replaced;
        }
        after.append(source.substr(scan));
    }

    if (replaced == 0)
        return 0;

    const Position begin{0, 0};
    const Position end{static_cast<u32>(m_lines.size()) - 1, lineLength(static_cast<u32>(m_lines.size()) - 1)};
    applyEdit(Range{begin, end}, after);
    record(Edit{.begin = begin, .removed = before, .inserted = after, .caretBefore = begin}, false);
    return replaced;
}

} // namespace luaug::app
