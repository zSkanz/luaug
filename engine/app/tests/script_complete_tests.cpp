// Autocomplete, asserted without a window (ADR 0057).
//
// **The whole of it is testable**, which is the argument for building it on the
// engine's own reflection rather than on a language server: what the list holds
// is a question about `ClassRegistry`, and only the popup is a picture.
//
// The case worth reading twice is the last one. What this deliberately does NOT
// do -- resolve a local to a class -- is asserted as an absence, because an
// absence nobody wrote down is a bug report waiting to happen.
#include "luaug/app/script_complete.h"
#include "luaug/core/name_atom.h"
#include "luaug/scene/class_registry.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <ostream>
#include <string>

#include "class_descriptors.gen.h"

using namespace luaug;
using app::Completion;
using app::CompletionKind;
using app::CompletionRequest;
using app::Position;
using app::ScriptDocument;

namespace {

struct Reflection
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;

    Reflection() { scene::generated::registerClasses(classes, atoms); }
};

// The completions offered with the caret at the end of `source`.
[[nodiscard]] std::vector<Completion> at(Reflection& fixture, std::string_view source)
{
    ScriptDocument document(source);
    const core::u32 last = document.lineCount() - 1;
    const Position caret{last, document.lineLength(last)};
    const CompletionRequest request = app::completionAt(document, caret);

    std::vector<Completion> out;
    app::collectCompletions(document, request, fixture.classes, fixture.atoms, out);
    return out;
}

[[nodiscard]] bool has(const std::vector<Completion>& list, std::string_view label)
{
    return std::any_of(list.begin(), list.end(), [&](const Completion& c) { return c.label == label; });
}

[[nodiscard]] const Completion* find(const std::vector<Completion>& list, std::string_view label)
{
    const auto found = std::find_if(list.begin(), list.end(), [&](const Completion& c) { return c.label == label; });
    return found != list.end() ? &*found : nullptr;
}

} // namespace

TEST_CASE("the request reads what the caret is completing")
{
    ScriptDocument document("local p = workspace.Curr");
    const CompletionRequest request = app::completionAt(document, Position{0, 24});

    CHECK(request.prefix == "Curr");
    CHECK(request.subject == "workspace");
    CHECK_FALSE(request.method);
    // The range accepting a row replaces: the partial word and nothing else.
    CHECK(request.replace == app::Range{Position{0, 20}, Position{0, 24}});
}

TEST_CASE("a colon is a call and a dot is not")
{
    ScriptDocument document("game:GetSer");
    const CompletionRequest colon = app::completionAt(document, Position{0, 11});
    CHECK(colon.method);
    CHECK(colon.subject == "game");
    CHECK(colon.prefix == "GetSer");

    ScriptDocument dotted("game.Work");
    const CompletionRequest dot = app::completionAt(dotted, Position{0, 9});
    CHECK_FALSE(dot.method);
}

TEST_CASE("a space after the dot ends the subject")
{
    // Somebody who has moved on is not completing a member, and offering one
    // would be a popup appearing over an unrelated line.
    ScriptDocument document("workspace. Curr");
    const CompletionRequest request = app::completionAt(document, Position{0, 15});
    CHECK(request.prefix == "Curr");
    CHECK(request.subject.empty());
}

TEST_CASE("a member list comes from the class the subject names")
{
    Reflection fixture;

    const std::vector<Completion> list = at(fixture, "workspace.");
    REQUIRE(!list.empty());
    // Declared on `Workspace` itself.
    CHECK(has(list, "CurrentCamera"));
    // And inherited from `Instance`, which is the walk the registry does not do
    // for an enumeration.
    CHECK(has(list, "Name"));
    CHECK(has(list, "Parent"));
}

TEST_CASE("after a colon only the methods are offered")
{
    Reflection fixture;

    const std::vector<Completion> methods = at(fixture, "game:");
    CHECK(has(methods, "GetService"));
    // A property is not callable, so it is not offered where a call is being
    // written.
    CHECK_FALSE(has(methods, "Name"));

    const std::vector<Completion> everything = at(fixture, "game.");
    CHECK(has(everything, "Name"));
}

TEST_CASE("a row carries the type and the prose the IDL wrote")
{
    Reflection fixture;

    const std::vector<Completion> list = at(fixture, "workspace.Curr");
    const Completion* camera = find(list, "CurrentCamera");
    REQUIRE(camera != nullptr);
    CHECK(camera->kind == CompletionKind::Property);
    CHECK(!camera->detail.empty());
    // The doc is the generated one, which is what makes this worth more than a
    // list of names -- and what a hand-written list would have gone stale
    // against.
    CHECK(!camera->doc.empty());
}

TEST_CASE("the prefix filters, and it does not care about case")
{
    Reflection fixture;

    const std::vector<Completion> exact = at(fixture, "game:GetSer");
    CHECK(has(exact, "GetService"));

    // This API is PascalCase off an object and camelCase off a module (ADR
    // 0034), so requiring the right case would be requiring somebody to
    // remember the rule before the completion can remind them of it.
    const std::vector<Completion> lower = at(fixture, "game:getser");
    CHECK(has(lower, "GetService"));

    const std::vector<Completion> nothing = at(fixture, "game:Zzzz");
    CHECK(nothing.empty());
}

TEST_CASE("with no subject the list is keywords, globals and classes")
{
    Reflection fixture;

    const std::vector<Completion> list = at(fixture, "loc");
    CHECK(has(list, "local"));

    const std::vector<Completion> classes = at(fixture, "Par");
    CHECK(has(classes, "Part"));
    // Abstract classes are not offered, because `Instance.new("BasePart")` is
    // refused and a completion that produces a refusal is worse than none.
    CHECK_FALSE(has(classes, "BasePart"));

    const std::vector<Completion> globals = at(fixture, "wor");
    CHECK(has(globals, "workspace"));
}

TEST_CASE("the words already in the file are offered")
{
    Reflection fixture;

    // The completion a type checker would not have improved on: a local named
    // four lines up is the thing somebody is most likely typing next.
    const std::vector<Completion> list = at(fixture, "local speedLimit = 12\nlocal x = spee");
    const Completion* found = find(list, "speedLimit");
    REQUIRE(found != nullptr);
    CHECK(found->kind == CompletionKind::Identifier);
}

TEST_CASE("the list is in a stable order")
{
    Reflection fixture;

    const std::vector<Completion> first = at(fixture, "workspace.");
    const std::vector<Completion> second = at(fixture, "workspace.");
    REQUIRE(first.size() == second.size());
    for (std::size_t index = 0; index < first.size(); ++index)
        CHECK(first[index].label == second[index].label);

    // Sorted, so somebody learns where a row will be instead of reading the
    // whole list every time.
    for (std::size_t index = 1; index < first.size(); ++index) {
        const bool ordered =
            first[index - 1].kind < first[index].kind ||
            (first[index - 1].kind == first[index].kind && first[index - 1].label <= first[index].label);
        CHECK(ordered);
    }
}

TEST_CASE("a local holding an instance is NOT resolved, and that is the decision")
{
    Reflection fixture;

    // `p` is a `Part` to a reader and nothing to this, because knowing it would
    // be type inference and type inference is `luau-analyze` (ADR 0018). Stated
    // as an assertion so the absence is a decision somebody can find rather
    // than a defect somebody reports.
    const std::vector<Completion> list = at(fixture, "local p = workspace.Baseplate\np.");
    CHECK(list.empty());
}
