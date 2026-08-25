// One script being edited: the text, its tokens, its diagnostics, and its own
// undo (ADR 0057).
//
// **This is the half of the code pane that can be asserted on**, which is the
// split `inspector.h` established and `ui_theme.h` restates: the ImGui shell
// cannot render headlessly and SDL does not accept injected input, so a picture
// of the editor needs a person and everything else belongs on this side of the
// line. What is left for the panel is glyphs, a caret and a scrollbar.
//
// ## Lines, and the reason is the lexer rather than the clipper
//
// The text is a `std::vector<Line>`, each line its own `std::string`. The
// clipper's read being a subscript is the obvious half of the argument; the half
// that decides it is that **Luau's lexer can be run over one line in isolation**,
// which two things in the vendored source make true:
//
//   `Lexer`'s constructor takes a `Position startPosition` and derives its line
//   counter from it (`Ast/src/Lexer.cpp:346-357`), so a lexer over line N alone,
//   built with `Position(N, 0)`, reports absolute positions with no fixup.
//
//   `peekch` is bounds-checked against the buffer size (`Lexer.cpp:437-446`), so
//   **no NUL terminator is required** and a `string_view` into one line is a
//   legal buffer whose end reads as `Eof`.
//
// A single flat buffer would have been fine for lexing and worse for everything
// else; a gap buffer or a piece table would have nowhere to hang the per-line
// state below, which is the whole mechanism that makes an edit cost one line.
//
// ## What crosses a line, and it is one thing
//
// A **long bracket** -- `--[[ ]]`, `--[==[ ]==]`, `[[ ]]`, `[==[ ]==]`. Nothing
// else in Luau spans a newline: an unterminated quoted string stops at the line
// end as `BrokenString`, and so does an interpolated one
// (`Lexer.cpp:630-637`, which returns `BrokenString` on `\r` and `\n`). So the
// state carried from one line to the next is a kind and a level, two bytes, and
// that is `LineState`.
//
// ## Columns are BYTES
//
// A `Position` column is a byte offset into the line -- not a codepoint index
// and not a pixel. Bytes are what the lexer reports, what an edit splices at,
// and what a test can write down. Moving a caret steps whole codepoints
// (`nextColumn`/`prevColumn`), and turning a column into an x is the panel's
// job, because only the panel knows the font.
//
// R3 does not apply to what this carries, for the reason `debug_overlay.h`
// states: the editor exists for whoever is building a game, never for a player.
#pragma once

#include "luaug/core/types.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::app {

// A place in the document. Both ZERO-based, which is what Luau's own `Location`
// uses -- converting once, where a human reads a line number, is cheaper than
// converting at every comparison.
struct Position
{
    core::u32 line = 0;
    // Bytes from the start of the line. See the header note.
    core::u32 column = 0;

    [[nodiscard]] constexpr bool operator==(const Position&) const noexcept = default;
    [[nodiscard]] constexpr bool operator<(const Position& other) const noexcept
    {
        return line != other.line ? line < other.line : column < other.column;
    }
    [[nodiscard]] constexpr bool operator<=(const Position& other) const noexcept { return !(other < *this); }
};

// Half-open. Every function taking one normalises it first, so a caller may
// hand over a selection dragged upwards without thinking about it.
struct Range
{
    Position begin;
    Position end;

    [[nodiscard]] constexpr bool empty() const noexcept { return begin == end; }
    [[nodiscard]] constexpr bool operator==(const Range&) const noexcept = default;
};

[[nodiscard]] constexpr Range ordered(Position a, Position b) noexcept
{
    return b < a ? Range{b, a} : Range{a, b};
}

// What a run of characters IS, for colour. One per class Luau's lexer
// distinguishes and not one more: the palette has to define every one of these
// and clear a contrast bar against the code pane's ground, so a kind nobody can
// name a colour for is a kind that should not exist.
enum class TokenKind : core::u8
{
    // The gaps: whitespace, and anything the lexer had no opinion about. Drawn
    // in the pane's own foreground.
    Text,
    Keyword,
    Identifier,
    Number,
    String,
    Comment,
    Operator,
    // `@native`, `@checked`.
    Attribute,
    // A broken string, comment or codepoint. The lexer answers with these rather
    // than throwing, which is why a half-typed line still colours.
    Error,
};

// A run within one line. In column order, never overlapping. Gaps between runs
// are whitespace and are the panel's to skip.
struct Token
{
    core::u32 column = 0;
    core::u32 length = 0;
    TokenKind kind = TokenKind::Text;
};

// What a line inherits from the one above it. The complete set -- see the
// header note on what crosses a line.
enum class LexKind : core::u8
{
    Normal,
    LongString,
    LongComment,
};

struct LineState
{
    LexKind kind = LexKind::Normal;
    // The `=` count of the long bracket that opened, so `[==[` is closed by
    // `]==]` and not by `]]`.
    core::u8 level = 0;

    [[nodiscard]] constexpr bool operator==(const LineState&) const noexcept = default;
};

// One parse error, where Luau reported it.
struct Diagnostic
{
    Position at;
    std::string message;
};

// **Refused rather than truncated.** A document past either bound is reported as
// a status the panel can say out loud; the alternative is a text editor that
// silently loses the end of a file. Eight mebibytes is far past any Luau anybody
// writes, and a 64 KiB line is a minified blob rather than code.
inline constexpr std::size_t kMaxDocumentBytes = 8u * 1024u * 1024u;
inline constexpr std::size_t kMaxLineBytes = 64u * 1024u;

// The text of one script, with everything derived from it.
//
// Not copyable: a document carries its own undo history, and a copy sharing one
// would be two panes disagreeing about what Ctrl+Z means.
class ScriptDocument
{
public:
    ScriptDocument();
    explicit ScriptDocument(std::string_view text);

    ScriptDocument(const ScriptDocument&) = delete;
    ScriptDocument& operator=(const ScriptDocument&) = delete;
    ScriptDocument(ScriptDocument&&) noexcept = default;
    ScriptDocument& operator=(ScriptDocument&&) noexcept = default;

    // Replaces everything and **clears the undo history**: a document whose text
    // was swapped underneath it cannot honour an undo that predates the swap.
    // `\r\n` and a lone `\r` become `\n` on the way in, which is what every
    // other file this engine writes already is.
    //
    // False when the text is past `kMaxDocumentBytes` or holds a line past
    // `kMaxLineBytes`; the document is left empty rather than half-loaded.
    bool setText(std::string_view text);

    // The whole document, joined with `\n`. This is the save payload and the
    // value written back to `Script.Source`, so it is built on demand rather
    // than cached -- a cache here would be a second copy of the file that has to
    // be kept true.
    [[nodiscard]] std::string text() const;

    [[nodiscard]] core::u32 lineCount() const noexcept { return static_cast<core::u32>(m_lines.size()); }
    // The line's bytes, without its newline. An index past the end answers empty
    // rather than trapping: a panel that scrolled one row too far should draw
    // nothing, not crash.
    [[nodiscard]] std::string_view line(core::u32 index) const noexcept;
    [[nodiscard]] core::u32 lineLength(core::u32 index) const noexcept;

    // Every edit bumps it. The panel uses it to know a re-highlight is due
    // without comparing text, and the editor uses it to know a tab is dirty.
    [[nodiscard]] core::u64 revision() const noexcept { return m_revision; }

    // --- Editing -------------------------------------------------------------
    //
    // Two mutators, and everything else composes from them -- which is what
    // keeps the undo log to two shapes. Each returns where a caret should sit
    // afterwards.

    Position insert(Position at, std::string_view text);
    Position erase(Range range);
    // Erase then insert as ONE undo step, which is what typing over a selection
    // is and what one iteration of replace-all is.
    Position replace(Range range, std::string_view text);

    // **Moves the lines `first` through `last` one row up or down**, trading
    // places with the line they run into. False when there is nowhere to go,
    // which is the top and the bottom.
    //
    // Here rather than in the panel because it is an edit and not a drawing --
    // the split this file exists for. It is also ONE `replace` over both blocks
    // rather than a delete and an insert, so Ctrl+Z takes the whole move back:
    // a move somebody has to undo twice is a move that will eat a line.
    bool moveLines(core::u32 first, core::u32 last, int delta);

    [[nodiscard]] std::string textIn(Range range) const;

    // --- Undo ----------------------------------------------------------------
    //
    // Text undo, and deliberately not the world's. `UndoStack` (editor.h) is
    // snapshot-based over the whole world because undoing a delete has to bring
    // a subtree back with the same ids; a text edit is a byte range whose
    // inverse is exactly known, and a world snapshot per keystroke would be
    // absurd. The two never meet.
    //
    // Steps coalesce while somebody is typing: consecutive inserts with no
    // newline, each starting where the last ended, are one step. A deletion, a
    // newline, a caret moved by hand or an explicit `breakUndoRun` ends the run
    // -- which is the rule that makes Ctrl+Z undo a word rather than a letter.
    //
    // **Bounded by bytes, not by steps.** `UndoStack::Depth = 64` is right there
    // because every step is a world; here a step is one byte or one paste, so a
    // step count bounds nothing.

    [[nodiscard]] bool canUndo() const noexcept { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !m_redo.empty(); }
    // False when there was nothing to do. `caret` is moved to where the change
    // happened, because an undo somebody cannot see is an undo they press again.
    bool undo(Position& caret);
    bool redo(Position& caret);
    void breakUndoRun() noexcept { m_coalescing = false; }
    void clearHistory() noexcept;

    static constexpr std::size_t MaxUndoBytes = 4u * 1024u * 1024u;

    // --- What the panel colours ----------------------------------------------

    // The runs on one line, lexed on demand and cached. Empty past the end.
    [[nodiscard]] std::span<const Token> tokens(core::u32 line) const;

    // Lines re-lexed by the most recent edit. **Exposed for the test rather than
    // for the panel**: "an edit inside a block comment re-lexes exactly the lines
    // whose incoming state changed" is only falsifiable if the number is
    // readable, and a bound that is merely small passes while the defect is
    // still there (the E4 precedent).
    [[nodiscard]] core::u32 lastRelexedLines() const noexcept { return m_lastRelexed; }

    // --- What the gutter marks -----------------------------------------------

    // Runs Luau's parser over the whole document. **The caller decides when** --
    // once the text has been still for a moment, not on every keystroke -- so
    // this is a plain call rather than something an edit triggers.
    void refreshDiagnostics();
    [[nodiscard]] std::span<const Diagnostic> diagnostics() const noexcept { return m_diagnostics; }
    // Whether the text has moved since `refreshDiagnostics` last ran, so the
    // panel can ask at rest instead of on a timer.
    [[nodiscard]] bool diagnosticsStale() const noexcept { return m_diagnosticsRevision != m_revision; }

    // --- Positions -----------------------------------------------------------

    // Onto a real place: a line past the end becomes the last line, a column
    // past its line's end becomes that end, and a column inside a multi-byte
    // codepoint moves back to its start.
    [[nodiscard]] Position clamp(Position at) const noexcept;

    // One codepoint left or right, crossing a line boundary when there is one.
    [[nodiscard]] Position nextColumn(Position at) const noexcept;
    [[nodiscard]] Position prevColumn(Position at) const noexcept;

    // The word under `at`, for double-click and for whole-word search. An empty
    // range when `at` is not on a word character.
    [[nodiscard]] Range wordAt(Position at) const noexcept;

    // The first non-whitespace column of a line, which is what Home goes to
    // before it goes to column zero, and what a new line inherits as its indent.
    [[nodiscard]] core::u32 indentOf(core::u32 line) const noexcept;

    // --- Bytes and cells -----------------------------------------------------
    //
    // **A column is bytes and a monospace cell is a codepoint, and the two are
    // not the same the moment somebody types an accent.** `á` is two bytes and
    // one glyph: a pane that placed runs at `byteColumn * advance` would leave a
    // gap the width of a space after every accented letter, and its caret would
    // sit one cell to the right of the character it is on.
    //
    // Found by a person typing Portuguese into it, which is the only way this
    // class of defect is ever found -- every ASCII test passes either way.
    //
    // The document keeps BYTES, because that is what an edit splices at and what
    // the lexer reports. These two turn a byte column into the cell a monospace
    // pane should draw it in, and back.
    [[nodiscard]] core::u32 cellOf(core::u32 line, core::u32 column) const noexcept;
    [[nodiscard]] core::u32 columnOfCell(core::u32 line, core::u32 cell) const noexcept;
    // Cells in the whole line, which is its drawn width.
    [[nodiscard]] core::u32 cellCount(core::u32 line) const noexcept;

    // --- Searching -----------------------------------------------------------

    struct SearchOptions
    {
        bool matchCase = false;
        bool wholeWord = false;
    };

    // The first match at or after `from`, wrapping to the top once. An empty
    // range when `needle` is empty or nothing matches.
    [[nodiscard]] Range findNext(std::string_view needle, Position from, SearchOptions options) const;
    [[nodiscard]] Range findPrevious(std::string_view needle, Position from, SearchOptions options) const;
    [[nodiscard]] core::u32 countMatches(std::string_view needle, SearchOptions options) const;
    // Every match replaced, as ONE undo step. Returns how many.
    core::u32 replaceAll(std::string_view needle, std::string_view with, SearchOptions options);

private:
    struct Line
    {
        std::string text;
        // What this line inherits from the one above. Line 0 is always Normal.
        LineState entry;
        // **Always current.** Lexing cannot be deferred to the draw the way
        // drawing can: a line's ENTRY state is the previous line's EXIT state,
        // and the only way to know an exit state is to have lexed. So an edit
        // lexes what it reached and `tokens()` is a read.
        std::vector<Token> tokens;
    };

    struct Edit
    {
        // Where the change started, in the document as it stood before it. The
        // two ends are derived rather than stored: advancing `begin` by
        // `removed` gives what was replaced, and advancing it by `inserted`
        // gives what replaced it. Storing both ends as well would be two more
        // fields to keep true.
        Position begin;
        std::string removed;
        std::string inserted;
        Position caretBefore;
    };

    // The one place text actually changes. Returns the position after the
    // inserted text and re-lexes exactly what the change reached.
    Position applyEdit(Range range, std::string_view inserted);
    // Re-lexes from `first` forward, stopping at the first line past `last`
    // whose entry state is unchanged. Records the count.
    void propagate(core::u32 first, core::u32 last);
    void record(Edit edit, bool coalescable);
    void trimHistory();

    std::vector<Line> m_lines;

    std::vector<Edit> m_undo;
    std::vector<Edit> m_redo;
    std::size_t m_undoBytes = 0;
    bool m_coalescing = false;

    std::vector<Diagnostic> m_diagnostics;
    core::u64 m_revision = 0;
    core::u64 m_diagnosticsRevision = ~0ull;

    core::u32 m_lastRelexed = 0;
};

// --- The highlighter ---------------------------------------------------------
//
// One line at a time, which is what makes an edit cost one line. Separate from
// `ScriptDocument` because it is the only part that knows about Luau: the
// document is a container and this is the thing with an opinion about what the
// bytes mean.
//
// Returns the state the NEXT line inherits. Without `LUAUG_LUAU_COMPILER` there
// is no `Luau.Ast` to link (`cmake/luaug_luau.cmake:47-49`), and this answers a
// single `Text` run spanning the line -- a real fallback rather than a stub, so
// that `script_document.cpp` and its tests never mention the option.
[[nodiscard]] LineState lexLine(std::string_view text, core::u32 lineIndex, LineState entry, std::vector<Token>& out);

// Luau's parser over the whole document, for the squiggles and the gutter marks.
// A failed parse still carries its errors -- `Parser::parse` catches its own
// `ParseError` and returns rather than propagating
// (`Ast/src/Parser.cpp:227-247`) -- so a half-typed file still says where it
// went wrong. Without `LUAUG_LUAU_COMPILER` it answers nothing at all.
void parseDiagnostics(const std::string& text, std::vector<Diagnostic>& out);

} // namespace luaug::app
