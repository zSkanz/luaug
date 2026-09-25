#include "luaug/app/script_complete.h"

#include "luaug/core/name_atom.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/world.h"
#include "luaug/script/stdlib.h"

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

// **What the ENGINE puts in front of a script**, as opposed to what Luau does.
// Every one of these is a `lua_setglobal` or a `luaL_register` in
// `engine/script/src/` -- the world globals, the datatype namespaces, and the
// three functions the runtime installs. Luau's own globals are not here: they
// come from `script::stdGlobals()`, which is checked against the VM.
//
// `Material` joined with ADR 0090 and this list did not, so every
// `Material.load` was underlined as an unknown global -- which is why a test
// now boots a VM and asks it for each name (`world_host_tests.cpp`).
constexpr std::array<std::string_view, 24> kEngineGlobals{
    "game",      "workspace", "script",        "print",  "warn",    "require",  "task",      "Instance",
    "Enum",      "Vector3",   "CFrame",        "Color3", "Vector2", "UDim",     "UDim2",     "Rect",
    "TweenInfo", "Signal",    "RaycastParams", "Random", "Content", "Material", "Collector", "Promise",
};

// `task` is the engine's library rather than Luau's, so it is not in
// `stdLibraries()` and its five names are written here beside the global that
// names it. Verified by the conformance specs, which call every one of them.
constexpr std::array<std::string_view, 5> kTaskMembers{
    "spawn", "defer", "delay", "wait", "cancel",
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

// **A call that NAMES something is a step in a path**, and there are three of
// them: `GetService("X")`, `WaitForChild("X")` and `FindFirstChild("X")`. Each
// one answers the instance its own argument names, so `X` is the step.
//
// Reads leftwards from `end`, which is one past the closing bracket. Answers
// where the call started and fills `segment`, or `end` unchanged when what is
// there is not one of these.
[[nodiscard]] u32 readNamingCall(std::string_view line, u32 end, std::string& segment)
{
    if (end == 0 || line[end - 1] != ')')
        return end;

    u32 at = end - 1;
    while (at > 0 && line[at - 1] == ' ')
        --at;
    // A single string literal and nothing else. A call with an expression in it
    // is a call this file cannot read, and guessing would be worse than
    // stopping.
    if (at == 0 || (line[at - 1] != '"' && line[at - 1] != '\''))
        return end;
    const char quote = line[at - 1];
    --at;

    const u32 textEnd = at;
    while (at > 0 && line[at - 1] != quote)
        --at;
    if (at == 0)
        return end;
    const u32 textStart = at;
    --at;

    while (at > 0 && line[at - 1] == ' ')
        --at;
    if (at == 0 || line[at - 1] != '(')
        return end;
    --at;
    while (at > 0 && line[at - 1] == ' ')
        --at;

    u32 nameStart = at;
    while (nameStart > 0 && isWordByte(line[nameStart - 1]))
        --nameStart;
    const std::string_view method = line.substr(nameStart, at - nameStart);
    if (method != "GetService" && !namesAChild(method))
        return end;

    segment = std::string(line.substr(textStart, textEnd - textStart));
    return nameStart;
}

// Reads `a.b.c` leftwards from `end`, which is one past the last byte of the
// last step. Fills `path` outermost-first and answers where the chain started.
[[nodiscard]] u32 readPath(std::string_view line, u32 end, std::vector<std::string>& path)
{
    u32 at = end;
    while (true) {
        // **An index is a step too**: `Snake.Body[1].` is the element of
        // `Snake.Body`, written as the segment `[]` (the owner's report -- the
        // chain stopped at the bracket and the list offered nothing). Whatever
        // is inside the brackets is skipped, nested ones included.
        while (at > 0 && line[at - 1] == ']') {
            int depth = 0;
            u32 open = at;
            while (open > 0) {
                --open;
                if (line[open] == ']')
                    ++depth;
                else if (line[open] == '[' && --depth == 0)
                    break;
            }
            if (depth != 0 || line[open] != '[')
                return at;
            path.insert(path.begin(), std::string(kElementStep));
            at = open;
        }
        std::string called;
        const u32 callStart = readNamingCall(line, at, called);
        u32 start = callStart;
        if (callStart != at) {
            path.insert(path.begin(), std::move(called));
        }
        else {
            while (start > 0 && isWordByte(line[start - 1]))
                --start;
            if (start == at)
                break;
            path.insert(path.begin(), std::string(line.substr(start, at - start)));
        }
        // Only a `.` or a `:` continues a chain: `a.b` continues, `+ b` does
        // not, because whatever is on the far side is an expression and this
        // file does not read expressions.
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

// **What a `local` was assigned, when it was assigned a path.**
//
// `local RunService = game:GetService("RunService")` is the first line of most
// Luau files ever written, and without this every one of them completes nothing
// from the line after it. Read textually and not inferred: this looks for one
// shape, `local NAME = <path>`, and splices that path in front of the one being
// resolved.
//
// **That is not type inference and the difference is the point.** Nothing here
// evaluates anything, follows a function, or decides what an expression is
// worth. It reads an assignment the way a person scrolling up would, which is
// the limit ADR 0057 draws and the reason this stays honest.
// Whether `value` is exactly `require(<something>)`, and if so what the
// something is. Read textually, the way everything else here is.
[[nodiscard]] bool unwrapRequire(std::string_view& value)
{
    constexpr std::string_view Call = "require";
    if (!value.starts_with(Call))
        return false;
    std::string_view rest = value.substr(Call.size());
    while (!rest.empty() && rest.front() == ' ')
        rest.remove_prefix(1);
    if (rest.size() < 2 || rest.front() != '(' || rest.back() != ')')
        return false;
    value = rest.substr(1, rest.size() - 2);
    return true;
}

// True when the head of `path` was a local assigned a `require`. The path is
// rewritten to what was required either way.
bool expandLocals(const ScriptDocument& document, std::vector<std::string>& path)
{
    bool required = false;
    // Three hops, which is more than any real file needs and a hard stop on a
    // pair of locals that name each other.
    for (int hop = 0; hop < 3 && !path.empty(); ++hop) {
        const std::string& head = path.front();
        if (head == "game" || head == "script" || head == "workspace")
            return required;

        std::vector<std::string> assigned;
        for (u32 index = 0; index < document.lineCount() && assigned.empty(); ++index) {
            const std::string_view line = document.line(index);
            const std::size_t equals = line.find('=');
            if (equals == std::string_view::npos || !line.starts_with("local "))
                continue;

            std::string_view name = line.substr(6, equals - 6);
            while (!name.empty() && name.back() == ' ')
                name.remove_suffix(1);
            if (name != head)
                continue;

            // Everything after the `=`, trailing spaces and comment trimmed by
            // reading the path leftwards from the end of the value.
            std::string_view value = line.substr(equals + 1);
            const std::size_t comment = value.find("--");
            if (comment != std::string_view::npos)
                value = value.substr(0, comment);
            // Both ends. The space after `=` is always there, and leaving it
            // made `require(` fail to match its own name.
            while (!value.empty() && value.front() == ' ')
                value.remove_prefix(1);
            while (!value.empty() && value.back() == ' ')
                value.remove_suffix(1);
            if (value.empty())
                continue;
            // **`local M = require(x)` is `x`, plus a note.** The path that
            // comes out names the MODULE INSTANCE; the note is what says `M` is
            // its returned value rather than the instance itself, which is the
            // difference between offering `Source` and offering `foo`.
            required = unwrapRequire(value) || required;
            (void)readPath(value, static_cast<u32>(value.size()), assigned);
        }

        if (assigned.empty())
            return required;
        path.erase(path.begin());
        path.insert(path.begin(), assigned.begin(), assigned.end());
    }
    return required;
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
//
// **Then the likely word first** (the owner: "`els` offered `elseif` first --
// it should make things easier, not harder", and "this applies to
// everything"). Within where a name stands: the rows that begin with what was
// typed in its own case, then the SHORTEST -- the word the fewest keystrokes
// finish -- and only then the kind and the alphabet.
void sortCompletions(std::vector<Completion>& out, std::string_view prefix)
{
    const auto rank = [](CompletionKind kind) {
        return kind == CompletionKind::Instance ? 0 : static_cast<int>(kind) + 1;
    };
    const auto exact = [prefix](const Completion& row) {
        return !prefix.empty() && std::string_view(row.label).starts_with(prefix);
    };
    std::stable_sort(out.begin(), out.end(), [&](const Completion& a, const Completion& b) {
        if (a.scope != b.scope)
            return a.scope < b.scope;
        if (exact(a) != exact(b))
            return exact(a);
        // Only once something is typed: a bare `part.` is a list somebody
        // reads by name, and ordering it by length would scatter it.
        if (!prefix.empty() && a.label.size() != b.label.size())
            return a.label.size() < b.label.size();
        if (a.kind != b.kind)
            return rank(a.kind) < rank(b.kind);
        return a.label < b.label;
    });
}

} // namespace

bool completesExactly(std::span<const Completion> rows, std::string_view prefix) noexcept
{
    return !prefix.empty() &&
           std::any_of(rows.begin(), rows.end(), [prefix](const Completion& row) { return row.label == prefix; });
}

void mergeCompletions(std::vector<Completion>& shown, const std::vector<Completion>& analyzed, bool inType,
                      std::string_view prefix)
{
    if (completesExactly(analyzed, prefix)) {
        shown.clear();
        return;
    }
    std::vector<Completion> merged;
    for (const Completion& row : analyzed) {
        if (startsWith(row.label, prefix) && row.label != prefix)
            merged.push_back(row);
    }
    if (!inType && merged.empty())
        return;
    if (!inType) {
        for (Completion& row : shown) {
            const auto same = [&row](const Completion& kept) { return kept.label == row.label; };
            const auto kept = std::find_if(merged.begin(), merged.end(), same);
            // The checker's row wins, but where the name stands is ours: it
            // does not say whether a name is in scope, and the scan does.
            if (kept != merged.end())
                kept->scope = std::min(kept->scope, row.scope);
            else if (startsWith(row.label, prefix) && row.label != prefix)
                merged.push_back(std::move(row));
        }
    }
    shown = std::move(merged);
    sortCompletions(shown, prefix);
}

std::span<const std::string_view> engineGlobals() noexcept
{
    return kEngineGlobals;
}

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
    request.joined = true;

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
        sortCompletions(out, request.prefix);
        return;
    }
    if (request.quoted == CompletionQuoted::Child) {
        std::vector<std::string> path = request.path;
        (void)expandLocals(document, path);
        collectChildren(tree, atoms, resolvePath(tree, atoms, path), request, out);
        sortCompletions(out, request.prefix);
        return;
    }
    if (request.quoted == CompletionQuoted::Other)
        return;

    // **A Luau library is not a class and has no instance**, so it is answered
    // before either is looked for. `math.` is `math.`, in every project, whether
    // or not a world is open.
    if (request.joined && !request.method && request.path.size() == 1) {
        if (request.path[0] == "task") {
            for (const std::string_view member : kTaskMembers)
                push(out, request, std::string(member), "function", "", CompletionKind::Library);
            sortCompletions(out, request.prefix);
            return;
        }
        for (const script::StdLibrary& library : script::stdLibraries()) {
            if (library.name != request.path[0])
                continue;
            for (const script::StdName& member : library.members)
                push(out, request, std::string(member.name), std::string(member.type), "", CompletionKind::Library);
            sortCompletions(out, request.prefix);
            return;
        }
    }

    if (request.joined) {
        // **What this file's own code says the path is, first** (the owner's
        // report: `Snake.` offered nothing in a file that had just built
        // `Snake`). A table the file fills in, an instance of one, a type it
        // wrote, or an engine class a value is annotated or made as. When it
        // cannot tell, what follows answers as it always did.
        if (const SourceMembers own = sourceMembersOf(document.text(), request.path, request.replace.end); own.known) {
            if (!own.className.empty()) {
                if (const scene::ClassId named = classes.findId(atoms.lookup(own.className));
                    named != scene::InvalidClass)
                    collectMembers(classes, atoms, named, request, out);
            }
            for (const SourceMember& member : own.members) {
                if (request.method && !member.callable)
                    continue;
                push(out, request, member.name, member.detail, "",
                     member.callable ? CompletionKind::Method : CompletionKind::Property);
            }
            if (!out.empty()) {
                sortCompletions(out, request.prefix);
                return;
            }
        }

        // **The instance first, its class second.** A resolved path knows both
        // -- what the thing IS and what is inside it -- and a class name alone
        // knows only the first. `classOfSubject` is the fallback for a local
        // that happens to be spelled like a service, which is how somebody
        // reaches one after `local Lighting = game:GetService("Lighting")`.
        std::vector<std::string> path = request.path;
        const bool required = expandLocals(document, path);
        const core::InstanceId at = resolvePath(tree, atoms, path);

        // **A required module is what it RETURNS, not the instance it lives
        // in.** `local M = require(script.Util)` then `M.` wants `M`'s own
        // names -- offering `Source` and `Parent` there would be offering the
        // ModuleScript, which is not what `M` is.
        if (required && at.valid() && tree.world != nullptr && !request.method) {
            const std::optional<scene::Value> source = tree.world->getProperty(at, atoms.lookup("Source"));
            const auto* text = source.has_value() ? std::get_if<std::string>(&*source) : nullptr;
            if (text != nullptr) {
                std::vector<ModuleMember> members;
                moduleMembers(*text, members);
                for (ModuleMember& member : members)
                    push(out, request, std::move(member.name), std::move(member.detail), "", CompletionKind::Module);
            }
            // Nothing else: a module's table has no class and no children, and
            // adding either would be describing the wrong object.
            sortCompletions(out, request.prefix);
            return;
        }
        const scene::ClassId id = at.valid() && tree.world != nullptr ? tree.world->classOf(at)
                                                                      : classOfSubject(classes, atoms, request.subject);
        if (id != scene::InvalidClass)
            collectMembers(classes, atoms, id, request, out);
        // **And its children, after its members** (ADR 0078): `workspace.`
        // offers what is in the workspace, because a dot reaches a child. The
        // members come first in the same list, which is also the order the
        // runtime resolves a name in -- a child called `Name` never hides the
        // property.
        // After a colon only methods: a child is not callable.
        if (at.valid() && !request.method)
            collectChildren(tree, atoms, at, request, out);
        //
        // A subject nothing recognises offers nothing rather than offering the
        // whole world: a list that is always the same is a list people learn to
        // dismiss, and a list of keywords under a dot is never right.
    }
    else {
        for (const std::string_view keyword : kKeywords)
            push(out, request, std::string(keyword), "keyword", "", CompletionKind::Keyword);
        for (const std::string_view global : kEngineGlobals)
            push(out, request, std::string(global), "global", "", CompletionKind::Global);
        // Luau's own: `typeof`, `pcall`, `assert`, `_VERSION` -- the names
        // somebody types most and the ones an engine-only list would have left
        // out entirely.
        for (const script::StdName& global : script::stdGlobals())
            push(out, request, std::string(global.name), std::string(global.type), "", CompletionKind::Global);
        // And the library tables themselves, so `mat` finds `math`.
        push(out, request, "task", "library", "", CompletionKind::Library);
        for (const script::StdLibrary& library : script::stdLibraries())
            push(out, request, std::string(library.name), "library", "", CompletionKind::Library);

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
        //
        // **Only the ones the caret can see** (the owner: `matrix`, a local of
        // another function, offered at the top of the file). This used to be
        // every identifier the file contained -- a field name, a parameter of
        // a function far away, a local of a block already closed -- and a
        // suggestion that the code cannot use is worse than none.
        //
        // **Where a name can be seen decides its place, not whether it is
        // offered** (the owner, on second thought: "it should only change the
        // order -- scope, then out of scope, then global"). The names the
        // caret can see come first; every other name the file writes follows.
        if (request.prefix.size() >= 2) {
            //
            // **And nothing that does not exist yet** (the owner: `ServerInfo`
            // offered inside `local ServerInfo = require(Ser|)`): a name the
            // file declares after the caret, or in the statement the caret is
            // still writing, is in neither list.
            static std::vector<std::string> visible;
            static std::vector<std::string> elsewhere;
            visibleNames(document.text(), request.replace.begin, visible, &elsewhere);
            const auto offer = [&](const std::string& word, CompletionScope scope) {
                if (word == request.prefix || !startsWith(word, request.prefix))
                    return;
                // **One row per name** (the owner's report: `print` offered
                // twice, "in this file" and "global"). A name the engine
                // already offers keeps the engine's row -- at this name's
                // place in the order when the file declares it.
                const auto offered = [&word](const Completion& row) { return row.label == word; };
                if (const auto found = std::find_if(out.begin(), out.end(), offered); found != out.end()) {
                    found->scope = std::min(found->scope, scope);
                    return;
                }
                push(out, request, word, "in this file", "", CompletionKind::Identifier);
                out.back().scope = scope;
            };
            for (const std::string& word : visible)
                offer(word, CompletionScope::Visible);
            for (const std::string& word : elsewhere)
                offer(word, CompletionScope::Elsewhere);
        }
    }

    sortCompletions(out, request.prefix);
}

// --- Assigning to a live child's name, at edit time (ADR 0078) ---------------

void lintInstanceAccess(const ScriptDocument& document, const scene::ClassRegistry& classes,
                        const core::AtomTable& atoms, const CompletionWorld& tree, std::vector<Diagnostic>& out)
{
    if (tree.world == nullptr)
        return;

    const scene::World& world = *tree.world;

    // **Line by line, over the same reader the completion uses**, rather than
    // over an AST. The completion already answers "what is the dotted path
    // ending here" for any caret, and asking it once per dot is the same
    // question this needs -- so there is one path resolver in this file rather
    // than a second one that would disagree with it the first time either was
    // touched. It also inherits `expandLocals` for free, which is what makes
    // `local W = game:GetService("Workspace")` followed by `W.Thing` resolve.
    for (core::u32 line = 0; line < document.lineCount(); ++line) {
        const std::string_view text = document.line(line);
        for (core::u32 column = 0; column < text.size(); ++column) {
            if (text[column] != '.')
                continue;

            // The identifier after the dot, which is what would be indexed.
            core::u32 end = column + 1;
            while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) != 0 || text[end] == '_'))
                ++end;
            if (end == column + 1)
                continue;
            const std::string member(text.substr(column + 1, end - column - 1));

            // **Only an assignment is a finding.** Reading a child with a dot
            // works (ADR 0078); writing to its name does not, because a child
            // is replaced by parenting another instance.
            core::u32 after = end;
            while (after < text.size() && (text[after] == ' ' || text[after] == '	'))
                ++after;
            if (after >= text.size() || text[after] != '=' || (after + 1 < text.size() && text[after + 1] == '='))
                continue;

            // What the completion would answer at the caret just after the dot,
            // which is the resolved subject of this access.
            const CompletionRequest request = completionAt(document, Position{line, column + 1});
            if (!request.joined || request.method || request.quoted != CompletionQuoted::No)
                continue;
            std::vector<std::string> path = request.path;
            if (path.empty())
                continue;
            (void)expandLocals(document, path);

            const core::InstanceId at = resolvePath(tree, atoms, path);
            if (!at.valid())
                continue;

            // **A declared member wins, and is not a finding.** The runtime
            // resolves a property before it looks for a child, so a child
            // called `Name` does not shadow `Name` -- and reporting one here
            // would underline a line that works.
            const core::NameAtom memberAtom = atoms.lookup(member);
            if (memberAtom.valid() && classes.findProperty(world.classOf(at), memberAtom) != nullptr)
                continue;
            if (memberAtom.valid() && classes.findMethod(world.classOf(at), memberAtom) != nullptr)
                continue;

            // Only when the name really is a child. A misspelled property is
            // somebody else's diagnostic -- `luau-analyze` types this file and
            // ADR 0018 keeps type inference there -- and guessing at one here
            // would be the false positive this pass is written to avoid.
            if (!memberAtom.valid() || !world.findFirstChild(at, memberAtom).valid())
                continue;

            out.push_back(Diagnostic{
                .at = Position{line, column + 1},
                .length = static_cast<core::u32>(member.size()),
                .message = "`" + member +
                           "` is a child: a dot reads it, and it is replaced by parenting another instance, not by "
                           "assignment",
                .severity = Severity::Warning,
            });
        }
    }
}

} // namespace luaug::app
