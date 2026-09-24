#include "luaug/app/script_document.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
            row.insert(0, "    ");
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

u32 ScriptDocument::cellOf(u32 index, u32 column) const noexcept
{
    const std::string_view text = line(index);
    const std::size_t limit = std::min<std::size_t>(column, text.size());
    u32 cells = 0;
    for (std::size_t at = 0; at < limit; ++at) {
        if (!isContinuation(text[at]))
            ++cells;
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
        if (cells == cell)
            return static_cast<u32>(at);
        ++cells;
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
    return after;
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
        state = lexLine(m_lines[index].text, index, state, m_lines[index].tokens);
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

    if (coalescable && m_coalescing && !m_undo.empty()) {
        Edit& top = m_undo.back();
        if (top.removed.empty() && positionAfter(top.begin, top.inserted) == edit.begin) {
            m_undoBytes += edit.inserted.size();
            top.inserted += edit.inserted;
            trimHistory();
            return;
        }
    }

    m_coalescing = coalescable;
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

    Edit edit = std::move(m_undo.back());
    m_undo.pop_back();
    m_undoBytes -= edit.removed.size() + edit.inserted.size();
    m_coalescing = false;

    applyEdit(Range{edit.begin, positionAfter(edit.begin, edit.inserted)}, edit.removed);
    caret = clamp(edit.caretBefore);
    m_redo.push_back(std::move(edit));
    return true;
}

bool ScriptDocument::redo(Position& caret)
{
    if (m_redo.empty())
        return false;

    Edit edit = std::move(m_redo.back());
    m_redo.pop_back();
    m_coalescing = false;

    caret = applyEdit(Range{edit.begin, positionAfter(edit.begin, edit.removed)}, edit.inserted);
    m_undoBytes += edit.removed.size() + edit.inserted.size();
    m_undo.push_back(std::move(edit));
    trimHistory();
    return true;
}

void ScriptDocument::refreshDiagnostics()
{
    parseDiagnostics(text(), m_diagnostics);
    m_diagnosticsRevision = m_revision;
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

} // namespace

Range ScriptDocument::findNext(std::string_view needle, Position from, SearchOptions options) const
{
    if (needle.empty())
        return Range{from, from};

    const Position start = clamp(from);
    // Two passes rather than a modulo walk, because "wrapping once" is exactly
    // this: everything at or after the caret, then everything before it.
    for (int pass = 0; pass < 2; ++pass) {
        const u32 firstLine = pass == 0 ? start.line : 0;
        const u32 lastLine = pass == 0 ? static_cast<u32>(m_lines.size()) - 1 : start.line;
        for (u32 index = firstLine; index <= lastLine && index < m_lines.size(); ++index) {
            const std::size_t begin = pass == 0 && index == start.line ? start.column : 0;
            const std::size_t hit =
                findInLine(m_lines[index].text, needle, begin, options.matchCase, options.wholeWord);
            if (hit == std::string_view::npos)
                continue;
            if (pass == 1 && index == start.line && hit >= start.column)
                continue;
            return Range{Position{index, static_cast<u32>(hit)},
                         Position{index, static_cast<u32>(hit + needle.size())}};
        }
    }
    return Range{start, start};
}

Range ScriptDocument::findPrevious(std::string_view needle, Position from, SearchOptions options) const
{
    if (needle.empty())
        return Range{from, from};

    const Position start = clamp(from);
    for (int pass = 0; pass < 2; ++pass) {
        const auto firstLine = static_cast<std::ptrdiff_t>(pass == 0 ? start.line : m_lines.size() - 1);
        const auto lastLine = static_cast<std::ptrdiff_t>(pass == 0 ? 0 : start.line);
        for (std::ptrdiff_t index = firstLine; index >= lastLine; --index) {
            const auto lineIndex = static_cast<u32>(index);
            const std::string_view text = m_lines[lineIndex].text;
            std::size_t best = std::string_view::npos;
            std::size_t scan = 0;
            for (;;) {
                const std::size_t hit = findInLine(text, needle, scan, options.matchCase, options.wholeWord);
                if (hit == std::string_view::npos)
                    break;
                const bool beforeCaret = lineIndex != start.line || hit + needle.size() <= start.column;
                if ((pass == 0 && beforeCaret) || (pass == 1 && !(lineIndex == start.line && beforeCaret)))
                    best = hit;
                scan = hit + 1;
            }
            if (best != std::string_view::npos) {
                return Range{Position{lineIndex, static_cast<u32>(best)},
                             Position{lineIndex, static_cast<u32>(best + needle.size())}};
            }
        }
    }
    return Range{start, start};
}

u32 ScriptDocument::countMatches(std::string_view needle, SearchOptions options) const
{
    if (needle.empty())
        return 0;

    u32 total = 0;
    for (const Line& line : m_lines) {
        std::size_t scan = 0;
        for (;;) {
            const std::size_t hit = findInLine(line.text, needle, scan, options.matchCase, options.wholeWord);
            if (hit == std::string_view::npos)
                break;
            ++total;
            scan = hit + needle.size();
        }
    }
    return total;
}

u32 ScriptDocument::replaceAll(std::string_view needle, std::string_view with, SearchOptions options)
{
    if (needle.empty())
        return 0;

    // **Rebuilt whole, recorded as one step.** A loop of `replace` calls would
    // be N undo steps for one action, and every one after the first would be
    // computed against positions the previous had already moved.
    const std::string before = text();
    std::string after;
    after.reserve(before.size());

    u32 replaced = 0;
    for (const Line& line : m_lines) {
        if (&line != &m_lines.front())
            after.push_back('\n');
        const std::string_view source = line.text;
        std::size_t scan = 0;
        for (;;) {
            const std::size_t hit = findInLine(source, needle, scan, options.matchCase, options.wholeWord);
            if (hit == std::string_view::npos)
                break;
            after.append(source.substr(scan, hit - scan));
            after.append(with);
            scan = hit + needle.size();
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
