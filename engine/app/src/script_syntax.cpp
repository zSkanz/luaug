// The highlighter and the diagnostics: the only part of the script editor that
// knows what Luau IS (ADR 0057).
//
// Separate from `script_document.cpp` for two reasons. It is the one place that
// includes a Luau header, so `#if LUAUG_LUAU_COMPILER` lives here and nowhere
// else -- `script_document.cpp` and its tests never mention the option. And the
// document is a container while this is the thing with an opinion about the
// bytes, which is the same seam `inspector.h` draws between what a panel decides
// and what it draws.
#include "luaug/app/script_complete.h"
#include "luaug/app/script_document.h"
#include "luaug/script/stdlib.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if LUAUG_LUAU_COMPILER
#include <Luau/Allocator.h>
#include <Luau/Ast.h>
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

#if LUAUG_LUAU_COMPILER

[[nodiscard]] std::string_view textOf(std::string_view text, const Token& token) noexcept
{
    return text.substr(token.column, token.length);
}

// **The names in a type, marked from where they stand** (the owner's report:
// "the editor does not colour types"). Luau's lexer has no type lexeme --
// `number` is a `Name` like any other -- and the grammar
// (https://luau.org/grammar/) says where a type begins: after `:` in a binding,
// after `::`, after `->`, on the right of `type X =`, and in a generic list.
// This walks one line's tokens and turns the identifiers in those places into
// `Type`, and the contextual keywords (`type`, `export`, `continue`) into
// keywords where they begin a statement.
//
// One line at a time, like the lexer it follows: a type that runs onto the next
// line colours its first line. A method call `obj:Method(` is not a binding --
// the colon is followed by a name and then a call -- and is left alone.
void markTypes(std::string_view text, std::vector<Token>& tokens)
{
    const auto isOp = [&](std::size_t at, std::string_view op) {
        return at < tokens.size() && tokens[at].kind == TokenKind::Operator && textOf(text, tokens[at]) == op;
    };
    const auto isName = [&](std::size_t at, std::string_view name = {}) {
        return at < tokens.size() && tokens[at].kind == TokenKind::Identifier &&
               (name.empty() || textOf(text, tokens[at]) == name);
    };

    // `export type Name`, `type Name`, `continue` -- contextual keywords the
    // lexer reads as names, so they are only keywords where a statement starts.
    std::size_t start = 0;
    if (isName(0, "export") && isName(1, "type")) {
        tokens[0].kind = TokenKind::Keyword;
        start = 1;
    }
    bool typeStatement = false;
    if (isName(start, "type") && isName(start + 1)) {
        tokens[start].kind = TokenKind::Keyword;
        tokens[start + 1].kind = TokenKind::Type;
        typeStatement = true;
    }
    // `continue` is a statement wherever it stands alone between statements:
    // not after `.`, `:` or `local`, and not followed by what makes a name an
    // expression -- `(`, `=`, `.`, `:`, `[` or a string.
    for (std::size_t at = 0; at < tokens.size(); ++at) {
        if (!isName(at, "continue"))
            continue;
        const bool after =
            at > 0 && (isOp(at - 1, ".") || isOp(at - 1, ":") ||
                       (tokens[at - 1].kind == TokenKind::Keyword && textOf(text, tokens[at - 1]) == "local"));
        const bool before = isOp(at + 1, "(") || isOp(at + 1, "=") || isOp(at + 1, ".") || isOp(at + 1, ":") ||
                            isOp(at + 1, "[") || (at + 1 < tokens.size() && tokens[at + 1].kind == TokenKind::String);
        if (!after && !before)
            tokens[at].kind = TokenKind::Keyword;
    }

    // A generic list `<T, U...>`: every name up to the matching `>` is a type.
    const auto markGenerics = [&](std::size_t open) {
        int depth = 0;
        for (std::size_t at = open; at < tokens.size(); ++at) {
            if (isOp(at, "<"))
                ++depth;
            else if (isOp(at, ">") && --depth == 0)
                return at;
            else if (tokens[at].kind == TokenKind::Identifier)
                tokens[at].kind = TokenKind::Type;
        }
        return tokens.size();
    };
    // `function name<T>(`, `local function name<T>(`, `function M.name<T>(`.
    const auto namesFunction = [&](std::size_t before) {
        std::size_t at = before;
        while (at > 0 && (tokens[at].kind == TokenKind::Identifier || isOp(at, ".") || isOp(at, ":")))
            --at;
        return tokens[at].kind == TokenKind::Keyword && textOf(text, tokens[at]) == "function";
    };

    bool inType = false;
    int depth = 0;
    for (std::size_t at = 0; at < tokens.size(); ++at) {
        Token& token = tokens[at];
        const std::string_view here = textOf(text, token);

        if (!inType) {
            if (token.kind != TokenKind::Operator)
                continue;
            if (here == "::" || here == "->") {
                inType = true;
                depth = 0;
            }
            else if (here == ":" && at > 0) {
                // `obj:Method(`, `obj:Method "x"` and `obj:Method {}` are calls.
                const bool call =
                    isName(at + 1) && (isOp(at + 2, "(") || isOp(at + 2, "{") ||
                                       (at + 2 < tokens.size() && tokens[at + 2].kind == TokenKind::String));
                if (!call) {
                    inType = true;
                    depth = 0;
                }
            }
            else if (here == "=" && typeStatement) {
                inType = true;
                depth = 0;
            }
            else if (here == "<" && at > 0 &&
                     ((typeStatement && at == start + 2) ||
                      (tokens[at - 1].kind == TokenKind::Identifier && namesFunction(at - 1)))) {
                at = markGenerics(at);
            }
            continue;
        }

        switch (token.kind) {
        case TokenKind::Operator:
            if (here == "(" || here == "{" || here == "[" || here == "<") {
                ++depth;
            }
            else if (here == ")" || here == "}" || here == "]" || here == ">") {
                // A closer the type did not open ends it: the `)` of a
                // parameter list, the `}` of the table the annotation sat in.
                if (depth == 0)
                    inType = false;
                else
                    --depth;
            }
            else if (depth == 0 && (here == "=" || here == ",")) {
                inType = false;
            }
            break;
        case TokenKind::Keyword:
            // `nil` is a type; `function` opens a function type; any other
            // keyword -- `then`, `do`, `in`, `end` -- is code again.
            if (here != "nil" && here != "function" && here != "true" && here != "false")
                inType = false;
            break;
        case TokenKind::Identifier:
            // A field name in a table type (`{ name: string }`) is not a type,
            // and `typeof(x)` is a call inside one.
            if (!(depth > 0 && isOp(at + 1, ":")) && here != "typeof")
                token.kind = TokenKind::Type;
            break;
        default:
            break;
        }
    }
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
        const LineState next = lexSegment(text, lineIndex, static_cast<core::u32>(close), out);
        markTypes(text, out);
        return next;
    }

    const LineState next = lexSegment(text, lineIndex, 0, out);
    markTypes(text, out);
    return next;
#endif
}

namespace {

[[nodiscard]] bool isBuiltinName(std::string_view name)
{
    // Built once: Luau's globals and libraries, and what the engine installs --
    // the same names the completion offers and the lint accepts.
    static const std::unordered_set<std::string_view> names = [] {
        std::unordered_set<std::string_view> all;
        for (const script::StdName& global : script::stdGlobals())
            all.insert(global.name);
        for (const script::StdLibrary& library : script::stdLibraries())
            all.insert(library.name);
        for (const std::string_view global : engineGlobals())
            all.insert(global);
        return all;
    }();
    return names.contains(name);
}

// A comment, with each `TODO` in it a run of its own.
void styleComment(std::string_view text, const Token& token, std::vector<StyledRun>& out)
{
    constexpr std::string_view Marker = "TODO";
    const std::string_view body = text.substr(token.column, token.length);
    core::u32 from = 0;
    for (std::size_t at = body.find(Marker); at != std::string_view::npos; at = body.find(Marker, at + 1)) {
        const auto column = static_cast<core::u32>(at);
        if (column > from)
            out.push_back(StyledRun{token.column + from, column - from, ScriptColor::Comment});
        out.push_back(StyledRun{token.column + column, static_cast<core::u32>(Marker.size()), ScriptColor::Todo});
        from = column + static_cast<core::u32>(Marker.size());
    }
    if (from < token.length)
        out.push_back(StyledRun{token.column + from, token.length - from, ScriptColor::Comment});
}

} // namespace

void styleLine(std::string_view text, std::span<const Token> tokens, std::vector<StyledRun>& out)
{
    out.clear();
    const auto word = [&text](const Token& token) { return text.substr(token.column, token.length); };
    const auto isOp = [&](std::size_t at, std::string_view op) {
        return at < tokens.size() && tokens[at].kind == TokenKind::Operator && word(tokens[at]) == op;
    };
    // `function a.b:c` and `local function c`: the names after `function`.
    const auto declaresFunction = [&](std::size_t at) {
        while (at > 0) {
            --at;
            const Token& before = tokens[at];
            if (before.kind == TokenKind::Keyword)
                return word(before) == "function";
            if (before.kind != TokenKind::Identifier && !isOp(at, ".") && !isOp(at, ":"))
                return false;
        }
        return false;
    };

    for (std::size_t at = 0; at < tokens.size(); ++at) {
        const Token& token = tokens[at];
        const std::string_view here = word(token);
        ScriptColor color = ScriptColor::Text;
        switch (token.kind) {
        case TokenKind::Comment:
            styleComment(text, token, out);
            continue;
        case TokenKind::Keyword:
            if (here == "function")
                color = ScriptColor::FunctionKeyword;
            else if (here == "local")
                color = ScriptColor::LocalKeyword;
            else if (here == "nil")
                color = ScriptColor::Nil;
            else if (here == "true" || here == "false")
                color = ScriptColor::Bool;
            else if (here == "export" || here == "type" || here == "continue")
                color = ScriptColor::LuauKeyword;
            else
                color = ScriptColor::Keyword;
            break;
        case TokenKind::Operator:
            color = here == "(" || here == ")" || here == "[" || here == "]" || here == "{" || here == "}"
                        ? ScriptColor::Bracket
                        : ScriptColor::Operator;
            break;
        case TokenKind::Identifier: {
            const bool afterColon = at > 0 && isOp(at - 1, ":");
            const bool afterDot = at > 0 && isOp(at - 1, ".");
            const bool called = isOp(at + 1, "(") || isOp(at + 1, "{") ||
                                (at + 1 < tokens.size() && tokens[at + 1].kind == TokenKind::String);
            if (here == "self")
                color = ScriptColor::Self;
            else if (declaresFunction(at))
                color = ScriptColor::FunctionName;
            else if (afterColon || (afterDot && called))
                color = ScriptColor::Method;
            else if (afterDot)
                color = ScriptColor::Property;
            else if (isBuiltinName(here))
                color = ScriptColor::BuiltinFunction;
            break;
        }
        case TokenKind::Number:
            color = ScriptColor::Number;
            break;
        case TokenKind::String:
            color = ScriptColor::String;
            break;
        case TokenKind::Attribute:
            color = ScriptColor::Attribute;
            break;
        case TokenKind::Error:
            color = ScriptColor::BrokenToken;
            break;
        case TokenKind::Type:
            color = ScriptColor::Type;
            break;
        case TokenKind::Text:
            break;
        }
        out.push_back(StyledRun{token.column, token.length, color});
    }
}

#if LUAUG_LUAU_COMPILER

// **The two lints a person actually wants, and no more.**
//
// `Luau.Analysis` carries a whole linter and is deliberately not built (ADR
// 0057, ADR 0018) -- it was 35% of a cold build. These two need none of it: the
// PARSER already resolves scope, which is the hard half. A name it could not
// bind to a local comes out as `AstExprGlobal` and a name it could comes out as
// `AstExprLocal`, so "used but never declared" and "declared but never used"
// are a walk over an AST that has already been built for the syntax errors.
//
// **An unknown global is not a style note here, it is a fact.** `sealGlobals`
// freezes the globals table (R4), so nothing can add one at runtime -- a name
// that is not in the sandbox's surface will be nil, or will raise on write.
// That is what makes this lint worth drawing: it has no false positives to
// apologise for.
//
// What is deliberately NOT reported, because the point is to stay out of the
// way: a function's parameters, and a loop variable. Both are unused constantly
// and on purpose, which is why `_` exists -- and a warning somebody learns to
// ignore has made every other warning worth less.
// The `return`s of one function body that carry values, not those of the
// functions nested inside it -- a helper defined in an executor returns to
// its own caller.
class ValueReturns : public Luau::AstVisitor
{
public:
    std::vector<const Luau::AstStatReturn*> found;

    bool visit(Luau::AstExprFunction*) override { return false; }
    bool visit(Luau::AstStatReturn* node) override
    {
        if (node->list.size > 0)
            found.push_back(node);
        return true;
    }
};

class Lints : public Luau::AstVisitor
{
public:
    explicit Lints(std::vector<Diagnostic>& out) : m_out(out) {}

    // **A value returned from a `Promise.new` executor goes nowhere** (the
    // owner's module loader: every promise stayed pending, so `Promise.all`
    // never ran what came after). The promise settles when `resolve` is
    // called; `Promise.try` is the form whose return value resolves it. The
    // reference library behaves the same, which is what makes this easy to
    // write and hard to see.
    bool visit(Luau::AstExprCall* node) override
    {
        const auto* index = node->func->as<Luau::AstExprIndexName>();
        const auto* owner = index != nullptr ? index->expr->as<Luau::AstExprGlobal>() : nullptr;
        if (owner == nullptr || std::string_view(owner->name.value) != "Promise" || node->args.size < 1)
            return true;
        const std::string_view method = index->index.value;
        if (method != "new" && method != "defer")
            return true;
        const auto* executor = node->args.data[0]->as<Luau::AstExprFunction>();
        if (executor == nullptr || executor->body == nullptr)
            return true;
        ValueReturns returns;
        for (Luau::AstStat* statement : executor->body->body)
            statement->visit(&returns);
        for (const Luau::AstStatReturn* returned : returns.found) {
            const Luau::Location where = returned->location;
            m_out.push_back(Diagnostic{
                .at = Position{static_cast<core::u32>(where.begin.line), static_cast<core::u32>(where.begin.column)},
                .length = where.begin.line == where.end.line && where.end.column > where.begin.column
                              ? static_cast<core::u32>(where.end.column - where.begin.column)
                              : 0,
                .message = std::string("Promise.") + std::string(method) +
                           " ignores what its executor returns: call resolve(...), or use Promise.try",
                .severity = Severity::Warning,
            });
        }
        return true;
    }

    bool visit(Luau::AstExprGlobal* node) override
    {
        if (!known(node->name.value))
            report(node->location, std::string("unknown global `") + node->name.value +
                                       "` -- nothing declares it, and the globals table is frozen");
        return true;
    }

    bool visit(Luau::AstExprLocal* node) override
    {
        m_used.insert(node->local);
        return true;
    }

    bool visit(Luau::AstStatLocal* node) override
    {
        for (Luau::AstLocal* local : node->vars)
            m_declared.push_back(local);
        return true;
    }

    bool visit(Luau::AstStatLocalFunction* node) override
    {
        m_declared.push_back(node->name);
        return true;
    }

    // Called once the whole tree has been walked, because a local declared on
    // line one may be read on line four hundred.
    void finish()
    {
        for (const Luau::AstLocal* local : m_declared) {
            if (m_used.contains(local))
                continue;
            const std::string_view name = local->name.value;
            // The universal "I know, and I meant it" marker. Warning through it
            // would leave somebody no way to say so.
            if (!name.empty() && name.front() == '_')
                continue;
            report(local->location, std::string("`") + std::string(name) + "` is never used");
        }
    }

private:
    [[nodiscard]] static bool known(std::string_view name)
    {
        for (const script::StdName& global : script::stdGlobals()) {
            if (global.name == name)
                return true;
        }
        for (const script::StdLibrary& library : script::stdLibraries()) {
            if (library.name == name)
                return true;
        }
        // The same list the completion offers, so this never underlines a name
        // the editor itself just suggested.
        for (const std::string_view global : engineGlobals()) {
            if (global == name)
                return true;
        }
        return false;
    }

    void report(const Luau::Location& where, std::string message)
    {
        const auto begin = static_cast<core::u32>(where.begin.column);
        const auto end = static_cast<core::u32>(where.end.column);
        m_out.push_back(Diagnostic{
            .at = Position{static_cast<core::u32>(where.begin.line), begin},
            // The name and nothing else. A warning that underlines to the end
            // of the line points at the line rather than at the word, and the
            // word is the whole message.
            .length = where.begin.line == where.end.line && end > begin ? end - begin : 0,
            .message = std::move(message),
            .severity = Severity::Warning,
        });
    }

    std::vector<Diagnostic>& m_out;
    std::vector<Luau::AstLocal*> m_declared;
    std::unordered_set<const Luau::AstLocal*> m_used;
};

#endif

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
            .length = 0,
            .message = error.getMessage(),
            .severity = Severity::Error,
            .syntax = true,
        });
    }

    // **Only over a file that parsed.** A half-typed one has a partial AST, and
    // linting it would put a warning under every name somebody is in the middle
    // of writing -- which is the fastest way to make a person turn warnings off.
    if (result.errors.empty() && result.root != nullptr) {
        Lints lints(out);
        result.root->visit(&lints);
        lints.finish();
    }
#endif
}

#if LUAUG_LUAU_COMPILER
namespace {

// What a value looks like, for the right-hand column. Deliberately shallow --
// this is the shape of what was written, not the type of what it evaluates to.
[[nodiscard]] std::string shapeOf(const Luau::AstExpr* value)
{
    if (value == nullptr)
        return "field";
    if (value->is<Luau::AstExprFunction>())
        return "function";
    if (value->is<Luau::AstExprTable>())
        return "table";
    if (const auto* constant = value->as<Luau::AstExprConstantString>(); constant != nullptr) {
        (void)constant;
        return "string";
    }
    if (value->is<Luau::AstExprConstantNumber>())
        return "number";
    if (value->is<Luau::AstExprConstantBool>())
        return "boolean";
    return "field";
}

void addMember(std::vector<ModuleMember>& out, std::string name, std::string detail)
{
    if (name.empty())
        return;
    const auto already =
        std::find_if(out.begin(), out.end(), [&name](const ModuleMember& member) { return member.name == name; });
    if (already != out.end())
        return;
    out.push_back(ModuleMember{std::move(name), std::move(detail)});
}

// `return { a = 1, b = function() end }` -- the keys written out.
void fromTable(const Luau::AstExprTable* table, std::vector<ModuleMember>& out)
{
    for (const Luau::AstExprTable::Item& item : table->items) {
        const auto* key = item.key != nullptr ? item.key->as<Luau::AstExprConstantString>() : nullptr;
        if (key == nullptr)
            continue;
        addMember(out, std::string(key->value.data, key->value.size), shapeOf(item.value));
    }
}

// Whether `expr` is a read of exactly `target`, which is how `function M.foo()`
// and `M.bar = 1` are told apart from the same thing done to something else.
[[nodiscard]] bool isRead(const Luau::AstExpr* expr, const Luau::AstLocal* target)
{
    const auto* local = expr != nullptr ? expr->as<Luau::AstExprLocal>() : nullptr;
    return local != nullptr && local->local == target;
}

// The other shape, and the commoner one:
//
//   local M = {}
//   function M.foo() end
//   function M:bar() end
//   M.value = 1
//   return M
void fromLocal(const Luau::AstStatBlock* body, const Luau::AstLocal* target, std::vector<ModuleMember>& out)
{
    for (Luau::AstStat* statement : body->body) {
        if (const auto* declared = statement->as<Luau::AstStatLocal>(); declared != nullptr) {
            for (std::size_t index = 0; index < declared->vars.size; ++index) {
                if (declared->vars.data[index] != target || index >= declared->values.size)
                    continue;
                if (const auto* table = declared->values.data[index]->as<Luau::AstExprTable>(); table != nullptr)
                    fromTable(table, out);
            }
            continue;
        }
        if (const auto* function = statement->as<Luau::AstStatFunction>(); function != nullptr) {
            const auto* named = function->name->as<Luau::AstExprIndexName>();
            if (named != nullptr && isRead(named->expr, target))
                addMember(out, std::string(named->index.value), "function");
            continue;
        }
        if (const auto* assign = statement->as<Luau::AstStatAssign>(); assign != nullptr) {
            for (std::size_t index = 0; index < assign->vars.size; ++index) {
                const auto* named = assign->vars.data[index]->as<Luau::AstExprIndexName>();
                if (named == nullptr || !isRead(named->expr, target))
                    continue;
                const Luau::AstExpr* value = index < assign->values.size ? assign->values.data[index] : nullptr;
                addMember(out, std::string(named->index.value), shapeOf(value));
            }
        }
    }
}

} // namespace
#endif

#if LUAUG_LUAU_COMPILER

namespace {

// **One file, read for what its own code says values are** (`sourceMembersOf`).
// Filled by a walk of the AST, then asked about a path.
class SourceModel : public Luau::AstVisitor
{
public:
    // What the model knows a value to be.
    struct Shape
    {
        enum class Kind : core::u8
        {
            None,
            // The table a name holds, filled in by the file: `local X = {}`
            // then `function X.f`.
            Table,
            // Something `setmetatable({...}, X)` made -- an instance of X.
            Instance,
            // A type the file wrote: an alias, or a table type in place.
            Type,
            // An engine class, answered from reflection by the caller.
            Class,
        };
        Kind kind = Kind::None;
        std::string name;
        const Luau::AstType* type = nullptr;
    };

    explicit SourceModel(std::string_view source) : m_source(source)
    {
        m_lineStarts.push_back(0);
        for (std::size_t at = 0; at < source.size(); ++at) {
            if (source[at] == '\n')
                m_lineStarts.push_back(at + 1);
        }
    }

    bool visit(Luau::AstStatLocal* node) override
    {
        for (std::size_t index = 0; index < node->vars.size; ++index) {
            Luau::AstLocal* local = node->vars.data[index];
            m_locals.push_back(local);
            if (index < node->values.size) {
                m_init[local] = node->values.data[index];
                if (const auto* table = node->values.data[index]->as<Luau::AstExprTable>(); table != nullptr)
                    addFields(m_tables[local->name.value], table);
            }
        }
        return true;
    }

    bool visit(Luau::AstStatLocalFunction* node) override
    {
        m_locals.push_back(node->name);
        return true;
    }

    bool visit(Luau::AstExprFunction* node) override
    {
        for (Luau::AstLocal* argument : node->args)
            m_locals.push_back(argument);
        if (node->self != nullptr)
            m_locals.push_back(node->self);
        return true;
    }

    bool visit(Luau::AstStatAssign* node) override
    {
        for (std::size_t index = 0; index < node->vars.size; ++index) {
            const Luau::AstExpr* value = index < node->values.size ? node->values.data[index] : nullptr;
            if (const auto* field = node->vars.data[index]->as<Luau::AstExprIndexName>(); field != nullptr) {
                const std::string base(nameOf(field->expr));
                if (base.empty())
                    continue;
                add(m_tables[base], field->index.value, value);
                m_fieldValues[base + "." + field->index.value] = value;
            }
            else if (const auto* global = node->vars.data[index]->as<Luau::AstExprGlobal>(); global != nullptr) {
                if (const auto* table = value != nullptr ? value->as<Luau::AstExprTable>() : nullptr)
                    addFields(m_tables[global->name.value], table);
            }
        }
        return true;
    }

    bool visit(Luau::AstStatFunction* node) override
    {
        if (const auto* field = node->name->as<Luau::AstExprIndexName>(); field != nullptr) {
            const std::string base(nameOf(field->expr));
            if (!base.empty()) {
                addMember(m_tables[base],
                          SourceMember{field->index.value, field->op == ':' ? "method" : "function", true});
                m_functions[base + "." + field->index.value] = node->func;
                if (node->func->self != nullptr)
                    m_selfOf[node->func->self] = base;
            }
        }
        return true;
    }

    bool visit(Luau::AstExprCall* node) override
    {
        if (isGlobal(node->func, "setmetatable") && node->args.size >= 2) {
            const auto* table = node->args.data[0]->as<Luau::AstExprTable>();
            const std::string owner(nameOf(node->args.data[1]));
            if (table != nullptr && !owner.empty())
                addFields(m_instanceFields[owner], table);
        }
        return true;
    }

    bool visit(Luau::AstStatTypeAlias* node) override
    {
        m_aliases[node->name.value] = node->type;
        return true;
    }

    // The local called `name` a reader at `caret` sees: the latest declared
    // before it. Scopes are not tracked, which is the approximation this
    // makes -- a shadowed name in a closed block can answer for the outer one.
    [[nodiscard]] Luau::AstLocal* localAt(std::string_view name, Luau::Position caret) const
    {
        Luau::AstLocal* found = nullptr;
        for (Luau::AstLocal* local : m_locals) {
            if (local->name.value == nullptr || name != local->name.value)
                continue;
            const Luau::Position at = local->location.begin;
            if (at.line > caret.line || (at.line == caret.line && at.column >= caret.column))
                continue;
            if (found == nullptr || found->location.begin.line < at.line ||
                (found->location.begin.line == at.line && found->location.begin.column < at.column)) {
                found = local;
            }
        }
        return found;
    }

    [[nodiscard]] bool hasTable(const std::string& name) const { return m_tables.contains(name); }

    [[nodiscard]] Shape shapeOfLocal(const Luau::AstLocal* local, int depth) const
    {
        if (local == nullptr || depth > kDepth)
            return {};
        if (const auto owner = m_selfOf.find(local); owner != m_selfOf.end())
            return Shape{Shape::Kind::Instance, owner->second, nullptr};
        if (local->annotation != nullptr)
            return fromType(local->annotation, depth + 1);
        if (const auto init = m_init.find(local); init != m_init.end()) {
            const Shape shape = shapeOfExpr(init->second, depth + 1);
            if (shape.kind != Shape::Kind::None)
                return shape;
        }
        if (local->name.value != nullptr && hasTable(local->name.value))
            return Shape{Shape::Kind::Table, local->name.value, nullptr};
        return {};
    }

    [[nodiscard]] Shape fieldOf(const Shape& shape, std::string_view field, int depth) const
    {
        if (depth > kDepth)
            return {};
        if (const Luau::AstTypeTable* table = typeTableOf(shape, depth + 1); table != nullptr) {
            for (const Luau::AstTableProp& prop : table->props) {
                if (prop.name.value != nullptr && field == prop.name.value)
                    return fromType(prop.type, depth + 1);
            }
        }
        if (shape.kind == Shape::Kind::Table || shape.kind == Shape::Kind::Instance) {
            if (const auto value = m_fieldValues.find(shape.name + "." + std::string(field));
                value != m_fieldValues.end())
                return shapeOfExpr(value->second, depth + 1);
        }
        return {};
    }

    // An element of a list type: `{ BasePart }` indexed is a `BasePart`.
    [[nodiscard]] Shape elementOf(const Shape& shape, int depth) const
    {
        const Luau::AstTypeTable* table = typeTableOf(shape, depth + 1);
        if (table != nullptr && table->indexer != nullptr)
            return fromType(table->indexer->resultType, depth + 1);
        return {};
    }

    [[nodiscard]] SourceMembers membersOf(const Shape& shape) const
    {
        SourceMembers out;
        switch (shape.kind) {
        case Shape::Kind::None:
            return out;
        case Shape::Kind::Class:
            out.known = true;
            out.className = shape.name;
            return out;
        case Shape::Kind::Table:
        case Shape::Kind::Instance:
        case Shape::Kind::Type:
            break;
        }
        out.known = true;
        if (const Luau::AstTypeTable* table = typeTableOf(shape, 0); table != nullptr) {
            for (const Luau::AstTableProp& prop : table->props) {
                if (prop.name.value != nullptr)
                    addMember(out.members, SourceMember{prop.name.value, textOf(prop.type->location),
                                                        prop.type->is<Luau::AstTypeFunction>()});
            }
        }
        // **A type and a table of one name are one thing**: `type Snake` says
        // what an instance holds and `local Snake = {}` holds its methods.
        if (shape.kind == Shape::Kind::Instance || shape.kind == Shape::Kind::Type) {
            if (const auto fields = m_instanceFields.find(shape.name); fields != m_instanceFields.end()) {
                for (const SourceMember& member : fields->second)
                    addMember(out.members, member);
            }
        }
        if (const auto members = m_tables.find(shape.name); members != m_tables.end()) {
            for (const SourceMember& member : members->second)
                addMember(out.members, member);
        }
        return out;
    }

private:
    static constexpr int kDepth = 12;

    [[nodiscard]] static std::string_view nameOf(const Luau::AstExpr* expr) noexcept
    {
        if (expr == nullptr)
            return {};
        if (const auto* local = expr->as<Luau::AstExprLocal>(); local != nullptr)
            return local->local->name.value != nullptr ? local->local->name.value : std::string_view{};
        if (const auto* global = expr->as<Luau::AstExprGlobal>(); global != nullptr)
            return global->name.value != nullptr ? global->name.value : std::string_view{};
        return {};
    }

    [[nodiscard]] static bool isGlobal(const Luau::AstExpr* expr, std::string_view name) noexcept
    {
        const auto* global = expr != nullptr ? expr->as<Luau::AstExprGlobal>() : nullptr;
        return global != nullptr && global->name.value != nullptr && name == global->name.value;
    }

    [[nodiscard]] std::string textOf(const Luau::Location& where) const
    {
        if (where.begin.line >= m_lineStarts.size())
            return {};
        const std::size_t from = m_lineStarts[where.begin.line] + where.begin.column;
        const std::size_t lineEnd =
            where.begin.line + 1 < m_lineStarts.size() ? m_lineStarts[where.begin.line + 1] - 1 : m_source.size();
        const std::size_t to = where.end.line == where.begin.line
                                   ? std::min(m_lineStarts[where.begin.line] + where.end.column, m_source.size())
                                   : lineEnd;
        return from < to ? std::string(m_source.substr(from, to - from)) : std::string{};
    }

    static void addMember(std::vector<SourceMember>& members, SourceMember member)
    {
        for (const SourceMember& existing : members) {
            if (existing.name == member.name)
                return;
        }
        members.push_back(std::move(member));
    }

    void add(std::vector<SourceMember>& members, const char* name, const Luau::AstExpr* value) const
    {
        const bool callable = value != nullptr && value->is<Luau::AstExprFunction>();
        const bool table = value != nullptr && value->is<Luau::AstExprTable>();
        addMember(members, SourceMember{name, callable ? "function" : table ? "table" : "field", callable});
    }

    void addFields(std::vector<SourceMember>& members, const Luau::AstExprTable* table) const
    {
        for (const Luau::AstExprTable::Item& item : table->items) {
            if (item.kind != Luau::AstExprTable::Item::Kind::Record || item.key == nullptr)
                continue;
            const auto* key = item.key->as<Luau::AstExprConstantString>();
            if (key == nullptr)
                continue;
            const std::string name(key->value.data, key->value.size);
            add(members, name.c_str(), item.value);
        }
    }

    [[nodiscard]] Shape shapeOfExpr(const Luau::AstExpr* expr, int depth) const
    {
        if (expr == nullptr || depth > kDepth)
            return {};
        if (const auto* group = expr->as<Luau::AstExprGroup>(); group != nullptr)
            return shapeOfExpr(group->expr, depth + 1);
        if (const auto* local = expr->as<Luau::AstExprLocal>(); local != nullptr)
            return shapeOfLocal(local->local, depth + 1);
        if (const auto* global = expr->as<Luau::AstExprGlobal>(); global != nullptr) {
            if (global->name.value != nullptr && hasTable(global->name.value))
                return Shape{Shape::Kind::Table, global->name.value, nullptr};
            return {};
        }
        if (const auto* cast = expr->as<Luau::AstExprTypeAssertion>(); cast != nullptr)
            return fromType(cast->annotation, depth + 1);
        if (const auto* call = expr->as<Luau::AstExprCall>(); call != nullptr) {
            if (isGlobal(call->func, "setmetatable") && call->args.size >= 2) {
                const std::string owner(nameOf(call->args.data[1]));
                if (!owner.empty())
                    return Shape{Shape::Kind::Instance, owner, nullptr};
            }
            if (const auto* callee = call->func->as<Luau::AstExprIndexName>(); callee != nullptr) {
                const std::string_view method = callee->index.value != nullptr ? callee->index.value : "";
                // `Instance.new("Part")` is a Part.
                if (isGlobal(callee->expr, "Instance") && method == "new" && call->args.size >= 1) {
                    if (const auto* named = call->args.data[0]->as<Luau::AstExprConstantString>(); named != nullptr)
                        return Shape{Shape::Kind::Class, std::string(named->value.data, named->value.size), nullptr};
                }
                const std::string base(nameOf(callee->expr));
                if (!base.empty()) {
                    const auto function = m_functions.find(base + "." + std::string(method));
                    if (function != m_functions.end() && function->second->returnAnnotation != nullptr) {
                        if (const auto* pack = function->second->returnAnnotation->as<Luau::AstTypePackExplicit>();
                            pack != nullptr && pack->typeList.types.size > 0) {
                            return fromType(pack->typeList.types.data[0], depth + 1);
                        }
                    }
                    // `X.new(...)` with no annotation: an instance of X is the
                    // honest guess, and the one every constructor means.
                    if (method == "new" && hasTable(base))
                        return Shape{Shape::Kind::Instance, base, nullptr};
                }
            }
            return {};
        }
        if (const auto* field = expr->as<Luau::AstExprIndexName>(); field != nullptr)
            return fieldOf(shapeOfExpr(field->expr, depth + 1), field->index.value != nullptr ? field->index.value : "",
                           depth + 1);
        if (const auto* index = expr->as<Luau::AstExprIndexExpr>(); index != nullptr)
            return elementOf(shapeOfExpr(index->expr, depth + 1), depth + 1);
        return {};
    }

    [[nodiscard]] Shape fromType(const Luau::AstType* type, int depth) const
    {
        if (type == nullptr || depth > kDepth)
            return {};
        if (const auto* group = type->as<Luau::AstTypeGroup>(); group != nullptr)
            return fromType(group->type, depth + 1);
        // `T?` is `T | nil`: the value is a T whenever it is anything.
        if (const auto* either = type->as<Luau::AstTypeUnion>(); either != nullptr) {
            for (const Luau::AstType* option : either->types) {
                if (option->is<Luau::AstTypeOptional>())
                    continue;
                const auto* named = option->as<Luau::AstTypeReference>();
                if (named != nullptr && named->name.value != nullptr && std::string_view(named->name.value) == "nil")
                    continue;
                return fromType(option, depth + 1);
            }
            return {};
        }
        if (const auto* named = type->as<Luau::AstTypeReference>(); named != nullptr) {
            if (named->prefix.has_value() || named->name.value == nullptr)
                return {};
            const std::string name(named->name.value);
            if (const auto alias = m_aliases.find(name); alias != m_aliases.end())
                return Shape{Shape::Kind::Type, name, alias->second};
            return Shape{Shape::Kind::Class, name, nullptr};
        }
        if (type->is<Luau::AstTypeTable>())
            return Shape{Shape::Kind::Type, {}, type};
        return {};
    }

    // The table type a shape is, following aliases -- or, for a table or an
    // instance, the alias that shares its name.
    [[nodiscard]] const Luau::AstTypeTable* typeTableOf(const Shape& shape, int depth) const
    {
        if (depth > kDepth)
            return nullptr;
        const Luau::AstType* type = shape.type;
        if (type == nullptr && (shape.kind == Shape::Kind::Table || shape.kind == Shape::Kind::Instance)) {
            if (const auto alias = m_aliases.find(shape.name); alias != m_aliases.end())
                type = alias->second;
        }
        for (int step = 0; type != nullptr && step < kDepth; ++step) {
            if (const auto* table = type->as<Luau::AstTypeTable>(); table != nullptr)
                return table;
            const Shape next = fromType(type, depth + 1);
            if (next.type == type || next.type == nullptr)
                return nullptr;
            type = next.type;
        }
        return nullptr;
    }

    std::string_view m_source;
    std::vector<std::size_t> m_lineStarts;
    std::vector<Luau::AstLocal*> m_locals;
    std::unordered_map<const Luau::AstLocal*, const Luau::AstExpr*> m_init;
    std::unordered_map<const Luau::AstLocal*, std::string> m_selfOf;
    std::unordered_map<std::string, std::vector<SourceMember>> m_tables;
    std::unordered_map<std::string, std::vector<SourceMember>> m_instanceFields;
    std::unordered_map<std::string, const Luau::AstExpr*> m_fieldValues;
    std::unordered_map<std::string, const Luau::AstExprFunction*> m_functions;
    std::unordered_map<std::string, const Luau::AstType*> m_aliases;
};

} // namespace

#endif

SourceMembers sourceMembersOf(const std::string& source, std::span<const std::string> path, Position caret)
{
#if !LUAUG_LUAU_COMPILER
    (void)source;
    (void)path;
    (void)caret;
    return {};
#else
    if (path.empty())
        return {};
    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    Luau::ParseOptions options;
    options.captureComments = false;
    // **A half-typed file is read too**: the caret is almost always just after
    // a `.` that does not parse yet, and the parser recovers around it.
    const Luau::ParseResult result = Luau::Parser::parse(source.data(), source.size(), names, allocator, options);
    if (result.root == nullptr)
        return {};

    SourceModel model(source);
    result.root->visit(&model);

    using Shape = SourceModel::Shape;
    Shape shape = model.shapeOfLocal(model.localAt(path[0], Luau::Position{caret.line, caret.column}), 0);
    if (shape.kind == Shape::Kind::None && model.hasTable(path[0]))
        shape = Shape{Shape::Kind::Table, path[0], nullptr};
    for (std::size_t step = 1; step < path.size() && shape.kind != Shape::Kind::None; ++step)
        shape = path[step] == kElementStep ? model.elementOf(shape, 0) : model.fieldOf(shape, path[step], 0);
    return model.membersOf(shape);
#endif
}

#if LUAUG_LUAU_COMPILER
namespace {

// Walks every block and function and records, per name, where it can be seen.
class ScopeCollector : public Luau::AstVisitor
{
public:
    ScopeCollector(Position caret, std::vector<std::string>& out, std::vector<std::string>* earlier)
        : m_caret(caret), m_out(out), m_earlier(earlier)
    {}

    bool visit(Luau::AstStatBlock* block) override
    {
        for (Luau::AstStat* statement : block->body) {
            if (const auto* local = statement->as<Luau::AstStatLocal>(); local != nullptr) {
                for (Luau::AstLocal* variable : local->vars)
                    offer(variable->name.value, local->location.end, block->location.end, /*after*/ true);
            }
            else if (const auto* function = statement->as<Luau::AstStatLocalFunction>(); function != nullptr) {
                offer(function->name->name.value, function->location.begin, block->location.end);
            }
        }
        return true;
    }

    bool visit(Luau::AstExprFunction* function) override
    {
        const Luau::Location body = function->body->location;
        if (function->self != nullptr)
            offer(function->self->name.value, body.begin, body.end);
        for (Luau::AstLocal* argument : function->args)
            offer(argument->name.value, body.begin, body.end);
        return true;
    }

    bool visit(Luau::AstStatFor* loop) override
    {
        offer(loop->var->name.value, loop->body->location.begin, loop->body->location.end);
        return true;
    }

    bool visit(Luau::AstStatForIn* loop) override
    {
        for (Luau::AstLocal* variable : loop->vars)
            offer(variable->name.value, loop->body->location.begin, loop->body->location.end);
        return true;
    }

    // A global the file defines -- `function Name()` or `Name = ...` -- is
    // everybody's, wherever it was written.
    bool visit(Luau::AstStatFunction* function) override
    {
        if (const auto* global = function->name->as<Luau::AstExprGlobal>(); global != nullptr)
            add(global->name.value);
        return true;
    }

    bool visit(Luau::AstStatAssign* assign) override
    {
        for (Luau::AstExpr* target : assign->vars) {
            if (const auto* global = target->as<Luau::AstExprGlobal>(); global != nullptr)
                add(global->name.value);
        }
        return true;
    }

private:
    // `after`: the name exists only once the caret is PAST `from` -- a
    // `local`'s own statement, which a half-typed line ends exactly at the
    // caret (`local x = f(|`), declares nothing yet.
    void offer(const char* name, Luau::Position from, Luau::Position to, bool after = false)
    {
        const Position begin{static_cast<core::u32>(from.line), static_cast<core::u32>(from.column)};
        const Position end{static_cast<core::u32>(to.line), static_cast<core::u32>(to.column)};
        if (m_caret < begin || (after && m_caret == begin))
            return; // not declared yet
        if (m_caret <= end)
            add(name);
        else if (m_earlier != nullptr && name != nullptr && *name != '\0' &&
                 std::find(m_earlier->begin(), m_earlier->end(), name) == m_earlier->end())
            m_earlier->emplace_back(name);
    }

    void add(const char* name)
    {
        if (name == nullptr || *name == '\0')
            return;
        if (std::find(m_out.begin(), m_out.end(), name) == m_out.end())
            m_out.emplace_back(name);
    }

    Position m_caret;
    std::vector<std::string>& m_out;
    std::vector<std::string>* m_earlier = nullptr;
};

} // namespace
#endif

void visibleNames(const std::string& source, Position caret, std::vector<std::string>& out,
                  std::vector<std::string>* earlier)
{
    out.clear();
    if (earlier != nullptr)
        earlier->clear();
#if !LUAUG_LUAU_COMPILER
    (void)source;
    (void)caret;
#else
    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    Luau::ParseOptions options;
    options.captureComments = false;
    const Luau::ParseResult result = Luau::Parser::parse(source.data(), source.size(), names, allocator, options);
    if (result.root == nullptr)
        return;
    ScopeCollector collector(caret, out, earlier);
    result.root->visit(&collector);
    // A name both visible and declared earlier elsewhere is visible.
    if (earlier != nullptr)
        std::erase_if(*earlier,
                      [&out](const std::string& name) { return std::find(out.begin(), out.end(), name) != out.end(); });
#endif
}

void moduleMembers(const std::string& source, std::vector<ModuleMember>& out)
{
    out.clear();

#if !LUAUG_LUAU_COMPILER
    (void)source;
#else
    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);

    Luau::ParseOptions options;
    options.captureComments = false;

    const Luau::ParseResult result = Luau::Parser::parse(source.data(), source.size(), names, allocator, options);
    if (!result.errors.empty() || result.root == nullptr)
        return;

    // **The LAST top-level return**, because that is the one that runs. A module
    // with an early `return` behind an `if` has two, and the one at the end is
    // what a reader means by "what this module gives you".
    const Luau::AstStatReturn* returned = nullptr;
    for (Luau::AstStat* statement : result.root->body) {
        if (const auto* found = statement->as<Luau::AstStatReturn>(); found != nullptr)
            returned = found;
    }
    if (returned == nullptr || returned->list.size == 0)
        return;

    const Luau::AstExpr* value = returned->list.data[0];
    if (const auto* table = value->as<Luau::AstExprTable>(); table != nullptr) {
        fromTable(table, out);
        return;
    }
    if (const auto* local = value->as<Luau::AstExprLocal>(); local != nullptr)
        fromLocal(result.root, local->local, out);
#endif
}

} // namespace luaug::app
