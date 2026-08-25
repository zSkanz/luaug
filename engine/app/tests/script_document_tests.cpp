// The text of a script, asserted without a window (ADR 0057).
//
// **The code pane is the one panel whose behaviour is almost entirely not
// pixels**, and this file is why that split was worth drawing: editing, undo,
// search and incremental highlighting are all arithmetic over bytes, and the
// only thing left for a person to look at is whether the colours are pleasant.
//
// The case worth reading twice is the last group. "An edit re-lexes exactly the
// lines whose incoming state changed" is a claim nobody can see, and a
// highlighter that quietly re-lexed the whole file on every keystroke would look
// identical and behave identically until somebody opened a large file. So the
// count is asserted as an EQUALITY -- the E4 precedent, where a bound that is
// merely small passes while the defect is still there.
#include "luaug/app/script_document.h"

#include <doctest/doctest.h>
#include <ostream>
#include <string>

using namespace luaug;
using app::Position;
using app::Range;
using app::ScriptDocument;
using app::TokenKind;

namespace {

// The kind covering a column, for the highlighting cases. `Text` when nothing
// does, which is what a gap between runs is.
[[nodiscard]] TokenKind kindAt(const ScriptDocument& document, core::u32 line, core::u32 column)
{
    for (const app::Token& token : document.tokens(line)) {
        if (column >= token.column && column < token.column + token.length)
            return token.kind;
    }
    return TokenKind::Text;
}

[[nodiscard]] std::string manyLines(core::u32 count, std::string_view body)
{
    std::string out;
    for (core::u32 index = 0; index < count; ++index) {
        out += body;
        if (index + 1 < count)
            out.push_back('\n');
    }
    return out;
}

} // namespace

TEST_CASE("a document is at least one line, and round-trips its text")
{
    ScriptDocument empty;
    CHECK(empty.lineCount() == 1);
    CHECK(empty.text().empty());

    ScriptDocument document("local x = 1\nreturn x\n");
    // A trailing newline is a last line that is empty, which is what every text
    // editor shows and what keeps `text()` an exact round trip.
    CHECK(document.lineCount() == 3);
    CHECK(document.line(0) == "local x = 1");
    CHECK(document.line(1) == "return x");
    CHECK(document.line(2).empty());
    CHECK(document.text() == "local x = 1\nreturn x\n");
}

TEST_CASE("line endings are normalised on the way in")
{
    // A file written on Windows, edited here, comes back with the endings every
    // other file this engine writes already has.
    ScriptDocument document("a\r\nb\rc\n");
    CHECK(document.lineCount() == 4);
    CHECK(document.line(0) == "a");
    CHECK(document.line(1) == "b");
    CHECK(document.line(2) == "c");
    CHECK(document.text() == "a\nb\nc\n");
}

TEST_CASE("a document past its bounds is refused, not truncated")
{
    ScriptDocument document;
    CHECK(!document.setText(std::string(app::kMaxDocumentBytes + 1, 'x')));
    CHECK(document.lineCount() == 1);
    CHECK(document.text().empty());

    CHECK(!document.setText(std::string(app::kMaxLineBytes + 1, 'y')));
    CHECK(document.text().empty());

    // And the line just under the bound is fine, so the refusal is a bound
    // rather than an accident.
    CHECK(document.setText(std::string(app::kMaxLineBytes, 'y')));
    CHECK(document.lineLength(0) == app::kMaxLineBytes);
}

TEST_CASE("inserting and erasing move text and the caret together")
{
    ScriptDocument document("local x = 1");

    const Position after = document.insert(Position{0, 5}, "!");
    CHECK(after == Position{0, 6});
    CHECK(document.line(0) == "local! x = 1");

    // A newline splits the line and lands the caret at the start of the new one.
    const Position split = document.insert(Position{0, 6}, "\n");
    CHECK(split == Position{1, 0});
    CHECK(document.lineCount() == 2);
    CHECK(document.line(0) == "local!");
    CHECK(document.line(1) == " x = 1");

    // Erasing across the boundary joins them back.
    const Position joined = document.erase(Range{Position{0, 6}, Position{1, 0}});
    CHECK(joined == Position{0, 6});
    CHECK(document.lineCount() == 1);
    CHECK(document.line(0) == "local! x = 1");
}

TEST_CASE("a range reads and erases the same text, dragged either way")
{
    ScriptDocument document("one\ntwo\nthree");
    const Range span{Position{0, 1}, Position{2, 2}};

    CHECK(document.textIn(span) == "ne\ntwo\nth");
    // Dragged upwards is the same selection, which every caller gets for free
    // rather than normalising at each call site.
    CHECK(document.textIn(Range{span.end, span.begin}) == "ne\ntwo\nth");

    document.erase(Range{span.end, span.begin});
    CHECK(document.text() == "oree");
}

TEST_CASE("typing coalesces into one undo step and anything else breaks the run")
{
    ScriptDocument document;
    Position caret{0, 0};
    for (const char* c : {"h", "e", "l", "l", "o"})
        caret = document.insert(caret, c);
    CHECK(document.line(0) == "hello");

    // Five keystrokes, one step -- which is what makes Ctrl+Z undo a word rather
    // than a letter.
    CHECK(document.undo(caret));
    CHECK(document.text().empty());
    CHECK(!document.canUndo());

    CHECK(document.redo(caret));
    CHECK(document.line(0) == "hello");

    // A newline ends the run, so what follows is its own step.
    caret = document.insert(caret, "\n");
    caret = document.insert(caret, "x");
    CHECK(document.undo(caret));
    CHECK(document.text() == "hello\n");
    CHECK(document.undo(caret));
    CHECK(document.text() == "hello");
}

TEST_CASE("undo puts the caret where the change was")
{
    ScriptDocument document("alpha");
    Position caret = document.erase(Range{Position{0, 0}, Position{0, 5}});
    CHECK(document.text().empty());

    // Moved to where it happened, because an undo somebody cannot see is an undo
    // they press again.
    CHECK(document.undo(caret));
    CHECK(document.text() == "alpha");
    CHECK(caret == Position{0, 0});
}

TEST_CASE("a caret never lands inside a codepoint")
{
    // Two bytes for the e-acute, four for the emoji.
    ScriptDocument document("a\xc3\xa9\xf0\x9f\x8e\xb2z");

    Position caret{0, 0};
    caret = document.nextColumn(caret);
    CHECK(caret == Position{0, 1});
    caret = document.nextColumn(caret);
    CHECK(caret == Position{0, 3});
    caret = document.nextColumn(caret);
    CHECK(caret == Position{0, 7});

    caret = document.prevColumn(caret);
    CHECK(caret == Position{0, 3});
    caret = document.prevColumn(caret);
    CHECK(caret == Position{0, 1});

    // And a column handed in from the middle of one is pulled back to its start,
    // which is what a click between two halves of a glyph produces.
    CHECK(document.clamp(Position{0, 2}) == Position{0, 1});
    CHECK(document.clamp(Position{0, 5}) == Position{0, 3});
}

TEST_CASE("the word under a position is what a double click selects")
{
    ScriptDocument document("local speed = 12");
    CHECK(document.textIn(document.wordAt(Position{0, 8})) == "speed");
    CHECK(document.textIn(document.wordAt(Position{0, 6})) == "speed");
    CHECK(document.textIn(document.wordAt(Position{0, 14})) == "12");
    // Not on a word: an empty range rather than a guess.
    CHECK(document.wordAt(Position{0, 5}).empty());
}

TEST_CASE("search wraps once, and whole-word means whole word")
{
    ScriptDocument document("x = 1\ny = x\nxx = 2");

    Range hit = document.findNext("x", Position{0, 0}, {});
    CHECK(hit == Range{Position{0, 0}, Position{0, 1}});

    hit = document.findNext("x", hit.end, {});
    CHECK(hit == Range{Position{1, 4}, Position{1, 5}});

    // Past the last match, the search comes back to the top rather than
    // answering nothing.
    hit = document.findNext("y", Position{2, 0}, {});
    CHECK(hit == Range{Position{1, 0}, Position{1, 1}});

    CHECK(document.countMatches("x", {}) == 4);
    CHECK(document.countMatches("x", {.matchCase = true, .wholeWord = true}) == 2);
    CHECK(document.countMatches("X", {.matchCase = true, .wholeWord = false}) == 0);
    CHECK(document.countMatches("X", {.matchCase = false, .wholeWord = false}) == 4);
}

TEST_CASE("replace all is one undo step")
{
    ScriptDocument document("a = 1\nb = a\na = a + 1");
    CHECK(document.replaceAll("a", "speed", {.matchCase = true, .wholeWord = true}) == 4);
    CHECK(document.text() == "speed = 1\nb = speed\nspeed = speed + 1");

    Position caret{0, 0};
    CHECK(document.undo(caret));
    CHECK(document.text() == "a = 1\nb = a\na = a + 1");
    CHECK(!document.canUndo());
}

// --- Highlighting ------------------------------------------------------------

TEST_CASE("Luau's own lexer decides the colours")
{
    ScriptDocument document(R"(local speed = 12 -- how fast)");

    CHECK(kindAt(document, 0, 0) == TokenKind::Keyword);    // local
    CHECK(kindAt(document, 0, 6) == TokenKind::Identifier); // speed
    CHECK(kindAt(document, 0, 12) == TokenKind::Operator);  // =
    CHECK(kindAt(document, 0, 14) == TokenKind::Number);    // 12
    CHECK(kindAt(document, 0, 17) == TokenKind::Comment);   // -- how fast

    ScriptDocument strings("local s = \"hi\"");
    CHECK(kindAt(strings, 0, 10) == TokenKind::String);
}

TEST_CASE("a half-typed line still colours rather than throwing")
{
    // The lexer answers `Broken*` for this rather than raising, which is the
    // whole reason a highlighter can run on every keystroke.
    ScriptDocument document("local s = \"unterminated");
    CHECK(kindAt(document, 0, 0) == TokenKind::Keyword);
    CHECK(kindAt(document, 0, 10) == TokenKind::Error);
}

TEST_CASE("a long bracket carries state across lines and closes at its own level")
{
    ScriptDocument document("--[==[\nstill a comment\n]]\nstill a comment\n]==]\nlocal x = 1");

    CHECK(kindAt(document, 1, 0) == TokenKind::Comment);
    // `]]` does not close a `[==[`, which is the whole reason the level is
    // carried and not just a flag.
    CHECK(kindAt(document, 3, 0) == TokenKind::Comment);
    CHECK(kindAt(document, 5, 0) == TokenKind::Keyword);

    ScriptDocument str("local s = [[\nline\n]]\nlocal y = 2");
    CHECK(kindAt(str, 1, 0) == TokenKind::String);
    CHECK(kindAt(str, 3, 0) == TokenKind::Keyword);
}

TEST_CASE("an edit costs the lines it reached, and not one more")
{
    // Two documents an order of magnitude apart, so a highlighter that re-lexed
    // everything would report two different numbers here.
    ScriptDocument small(manyLines(200, "local x = 1"));
    ScriptDocument large(manyLines(20000, "local x = 1"));

    Position smallCaret = small.insert(Position{100, 0}, "y");
    Position largeCaret = large.insert(Position{100, 0}, "y");

    CHECK(small.lastRelexedLines() == 1);
    CHECK(large.lastRelexedLines() == 1);
    // Equal, not merely both small: that is the assertion a re-lex-everything
    // implementation fails and a "re-lex a few lines around it" one passes.
    CHECK(small.lastRelexedLines() == large.lastRelexedLines());

    (void)smallCaret;
    (void)largeCaret;
}

TEST_CASE("opening a block comment re-lexes what it changed, and closing it costs the same")
{
    ScriptDocument document(manyLines(500, "local x = 1"));

    // Nothing below line 0 inherits anything different yet.
    document.insert(Position{0, 0}, "z");
    CHECK(document.lastRelexedLines() == 1);

    // Now every line below becomes a comment, so every line below is re-lexed --
    // once, and honestly.
    document.insert(Position{0, 0}, "--[[");
    CHECK(document.lastRelexedLines() == 500);

    // And the next keystroke INSIDE the comment costs one line again, because
    // the state the next line inherits did not move.
    document.insert(Position{1, 0}, "q");
    CHECK(document.lastRelexedLines() == 1);

    // Closing it puts everything below back, and costs the same walk.
    document.insert(Position{1, 0}, "]]");
    CHECK(document.lastRelexedLines() == 499);
}

TEST_CASE("undo re-lexes exactly what redoing it did")
{
    ScriptDocument document(manyLines(50, "local x = 1"));
    document.insert(Position{0, 0}, "--[[");
    CHECK(kindAt(document, 10, 0) == TokenKind::Comment);

    Position caret{0, 0};
    CHECK(document.undo(caret));
    CHECK(kindAt(document, 10, 0) == TokenKind::Keyword);

    CHECK(document.redo(caret));
    CHECK(kindAt(document, 10, 0) == TokenKind::Comment);
}

// --- Diagnostics -------------------------------------------------------------

TEST_CASE("a syntax error is reported where Luau puts it")
{
    ScriptDocument document("local x = 1\nlocal = 2\n");
    CHECK(document.diagnosticsStale());

    document.refreshDiagnostics();
    CHECK(!document.diagnosticsStale());
    REQUIRE(!document.diagnostics().empty());
    // Zero-based, like Luau's own `Location`: the second line is 1.
    CHECK(document.diagnostics().front().at.line == 1);
    CHECK(!document.diagnostics().front().message.empty());

    // And an edit makes them stale again, so the panel can ask at rest instead
    // of on a timer.
    document.insert(Position{0, 0}, " ");
    CHECK(document.diagnosticsStale());
}

TEST_CASE("a document that parses has nothing to say")
{
    ScriptDocument document("local function add(a, b)\n    return a + b\nend\nreturn add\n");
    document.refreshDiagnostics();
    CHECK(document.diagnostics().empty());
}

// --- Bytes and cells ---------------------------------------------------------
//
// **The defect a person typing Portuguese found and every ASCII test passed
// through.** A column is bytes, a monospace cell is a codepoint, and `á` is two
// bytes and one glyph -- so a pane placing runs at `byteColumn * advance` left a
// gap the width of a space after every accented letter, and put its caret one
// cell to the right of the character it was on.

TEST_CASE("a cell is a codepoint and a column is bytes")
{
    // `local á = 1`: the accented letter is two bytes, so the line is twelve
    // bytes and eleven cells.
    ScriptDocument document("local \xc3\xa1 = 1");
    CHECK(document.lineLength(0) == 12);
    CHECK(document.cellCount(0) == 11);

    // Everything before the accent agrees, and everything after it is off by
    // exactly the extra byte -- which is the gap that showed up on screen.
    CHECK(document.cellOf(0, 0) == 0);
    CHECK(document.cellOf(0, 6) == 6);
    CHECK(document.cellOf(0, 8) == 7);
    CHECK(document.cellOf(0, 12) == 11);

    // And back, which is what a click has to do.
    CHECK(document.columnOfCell(0, 0) == 0);
    CHECK(document.columnOfCell(0, 6) == 6);
    CHECK(document.columnOfCell(0, 7) == 8);
    CHECK(document.columnOfCell(0, 11) == 12);
    // Past the end is the end, which is where a click to the right of the last
    // character lands.
    CHECK(document.columnOfCell(0, 99) == 12);
}

TEST_CASE("every cell round-trips to the column it came from")
{
    // A four-byte codepoint beside a two-byte one, because the arithmetic that
    // works for one and not the other is exactly the arithmetic somebody writes
    // by hand.
    ScriptDocument document("a\xc3\xa9z\xf0\x9f\x8e\xb2"
                            "b");
    for (core::u32 cell = 0; cell <= document.cellCount(0); ++cell) {
        const core::u32 column = document.columnOfCell(0, cell);
        INFO("cell " << cell << " -> column " << column);
        CHECK(document.cellOf(0, column) == cell);
        // And a column a caret can really occupy: never inside a codepoint.
        CHECK(document.clamp(Position{0, column}).column == column);
    }
}

TEST_CASE("an ASCII line is its own cell count, which is why this was invisible")
{
    ScriptDocument document("local x = 1");
    CHECK(document.cellCount(0) == document.lineLength(0));
    for (core::u32 column = 0; column <= document.lineLength(0); ++column)
        CHECK(document.cellOf(0, column) == column);
}
