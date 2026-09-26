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

#include "luaug/app/script_editor_settings.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
    // A name in a TYPE: after `:` in a declaration, after `::` and `->`, the
    // right of `type X =`, and a generic list. The Luau lexer has no such
    // lexeme -- `number` is a `Name` like any other -- so the highlighter marks
    // it from where it stands (see `markTypes`).
    Type,
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
// **How loudly a diagnostic is drawn.**
//
// A warning is not a smaller error and must not look like one: an error means
// this file will not compile, and a warning means it will and probably should
// not have to. Drawing them the same is how a panel trains somebody to ignore
// both.
enum class Severity : core::u8
{
    Error,
    Warning,
};

struct Diagnostic
{
    Position at;
    // How far the mark runs, in BYTES on that line. Zero means "to the end of
    // the line", which is the honest extent for a parse error -- the parser
    // reports where it gave up, not how much is wrong. A lint knows exactly
    // which name it is talking about and says so.
    core::u32 length = 0;
    std::string message;
    Severity severity = Severity::Error;
    // The PARSER's, as opposed to a lint's or the type checker's. A line still
    // being typed breaks the parse further down -- an open `(` is reported at
    // the `end` below it -- so the pane holds these back below the line being
    // edited too, until the typing stops.
    bool syntax = false;
};

// **Refused rather than truncated.** A document past either bound is reported as
// a status the panel can say out loud; the alternative is a text editor that
// silently loses the end of a file. Eight mebibytes is far past any Luau anybody
// writes, and a 64 KiB line is a minified blob rather than code.
inline constexpr std::size_t kMaxDocumentBytes = 8u * 1024u * 1024u;
inline constexpr std::size_t kMaxLineBytes = 64u * 1024u;

// **Code is indented with tabs, drawn four cells wide** (the owner: "it uses
// spaces instead of tabs"), which is what the reference editor writes. A tab
// advances to the next multiple of this many cells, so a caret, a click and a
// selection over a tab all land where the eye sees it.
inline constexpr core::u32 kTabWidth = 4;

// **Old indentation, made tabs**: the leading whitespace of every line, read as
// cells (a tab to its stop, a space one), written back as whole tabs and the
// remainder as spaces. Only the INDENT -- a space inside a line is somebody's
// alignment or somebody's string. A multi-line string's lines are indentation
// too as far as this can tell; the reference editor makes the same trade.
[[nodiscard]] std::string indentWithTabs(std::string_view source);

// **What language the text is.** Luau is what a script is; HLSL is what a
// surface shader is (ADR 0091), opened in the same pane because a person
// writing one wants the same editor they write everything else in. The
// language decides the highlighter and whether Luau's parser has an opinion;
// everything else a document does is about text and is the same for both.
enum class ScriptLanguage : core::u8
{
    Luau,
    Hlsl,
};

// Which language a file is, by its extension: `.hlsl` and `.hlsli` are HLSL,
// and everything else is Luau.
[[nodiscard]] ScriptLanguage scriptLanguageOf(std::string_view fileName) noexcept;

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

    // Re-lexes the whole document in `language`. Neither an edit nor an undo
    // step: the text is the same text, read differently.
    void setLanguage(ScriptLanguage language);
    [[nodiscard]] ScriptLanguage language() const noexcept { return m_language; }

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

    // **Ctrl+/: comment the lines, or uncomment them when every one already
    // is.** A `-- ` goes in at the block's shallowest indent, so the comments
    // line up; taking them out removes a `--` and the one space after it.
    // Blank lines are left alone either way. One `replace`, so one Ctrl+Z.
    // False when there was nothing to do.
    bool toggleComment(core::u32 first, core::u32 last);

    // **The line edits every code editor has**, each ONE `replace`, so each is
    // one Ctrl+Z. `indentLines` adds four spaces to every non-blank line, or
    // takes up to four (or a tab) off each; `duplicateLines` puts a copy of the
    // block directly below it; `deleteLines` removes the block and its newline.
    // False when there was nothing to do.
    bool indentLines(core::u32 first, core::u32 last, bool outdent);
    bool duplicateLines(core::u32 first, core::u32 last);
    bool deleteLines(core::u32 first, core::u32 last);

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

    // **Every edit between these two is one undo step** -- what one keystroke
    // typed at several carets is. Nesting is not supported and not needed: the
    // panel opens one around each pass over its carets.
    void beginGroup() noexcept;
    void endGroup() noexcept { m_group = 0; }

    // **Where each edit landed since the last take**, in the order they were
    // made: what was replaced (`begin` to `oldEnd`) and where the new text ends.
    // The panel moves its other carets by these, so a letter typed at the first
    // caret does not leave the second one pointing at the wrong byte.
    struct EditSpan
    {
        Position begin;
        Position oldEnd;
        Position newEnd;
    };
    [[nodiscard]] std::vector<EditSpan> takeEditLog() { return std::exchange(m_editLog, {}); }
    // `at`, moved by one edit: before it, untouched; inside what it replaced,
    // to the end of what replaced it; after it, shifted along.
    [[nodiscard]] static Position shifted(Position at, const EditSpan& edit) noexcept;

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

    // Runs Luau's parser over the whole document -- or, for HLSL, which has no
    // parser here, puts back what `setExternalDiagnostics` last said: the
    // compiler's errors, by line. **The caller decides when** --
    // once the text has been still for a moment, not on every keystroke -- so
    // this is a plain call rather than something an edit triggers.
    void refreshDiagnostics();
    [[nodiscard]] std::span<const Diagnostic> diagnostics() const noexcept { return m_diagnostics; }
    // Diagnostics a second pass produced. `refreshDiagnostics` parses TEXT and
    // knows nothing about a world; what needs the tree is appended by the panel
    // in the same breath, so a reader of `diagnostics()` sees one list.
    void appendDiagnostics(std::span<const Diagnostic> extra)
    {
        m_diagnostics.insert(m_diagnostics.end(), extra.begin(), extra.end());
    }
    // **What a compiler said about this text**, for a language whose errors
    // come from outside -- a surface shader's, from the shader compiler. Shown
    // at once and kept across `refreshDiagnostics`, until the next answer
    // replaces them.
    void setExternalDiagnostics(std::vector<Diagnostic> diagnostics);
    [[nodiscard]] std::span<const Diagnostic> externalDiagnostics() const noexcept { return m_external; }
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
    //
    // A TAB is the exception to "a codepoint is a cell": it runs to the next
    // multiple of `kTabWidth`, so these three count it that way.

    // --- Blocks ----------------------------------------------------------------

    // What an Enter at `caret` should do about the block the line opens.
    struct BlockBreak
    {
        // The line opens a block -- it ends in `then`, `do`, `repeat`, `else`
        // or a function's `)` -- so the new line is one step deeper.
        bool opens = false;
        // What closes it, when the document has no closer for it yet: `end`,
        // `end)` for a function passed as an argument, or `until ` for a
        // `repeat`. Empty when it is already closed, or the line only
        // continues a block (`elseif`, `else`).
        std::string closer;
    };
    // **Whether THIS block is closed below** decides "not closed yet": from
    // the caret, `function`, `if`, `do` and `repeat` open and `end` and
    // `until` close, the lexer having set comments and strings aside, until
    // the closer that matches this line's opener. Closed when that closer
    // stands at least as deep as this line; one indented less is an outer
    // block's. An Enter that wrote a second `end` under one somebody already
    // typed is worse than none. An `if` expression opens nothing.
    [[nodiscard]] BlockBreak blockBreakAt(Position caret) const;

    // --- Folding ---------------------------------------------------------------

    // **A block that can be folded away** (the owner: "a button to open and
    // close a block"): `first` is the line its opener is on and `last` the
    // line its closer is on, and folding hides the lines between -- the
    // opener's line and the `end` stay, so a folded block still reads as one.
    // Blocks, as `blockBreakAt` reads them, and tables written over several
    // lines. Only a block with at least one line inside it is a fold.
    struct FoldRange
    {
        core::u32 first = 0;
        core::u32 last = 0;
    };
    // In order of `first`, outer before inner.
    [[nodiscard]] std::vector<FoldRange> foldRanges() const;

    // --- Searching -----------------------------------------------------------

    struct SearchOptions
    {
        bool matchCase = false;
        bool wholeWord = false;
        // `needle` is an ECMAScript regular expression, matched a line at a
        // time; `with` may name its groups as `$1`..`$9` and the whole as `$&`.
        bool regex = false;
    };

    // Whether `needle` can search at all: not empty, and a valid pattern when
    // it is one. The find box tints its field when it cannot.
    [[nodiscard]] static bool searchable(std::string_view needle, SearchOptions options);
    // Every match, in document order. An empty match is a place rather than a
    // piece of text, and is left out.
    [[nodiscard]] std::vector<Range> findAll(std::string_view needle, SearchOptions options) const;
    // The first match at or after `from`, wrapping to the top once. An empty
    // range when `needle` is empty or nothing matches.
    [[nodiscard]] Range findNext(std::string_view needle, Position from, SearchOptions options) const;
    [[nodiscard]] Range findPrevious(std::string_view needle, Position from, SearchOptions options) const;
    [[nodiscard]] core::u32 countMatches(std::string_view needle, SearchOptions options) const;
    // One match replaced, as one undo step, returning what it became. A range
    // that is no longer a match -- the text moved -- replaces nothing.
    Range replaceMatch(Range match, std::string_view needle, std::string_view with, SearchOptions options);
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
        // Non-zero when the edit belongs to a group (`beginGroup`), which undo
        // and redo take whole.
        core::u64 group = 0;
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
    core::u64 m_group = 0;
    core::u64 m_groups = 0;
    std::vector<EditSpan> m_editLog;

    std::vector<Diagnostic> m_diagnostics;
    std::vector<Diagnostic> m_external;
    ScriptLanguage m_language = ScriptLanguage::Luau;
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

// The same for HLSL (ADR 0091). Hand-written, and always built: there is no
// HLSL front end in this process to borrow, and a highlighter needs only to
// agree with the compiler about where a comment, a string and a word end. A
// `/* */` that crosses a line is carried as `LexKind::LongComment`; a
// preprocessor directive is an `Attribute`, and a built-in type a `Type`.
[[nodiscard]] LineState lexHlslLine(std::string_view text, LineState entry, std::vector<Token>& out);

// **What colour each run of a line is drawn in** -- finer than `TokenKind`,
// which says what the lexer saw and is what completion and the automatic `end`
// read. This says what the reference editor colours separately: `function`,
// `local`, `nil`, `self`, a bool, a bracket, a built-in, a function's name
// where it is declared, a method, a property, and a `TODO` inside a comment.
// Worked out from a line's tokens when it is drawn, so no other reader of the
// tokens can be surprised by it.
struct StyledRun
{
    core::u32 column = 0;
    core::u32 length = 0;
    ScriptColor color = ScriptColor::Text;
};
void styleLine(std::string_view text, std::span<const Token> tokens, std::vector<StyledRun>& out);

// Luau's parser over the whole document, for the squiggles and the gutter marks.
// A failed parse still carries its errors -- `Parser::parse` catches its own
// `ParseError` and returns rather than propagating
// (`Ast/src/Parser.cpp:227-247`) -- so a half-typed file still says where it
// went wrong. Without `LUAUG_LUAU_COMPILER` it answers nothing at all.
void parseDiagnostics(const std::string& text, std::vector<Diagnostic>& out);

// One name a module hands back, and what kind of thing it is.
struct ModuleMember
{
    std::string name;
    // `function`, or the shape of the value -- whatever fits the right-hand
    // column of a completion row.
    std::string detail;
};

// **What `require` of this source would give you**, read from the module's own
// text.
//
// This is what makes `local M = require(...)` followed by `M.` mean something.
// It is a walk over the AST the parser already builds and NOT type inference:
// it finds the `return` at the end of the file and reads what is being returned
// -- a table written out, or a local that the file filled in with
// `function M.foo()` and `M.bar = ...`. Those two shapes are what almost every
// module ever written looks like, and anything else answers nothing rather than
// guessing.
//
// Empty when the source does not parse, for the reason the lints stop there: a
// half-typed module has a partial tree and would offer half-typed names.
void moduleMembers(const std::string& source, std::vector<ModuleMember>& out);

// **The names this file lets code at `caret` see** (the owner: "`matrix` is
// offered where it does not exist -- a name in a function's scope should be
// offered in that scope only"). Luau's own scoping, read off the AST: a local
// from the end of its statement to the end of its block (a `local function`
// from its own line, since it may call itself), a function's parameters and a
// loop's variables inside that body, and a global the file assigns or defines
// anywhere. In the order they are declared, each once. A document that does
// not parse still has the partial tree the parser recovered.
//
// `earlier`, when given, receives the names declared BEFORE the caret that it
// cannot see -- a local of a function already closed. A name declared after
// the caret, or by the statement the caret is still inside (`local x = |`),
// does not exist yet and is in neither list.
void visibleNames(const std::string& source, Position caret, std::vector<std::string>& out,
                  std::vector<std::string>* earlier = nullptr);

// **What this file's own code says a dotted path is** (the owner's report:
// `Snake.` offered nothing in a file that had just built `Snake`).
//
// A small reading of the AST, not Luau's type checker (ADR 0057 keeps
// `Luau.Analysis` out of the build). It knows the shapes a script is written
// in: a table filled with `X.f = ...`, `function X.f` and `function X:m`, the
// fields `setmetatable({...}, X)` gives an instance, `type T = { ... }`, a
// value annotated `: T` or returned by `function X.new(): T`, `self` inside a
// method, a list type's element (`{ BasePart }[i]`), and `Instance.new("C")`.
// Where it lands on an engine class it names the class, which the caller
// answers from reflection. Anything it cannot follow answers `known = false`,
// and the caller falls back to what it did before.
// **A colour written in code**, for the swatch the script editor draws beside
// it and the picker that rewrites it (the owner: "the same colour picker the
// Properties has"). `Color3.new(r, g, b)`, `Color3.fromRGB(r, g, b)` and
// `Color3.fromHex("#rrggbb")`, with literal numbers or a literal string --
// a colour built from variables is not one a picker can rewrite.
enum class ColorLiteralKind : core::u8
{
    New,
    FromRgb,
    FromHex,
};
struct ColorLiteral
{
    ColorLiteralKind kind = ColorLiteralKind::New;
    // What is inside the parentheses, which is what the picker replaces.
    Range args;
    // The whole call, `Color3` to `)` inclusive: what the pointer hovers to
    // bring up the swatch.
    Range call;
    core::Color3 color;
};
// The first such colour on a line, or nothing.
[[nodiscard]] std::optional<ColorLiteral> findColorLiteral(std::string_view line, core::u32 lineIndex);
// The arguments a colour is written back as: three decimals at most for
// `new`, whole numbers for `fromRGB`, `"#RRGGBB"` for `fromHex`.
[[nodiscard]] std::string formatColorLiteral(ColorLiteralKind kind, core::Color3 color);

// The path segment an index takes: `a.b[i].` reads as `{"a", "b", "[]"}`, the
// element of `a.b`.
inline constexpr std::string_view kElementStep = "[]";

struct SourceMember
{
    std::string name;
    std::string detail;
    // A function: offered after `:` as well as `.`.
    bool callable = false;
};
struct SourceMembers
{
    bool known = false;
    std::vector<SourceMember> members;
    // The engine class the path ends on, when it ends on one.
    std::string className;
};
[[nodiscard]] SourceMembers sourceMembersOf(const std::string& source, std::span<const std::string> path,
                                            Position caret);

} // namespace luaug::app
