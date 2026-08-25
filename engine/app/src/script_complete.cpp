#include "luaug/app/script_complete.h"

#include "luaug/core/name_atom.h"
#include "luaug/scene/class_registry.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace luaug::app {
namespace {

using core::u32;

[[nodiscard]] bool isWordByte(char c) noexcept
{
    const auto value = static_cast<unsigned char>(c);
    return value == '_' || std::isalnum(value) != 0;
}

[[nodiscard]] char lower(char c) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// Case-insensitive prefix, which is what somebody typing `getse` expects to
// find `GetService`. Case-SENSITIVE would be defensible and is wrong here for
// one reason: this API is PascalCase off an object and camelCase off a module
// (ADR 0034), so requiring the right case is requiring people to remember the
// rule before the completion can remind them of it.
[[nodiscard]] bool startsWith(std::string_view text, std::string_view prefix) noexcept
{
    if (prefix.size() > text.size())
        return false;
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (lower(text[index]) != lower(prefix[index]))
            return false;
    }
    return true;
}

// The reserved words, which the lexer knows and does not enumerate.
constexpr std::array<std::string_view, 21> kKeywords{
    "and",   "break", "do",  "else", "elseif", "end",    "false", "for",  "function", "if",    "in",
    "local", "nil",   "not", "or",   "repeat", "return", "then",  "true", "until",    "while",
};

// The names a script starts with. Not read from anywhere because they are not
// declared anywhere -- the sandbox installs them, and a list nobody generated is
// a list that has to be written down once.
constexpr std::array<std::string_view, 10> kGlobals{
    "game", "workspace", "script", "Instance", "Vector3", "CFrame", "Color3", "Enum", "task", "require",
};

void push(std::vector<Completion>& out, const CompletionRequest& request, std::string label, std::string detail,
          std::string doc, CompletionKind kind)
{
    if (!startsWith(label, request.prefix))
        return;
    out.push_back(Completion{std::move(label), std::move(detail), std::move(doc), kind});
}

// Every member a class has, its ancestors' included. The registry resolves a
// single member through the hierarchy and memoises it, but does not enumerate --
// so the walk is here, root-last, and a name declared twice keeps the derived
// one because that is the one the VM would reach.
void collectMembers(const scene::ClassRegistry& classes, const core::AtomTable& atoms, scene::ClassId id,
                    const CompletionRequest& request, std::vector<Completion>& out)
{
    std::vector<std::string> seen;
    const auto fresh = [&seen](std::string_view name) {
        if (std::find(seen.begin(), seen.end(), name) != seen.end())
            return false;
        seen.emplace_back(name);
        return true;
    };

    for (scene::ClassId walk = id; walk != scene::InvalidClass;) {
        const scene::ClassDescriptor* descriptor = classes.find(walk);
        if (descriptor == nullptr)
            break;

        // A colon is a call, so only methods are offered after one. A dot
        // offers everything, because `instance.Method` is legal Luau even when
        // it is not what somebody meant.
        if (!request.method) {
            for (const scene::PropertyDesc& property : descriptor->properties) {
                const std::string_view name = atoms.text(property.name);
                if (fresh(name))
                    push(out, request, std::string(name), scene::valueTypeName(property.type),
                         property.doc != nullptr ? property.doc : "", CompletionKind::Property);
            }
            for (const scene::EventDesc& event : descriptor->events) {
                const std::string_view name = atoms.text(event.name);
                if (fresh(name))
                    push(out, request, std::string(name), "event", event.doc != nullptr ? event.doc : "",
                         CompletionKind::Event);
            }
        }
        for (const scene::MethodDesc& method : descriptor->methods) {
            const std::string_view name = atoms.text(method.name);
            if (fresh(name))
                push(out, request, std::string(name), method.yields ? "function (yields)" : "function",
                     method.doc != nullptr ? method.doc : "", CompletionKind::Method);
        }

        walk = descriptor->super;
    }
}

// What a token before a `.` or `:` names, when it names a class at all.
//
// Deliberately shallow: a name that IS a class, a service, or one of the three
// globals that have one. Anything else answers nothing, and the caller falls
// back to the file's own identifiers -- which is the honest behaviour for an
// engine with no type inference in it.
[[nodiscard]] scene::ClassId classOfSubject(const scene::ClassRegistry& classes, const core::AtomTable& atoms,
                                            std::string_view subject)
{
    if (subject == "game")
        return classes.findId(atoms.lookup("DataModel"));
    if (subject == "workspace")
        return classes.findId(atoms.lookup("Workspace"));
    // `script` is whatever kind of script the file is, and a `Script` is the
    // one somebody is nearly always looking at.
    if (subject == "script")
        return classes.findId(atoms.lookup("Script"));

    // A service or a class written by its own name, which is how somebody
    // reaches one after `game:GetService("Lighting")` has been assigned to a
    // local called `Lighting`.
    return classes.findId(atoms.lookup(subject));
}

} // namespace

CompletionRequest completionAt(const ScriptDocument& document, Position caret)
{
    CompletionRequest request;
    const Position here = document.clamp(caret);
    const std::string_view line = document.line(here.line);

    u32 start = here.column;
    while (start > 0 && isWordByte(line[start - 1]))
        --start;
    request.prefix = std::string(line.substr(start, here.column - start));
    request.replace = Range{Position{here.line, start}, here};

    // What the word hangs off, if anything. Only a `.` or a `:` immediately
    // before it counts: a space between them is somebody who has moved on.
    if (start == 0)
        return request;
    const char joiner = line[start - 1];
    if (joiner != '.' && joiner != ':')
        return request;
    request.method = joiner == ':';

    u32 subjectEnd = start - 1;
    u32 subjectStart = subjectEnd;
    while (subjectStart > 0 && isWordByte(line[subjectStart - 1]))
        --subjectStart;
    request.subject = std::string(line.substr(subjectStart, subjectEnd - subjectStart));
    return request;
}

void collectCompletions(const ScriptDocument& document, const CompletionRequest& request,
                        const scene::ClassRegistry& classes, const core::AtomTable& atoms, std::vector<Completion>& out)
{
    out.clear();

    if (!request.subject.empty()) {
        const scene::ClassId id = classOfSubject(classes, atoms, request.subject);
        if (id != scene::InvalidClass)
            collectMembers(classes, atoms, id, request, out);
        // A subject nothing recognises offers nothing rather than offering the
        // whole world: a list that is always the same is a list people learn to
        // dismiss.
    }
    else {
        for (const std::string_view keyword : kKeywords)
            push(out, request, std::string(keyword), "keyword", "", CompletionKind::Keyword);
        for (const std::string_view global : kGlobals)
            push(out, request, std::string(global), "global", "", CompletionKind::Global);

        // Every class the engine ships, so `Instance.new("Par` finds `Part` --
        // and so does somebody typing a service's name.
        for (scene::ClassId id = 1; id < static_cast<scene::ClassId>(classes.classCount()); ++id) {
            const scene::ClassDescriptor* descriptor = classes.find(id);
            if (descriptor == nullptr || hasFlag(descriptor->flags, scene::ClassFlags::Abstract))
                continue;
            const std::string_view name = atoms.text(descriptor->name);
            push(out, request, std::string(name),
                 hasFlag(descriptor->flags, scene::ClassFlags::Service) ? "service" : "class",
                 descriptor->doc != nullptr ? descriptor->doc : "",
                 hasFlag(descriptor->flags, scene::ClassFlags::Service) ? CompletionKind::Service
                                                                        : CompletionKind::Class);
        }

        // **And the words already in this file**, which is the completion a type
        // checker would not have improved on: a local somebody named four lines
        // up is the thing they are most likely to be typing next.
        if (request.prefix.size() >= 2) {
            std::vector<std::string> words;
            for (u32 line = 0; line < document.lineCount(); ++line) {
                for (const Token& token : document.tokens(line)) {
                    if (token.kind != TokenKind::Identifier)
                        continue;
                    std::string word(document.line(line).substr(token.column, token.length));
                    if (word == request.prefix || !startsWith(word, request.prefix))
                        continue;
                    if (std::find(words.begin(), words.end(), word) == words.end())
                        words.push_back(std::move(word));
                }
            }
            for (std::string& word : words)
                push(out, request, std::move(word), "in this file", "", CompletionKind::Identifier);
        }
    }

    // Sorted by kind and then by name, so the same prefix always produces the
    // same list in the same order -- which is what lets somebody learn where a
    // row will be instead of reading the whole thing every time.
    std::sort(out.begin(), out.end(), [](const Completion& a, const Completion& b) {
        if (a.kind != b.kind)
            return static_cast<core::u8>(a.kind) < static_cast<core::u8>(b.kind);
        return a.label < b.label;
    });
}

} // namespace luaug::app
