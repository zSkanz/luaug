// The script editor's language service (ADR 0093): Luau's checker over the
// engine's definitions and the scripts in the tree.
#include "luaug/app/language_service.h"
#include "luaug/platform/file.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <string_view>

#include "inspector_fixture.h"

using namespace luaug;
using app::Completion;
using app::LanguageCore;
using app::LanguageTree;
using app::Position;

namespace {

[[nodiscard]] std::string definitions()
{
    const std::filesystem::path path =
        std::filesystem::path(LUAUG_TEST_CATALOG).parent_path().parent_path() / "runtime" / "types" / "engine.d.luau";
    std::string text;
    REQUIRE(platform::readTextFile(path, text));
    return text;
}

// A tree by hand: `game`, its services, and scripts in them.
struct TreeBuilder
{
    LanguageTree tree;

    TreeBuilder()
    {
        LanguageTree::Node game;
        game.name = "game";
        game.className = "DataModel";
        game.path = "game";
        tree.nodes.push_back(game);
    }

    core::u32 add(core::u32 parent, std::string name, std::string className, std::string source = {})
    {
        LanguageTree::Node node;
        node.name = std::move(name);
        node.className = std::move(className);
        node.parent = static_cast<core::i32>(parent);
        node.path = tree.nodes[parent].path + "." + node.name;
        node.script = node.className == "Script" || node.className == "ModuleScript";
        node.module = node.className == "ModuleScript";
        node.source = std::move(source);
        const auto index = static_cast<core::u32>(tree.nodes.size());
        tree.nodes[parent].children.push_back(index);
        tree.nodes.push_back(std::move(node));
        return index;
    }
};

// The line and column just past the first `marker` in `source`, which the
// marker is then removed from.
[[nodiscard]] Position caretAt(std::string& source, std::string_view marker)
{
    const std::size_t at = source.find(marker);
    REQUIRE(at != std::string::npos);
    source.erase(at, marker.size());
    Position caret;
    for (std::size_t index = 0; index < at; ++index) {
        if (source[index] == '\n') {
            ++caret.line;
            caret.column = 0;
        }
        else {
            ++caret.column;
        }
    }
    return caret;
}

[[nodiscard]] bool offers(const app::LanguageCompletions& found, std::string_view label)
{
    const std::vector<Completion>& list = found.items;
    return std::any_of(list.begin(), list.end(), [label](const Completion& row) { return row.label == label; });
}

const std::string_view SnakeModule = R"(--!strict
local Snake = {}
Snake.__index = Snake

export type Snake = typeof(setmetatable({} :: { Length: number }, Snake))

function Snake.new(): Snake
    return setmetatable({ Length = 3 }, Snake)
end

function Snake.Grow(self: Snake, by: number)
    self.Length += by
end

return Snake
)";

} // namespace

TEST_CASE("the engine's definitions load into Luau's checker")
{
    const LanguageCore core(definitions());
    CHECK_MESSAGE(core.loadError().empty(), core.loadError());
}

TEST_CASE("a type a required module defines is known in the script that requires it")
{
    // **The owner's report**: a `Snake` in a ModuleScript in ReplicatedStorage
    // was not typed in the script requiring it.
    LanguageCore core(definitions());
    TreeBuilder builder;
    const core::u32 storage = builder.add(0, "ReplicatedStorage", "ReplicatedStorage");
    (void)builder.add(storage, "Snake", "ModuleScript", std::string(SnakeModule));
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    std::string main = R"(--!strict
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local Snake = require(ReplicatedStorage:WaitForChild("Snake"))
local snake = Snake.new()
snake.|
)";
    const Position caret = caretAt(main, "|");
    (void)builder.add(service, "Main", "Script", main);
    core.update(builder.tree);

    const app::LanguageCompletions members = core.complete("game.ScriptService.Main", caret);
    CHECK(offers(members, "Length"));
    CHECK(offers(members, "Grow"));

    // And a `script.Parent` walk reaches it too.
    TreeBuilder sibling;
    const core::u32 folder = sibling.add(0, "ReplicatedStorage", "ReplicatedStorage");
    (void)sibling.add(folder, "Snake", "ModuleScript", std::string(SnakeModule));
    std::string near = "local Snake = require(script.Parent.Snake)\nlocal s = Snake.new()\ns.|\n";
    const Position nearCaret = caretAt(near, "|");
    (void)sibling.add(folder, "User", "ModuleScript", near);
    core.update(sibling.tree);
    CHECK(offers(core.complete("game.ReplicatedStorage.User", nearCaret), "Length"));
}

TEST_CASE("Signal is typed, made by a script or an event")
{
    // **The owner's report**: "o Signal não está tipado".
    LanguageCore core(definitions());
    TreeBuilder builder;
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    std::string made = "local changed = Signal.new()\nchanged:|\n";
    const Position madeCaret = caretAt(made, "|");
    (void)builder.add(service, "Made", "Script", made);
    std::string event = "workspace.ChildAdded:|\n";
    const Position eventCaret = caretAt(event, "|");
    (void)builder.add(service, "Event", "Script", event);
    core.update(builder.tree);

    const app::LanguageCompletions own = core.complete("game.ScriptService.Made", madeCaret);
    CHECK(offers(own, "Connect"));
    CHECK(offers(own, "Fire"));
    CHECK(offers(core.complete("game.ScriptService.Event", eventCaret), "Connect"));
}

TEST_CASE("signature help names the parameters and the one being typed")
{
    LanguageCore core(definitions());
    TreeBuilder builder;
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    std::string source = "local c = Color3.fromRGB(10, |)\n";
    const Position caret = caretAt(source, "|");
    (void)builder.add(service, "Main", "Script", source);
    core.update(builder.tree);

    const std::optional<app::SignatureHelp> help = core.signature("game.ScriptService.Main", caret);
    REQUIRE(help.has_value());
    CHECK(help->label.find("fromRGB(") == 0);
    CHECK(help->parameters.size() == 3);
    CHECK(help->active == 1);
    CHECK_FALSE(help->doc.empty());
}

TEST_CASE("type errors are reported, and a child reached by name is not one")
{
    LanguageCore core(definitions());
    TreeBuilder builder;
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    (void)builder.add(service, "Main", "Script",
                      "--!strict\nlocal n: number = \"text\"\nlocal level = workspace.Level\nprint(n, level)\n");
    core.update(builder.tree);

    const app::LanguageCheck check = core.check("game.ScriptService.Main");
    REQUIRE(check.diagnostics.size() == 1);
    CHECK(check.diagnostics.front().at.line == 1);
}

TEST_CASE("a module cast to the type it promises is not an error, as in the reference's checker")
{
    // **The owner's module**: `return NN :: { ... }` where a function's return
    // did not match. The new solver -- the reference editor's -- accepts that
    // cast in a nonstrict script and in a strict one; the old solver flagged
    // it, which is the difference the owner saw.
    const std::string body = "local NN = {}\n"
                             "function NN.make(): number return 1 end\n"
                             "return NN :: { make: () -> string }\n";
    LanguageCore core(definitions());
    TreeBuilder builder;
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    (void)builder.add(service, "Loose", "ModuleScript", body);
    (void)builder.add(service, "Strict", "ModuleScript", "--!strict\n" + body);
    core.update(builder.tree);

    CHECK(core.check("game.ScriptService.Loose").diagnostics.empty());
    CHECK(core.check("game.ScriptService.Strict").diagnostics.empty());
}

TEST_CASE("a field written twice is a warning, in a table type and in a table")
{
    // **The owner**: "it should not let me define the same thing twice".
    LanguageCore core(definitions());
    TreeBuilder builder;
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    (void)builder.add(service, "Main", "Script",
                      "type T = {\n\tweights: number,\n\tweights: number,\n}\nlocal t = { a = 1, a = 2 }\nprint(t)\n");
    core.update(builder.tree);

    const app::LanguageCheck check = core.check("game.ScriptService.Main");
    const auto duplicateAt = [&check](core::u32 line) {
        return std::any_of(check.diagnostics.begin(), check.diagnostics.end(), [line](const app::Diagnostic& d) {
            return d.at.line == line && d.message.find("duplicate") != std::string::npos;
        });
    };
    CHECK(duplicateAt(2));
    CHECK(duplicateAt(4));
}

TEST_CASE("a vector has what a Vector3 has: X, Y, Z, Magnitude and the methods")
{
    // **The owner's report**: `size.X` on a part's `Size` was "key 'X' not
    // found in external type 'vector'".
    LanguageCore core(definitions());
    TreeBuilder builder;
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    (void)builder.add(service, "Main", "Script",
                      "--!strict\nlocal size = Vector3.new(1, 2, 3)\n"
                      "local a: number = size.X + size.y + size.Z + size.Magnitude\n"
                      "local b: number = size:Dot(size.Unit)\nprint(a, b)\n");
    core.update(builder.tree);

    const app::LanguageCheck check = core.check("game.ScriptService.Main");
    for (const app::Diagnostic& diagnostic : check.diagnostics)
        MESSAGE(diagnostic.message);
    CHECK(check.diagnostics.empty());
}

TEST_CASE("the tree a require walks is the scripts and their ancestors")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1u);
    const core::InstanceId root = fixture.widget(world, "Root");
    const auto make = [&](scene::ClassId cls, std::string_view name, core::InstanceId parent) {
        const core::InstanceId id = world.create(cls);
        world.setName(id, fixture.atom(name));
        REQUIRE_FALSE(world.setParent(id, parent).has_value());
        return id;
    };
    const core::InstanceId folder = make(fixture.folderClass, "Shared", root);
    const core::InstanceId module = make(fixture.moduleScriptClass, "Util", folder);
    (void)make(fixture.widgetClass, "Crate", root);

    const LanguageTree tree = app::captureLanguageTree(world, root);
    REQUIRE(tree.nodes.size() == 3); // game, Shared, Util -- not the crate
    const std::optional<core::u32> util = tree.find("game.Shared.Util");
    REQUIRE(util.has_value());
    CHECK(tree.nodes[*util].module);
    CHECK(tree.nodes[*util].id == module);
    CHECK(tree.pathOf(module) == "game.Shared.Util");
    CHECK(tree.nodes[*tree.find("game.Shared")].children.size() == 1);
}

TEST_CASE("the owner's snake: a required module's constructor gives its members")
{
    // **Reported with this code** -- kept verbatim in `data/snake/Snake.luau.txt`,
    // `.txt` so the repository's own analysis does not read the owner's type
    // errors as ours. `snake.` offered nothing in the script that
    // requires the module. Its `: Snake` names a type the file never
    // declares, which is an error type to any checker -- so the members are
    // what the constructor BUILT, and that is what is asked for here.
    LanguageCore core(definitions());
    std::string module;
    REQUIRE(platform::readTextFile(std::filesystem::path(LUAUG_TEST_CATALOG).parent_path().parent_path() / "engine" /
                                       "app" / "tests" / "data" / "snake" / "Snake.luau.txt",
                                   module));
    TreeBuilder builder;
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    std::string main = R"(local Snake = require(script.Snake)

local function createSnake()
    local snake = Snake.new(Vector3.new(0, 1, 0), Vector2.new(50, 50))
    snake.|
    return snake
end
)";
    const Position caret = caretAt(main, "|");
    const core::u32 script = builder.add(service, "Main", "Script", main);
    (void)builder.add(script, "Snake", "ModuleScript", module);
    core.update(builder.tree);

    const app::LanguageCompletions members = core.complete("game.ScriptService.Main", caret);
    std::string seen;
    for (const Completion& row : members.items)
        seen += row.label + " ";
    INFO("offered: " << seen);
    CHECK(offers(members, "IsAlive"));
    CHECK(offers(members, "Scored"));
    CHECK(offers(members, "SetDirection"));
}

TEST_CASE("after `::` a module's name offers its types, not its functions")
{
    // **The owner's report**: `:: Snake.` listed `new`, `Grow` and `__index`
    // -- a value's members, in a place only a type can be written.
    LanguageCore core(definitions());
    TreeBuilder builder;
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    std::string main = "local Snake = require(script.Snake)\nlocal s = nil :: Snake.|\n";
    const Position caret = caretAt(main, "|");
    const core::u32 script = builder.add(service, "Main", "Script", main);
    (void)builder.add(script, "Snake", "ModuleScript", std::string(SnakeModule) + "\nexport type A = number\n");
    core.update(builder.tree);

    const app::LanguageCompletions found = core.complete("game.ScriptService.Main", caret);
    CHECK(found.inType);
    CHECK(offers(found, "Snake"));
    CHECK_FALSE(offers(found, "new"));
    CHECK_FALSE(offers(found, "__index"));
}

TEST_CASE("a required constructor's parameters are shown as they were written")
{
    // **The owner**: "it should type `.new` when I write Snake.new, and its
    // parameters". Their module's `Position` and `Snake` are undeclared, so
    // the checker has error types there -- and the signature shows the names
    // they wrote, not `*error-type*`.
    LanguageCore core(definitions());
    std::string module;
    REQUIRE(platform::readTextFile(std::filesystem::path(LUAUG_TEST_CATALOG).parent_path().parent_path() / "engine" /
                                       "app" / "tests" / "data" / "snake" / "Snake.luau.txt",
                                   module));
    TreeBuilder builder;
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    std::string main = "local Snake = require(script.Snake)\nlocal s = Snake.new(Vector3.new(0, 1, 0), |)\n";
    const Position caret = caretAt(main, "|");
    const core::u32 script = builder.add(service, "Main", "Script", main);
    (void)builder.add(script, "Snake", "ModuleScript", module);
    core.update(builder.tree);

    const std::optional<app::SignatureHelp> help = core.signature("game.ScriptService.Main", caret);
    REQUIRE(help.has_value());
    CHECK(help->label == "new(gridPos: Position, gridSize: Vector2): Snake");
    CHECK(help->active == 1);
}

TEST_CASE("a global function's signature is its declaration, a variadic written as one")
{
    // `print(` showed `print(*error-type*)`: the type at a half-typed call is
    // a generic's broken instance, not what `print` is.
    LanguageCore core(definitions());
    TreeBuilder builder;
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    std::string source = "local t = {}\nprint(t.x|)\n";
    const Position caret = caretAt(source, "|");
    (void)builder.add(service, "Main", "Script", source);
    core.update(builder.tree);

    const std::optional<app::SignatureHelp> help = core.signature("game.ScriptService.Main", caret);
    REQUIRE(help.has_value());
    INFO(help->label);
    CHECK(help->label.find("print(...: ") == 0);
    CHECK(help->label.find("error-type") == std::string::npos);
}

TEST_CASE("signature help right after the opening parenthesis, before any argument")
{
    // **The owner's friend**: the parameters appeared only after a space was
    // typed -- `Color3.fromRGB(|)` with the pair closed showed nothing.
    LanguageCore core(definitions());
    TreeBuilder builder;
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    std::string source = "local c = Color3.fromRGB(|)--!strict\n";
    const Position caret = caretAt(source, "|");
    (void)builder.add(service, "Main", "Script", source);
    core.update(builder.tree);

    const std::optional<app::SignatureHelp> help = core.signature("game.ScriptService.Main", caret);
    REQUIRE(help.has_value());
    CHECK(help->label.find("fromRGB(") == 0);
    CHECK(help->active == 0);
}

TEST_CASE("a Vector3 reads as Vector3, not as the native vector it is")
{
    // **The owner**: a `Vector3` parameter showed as `vector`. It is Luau's
    // native vector underneath (R9), and the editor says the name a script
    // writes.
    LanguageCore core(definitions());
    TreeBuilder builder;
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    std::string source = "local function move(to: Vector3, by: number) end\nmove(|)\n";
    const Position caret = caretAt(source, "|");
    (void)builder.add(service, "Main", "Script", source);
    core.update(builder.tree);

    const std::optional<app::SignatureHelp> help = core.signature("game.ScriptService.Main", caret);
    REQUIRE(help.has_value());
    CHECK(help->label == "move(to: Vector3, by: number)");
}

TEST_CASE("inside a function passed as an argument there is no signature of the outer call")
{
    LanguageCore core(definitions());
    TreeBuilder builder;
    const core::u32 service = builder.add(0, "ScriptService", "ScriptService");
    std::string source = "workspace.ChildAdded:Connect(function(child)\n    local x = 1|\nend)\n";
    const Position caret = caretAt(source, "|");
    (void)builder.add(service, "Main", "Script", source);
    core.update(builder.tree);
    CHECK_FALSE(core.signature("game.ScriptService.Main", caret).has_value());
}
