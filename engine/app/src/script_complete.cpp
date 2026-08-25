#include "luaug/app/script_complete.h"

#include "luaug/core/name_atom.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <span>

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

// The methods whose FIRST argument is the name of a child. Two, and both are on
// `Instance`, so this list is short because the API is -- not because it was
// trimmed. `GetService` is handled apart: its argument is a class, not a child.
[[nodiscard]] bool namesAChild(std::string_view method) noexcept
{
    return method == "WaitForChild" || method == "FindFirstChild";
}

// Where an unterminated string starts on this line, or npos.
//
// Walked forward from the start of the line rather than backward from the
// caret, because backward cannot tell an opening quote from a closing one --
// `print("a", "b` has three quotes before the caret and the caret is inside the
// third. Forward with a state bit cannot get it wrong.
//
// Comments are not considered: a caret inside a comment is answered by whatever
// the comment happens to contain, and offering a child's name to somebody
// writing prose costs one Escape.
[[nodiscard]] std::size_t openQuoteBefore(std::string_view line, u32 caret) noexcept
{
    std::size_t open = std::string_view::npos;
    char quote = 0;
    for (u32 index = 0; index < caret && index < line.size(); ++index) {
        const char c = line[index];
        if (quote != 0) {
            if (c == '\\') {
                ++index;
                continue;
            }
            if (c == quote)
                quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            open = index;
        }
    }
    return quote != 0 ? open : std::string_view::npos;
}

// Reads `a.b.c` leftwards from `end`, which is one past the last byte of the
// last name. Fills `path` outermost-first and answers where the chain started.
[[nodiscard]] u32 readPath(std::string_view line, u32 end, std::vector<std::string>& path)
{
    u32 at = end;
    while (true) {
        u32 start = at;
        while (start > 0 && isWordByte(line[start - 1]))
            --start;
        if (start == at)
            break;
        path.insert(path.begin(), std::string(line.substr(start, at - start)));
        // Only a `.` or a `:` continues a chain, and only when a name is on the
        // far side of it: `a.b` continues, `).b` does not, because whatever the
        // call returned is a value and this file does not infer values.
        if (start == 0 || (line[start - 1] != '.' && line[start - 1] != ':'))
            return start;
        at = start - 1;
    }
    return at;
}

// What a resolved path names, walking the tree from `game`.
//
// **The first segment is the only one with rules**, and there are four: `game`
// is the root, `workspace` and any service's own name is a child of the root,
// and `script` is the instance being edited. Everything after that is a plain
// `FindFirstChild`, plus `Parent`, which is the one property people write in
// the middle of a path often enough that leaving it out would be noticed.
[[nodiscard]] core::InstanceId resolvePath(const CompletionWorld& tree, const core::AtomTable& atoms,
                                           std::span<const std::string> path)
{
    if (tree.world == nullptr || path.empty())
        return core::InstanceId{};

    const scene::World& world = *tree.world;
    core::InstanceId at;
    if (path[0] == "game")
        at = tree.root;
    else if (path[0] == "script")
        at = tree.self;
    else if (path[0] == "workspace")
        at = world.findFirstChild(tree.root, atoms.lookup("Workspace"));
    else
        at = world.findFirstChild(tree.root, atoms.lookup(path[0]));

    for (std::size_t index = 1; index < path.size() && at.valid(); ++index) {
        at = path[index] == "Parent" ? world.parentOf(at) : world.findFirstChild(at, atoms.lookup(path[index]));
    }
    return at.valid() && world.alive(at) ? at : core::InstanceId{};
}

// Every child of `parent`, as rows. Skips a name a member already took, because
// `workspace.Name` is the property and offering the child would be offering
// something the VM will not hand back.
void collectChildren(const CompletionWorld& tree, const core::AtomTable& atoms, core::InstanceId parent,
                     const CompletionRequest& request, std::vector<Completion>& out)
{
    if (tree.world == nullptr || !parent.valid())
        return;

    const scene::World& world = *tree.world;
    for (core::InstanceId child = world.firstChild(parent); child.valid(); child = world.nextSibling(child)) {
        const std::string_view name = atoms.text(world.name(child));
        if (name.empty())
            continue;
        // **One row per NAME, not per instance.** A member wins the collision --
        // `workspace.Name` is the property, and offering the child would be
        // offering something the VM will not hand back. And six parts all
        // called `Ground` are six rows that insert the same six characters,
        // which is a list nobody can choose from and a scroll bar for nothing.
        const auto taken = [name](const Completion& row) { return row.label == name; };
        if (std::find_if(out.begin(), out.end(), taken) != out.end())
            continue;

        const scene::ClassDescriptor* descriptor = world.classes().find(world.classOf(child));
        std::string className(descriptor != nullptr ? atoms.text(descriptor->name) : std::string_view("Instance"));
        push(out, request, std::string(name), std::move(className),
             descriptor != nullptr && descriptor->doc != nullptr ? descriptor->doc : "", CompletionKind::Instance);
    }
}

// Every class flagged as a service, which is what `GetService("` accepts.
void collectServices(const scene::ClassRegistry& classes, const core::AtomTable& atoms,
                     const CompletionRequest& request, std::vector<Completion>& out)
{
    for (scene::ClassId id = 1; id < static_cast<scene::ClassId>(classes.classCount()); ++id) {
        const scene::ClassDescriptor* descriptor = classes.find(id);
        if (descriptor == nullptr || !hasFlag(descriptor->flags, scene::ClassFlags::Service))
            continue;
        push(out, request, std::string(atoms.text(descriptor->name)), "service",
             descriptor->doc != nullptr ? descriptor->doc : "", CompletionKind::Service);
    }
}

// Sorted by kind and then by name, so the same prefix always produces the same
// list in the same order -- which is what lets somebody learn where a row will
// be instead of reading the whole thing every time.
//
// **`Instance` sorts first**, and that is the ordering decision worth stating:
// somebody typing `Workspace.` is far more often reaching for something in
// their own tree than for `ClassName`, and the members are still one keystroke
// of filtering away.
void sortCompletions(std::vector<Completion>& out)
{
    const auto rank = [](CompletionKind kind) {
        return kind == CompletionKind::Instance ? 0 : static_cast<int>(kind) + 1;
    };
    std::sort(out.begin(), out.end(), [&rank](const Completion& a, const Completion& b) {
        if (a.kind != b.kind)
            return rank(a.kind) < rank(b.kind);
        return a.label < b.label;
    });
}

} // namespace

CompletionRequest completionAt(const ScriptDocument& document, Position caret)
{
    CompletionRequest request;
    const Position here = document.clamp(caret);
    const std::string_view line = document.line(here.line);

    // **A caret inside quotes is a different question**, and it is asked first
    // because everything below reads the bytes as code. `WaitForChild("Ma` is
    // somebody naming a child, and the only thing that can answer is the tree.
    const std::size_t quote = openQuoteBefore(line, here.column);
    if (quote != std::string_view::npos) {
        const auto open = static_cast<u32>(quote);
        request.prefix = std::string(line.substr(open + 1, here.column - open - 1));
        request.replace = Range{Position{here.line, open + 1}, here};

        // Back over `(` to the method's own name. Anything else in between and
        // this is an ordinary string, which has no completion at all.
        request.quoted = CompletionQuoted::Other;
        u32 at = open;
        while (at > 0 && line[at - 1] == ' ')
            --at;
        if (at == 0 || line[at - 1] != '(')
            return request;
        --at;
        while (at > 0 && line[at - 1] == ' ')
            --at;

        u32 nameStart = at;
        while (nameStart > 0 && isWordByte(line[nameStart - 1]))
            --nameStart;
        const std::string_view method = line.substr(nameStart, at - nameStart);
        if (method == "GetService") {
            request.quoted = CompletionQuoted::Service;
            return request;
        }
        if (!namesAChild(method))
            return request;

        // The chain the call hangs off. `nameStart - 1` is its `.` or `:`.
        if (nameStart == 0 || (line[nameStart - 1] != '.' && line[nameStart - 1] != ':'))
            return request;
        (void)readPath(line, nameStart - 1, request.path);
        if (request.path.empty())
            return request;
        request.subject = request.path.back();
        request.quoted = CompletionQuoted::Child;
        return request;
    }

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

    // **The whole chain and not just the name before the dot.** `Camera` on its
    // own names nothing -- the same name under two parents is two instances --
    // so a path is resolvable only from where it starts.
    (void)readPath(line, start - 1, request.path);
    if (!request.path.empty())
        request.subject = request.path.back();
    return request;
}

void collectCompletions(const ScriptDocument& document, const CompletionRequest& request,
                        const scene::ClassRegistry& classes, const core::AtomTable& atoms, const CompletionWorld& tree,
                        std::vector<Completion>& out)
{
    out.clear();

    // **Inside quotes there is exactly one right answer and it is not a class
    // member.** `GetService("` takes a service; `WaitForChild("` takes the name
    // of a child, which only the tree knows.
    if (request.quoted == CompletionQuoted::Service) {
        collectServices(classes, atoms, request, out);
        sortCompletions(out);
        return;
    }
    if (request.quoted == CompletionQuoted::Child) {
        collectChildren(tree, atoms, resolvePath(tree, atoms, request.path), request, out);
        sortCompletions(out);
        return;
    }
    if (request.quoted == CompletionQuoted::Other)
        return;

    if (!request.subject.empty()) {
        // **The instance first, its class second.** A resolved path knows both
        // -- what the thing IS and what is inside it -- and a class name alone
        // knows only the first. `classOfSubject` is the fallback for a local
        // that happens to be spelled like a service, which is how somebody
        // reaches one after `local Lighting = game:GetService("Lighting")`.
        const core::InstanceId at = resolvePath(tree, atoms, request.path);
        const scene::ClassId id = at.valid() && tree.world != nullptr ? tree.world->classOf(at)
                                                                      : classOfSubject(classes, atoms, request.subject);
        if (id != scene::InvalidClass)
            collectMembers(classes, atoms, id, request, out);
        // A colon is a call, and a child is not one -- so children are offered
        // after a dot and not after a colon.
        if (!request.method)
            collectChildren(tree, atoms, at, request, out);
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

    sortCompletions(out);
}

} // namespace luaug::app
