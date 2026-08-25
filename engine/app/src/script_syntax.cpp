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
class Lints : public Luau::AstVisitor
{
public:
    explicit Lints(std::vector<Diagnostic>& out) : m_out(out) {}

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
