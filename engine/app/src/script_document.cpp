#include "luaug/app/script_document.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace luaug::app {
namespace {

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
