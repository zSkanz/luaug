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
using app::CompletionWorld;
using app::Diagnostic;
using app::Position;
using app::ScriptDocument;
using app::Severity;

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
    core::InstanceId util;
    core::InstanceId literal;

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

        // A module in each of the two shapes almost every module ever written
        // takes. `Util` is the commoner one and the harder one to read.
        util = make(fixture, "ModuleScript", "Util", workspace);
        world.setProperty(util, fixture.atoms.intern("Source"),
                          scene::Value{std::string("local M = {}\n"
                                                   "M.Version = 1\n"
                                                   "function M.clamp(x) return x end\n"
                                                   "function M:reset() end\n"
                                                   "return M\n")});

        literal = make(fixture, "ModuleScript", "Config", workspace);
        world.setProperty(literal, fixture.atoms.intern("Source"),
                          scene::Value{std::string("return { Speed = 4, name = \"a\", start = function() end }\n")});
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

TEST_CASE("a dot offers members and children, because a dot reaches both (ADR 0078)")
{
    Reflection fixture;
    Tree tree(fixture);

    const std::vector<Completion> rows = at(fixture, tree, "local c = Workspace.");
    const Completion* child = find(rows, "Baseplate");
    REQUIRE(child != nullptr);
    CHECK(child->kind == CompletionKind::Instance);
    CHECK(child->detail == "Part");
    // The class's own members are still all here.
    CHECK(has(rows, "Name"));
    CHECK(has(rows, "FindFirstChild"));
}

TEST_CASE("children are offered where they can be typed, with their class beside them")
{
    Reflection fixture;
    Tree tree(fixture);

    // Inside the quotes of an accessor that takes a child's name. Both are
    // completed, because the rule is about where a NAME goes rather than about
    // which of the two somebody reached for.
    const std::vector<Completion> rows = at(fixture, tree, "Workspace:FindFirstChild(\"");
    const Completion* part = find(rows, "Baseplate");
    REQUIRE(part != nullptr);
    // The CLASS on the right, because "what is this thing" is the question a
    // list of bare names cannot answer and the one somebody actually has.
    CHECK(part->detail == "Part");
    CHECK(part->kind == CompletionKind::Instance);

    CHECK(find(at(fixture, tree, "Workspace:WaitForChild(\""), "Baseplate") != nullptr);
}

TEST_CASE("the whole chain is walked, not just the name before the dot")
{
    Reflection fixture;
    Tree tree(fixture);

    // `Baseplate` on its own names nothing -- the same name under two parents
    // is two instances -- which is why the request carries the chain.
    // Resolving `game.Workspace.Baseplate.` to a `Part` is what puts `Size` in
    // the list, and it is the same walk `FindFirstChild("` uses.
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
    // Asked through the accessor, because a dot offers no children (decision
    // 10). What is under test is that `workspace` and `Workspace` are one
    // thing, and the child list is how that is visible.
    CHECK(has(at(fixture, tree, "workspace:FindFirstChild(\"Base"), "Baseplate"));
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
    // Asked through the accessor for the reason decision 10 gives: a dot
    // resolves the path and answers with the class, and children are typed
    // inside `FindFirstChild("`.
    const std::vector<Completion> rows = at(fixture, tree, "script.Parent:FindFirstChild(\"", tree.baseplate);
    CHECK(has(rows, "Baseplate"));
    CHECK(has(rows, "Level"));

    // And the dot itself still WALKS that path -- it just answers with the
    // class rather than with the tree.
    CHECK(has(at(fixture, tree, "script.Parent.", tree.baseplate), "Name"));
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
    // Five children by name and six by instance: the two called `Ground` insert
    // the same six characters, so they are one row. A list somebody cannot
    // choose between is a list that wastes their time.
    CHECK(rows.size() == 5);
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

TEST_CASE("a call that names something is a step in a path")
{
    Reflection fixture;
    Tree tree(fixture);

    // The shape at the top of most Luau files ever written, and without this
    // every one of them completes nothing from the line after it. What proves
    // the call was FOLLOWED is a member of the thing it returned -- children are
    // not offered under a dot (decision 10), so `Name` is the evidence and
    // `Baseplate` is asked for through the accessor beside it.
    CHECK(has(at(fixture, tree, "game:GetService(\"Workspace\")."), "Name"));
    CHECK(has(at(fixture, tree, "game:GetService(\"Workspace\"):FindFirstChild(\""), "Baseplate"));

    // And the same for the two that name a child.
    CHECK(has(at(fixture, tree, "Workspace:WaitForChild(\"Level\")."), "Name"));
    CHECK(has(at(fixture, tree, "game.Workspace:FindFirstChild(\"Baseplate\")."), "Size"));

    ScriptDocument document("game:GetService(\"Workspace\").");
    const CompletionRequest request = app::completionAt(document, Position{0, document.lineLength(0)});
    REQUIRE(request.path.size() == 2);
    CHECK(request.path[0] == "game");
    CHECK(request.path[1] == "Workspace");
}

TEST_CASE("a call this file cannot read stops the path rather than guessing")
{
    Reflection fixture;
    Tree tree(fixture);

    // An expression inside the brackets is an expression, and reading one would
    // be the type inference ADR 0057 says this does not do.
    CHECK(at(fixture, tree, "game:GetService(name).").empty());
    CHECK(at(fixture, tree, "Workspace:GetChildren().").empty());
}

TEST_CASE("a local assigned a path completes from what it was assigned")
{
    Reflection fixture;
    Tree tree(fixture);

    const std::string source = "local Tags = game:GetService(\"TagService\")\nTags.";
    CHECK(has(at(fixture, tree, source), "Name"));

    // Two hops, because `local a = X` followed by `local b = a.Y` is ordinary
    // code and stopping at one would be an arbitrary place to stop.
    const std::string twice = "local W = game:GetService(\"Workspace\")\nlocal G = W.Level\nG.";
    CHECK(has(at(fixture, tree, twice), "Name"));

    // A local assigned something this cannot read is a local this cannot
    // follow, and it says so by offering nothing rather than by guessing.
    CHECK(at(fixture, tree, "local X = someFunction()\nX.").empty());
}

TEST_CASE("a local whose assignment is commented does not derail the walk")
{
    Reflection fixture;
    Tree tree(fixture);

    const std::string source = "local W = game:GetService(\"Workspace\") -- the world\nW.";
    CHECK(has(at(fixture, tree, source), "Name"));
    // And through the accessor, which is where a child's name is typed.
    const std::string reached = "local W = game:GetService(\"Workspace\") -- the world\nW:FindFirstChild(\"";
    CHECK(has(at(fixture, tree, reached), "Baseplate"));
}

TEST_CASE("Luau's own libraries are offered, and from the pin rather than a guess")
{
    Reflection fixture;

    const std::vector<Completion> maths = at(fixture, "local x = math.");
    CHECK(has(maths, "floor"));
    CHECK(has(maths, "clamp"));
    // A constant is not a function, and the right-hand column says which. That
    // distinction is the reason `stdlib.h` carries a type at all.
    const Completion* pi = find(maths, "pi");
    REQUIRE(pi != nullptr);
    CHECK(pi->detail == "number");
    CHECK(pi->kind == CompletionKind::Library);

    CHECK(has(at(fixture, "string."), "format"));
    CHECK(has(at(fixture, "table.cr"), "create"));
    CHECK(has(at(fixture, "buffer.readf"), "readf32"));
    CHECK(has(at(fixture, "coroutine."), "yield"));
    CHECK(has(at(fixture, "utf8."), "charpattern"));
    CHECK(has(at(fixture, "bit32.b"), "band"));
    CHECK(has(at(fixture, "vector."), "magnitude"));
    CHECK(has(at(fixture, "debug."), "traceback"));

    // **The engine's own scheduler, which is not Luau's** and therefore not in
    // the list the VM is checked against.
    CHECK(has(at(fixture, "task."), "wait"));
}

TEST_CASE("os is this engine's os and not the one Luau ships")
{
    Reflection fixture;

    const std::vector<Completion> rows = at(fixture, "os.");
    CHECK(has(rows, "clock"));
    CHECK(has(rows, "time"));
    CHECK(has(rows, "date"));
    // Taken off by `removeUnsafeGlobals`. Offering it would be offering
    // something no script in this engine can call.
    CHECK_FALSE(has(rows, "difftime"));
    CHECK(rows.size() == 3);
}

TEST_CASE("the free globals include Luau's, not only the engine's")
{
    Reflection fixture;

    CHECK(has(at(fixture, "typeo"), "typeof"));
    CHECK(has(at(fixture, "pca"), "pcall"));
    CHECK(has(at(fixture, "asse"), "assert"));
    CHECK(has(at(fixture, "setmet"), "setmetatable"));
    // The engine's, beside them.
    CHECK(has(at(fixture, "wor"), "workspace"));
    CHECK(has(at(fixture, "pri"), "print"));
    // And the library tables themselves, so a prefix finds the namespace.
    CHECK(has(at(fixture, "mat"), "math"));

    // **Absent because the sandbox removes them** (`sandbox.cpp`). A completion
    // that offered `getfenv` would be offering a name that is nil, and worse:
    // mentioning either of those two disables `safeenv` for the whole module.
    CHECK_FALSE(has(at(fixture, "getf"), "getfenv"));
    CHECK_FALSE(has(at(fixture, "loads"), "loadstring"));
    CHECK_FALSE(has(at(fixture, "spa"), "spawn"));
}

TEST_CASE("a library is answered with no world at all")
{
    // The half that has nothing to do with a project: `math.` is `math.`
    // whether or not anything is open, and the earlier cases prove it by using
    // the world-free `at` overload throughout.
    Reflection fixture;
    Tree tree(fixture);
    CHECK(has(at(fixture, tree, "math.fl"), "floor"));
    // And a colon is not how a library is reached, so it offers nothing rather
    // than pretending `math` is an object.
    CHECK(at(fixture, tree, "math:fl").empty());
}

TEST_CASE("a required module completes to what it hands back")
{
    Reflection fixture;
    Tree tree(fixture);

    // The shape most modules take: a local filled in and returned.
    const std::string real = "local U = require(script.Parent.Util)\nU.";
    const std::vector<Completion> rows = at(fixture, tree, real, tree.baseplate);
    CHECK(has(rows, "Version"));
    CHECK(has(rows, "clamp"));
    CHECK(has(rows, "reset"));

    const Completion* clamp = find(rows, "clamp");
    REQUIRE(clamp != nullptr);
    CHECK(clamp->detail == "function");
    CHECK(clamp->kind == CompletionKind::Module);

    const Completion* version = find(rows, "Version");
    REQUIRE(version != nullptr);
    CHECK(version->detail == "number");

    // **What it is NOT.** `U` is the table the module returned, so offering the
    // ModuleScript's own properties there would be describing a different
    // object entirely.
    CHECK_FALSE(has(rows, "Source"));
    CHECK_FALSE(has(rows, "Parent"));
}

TEST_CASE("a module that returns a table written out completes the same way")
{
    Reflection fixture;
    Tree tree(fixture);

    const std::vector<Completion> rows =
        at(fixture, tree, "local C = require(script.Parent.Config)\nC.", tree.baseplate);
    REQUIRE(has(rows, "Speed"));
    REQUIRE(has(rows, "name"));
    REQUIRE(has(rows, "start"));
    CHECK(find(rows, "Speed")->detail == "number");
    CHECK(find(rows, "name")->detail == "string");
    CHECK(find(rows, "start")->detail == "function");
}

TEST_CASE("the instance itself still completes as an instance")
{
    // Without the `require`, `script.Parent.Util` is a ModuleScript and nothing
    // else -- so it gets the class's members, which is the honest answer.
    Reflection fixture;
    Tree tree(fixture);

    const std::vector<Completion> rows = at(fixture, tree, "local M = script.Parent.Util\nM.", tree.baseplate);
    CHECK(has(rows, "Source"));
    CHECK_FALSE(has(rows, "clamp"));
}

TEST_CASE("a module this cannot read answers nothing rather than guessing")
{
    Reflection fixture;
    Tree tree(fixture);

    // A require of something that is not a module in this tree.
    CHECK(at(fixture, tree, "local X = require(script.Parent.NoSuchModule)\nX.", tree.baseplate).empty());
}

// --- Dot access to a live child, at edit time (decision 10) ------------------
//
// The third of three instruments for one fact. The runtime raises a message
// naming the child; the completion no longer offers one under a dot; this
// underlines it while somebody is typing.
//
// **What matters most here is what it does NOT report.** The lint pass in
// `script_syntax.cpp` says in its own comment that it has no false positives to
// apologise for, and a warning people learn to ignore has made every other
// warning worth less. Half of these are cases that must stay silent.

namespace {

[[nodiscard]] std::vector<Diagnostic> lintOf(Reflection& fixture, Tree& tree, const std::string& source)
{
    ScriptDocument document(source);
    std::vector<Diagnostic> out;
    app::lintInstanceAccess(document, fixture.classes, fixture.atoms,
                            CompletionWorld{&tree.world, tree.root, core::InstanceId{}}, out);
    return out;
}

[[nodiscard]] bool mentions(const std::vector<Diagnostic>& out, std::string_view needle)
{
    for (const Diagnostic& diagnostic : out) {
        if (diagnostic.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("reading a child with a dot is not underlined, because it works (ADR 0078)")
{
    Reflection fixture;
    Tree tree(fixture);

    CHECK(lintOf(fixture, tree, "local x = workspace.Baseplate").empty());
    CHECK(lintOf(fixture, tree, "if workspace.Baseplate == nil then end").empty());
}

TEST_CASE("assigning to a child's name is underlined, and told why")
{
    Reflection fixture;
    Tree tree(fixture);

    const std::vector<Diagnostic> out = lintOf(fixture, tree, "workspace.Baseplate = nil");
    REQUIRE(out.size() == 1);
    CHECK(mentions(out, "Baseplate"));
    CHECK(mentions(out, "parenting"));
    CHECK(out.front().severity == Severity::Warning);
}

TEST_CASE("a declared member is never underlined, even when a child shares its name")
{
    Reflection fixture;
    Tree tree(fixture);

    // The runtime resolves a property before it looks for a child, so a child
    // called `Name` does not shadow `Name` -- and underlining one here would
    // mark a line that works.
    CHECK(lintOf(fixture, tree, "local n = workspace.Name").empty());
    CHECK(lintOf(fixture, tree, "local f = workspace.FindFirstChild").empty());
}

TEST_CASE("a misspelled property is left to the type checker")
{
    Reflection fixture;
    Tree tree(fixture);

    // `Positon` is nothing at all: not a member, not a child. Guessing at it
    // here would be the false positive this pass is written to avoid, and
    // `luau-analyze` types this file -- ADR 0018 keeps inference there.
    CHECK(lintOf(fixture, tree, "local p = workspace.Positon").empty());
}

TEST_CASE("a plain table is not an instance and is not underlined")
{
    Reflection fixture;
    Tree tree(fixture);

    // The case that makes an AST-only version of this lint impossible: without
    // resolving the path, `t.Baseplate` on a table is indistinguishable from
    // `workspace.Baseplate` on the service.
    CHECK(lintOf(fixture, tree, "local t = {}\nlocal x = t.Baseplate").empty());
    CHECK(lintOf(fixture, tree, "local s = string.format").empty());
}

TEST_CASE("a path through a local is followed, because that is how people write it")
{
    Reflection fixture;
    Tree tree(fixture);

    const std::string source = "local W = game:GetService(\"Workspace\")\nW.Baseplate = nil";
    CHECK(mentions(lintOf(fixture, tree, source), "Baseplate"));
}

TEST_CASE("a name inside quotes is where a child belongs and is not underlined")
{
    Reflection fixture;
    Tree tree(fixture);

    CHECK(lintOf(fixture, tree, "local b = workspace:FindFirstChild(\"Baseplate\")").empty());
}

TEST_CASE("a name is offered once, whether or not the file already uses it")
{
    // **The owner's report**: `print("Hello world!")` on one line and `pr` on
    // the next offered `print` twice, "in this file" and "global".
    Reflection fixture;
    const std::vector<Completion> list = at(fixture, "print(\"Hello world!\")\npr");
    CHECK(std::count_if(list.begin(), list.end(), [](const Completion& c) { return c.label == "print"; }) == 1);
    const Completion* print = find(list, "print");
    REQUIRE(print != nullptr);
    CHECK(print->detail != "in this file");
}

TEST_CASE("a call with a string argument and no parentheses offers nothing inside the string")
{
    // `print"olá"` is a call in Luau; inside its string there is nothing to
    // complete, accented or not, and nothing to complete after it either.
    Reflection fixture;
    CHECK(at(fixture, "print\"ol").empty());
    CHECK(at(fixture, "print\"ol\xC3\xA1").empty());
    // After the closing quote the request is an empty word with nothing it
    // hangs off, which the pane does not open a list for.
    ScriptDocument closed("print\"ol\xC3\xA1\"");
    const CompletionRequest after = app::completionAt(closed, Position{0, closed.lineLength(0)});
    CHECK(after.prefix.empty());
    CHECK(after.subject.empty());
    CHECK(after.quoted == app::CompletionQuoted::No);
}

namespace {

// The owner's friend's file, which is what these cases are held to.
constexpr std::string_view kSnake = R"(local RunService = game:GetService("RunService")

type Snake = {
    Body: { BasePart }
}

local Snake = {}
Snake.__index = Snake

function Snake.new(): Snake
    local snake = setmetatable({
        Body = {},
    }, Snake)
    return snake
end

function Snake:Grow()
    local tail = self.Body[#self.Body]
    tail.
end

function Snake:Update()
end

local TheLifeSnake = Snake.new()
)";

[[nodiscard]] std::vector<Completion> completeAfter(Reflection& fixture, std::string source, std::string_view typed)
{
    // Appended at the end, where the caret is, on a line of its own.
    source += typed;
    return at(fixture, source);
}

} // namespace

TEST_CASE("a table the file fills in offers what the file put in it")
{
    // **The owner's report**: `Snake.` offered nothing.
    Reflection fixture;
    const std::vector<Completion> list = completeAfter(fixture, std::string(kSnake), "Snake.");
    CHECK(has(list, "new"));
    CHECK(has(list, "Grow"));
    CHECK(has(list, "Update"));
    CHECK(has(list, "__index"));
}

TEST_CASE("a value made by the table's constructor offers its methods and the fields its type declares")
{
    Reflection fixture;
    const std::vector<Completion> dotted = completeAfter(fixture, std::string(kSnake), "TheLifeSnake.");
    CHECK(has(dotted, "Body"));
    const std::vector<Completion> called = completeAfter(fixture, std::string(kSnake), "TheLifeSnake:");
    CHECK(has(called, "Grow"));
    CHECK(has(called, "Update"));
    // After a colon, only what can be called.
    CHECK_FALSE(has(called, "Body"));
}

TEST_CASE("self inside a method is an instance, and a list type's element is its class")
{
    Reflection fixture;
    std::string source(kSnake);
    // `self.` on the line inside Grow, where the caret would be.
    const std::size_t inside = source.find("    tail.");
    REQUIRE(inside != std::string::npos);

    ScriptDocument selfDoc(source.substr(0, inside) + "    self.");
    const core::u32 last = selfDoc.lineCount() - 1;
    const CompletionRequest selfRequest = app::completionAt(selfDoc, Position{last, selfDoc.lineLength(last)});
    std::vector<Completion> selfList;
    app::collectCompletions(selfDoc, selfRequest, fixture.classes, fixture.atoms, app::CompletionWorld{}, selfList);
    CHECK(has(selfList, "Body"));
    CHECK(has(selfList, "Grow"));

    // `tail` is `self.Body[#self.Body]`, and `Body` is `{ BasePart }`.
    ScriptDocument tailDoc(source.substr(0, inside) + "    tail.");
    const core::u32 tailLine = tailDoc.lineCount() - 1;
    const CompletionRequest tailRequest = app::completionAt(tailDoc, Position{tailLine, tailDoc.lineLength(tailLine)});
    std::vector<Completion> tailList;
    app::collectCompletions(tailDoc, tailRequest, fixture.classes, fixture.atoms, app::CompletionWorld{}, tailList);
    CHECK(has(tailList, "CFrame"));
    CHECK(has(tailList, "Anchored"));
}

TEST_CASE("an index is a step: a list's element offers its class's members")
{
    // **The owner's report**: `Snake.Body[1].` offered nothing although the
    // file says `Body: { BasePart }`.
    Reflection fixture;
    const std::vector<Completion> list = completeAfter(fixture, std::string(kSnake), "Snake.Body[1].");
    CHECK(has(list, "CFrame"));
    CHECK(has(list, "Anchored"));
    const std::vector<Completion> nested =
        completeAfter(fixture, std::string(kSnake), "TheLifeSnake.Body[#TheLifeSnake.Body].");
    CHECK(has(nested, "CFrame"));

    // And the request reads the brackets as a step.
    ScriptDocument document("local p = Snake.Body[1].Po");
    const CompletionRequest request = app::completionAt(document, Position{0, document.lineLength(0)});
    REQUIRE(request.path.size() == 3);
    CHECK(request.path[0] == "Snake");
    CHECK(request.path[1] == "Body");
    CHECK(request.path[2] == "[]");
    CHECK(request.prefix == "Po");
}

TEST_CASE("the checker's rows lead, the tree's children stay, and a type position is the checker's alone")
{
    using luaug::app::Completion;
    using luaug::app::CompletionKind;
    // What the tree and the file found: a live child, and a name the checker
    // also knows.
    const std::vector<Completion> found{
        Completion{"Spinner", "Part", "", CompletionKind::Instance},
        Completion{"Name", "string", "", CompletionKind::Property},
    };
    const std::vector<Completion> checked{
        Completion{"Name", "string", "the checker's", CompletionKind::Property},
        Completion{"Parent", "Instance?", "", CompletionKind::Property},
        Completion{"Destroy", "(self) -> ()", "", CompletionKind::Method},
    };

    std::vector<Completion> shown = found;
    luaug::app::mergeCompletions(shown, checked, false, "");
    REQUIRE(shown.size() == 4);
    CHECK(shown.front().label == "Spinner"); // a child still sorts first
    const auto row = [&shown](std::string_view label) {
        return std::find_if(shown.begin(), shown.end(), [label](const Completion& c) { return c.label == label; });
    };
    REQUIRE(row("Name") != shown.end());
    CHECK(row("Name")->doc == "the checker's");

    // Filtered by what is typed.
    shown = found;
    luaug::app::mergeCompletions(shown, checked, false, "Pa");
    REQUIRE(shown.size() == 1);
    CHECK(shown.front().label == "Parent");

    // Nothing from the checker leaves the list as it was...
    shown = found;
    luaug::app::mergeCompletions(shown, {}, false, "");
    CHECK(shown.size() == 2);
    // ...except where a type is written: there it is the whole answer.
    shown = found;
    luaug::app::mergeCompletions(shown, {}, true, "");
    CHECK(shown.empty());
}

TEST_CASE("a word already whole closes the list, and one letter more opens it again")
{
    // **The owner**: with `Part` and `Part2D` both offered, `Part` typed is
    // done -- `Part2D` is worth offering once `Part2` is.
    Reflection fixture;

    const std::vector<Completion> whole = at(fixture, "Part");
    REQUIRE(has(whole, "Part"));
    REQUIRE(has(whole, "Part2D"));
    CHECK(app::completesExactly(whole, "Part"));

    const std::vector<Completion> longer = at(fixture, "Part2");
    CHECK(has(longer, "Part2D"));
    CHECK_FALSE(app::completesExactly(longer, "Part2"));

    // Case counts here, where it does not for filtering: `part` is not yet
    // `Part`, and the list is how it gets there.
    const std::vector<Completion> lower = at(fixture, "part");
    CHECK(has(lower, "Part"));
    CHECK_FALSE(app::completesExactly(lower, "part"));

    // Nothing typed is never whole.
    CHECK_FALSE(app::completesExactly(whole, ""));

    // The checker's answer, which arrives a frame later, closes it the same way.
    std::vector<Completion> shown{Completion{"Part2D", "", "", CompletionKind::Class}};
    const std::vector<Completion> checked{Completion{"Part", "", "", CompletionKind::Class},
                                          Completion{"Part2D", "", "", CompletionKind::Class}};
    app::mergeCompletions(shown, checked, false, "Part");
    CHECK(shown.empty());
}

TEST_CASE("the list is in scope first, then the rest of the file, then the globals")
{
    // **The owner**: "scope, then out of scope, then global" -- and where a name
    // stands changes its place in the list, never whether it is there.
    Reflection fixture;
    const std::vector<Completion> list = at(fixture, "local function build()\n"
                                                     "\tlocal matrix = {}\n"
                                                     "end\n"
                                                     "local Matter = 1\n"
                                                     "ma");
    const auto position = [&list](std::string_view label) {
        const auto found =
            std::find_if(list.begin(), list.end(), [&label](const Completion& c) { return c.label == label; });
        return found == list.end() ? list.size() : static_cast<std::size_t>(found - list.begin());
    };
    REQUIRE(position("Matter") < list.size());
    REQUIRE(position("matrix") < list.size());
    REQUIRE(position("math") < list.size());
    CHECK(position("Matter") < position("matrix"));
    CHECK(position("matrix") < position("math"));
}

TEST_CASE("a name that does not exist yet is not offered")
{
    // **The owner's report**: `local ServerInfo = require(Ser|)` offered
    // `ServerInfo` -- the local that very line is still declaring -- and a name
    // declared further down the file is no more real at the caret.
    Reflection fixture;
    const std::vector<Completion> list = at(fixture, "local ServerInfo = require(Ser");
    CHECK(find(list, "ServerInfo") == nullptr);

    ScriptDocument document("local x = Lat\nlocal Later = 1\n");
    const CompletionRequest request = app::completionAt(document, Position{0, document.lineLength(0)});
    std::vector<Completion> later;
    app::collectCompletions(document, request, fixture.classes, fixture.atoms, app::CompletionWorld{}, later);
    CHECK(find(later, "Later") == nullptr);

    // And the second report: `map`, a local of a loop further down, offered
    // while typing `Map.` above it.
    ScriptDocument loop("function m:Spawn()\n"
                        "\tlocal root = Ma\n"
                        "\tfor i = 1, 3 do\n"
                        "\t\tlocal map = 1\n"
                        "\tend\n"
                        "end\n");
    const CompletionRequest above = app::completionAt(loop, Position{1, loop.lineLength(1)});
    std::vector<Completion> offered;
    app::collectCompletions(loop, above, fixture.classes, fixture.atoms, app::CompletionWorld{}, offered);
    CHECK(find(offered, "map") == nullptr);
}

TEST_CASE("the likely word first: its own case, then the shortest")
{
    // **The owner**: "`els` offered `elseif` first ... it should make things
    // easier" -- and "this applies to everything".
    Reflection fixture;
    const std::vector<Completion> keywords = at(fixture, "if a then\nels");
    REQUIRE(keywords.size() >= 2);
    CHECK(keywords[0].label == "else");
    CHECK(keywords[1].label == "elseif");

    // A file's own names: the shorter one that finishes the word comes first.
    const std::vector<Completion> names = at(fixture, "local position = 1\nlocal pos = 2\nprint(position, pos)\npo");
    const auto place = [&names](std::string_view label) {
        const auto found =
            std::find_if(names.begin(), names.end(), [&label](const Completion& c) { return c.label == label; });
        return static_cast<std::size_t>(found - names.begin());
    };
    REQUIRE(place("pos") < names.size());
    REQUIRE(place("position") < names.size());
    CHECK(place("pos") < place("position"));
}

TEST_CASE("a string that names something the engine knows is completed, in the string")
{
    // **The owner**: `Instance.new("AASS")` should be completed inside the
    // quotes, "and the same for other things".
    Reflection fixture;
    Tree tree(fixture);
    (void)tree.world.setAttribute(tree.baseplate, fixture.atoms.intern("Health"), scene::Value{true});
    (void)tree.world.addTag(tree.baseplate, fixture.atoms.intern("Enemy"));

    // A class a person may create -- and not an abstract one.
    const std::vector<Completion> made = at(fixture, tree, "local p = Instance.new(\"Pa");
    CHECK(has(made, "Part"));
    CHECK(has(made, "Part2D"));
    CHECK_FALSE(has(made, "BasePart"));
    CHECK_FALSE(has(made, "Workspace"));

    // Any class, where the question is what something IS.
    CHECK(has(at(fixture, tree, "if hit:IsA(\"BaseP"), "BasePart"));
    CHECK(has(at(fixture, tree, "workspace:FindFirstChildOfClass(\"Pa"), "Part"));

    // A property of what the call hangs off, and only properties.
    const std::vector<Completion> watched = at(fixture, tree, "workspace.Baseplate:GetPropertyChangedSignal(\"");
    CHECK(has(watched, "Name"));
    CHECK_FALSE(has(watched, "Destroy"));

    // An attribute the instance has, a tag the world uses, an ancestor's name.
    CHECK(has(at(fixture, tree, "workspace.Baseplate:GetAttribute(\""), "Health"));
    CHECK(has(at(fixture, tree, "workspace.Baseplate:HasTag(\""), "Enemy"));
    CHECK(has(at(fixture, tree, "TagService:GetTagged(\"En"), "Enemy"));
    CHECK(has(at(fixture, tree, "workspace.Baseplate:FindFirstAncestor(\""), "Workspace"));

    // Free text stays free: a message is nobody's name.
    CHECK(at(fixture, tree, "print(\"Pa").empty());
}

TEST_CASE("a name is found with letters missing, after the names that begin with what was typed")
{
    // **The owner**: `pint "ola"` should still offer `print`.
    Reflection fixture;
    const std::vector<Completion> typo = at(fixture, "pint");
    CHECK(has(typo, "print"));
    // The first letter has to be the name's: `rint` is not `print`.
    CHECK_FALSE(has(at(fixture, "rint"), "print"));
    // A prefix is the better answer and comes first.
    const std::vector<Completion> begun = at(fixture, "pr");
    REQUIRE_FALSE(begun.empty());
    CHECK(begun.front().label.starts_with("pr"));
}

TEST_CASE("a string that begins asset: completes to the project's files")
{
    // **The owner**: "autocomplete for asset paths from the content".
    Reflection fixture;
    const std::vector<std::string> files{"sounds/hit.ogg", "textures/dirt.png", "textures/grass_top.png"};
    const auto with = [&](std::string_view source) {
        ScriptDocument document(source);
        const core::u32 last = document.lineCount() - 1;
        const CompletionRequest request = app::completionAt(document, Position{last, document.lineLength(last)});
        std::vector<Completion> out;
        app::CompletionWorld world;
        world.assets = files;
        app::collectCompletions(document, request, fixture.classes, fixture.atoms, world, out);
        return out;
    };

    const std::vector<Completion> assigned = with("sky.SkyboxBack = \"asset://tex");
    CHECK(has(assigned, "asset://textures/dirt.png"));
    CHECK(has(assigned, "asset://textures/grass_top.png"));
    CHECK_FALSE(has(assigned, "asset://sounds/hit.ogg"));
    // Inside a call, and on the way to the scheme.
    CHECK(has(with("voxels:SetBlockTextures(1, \"asset:"), "asset://sounds/hit.ogg"));
    CHECK(has(with("local path = \"as"), "asset://textures/dirt.png"));
    // A string that is somebody else's stays theirs.
    CHECK_FALSE(has(with("game:GetService(\"as"), "asset://textures/dirt.png"));
}
