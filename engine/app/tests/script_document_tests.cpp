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

#include <algorithm>
#include <doctest/doctest.h>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

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

TEST_CASE("the names in a type colour as types, where the grammar puts a type")
{
    // **The owner's report: "our editor does not colour types."** The lexer
    // has no type lexeme; these are the places the grammar says one begins.
    //           0         1         2         3         4
    //           0123456789012345678901234567890123456789012345
    ScriptDocument local("local speed: number = 12");
    CHECK(kindAt(local, 0, 6) == TokenKind::Identifier); // speed
    CHECK(kindAt(local, 0, 13) == TokenKind::Type);      // number
    CHECK(kindAt(local, 0, 22) == TokenKind::Number);    // 12

    ScriptDocument signature("function f(a: string, b: Part?): boolean");
    CHECK(kindAt(signature, 0, 11) == TokenKind::Identifier); // a
    CHECK(kindAt(signature, 0, 14) == TokenKind::Type);       // string
    CHECK(kindAt(signature, 0, 22) == TokenKind::Identifier); // b
    CHECK(kindAt(signature, 0, 25) == TokenKind::Type);       // Part
    CHECK(kindAt(signature, 0, 33) == TokenKind::Type);       // boolean

    // A method call is not an annotation.
    ScriptDocument call("part:Destroy()");
    CHECK(kindAt(call, 0, 5) == TokenKind::Identifier);

    // `type`, `export` and the declared name; the right-hand side is a type;
    // a field name inside a table type is not.
    ScriptDocument alias("export type Point = { x: number, y: number }");
    CHECK(kindAt(alias, 0, 0) == TokenKind::Keyword);     // export
    CHECK(kindAt(alias, 0, 7) == TokenKind::Keyword);     // type
    CHECK(kindAt(alias, 0, 12) == TokenKind::Type);       // Point
    CHECK(kindAt(alias, 0, 22) == TokenKind::Identifier); // x
    CHECK(kindAt(alias, 0, 25) == TokenKind::Type);       // number

    // `::`, `->` and a generic list.
    ScriptDocument cast("local n = value :: number");
    CHECK(kindAt(cast, 0, 10) == TokenKind::Identifier); // value
    CHECK(kindAt(cast, 0, 19) == TokenKind::Type);       // number
    ScriptDocument generic("local function first<T>(list: { T }): T");
    CHECK(kindAt(generic, 0, 21) == TokenKind::Type); // T
    CHECK(kindAt(generic, 0, 32) == TokenKind::Type); // T inside { }
    ScriptDocument arrow("local f: (number) -> string");
    CHECK(kindAt(arrow, 0, 10) == TokenKind::Type); // number
    CHECK(kindAt(arrow, 0, 21) == TokenKind::Type); // string

    // An ordinary comparison is not a generic list.
    ScriptDocument compare("if a < b then end");
    CHECK(kindAt(compare, 0, 7) == TokenKind::Identifier);
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

TEST_CASE("a line trades places with the one below it")
{
    app::ScriptDocument document("one\ntwo\nthree");

    REQUIRE(document.moveLines(0, 0, 1));
    CHECK(document.text() == "two\none\nthree");

    REQUIRE(document.moveLines(1, 1, 1));
    CHECK(document.text() == "two\nthree\none");

    // The bottom is the bottom. Answering false rather than doing nothing is
    // what lets the caller leave the caret alone.
    CHECK_FALSE(document.moveLines(2, 2, 1));
    CHECK_FALSE(document.moveLines(0, 0, -1));
    CHECK(document.text() == "two\nthree\none");
}

TEST_CASE("a block of lines moves together and keeps its order")
{
    app::ScriptDocument document("a\nb\nc\nd\ne");

    REQUIRE(document.moveLines(1, 2, 1));
    CHECK(document.text() == "a\nd\nb\nc\ne");

    REQUIRE(document.moveLines(2, 3, -1));
    CHECK(document.text() == "a\nb\nc\nd\ne");
}

TEST_CASE("moving a line is one undo")
{
    // **The whole reason it is one `replace`.** A move somebody has to press
    // Ctrl+Z twice to take back is a move that will eat the line underneath it
    // the first time somebody is not watching.
    app::ScriptDocument document("first\nsecond");
    const std::string before = document.text();

    REQUIRE(document.moveLines(0, 0, 1));
    CHECK(document.text() == "second\nfirst");

    app::Position caret{};
    CHECK(document.undo(caret));
    CHECK(document.text() == before);
}

TEST_CASE("a moved line keeps a trailing blank line where it was")
{
    // The last line of a file is usually empty, and a block moved into it must
    // not swallow it: the file would lose its final newline, which every tool
    // downstream would then rewrite.
    app::ScriptDocument document("a\nb\n");
    REQUIRE(document.lineCount() == 3);

    REQUIRE(document.moveLines(1, 1, 1));
    CHECK(document.text() == "a\n\nb");
}

namespace {

[[nodiscard]] std::vector<app::Diagnostic> lintsOf(std::string_view source)
{
    app::ScriptDocument document(source);
    document.refreshDiagnostics();
    std::vector<app::Diagnostic> out;
    for (const app::Diagnostic& diagnostic : document.diagnostics()) {
        if (diagnostic.severity == app::Severity::Warning)
            out.push_back(diagnostic);
    }
    return out;
}

[[nodiscard]] bool mentions(const std::vector<app::Diagnostic>& list, std::string_view needle)
{
    return std::any_of(list.begin(), list.end(), [needle](const app::Diagnostic& diagnostic) {
        return diagnostic.message.find(needle) != std::string::npos;
    });
}

} // namespace

TEST_CASE("a name nothing declares is reported, and the sandbox is why it can be")
{
    // **No false positives to apologise for.** `sealGlobals` freezes the globals
    // table (R4), so a name that is not in the sandbox's surface will be nil at
    // runtime -- this is a fact rather than a style note.
    const std::vector<app::Diagnostic> lints = lintsOf("local x = someHelper(1)\nprint(x)");
    REQUIRE(lints.size() == 1);
    CHECK(mentions(lints, "someHelper"));
    CHECK(lints.front().at.line == 0);
    // The NAME and not the rest of the line: a mark that runs to the end of the
    // line points at the line, and the word is the whole message.
    CHECK(lints.front().length == 10);
}

TEST_CASE("everything the sandbox really has is left alone")
{
    // The list this reads is the one the completion offers, which is the list
    // checked against a real VM. Underlining a name the editor itself just
    // suggested is the failure mode worth a case of its own.
    CHECK(lintsOf("print(typeof(math.floor(1.5)))").empty());
    CHECK(lintsOf("local t = table.create(4)\nprint(#t)").empty());
    CHECK(lintsOf("print(game, workspace, script, task)").empty());
    CHECK(lintsOf("local ok = pcall(function() end)\nprint(ok, _VERSION)").empty());
    CHECK(lintsOf("print(Vector3.new(1, 2, 3), CFrame.identity, Enum.PartShape.Ball)").empty());
}

TEST_CASE("a local nobody reads is reported")
{
    const std::vector<app::Diagnostic> lints = lintsOf("local unusedThing = 1\nprint(2)");
    REQUIRE(lints.size() == 1);
    CHECK(mentions(lints, "unusedThing"));

    // Read four hundred lines later is still read, which is why the walk
    // finishes before it decides.
    CHECK(lintsOf("local later = 1\nlocal function f() return later end\nprint(f())").empty());

    // `local function` is a declaration too.
    CHECK(mentions(lintsOf("local function helper() end\nprint(1)"), "helper"));
}

TEST_CASE("the lints stay out of the way where staying out of the way is the point")
{
    // **An underscore is how somebody says "I know".** Warning through it would
    // leave them no way to say it.
    CHECK(lintsOf("local _ignored = 1\nprint(2)").empty());

    // A parameter and a loop variable are unused constantly and on purpose.
    CHECK(lintsOf("local function f(a, b) return 1 end\nprint(f(1, 2))").empty());
    CHECK(lintsOf("for i = 1, 3 do print(0) end").empty());
    CHECK(lintsOf("for k, v in pairs({}) do print(0) end").empty());
}

TEST_CASE("a file that does not parse is not linted")
{
    // **The rule that decides whether warnings are worth having.** A half-typed
    // file has a partial tree, and linting it would put a warning under every
    // name somebody is in the middle of writing -- which is the fastest way to
    // make a person turn warnings off for good.
    app::ScriptDocument document("local value = \nprint(");
    document.refreshDiagnostics();

    bool sawError = false;
    for (const app::Diagnostic& diagnostic : document.diagnostics()) {
        CHECK(diagnostic.severity == app::Severity::Error);
        sawError = true;
    }
    CHECK(sawError);
}

TEST_CASE("Ctrl+/ comments a block at its shallowest indent, and takes the comments out again")
{
    ScriptDocument document("local a = 1\n    local b = 2\n\nprint(a)");
    REQUIRE(document.toggleComment(0, 3));
    CHECK(document.text() == "-- local a = 1\n--     local b = 2\n\n-- print(a)");
    // One Ctrl+Z takes the whole block back.
    Position caret{0, 0};
    REQUIRE(document.undo(caret));
    CHECK(document.text() == "local a = 1\n    local b = 2\n\nprint(a)");

    ScriptDocument indented("    x()\n    y()");
    REQUIRE(indented.toggleComment(0, 1));
    CHECK(indented.text() == "    -- x()\n    -- y()");
    REQUIRE(indented.toggleComment(0, 1));
    CHECK(indented.text() == "    x()\n    y()");

    // A block of blank lines has nothing to comment.
    ScriptDocument blank("\n\n");
    CHECK_FALSE(blank.toggleComment(0, 2));
}

TEST_CASE("the line edits every code editor has are one undo step each")
{
    ScriptDocument document("a()\n    b()\nc()");
    REQUIRE(document.indentLines(0, 1, false));
    CHECK(document.text() == "    a()\n        b()\nc()");
    REQUIRE(document.indentLines(0, 1, true));
    CHECK(document.text() == "a()\n    b()\nc()");

    REQUIRE(document.duplicateLines(0, 0));
    CHECK(document.text() == "a()\na()\n    b()\nc()");

    REQUIRE(document.deleteLines(1, 2));
    CHECK(document.text() == "a()\nc()");
    // The last line takes the newline before it.
    REQUIRE(document.deleteLines(1, 1));
    CHECK(document.text() == "a()");

    // Each was one step: five undos walk all five back.
    Position caret{0, 0};
    for (int step = 0; step < 5; ++step)
        REQUIRE(document.undo(caret));
    CHECK(document.text() == "a()\n    b()\nc()");

    // Nothing to outdent is nothing done.
    ScriptDocument flat("x");
    CHECK_FALSE(flat.indentLines(0, 0, true));
}

TEST_CASE("a colour written in code is found, and written back in its own form")
{
    // **The swatch and picker beside a colour in the code** (the owner: "the
    // same colour picker the Properties has").
    using app::ColorLiteralKind;
    const std::optional<app::ColorLiteral> made = app::findColorLiteral("local c = Color3.new(1, 0.5, 0)", 3);
    REQUIRE(made.has_value());
    CHECK(made->kind == ColorLiteralKind::New);
    CHECK(made->args.begin == Position{3, 21});
    CHECK(made->args.end == Position{3, 30});
    CHECK(static_cast<double>(made->color.g) == doctest::Approx(0.5));

    const std::optional<app::ColorLiteral> rgb = app::findColorLiteral("x = Color3.fromRGB(255, 128, 0)", 0);
    REQUIRE(rgb.has_value());
    CHECK(rgb->kind == ColorLiteralKind::FromRgb);
    CHECK(static_cast<double>(rgb->color.r) == doctest::Approx(1.0));

    const std::optional<app::ColorLiteral> hex = app::findColorLiteral("Color3.fromHex(\"#FF8000\")", 0);
    REQUIRE(hex.has_value());
    CHECK(hex->kind == ColorLiteralKind::FromHex);
    CHECK(static_cast<double>(hex->color.r) == doctest::Approx(1.0));

    // Built from variables is not a colour a picker can rewrite.
    CHECK_FALSE(app::findColorLiteral("Color3.new(r, g, b)", 0).has_value());
    CHECK_FALSE(app::findColorLiteral("Color3.new(math.random(), 0, 0)", 0).has_value());

    CHECK(app::formatColorLiteral(ColorLiteralKind::New, core::Color3{1.0f, 0.5f, 0.25f}) == "1, 0.5, 0.25");
    CHECK(app::formatColorLiteral(ColorLiteralKind::FromRgb, core::Color3{1.0f, 0.5f, 0.0f}) == "255, 128, 0");
    CHECK(app::formatColorLiteral(ColorLiteralKind::FromHex, core::Color3{1.0f, 0.5f, 0.0f}) == "\"#FF8000\"");
}

TEST_CASE("return and continue colour as keywords wherever a statement can be")
{
    // **The owner's report**: `continue` did not colour like `if` and `end`.
    // It is a contextual keyword the lexer reads as a name, so the highlighter
    // decides; `return` is reserved and the lexer already knows.
    //           0         1         2         3
    //           0123456789012345678901234567890123
    ScriptDocument loop("for i = 1, 3 do if i then continue end end");
    CHECK(kindAt(loop, 0, 26) == TokenKind::Keyword); // continue
    ScriptDocument alone("    continue");
    CHECK(kindAt(alone, 0, 4) == TokenKind::Keyword);
    ScriptDocument returned("if dead then return end");
    CHECK(kindAt(returned, 0, 13) == TokenKind::Keyword); // return
    ScriptDocument typed("function f(): Snake return snake end");
    CHECK(kindAt(typed, 0, 20) == TokenKind::Keyword); // return, after a return type
    // A variable called `continue` is a name.
    ScriptDocument variable("local continue = 1");
    CHECK(kindAt(variable, 0, 6) == TokenKind::Identifier);
}

TEST_CASE("Enter after a line that opens a block writes its end, once")
{
    // **The owner: "the automatic end, as other editors do".** A closer only
    // when the document has none for the block: a second `end` under one
    // somebody already typed is worse than none.
    const auto at = [](std::string_view text) {
        const std::string source(text);
        ScriptDocument document(source);
        const core::u32 line = document.lineCount() - 1;
        return document.blockBreakAt(Position{line, document.lineLength(line)});
    };

    CHECK(at("if ready then").closer == "end");
    CHECK(at("for i = 1, 10 do").closer == "end");
    CHECK(at("while true do").closer == "end");
    CHECK(at("local function spin(part: Part)").closer == "end");
    CHECK(at("local f = function()").closer == "end");
    CHECK(at("repeat").closer == "until ");
    // A function passed as an argument closes the call too.
    CHECK(at("RunService.Heartbeat:Connect(function(dt)").closer == "end)");
    CHECK(at("task.spawn(pcall(function()").closer == "end))");

    // Already closed: deeper, and nothing written.
    const auto closed = [](std::string_view text) {
        const std::string source(text);
        ScriptDocument document(source);
        return document.blockBreakAt(Position{0, document.lineLength(0)});
    };
    ScriptDocument::BlockBreak done = closed("if ready then\nend");
    CHECK(done.opens);
    CHECK(done.closer.empty());

    // `else` and `elseif` go one step deeper and close nothing of their own.
    CHECK(at("if a then\nelse").opens);
    CHECK(at("if a then\nelse").closer.empty());
    CHECK(at("if a then\nelseif b then").closer.empty());

    // A comment or a string that says `then` opens nothing, and nor does a
    // call that is only a call.
    CHECK_FALSE(at("print(1) -- then").opens);
    CHECK_FALSE(at("local s = \"then\"").opens);
    CHECK_FALSE(at("print(x)").opens);
}

TEST_CASE("find: every match, a regular expression, replace one or all")
{
    // **The owner: Ctrl+F "like the other editors -- find, replace, and the
    // rest".** The box reads all of this.
    ScriptDocument document("local speed = 10\nlocal Speed = speed * 2\n-- speedy");
    ScriptDocument::SearchOptions plain;

    // Case-insensitive by default, and every match in order.
    const std::vector<Range> all = document.findAll("speed", plain);
    REQUIRE(all.size() == 4);
    CHECK(all.front() == Range{Position{0, 6}, Position{0, 11}});
    CHECK(document.findAll("speed", {.matchCase = true}).size() == 3);
    CHECK(document.findAll("speed", {.wholeWord = true}).size() == 3);

    // A pattern: its own length per match, `$1` in the replacement.
    const ScriptDocument::SearchOptions pattern{.matchCase = true, .regex = true};
    const std::vector<Range> numbers = document.findAll("[0-9]+", pattern);
    REQUIRE(numbers.size() == 2);
    CHECK(numbers.front() == Range{Position{0, 14}, Position{0, 16}});
    CHECK_FALSE(ScriptDocument::searchable("(unclosed", pattern));
    CHECK(document.findAll("(unclosed", pattern).empty());
    CHECK(ScriptDocument::searchable("(closed)", pattern));

    // Previous wraps to the last; next from the end wraps to the first.
    CHECK(document.findPrevious("speed", Position{0, 0}, plain) == all.back());
    CHECK(document.findNext("speed", Position{2, 10}, plain) == all.front());

    // One replaced, with a group, as one undo step; a stale range replaces
    // nothing.
    const Range done = document.replaceMatch(numbers.front(), "([0-9]+)", "($1 + 1)", pattern);
    CHECK(document.line(0) == "local speed = (10 + 1)");
    CHECK(done == Range{Position{0, 14}, Position{0, 22}});
    const Range stale = document.replaceMatch(Range{Position{0, 0}, Position{0, 3}}, "[0-9]+", "x", pattern);
    CHECK(stale.begin == stale.end);
    CHECK(document.line(0) == "local speed = (10 + 1)");
    Position caret;
    CHECK(document.undo(caret));
    CHECK(document.line(0) == "local speed = 10");

    // All, as one step.
    CHECK(document.replaceAll("speed", "velocity", {.matchCase = true, .wholeWord = true}) == 2);
    CHECK(document.line(1) == "local Speed = velocity * 2");
}
