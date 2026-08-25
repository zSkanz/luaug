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
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

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
    scene::EnumRegistry enums;

    Reflection()
    {
        scene::generated::registerClasses(classes, atoms);
        scene::generated::registerEnums(enums, atoms);
    }
};

// A tree with something in it, because half of what completion answers is a
// fact about the WORLD rather than about the API: `Workspace.MainCamera` is not
// a member of anything, it is a name somebody gave an instance.
struct Tree
{
    scene::World world;

    core::InstanceId root;
    core::InstanceId workspace;
    core::InstanceId baseplate;
    core::InstanceId tags;

    // **Only classes `scene` registers.** `Camera` and `Lighting` come from
    // `render`, which this fixture does not build -- and a tree made of classes
    // the registry has never heard of resolves to nothing, which is a fixture
    // asserting its own mistake.
    explicit Tree(Reflection& fixture) : world(fixture.classes, fixture.enums, fixture.atoms, 1234u)
    {
        root = make(fixture, "DataModel", "game", core::InstanceId{});
        workspace = make(fixture, "Workspace", "Workspace", root);
        tags = make(fixture, "TagService", "TagService", root);
        baseplate = make(fixture, "Part", "Baseplate", workspace);
        (void)make(fixture, "Folder", "Level", workspace);
        // Two parts with ONE name, which is legal and common -- a scene full of
        // `Ground` is what an author actually builds.
        (void)make(fixture, "Part", "Ground", workspace);
        (void)make(fixture, "Part", "Ground", workspace);
    }

    core::InstanceId make(Reflection& fixture, std::string_view className, std::string_view name,
                          core::InstanceId parent)
    {
        const core::InstanceId id = world.create(fixture.classes.findId(fixture.atoms.intern(className)));
        world.setName(id, fixture.atoms.intern(name));
        if (parent.valid())
            (void)world.setParent(id, parent);
        return id;
    }
};

// The completions offered with the caret at the end of `source`, with no world
// at all -- the state something that knows the API and nothing else is in, and
// the state every case written before the tree existed asserted.
[[nodiscard]] std::vector<Completion> at(Reflection& fixture, std::string_view source)
{
    ScriptDocument document(source);
    const core::u32 last = document.lineCount() - 1;
    const Position caret{last, document.lineLength(last)};
    const CompletionRequest request = app::completionAt(document, caret);

    std::vector<Completion> out;
    app::collectCompletions(document, request, fixture.classes, fixture.atoms, app::CompletionWorld{}, out);
    return out;
}

// The same, against a tree. `self` is what `script` names.
[[nodiscard]] std::vector<Completion> at(Reflection& fixture, Tree& tree, std::string_view source,
                                         core::InstanceId self = core::InstanceId{})
{
    ScriptDocument document(source);
    const core::u32 last = document.lineCount() - 1;
    const Position caret{last, document.lineLength(last)};
    const CompletionRequest request = app::completionAt(document, caret);

    std::vector<Completion> out;
    const app::CompletionWorld world{&tree.world, tree.root, self};
    app::collectCompletions(document, request, fixture.classes, fixture.atoms, world, out);
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

TEST_CASE("a path through the tree offers what is inside it")
{
    Reflection fixture;
    Tree tree(fixture);

    const std::vector<Completion> rows = at(fixture, tree, "local c = Workspace.");
    const Completion* part = find(rows, "Baseplate");
    REQUIRE(part != nullptr);
    // The CLASS on the right, because "what is this thing" is the question a
    // list of bare names cannot answer and the one somebody actually has.
    CHECK(part->detail == "Part");
    CHECK(part->kind == CompletionKind::Instance);

    // Its members are still there: a child does not replace the class.
    CHECK(has(rows, "Name"));

    // **First**, which is the ordering decision. Somebody typing `Workspace.`
    // is reaching into their own tree far more often than for a class member,
    // and the members are one keystroke of filtering away either way.
    REQUIRE_FALSE(rows.empty());
    CHECK(rows.front().kind == CompletionKind::Instance);
}

TEST_CASE("the whole chain is walked, not just the name before the dot")
{
    Reflection fixture;
    Tree tree(fixture);

    // `Baseplate` on its own names nothing -- the same name under two parents
    // is two instances -- which is why the request carries the chain.
    CHECK(has(at(fixture, tree, "game.Workspace.Baseplate."), "Size"));

    ScriptDocument document("game.Workspace.Baseplate.");
    const CompletionRequest request = app::completionAt(document, Position{0, document.lineLength(0)});
    REQUIRE(request.path.size() == 3);
    CHECK(request.path[0] == "game");
    CHECK(request.path[1] == "Workspace");
    CHECK(request.path[2] == "Baseplate");
}

TEST_CASE("the lowercase workspace global resolves to the service")
{
    Reflection fixture;
    Tree tree(fixture);
    CHECK(has(at(fixture, tree, "workspace.Base"), "Baseplate"));
    // And any other service by its own name, because a service IS a child of
    // the DataModel -- so there is no list of service names in this file.
    CHECK(has(at(fixture, tree, "TagService."), "Name"));
}

TEST_CASE("script names the instance being edited")
{
    Reflection fixture;
    Tree tree(fixture);

    // `script.Parent` is `Workspace` when the script sits beside the baseplate.
    // `Parent` is the one property a path may step through, and this is why.
    const std::vector<Completion> rows = at(fixture, tree, "script.Parent.", tree.baseplate);
    CHECK(has(rows, "Baseplate"));
    CHECK(has(rows, "Level"));
}

TEST_CASE("a name in quotes is completed from the tree")
{
    Reflection fixture;
    Tree tree(fixture);

    const std::string source = "local c = Workspace:WaitForChild(\"Base";
    ScriptDocument document(source);
    const CompletionRequest request = app::completionAt(document, Position{0, document.lineLength(0)});
    CHECK(request.quoted == app::CompletionQuoted::Child);
    CHECK(request.prefix == "Base");
    // The range covers what is INSIDE the quotes and nothing else, so accepting
    // a row does not eat the quote that opened it.
    CHECK(request.replace.begin.column == 34);

    const std::vector<Completion> rows = at(fixture, tree, source);
    REQUIRE(rows.size() == 1);
    CHECK(rows.front().label == "Baseplate");

    // A dot reaches the same place: not everybody spells a call with a colon,
    // and neither form is a reason to stop answering.
    CHECK(has(at(fixture, tree, "Workspace.FindFirstChild(\"Lev"), "Level"));
}

TEST_CASE("an empty pair of quotes is the moment the list is worth the most")
{
    Reflection fixture;
    Tree tree(fixture);

    // Two children, nothing typed. Nothing about the class could ever answer
    // this, which is the whole argument for reading the world.
    const std::vector<Completion> rows = at(fixture, tree, "Workspace:WaitForChild(\"");
    // Three children by name and four by instance: the two called `Ground`
    // insert the same five characters, so they are one row. A list somebody
    // cannot choose between is a list that wastes their time.
    CHECK(rows.size() == 3);
    CHECK(has(rows, "Baseplate"));
    CHECK(has(rows, "Level"));
    CHECK(has(rows, "Ground"));
    CHECK(std::count_if(rows.begin(), rows.end(), [](const Completion& c) { return c.label == "Ground"; }) == 1);
}

TEST_CASE("GetService in quotes offers services and nothing else")
{
    Reflection fixture;
    Tree tree(fixture);

    const std::vector<Completion> rows = at(fixture, tree, "local s = game:GetService(\"Tag");
    REQUIRE_FALSE(rows.empty());
    CHECK(has(rows, "TagService"));
    for (const Completion& row : rows)
        CHECK(row.kind == CompletionKind::Service);

    // A `Part` is a class and not a service, so it is not offered here even
    // though it is offered wherever a class name is legal.
    CHECK_FALSE(has(at(fixture, tree, "game:GetService(\"Par"), "Part"));
}

TEST_CASE("a colon offers methods and not children")
{
    Reflection fixture;
    Tree tree(fixture);

    const std::vector<Completion> rows = at(fixture, tree, "Workspace:");
    CHECK(has(rows, "FindFirstChild"));
    // A child is not callable, and offering one after a colon would be offering
    // something that cannot work.
    CHECK_FALSE(has(rows, "Baseplate"));
}

TEST_CASE("a path that names nothing offers nothing")
{
    Reflection fixture;
    Tree tree(fixture);

    CHECK(at(fixture, tree, "Workspace.NoSuchThing.").empty());
    CHECK(at(fixture, tree, "Workspace:WaitForChild(\"Nope\").WaitForChild(\"").empty());
}

TEST_CASE("an ordinary string is not a completion site")
{
    Reflection fixture;
    Tree tree(fixture);

    // The caret is inside quotes, but the quotes are nobody's argument. Offering
    // the tree here would put a list over every string anybody ever types.
    const std::string source = "local greeting = \"Base";
    ScriptDocument document(source);
    const CompletionRequest request = app::completionAt(document, Position{0, document.lineLength(0)});
    CHECK(request.quoted == app::CompletionQuoted::Other);
    CHECK(at(fixture, tree, source).empty());
}
