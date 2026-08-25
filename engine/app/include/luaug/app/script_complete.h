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
// **What it does NOT do, stated so nobody reads the absence as a bug**: infer a
// type across an expression. `local p = workspace.Baseplate` followed by `p.` is
// not resolved, because knowing that would be type inference and type inference
// is `luau-analyze`. What IS resolved is the vocabulary somebody actually forgets
// -- which service has which member, and how each one is spelled.
#pragma once

#include "luaug/app/script_document.h"
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

// What is being completed: the word under the caret and what it hangs off.
struct CompletionRequest
{
    // The partial word before the caret, which is what the list is filtered by.
    std::string prefix;
    // The range `prefix` occupies, so accepting a row replaces it.
    Range replace;
    // The token before the `.` or `:`, empty when there is none.
    std::string subject;
    // Whether it was a colon, which is what tells a method from a property.
    bool method = false;
};

// Reads the document backwards from `caret`. Never fails: a caret in the middle
// of nothing answers an empty prefix and an empty subject, which is the state
// that offers keywords and the file's own identifiers.
[[nodiscard]] CompletionRequest completionAt(const ScriptDocument& document, Position caret);

// Fills `out`, sorted and filtered by `request.prefix`.
//
// `world` is used only to resolve `game` and the service names, so a call with
// no world still completes keywords, identifiers and class names.
void collectCompletions(const ScriptDocument& document, const CompletionRequest& request,
                        const scene::ClassRegistry& classes, const core::AtomTable& atoms,
                        std::vector<Completion>& out);

// How many rows the popup shows before it scrolls. A list somebody has to scan
// is a list they stop reading, and eight is what fits under a line of code
// without covering the next paragraph.
inline constexpr core::usize kMaxCompletionRows = 8;

} // namespace luaug::app
