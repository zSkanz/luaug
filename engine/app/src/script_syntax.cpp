// The highlighter and the diagnostics: the only part of the script editor that
// knows what Luau IS (ADR 0057).
//
// Separate from `script_document.cpp` for two reasons. It is the one place that
// includes a Luau header, so `#if LUAUG_LUAU_COMPILER` lives here and nowhere
// else -- `script_document.cpp` and its tests never mention the option. And the
// document is a container while this is the thing with an opinion about the
// bytes, which is the same seam `inspector.h` draws between what a panel decides
// and what it draws.
#include "luaug/app/script_document.h"

#include <algorithm>
#include <optional>

#if LUAUG_LUAU_COMPILER
#include <Luau/Allocator.h>
#include <Luau/Lexer.h>
#include <Luau/Location.h>
#include <Luau/ParseOptions.h>
#include <Luau/ParseResult.h>
#include <Luau/Parser.h>
#endif

namespace luaug::app {
namespace {

void pushToken(std::vector<Token>& out, core::u32 column, core::u32 end, TokenKind kind)
{
    if (end > column)
        out.push_back(Token{.column = column, .length = end - column, .kind = kind});
}

#if LUAUG_LUAU_COMPILER

// A long bracket opener at `at`: `[`, some `=`, `[`, optionally preceded by the
// `--` that makes it a comment. Returns how many bytes the opener spans and its
// level, or nothing when this is not one.
//
// Written here rather than taken from the lexer because the lexer's answer for
// an UNCLOSED one is a single `Broken*` lexeme that does not carry the level --
// and the level is the whole of what the next line needs to know.
struct LongOpen
{
    core::u8 level = 0;
    bool comment = false;
};

[[nodiscard]] std::optional<LongOpen> matchLongOpen(std::string_view text, std::size_t at) noexcept
{
    LongOpen open;
    std::size_t index = at;
    if (text.compare(index, 2, "--") == 0) {
        open.comment = true;
        index += 2;
    }
    if (index >= text.size() || text[index] != '[')
        return std::nullopt;
    ++index;
    while (index < text.size() && text[index] == '=') {
        // A level past 255 is not a program, it is somebody testing the parser.
        if (open.level == 255)
            return std::nullopt;
        ++open.level;
        ++index;
    }
    if (index >= text.size() || text[index] != '[')
        return std::nullopt;
    return open;
}

// Where a long bracket at `level` closes within `text`, searching from `from`.
// Returns the offset one past the closing `]`, or npos.
[[nodiscard]] std::size_t findLongClose(std::string_view text, core::u8 level, std::size_t from) noexcept
{
    for (std::size_t index = from; index < text.size(); ++index) {
        if (text[index] != ']')
            continue;
        std::size_t scan = index + 1;
        core::u8 seen = 0;
        while (scan < text.size() && text[scan] == '=' && seen < level) {
            ++seen;
            ++scan;
        }
        if (seen == level && scan < text.size() && text[scan] == ']')
            return scan + 1;
    }
    return std::string_view::npos;
}

// **One allocator and one name table for the whole process**, and that is safe
// rather than lucky: `setReadNames(false)` routes identifier lookup through
// `AstNameTable::getWithType`, which is const and allocates nothing
// (`Ast/src/Lexer.cpp:715-716`), while the 21 reserved words are pre-registered
// by the table's own constructor (`:204-212`) so keywords are still classified.
// The table therefore never grows, and the editor is main-thread only.
struct LexerScratch
{
    Luau::Allocator allocator;
    Luau::AstNameTable names{allocator};
};

[[nodiscard]] LexerScratch& scratch()
{
    static LexerScratch instance;
    return instance;
}

[[nodiscard]] TokenKind kindOf(Luau::Lexeme::Type type) noexcept
{
    if (type >= Luau::Lexeme::Reserved_BEGIN && type < Luau::Lexeme::Reserved_END)
        return TokenKind::Keyword;

    switch (type) {
    case Luau::Lexeme::Name:
        return TokenKind::Identifier;
    case Luau::Lexeme::Number:
        return TokenKind::Number;
    case Luau::Lexeme::RawString:
    case Luau::Lexeme::QuotedString:
    case Luau::Lexeme::InterpStringBegin:
    case Luau::Lexeme::InterpStringMid:
    case Luau::Lexeme::InterpStringEnd:
    case Luau::Lexeme::InterpStringSimple:
        return TokenKind::String;
    case Luau::Lexeme::Comment:
    case Luau::Lexeme::BlockComment:
        return TokenKind::Comment;
    case Luau::Lexeme::Attribute:
    case Luau::Lexeme::AttributeOpen:
        return TokenKind::Attribute;
    case Luau::Lexeme::BrokenString:
    case Luau::Lexeme::BrokenComment:
    case Luau::Lexeme::BrokenUnicode:
    case Luau::Lexeme::BrokenInterpDoubleBrace:
    case Luau::Lexeme::Error:
        return TokenKind::Error;
    case Luau::Lexeme::Eof:
        return TokenKind::Text;
    default:
        break;
    }
    // Everything left is punctuation: the raw character values below `Char_END`
    // and the multi-byte operators above it. There is no `Operator` member in
    // the enum, which is why this is a default rather than a list.
    return TokenKind::Operator;
}

// Lexes `text` from `startColumn` to its end, appending runs. Answers the state
// the next line inherits.
[[nodiscard]] LineState lexSegment(std::string_view text, core::u32 lineIndex, core::u32 startColumn,
                                   std::vector<Token>& out)
{
    if (startColumn >= text.size())
        return LineState{};

    Luau::Lexer lexer(text.data() + startColumn, text.size() - startColumn, scratch().names,
                      Luau::Position(lineIndex, startColumn));
    // Comments are what we colour, and identifiers are read by position rather
    // than by name -- see the note on `LexerScratch`.
    lexer.setSkipComments(false);
    lexer.setReadNames(false);

    for (;;) {
        const Luau::Lexeme& lexeme = lexer.next();
        if (lexeme.type == Luau::Lexeme::Eof)
            break;

        const auto begin = static_cast<core::u32>(lexeme.location.begin.column);
        const auto end = static_cast<core::u32>(lexeme.location.end.column);
        const TokenKind kind = kindOf(lexeme.type);
        pushToken(out, begin, std::min<core::u32>(end, static_cast<core::u32>(text.size())), kind);

        // **A broken run that began with a long bracket is the one thing that
        // continues onto the next line.** The lexer stopped at the end of the
        // buffer without finding a closer; the level it needs to be closed with
        // is in the opener's bytes, which the lexeme does not carry.
        if (kind == TokenKind::Error) {
            if (const std::optional<LongOpen> open = matchLongOpen(text, begin); open.has_value()) {
                return LineState{.kind = open->comment ? LexKind::LongComment : LexKind::LongString,
                                 .level = open->level};
            }
        }

        // The lexer reports `end` past the buffer for a run that hit the end, so
        // this is what stops the loop for a broken tail.
        if (end >= text.size())
            break;
    }
    return LineState{};
}

#endif

} // namespace

LineState lexLine(std::string_view text, core::u32 lineIndex, LineState entry, std::vector<Token>& out)
{
    out.clear();

#if !LUAUG_LUAU_COMPILER
    (void)lineIndex;
    // No `Luau.Ast` to link in this profile. One run in the pane's own
    // foreground is a real answer rather than a stub: the editor is compiled out
    // of shipping anyway, and a build that somehow has a pane still draws
    // readable text.
    (void)entry;
    pushToken(out, 0, static_cast<core::u32>(text.size()), TokenKind::Text);
    return LineState{};
#else
    // Continuing a long bracket from above: everything up to the closer is one
    // run, and only what follows it is Luau again.
    if (entry.kind != LexKind::Normal) {
        const TokenKind kind = entry.kind == LexKind::LongComment ? TokenKind::Comment : TokenKind::String;
        const std::size_t close = findLongClose(text, entry.level, 0);
        if (close == std::string_view::npos) {
            pushToken(out, 0, static_cast<core::u32>(text.size()), kind);
            return entry;
        }
        pushToken(out, 0, static_cast<core::u32>(close), kind);
        return lexSegment(text, lineIndex, static_cast<core::u32>(close), out);
    }

    return lexSegment(text, lineIndex, 0, out);
#endif
}

void parseDiagnostics(const std::string& text, std::vector<Diagnostic>& out)
{
    out.clear();

#if !LUAUG_LUAU_COMPILER
    (void)text;
#else
    // A fresh allocator per parse: `Luau::Allocator` is a bump allocator with no
    // reset (`Ast/include/Luau/Allocator.h`), so the only way to give the AST
    // back is to let one die. That is affordable here because this runs when the
    // text has been still, never per keystroke.
    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);

    Luau::ParseOptions options;
    options.captureComments = false;

    const Luau::ParseResult result = Luau::Parser::parse(text.data(), text.size(), names, allocator, options);
    out.reserve(result.errors.size());
    for (const Luau::ParseError& error : result.errors) {
        out.push_back(Diagnostic{
            .at = Position{static_cast<core::u32>(error.getLocation().begin.line),
                           static_cast<core::u32>(error.getLocation().begin.column)},
            .message = error.getMessage(),
        });
    }
#endif
}

} // namespace luaug::app
