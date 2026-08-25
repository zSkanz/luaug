// Autocomplete, from the engine's own reflection (ADR 0057).
//
// **Not from `Luau.Analysis`, and that is a decision rather than a shortcut.**
// `cmake/luaug_luau.cmake` records that Analysis was 35% of a cold build's
// compile time and excludes it, and ADR 0018 makes type checking `luau-analyze`
// -- a tool, never a runtime dependency. Linking it here would reverse both for
// a feature that does not need it.
//
// What it needs is already in the binary. `ClassRegistry` holds every class,
// property, method and event with the doc string the IDL wrote, generated from
// `api/defs/*.api.luau` -- the same tables the property grid already walks with
// no switch on any class name. Completion resolves the token before `.` or `:`
// to a class and lists its members through the hierarchy, plus the identifiers
// the file already contains and the keywords.
//
// **And the WORLD is the other half of it**, which is the half a type checker
// would not have. `Workspace.MainCamera` is not a fact about the `Workspace`
// class -- it is a fact about this project's tree, sitting in memory two panels
// away from the tab being typed into. So a dotted path is walked instance by
// instance from the DataModel, and what it resolves to offers its CHILDREN
// beside its members: the names somebody is reaching for are the names in their
// own Explorer. The same walk answers inside `WaitForChild("` and
// `FindFirstChild("`, where the thing being typed is a child's name in quotes
// and nothing about the class could ever say what it is.
//
// **What it does NOT do, stated so nobody reads the absence as a bug**: infer a
// type across an expression. `local p = workspace.Baseplate` followed by `p.` is
// not resolved, because knowing that would be type inference and type inference
// is `luau-analyze`. What IS resolved is the vocabulary somebody actually forgets
// -- which service has which member, and how each one is spelled.
#pragma once

#include "luaug/app/script_document.h"
#include "luaug/core/id.h"
#include "luaug/core/types.h"

#include <string>
#include <string_view>
#include <vector>

namespace luaug::core {
class AtomTable;
}

namespace luaug::scene {
class ClassRegistry;
class World;
} // namespace luaug::scene

namespace luaug::app {

// What kind of thing a row is, which is what the icon and the ordering use.
enum class CompletionKind : core::u8
{
    Keyword,
    // A name already written somewhere in this file. The cheapest useful
    // completion there is, and the one a type checker would not improve.
    Identifier,
    Property,
    Method,
    Event,
    Class,
    Service,
    Global,
    // **A child of the resolved instance**, by the name it has in the tree
    // right now. The one kind of row nothing but a live world can produce.
    Instance,
};

struct Completion
{
    std::string label;
    // The type, the signature, or the class a service is -- whatever fits on the
    // right of the row.
    std::string detail;
    // The IDL's own prose, shown for the highlighted row. Empty for anything
    // that is not a reflected member.
    std::string doc;
    CompletionKind kind = CompletionKind::Identifier;
};

// Where the caret is, when it is inside a string that names something.
enum class CompletionQuoted : core::u8
{
    // The ordinary case: the caret is in code.
    No,
    // Inside the quotes of `WaitForChild(` or `FindFirstChild(`, so what is
    // being typed is the name of a CHILD of whatever the call hangs off.
    Child,
    // Inside the quotes of `GetService(`, so what is being typed is a service.
    Service,
    // Inside quotes that are nobody's argument -- a message, a path, a name
    // being built by hand. **Offers nothing**, which is a state of its own
    // rather than a fall-through: the alternative is a list of every keyword
    // in the language popping up over every string anybody types.
    Other,
};

// What is being completed: the word under the caret and what it hangs off.
struct CompletionRequest
{
    // The partial word before the caret, which is what the list is filtered by.
    std::string prefix;
    // The range `prefix` occupies, so accepting a row replaces it.
    Range replace;
    // The token before the `.` or `:`, empty when there is none. The last
    // element of `path`, kept as its own field because most of what reads this
    // only ever wanted the one name.
    std::string subject;
    // Whether it was a colon, which is what tells a method from a property.
    bool method = false;

    // **The whole chain, outermost first**: `game.Workspace.Camera.` reads as
    // `{"game", "Workspace", "Camera"}`. Empty when there is no `.` or `:` in
    // front of the caret at all.
    //
    // A chain and not just `subject`, because an instance path is only
    // resolvable from its start: `Camera` alone names nothing, and the same
    // name under two parents is two different instances.
    std::vector<std::string> path;

    // Whether the caret is inside a string that names a child or a service, and
    // which. `path` is then the chain the CALL hangs off, and `replace` covers
    // what is between the quote and the caret.
    CompletionQuoted quoted = CompletionQuoted::No;
};

// The live tree, when the caller has one. Everything here is optional in the
// sense that a null `world` still completes keywords, classes and members --
// which is what a unit test with no world gets, and what an editor with no
// project open would get.
struct CompletionWorld
{
    const scene::World* world = nullptr;
    // What `game` names: the one instance whose own parent is nil. Services are
    // its children, which is what makes `Workspace.` and `Lighting.` resolve
    // without a table of service names anywhere in this file.
    core::InstanceId root;
    // What `script` names: the instance whose `Source` is in the tab. `nil` in
    // a context that is not editing one.
    core::InstanceId self;
};

// Reads the document backwards from `caret`. Never fails: a caret in the middle
// of nothing answers an empty prefix and an empty subject, which is the state
// that offers keywords and the file's own identifiers.
[[nodiscard]] CompletionRequest completionAt(const ScriptDocument& document, Position caret);

// Fills `out`, sorted and filtered by `request.prefix`.
//
// `tree` is what turns a dotted path into an instance and its children into
// rows. A default-constructed one is legal and answers what the registry alone
// can: keywords, identifiers, class names and a class's members.
void collectCompletions(const ScriptDocument& document, const CompletionRequest& request,
                        const scene::ClassRegistry& classes, const core::AtomTable& atoms, const CompletionWorld& tree,
                        std::vector<Completion>& out);

// How many rows the popup shows before it scrolls. A list somebody has to scan
// is a list they stop reading, and eight is what fits under a line of code
// without covering the next paragraph.
inline constexpr core::usize kMaxCompletionRows = 8;

} // namespace luaug::app
