#include "luaug/scene/world.h"
#include "luaug/ui/scene_types.h"
#include "luaug/ui/ui.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <optional>
#include <string>
#include <vector>

#include "class_descriptors.gen.h"

namespace {

namespace core = luaug::core;
namespace scene = luaug::scene;
namespace ui = luaug::ui;

using core::InstanceId;
using core::Vec2;

struct Fixture
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    std::optional<scene::World> world;
    InstanceId service;
    InstanceId screen;

    Fixture()
    {
        scene::generated::registerClasses(classes, atoms);
        ui::registerSceneTypes(classes, atoms);
        scene::generated::registerEnums(enums, atoms);
        world.emplace(classes, enums, atoms, 1u);
        service = make("UIService");
        screen = child("ScreenGui", service);
    }

    InstanceId make(const char* className)
    {
        const scene::ClassId id = classes.findId(atoms.intern(className));
        REQUIRE(id != scene::InvalidClass);
        return world->create(id);
    }

    InstanceId child(const char* className, InstanceId parent)
    {
        const InstanceId id = make(className);
        REQUIRE_FALSE(world->setParent(id, parent).has_value());
        return id;
    }

    // A box at an exact place, so a hit test is arithmetic a reader can check.
    InstanceId box(InstanceId parent, float x, float y, float w, float h)
    {
        const InstanceId id = child("TextButton", parent);
        scene::UIObjectComponent* object = world->uiObjects().find(id);
        object->position = core::UDim2{core::UDim{0.0f, x}, core::UDim{0.0f, y}};
        object->size = core::UDim2{core::UDim{0.0f, w}, core::UDim{0.0f, h}};
        return id;
    }

    void run() { ui::layout(*world, service, Vec2{800.0f, 600.0f}); }

    ui::InteractionResult interact(Vec2 pointer, bool pressed = false, bool released = false,
                                   std::string_view text = {}, bool backspace = false, bool submit = false)
    {
        ui::InteractionInput input;
        input.pointer = pointer;
        input.pressed = pressed;
        input.released = released;
        input.text = text;
        input.backspace = backspace;
        input.submit = submit;
        return ui::updateInteraction(*world, service, input);
    }

    // The caret's own keys (S6.7), as a whole `InteractionInput` rather than six
    // more defaulted parameters on the one above -- which would be a signature
    // nobody can read a call to.
    ui::InteractionResult send(const ui::InteractionInput& input)
    {
        return ui::updateInteraction(*world, service, input);
    }

    [[nodiscard]] std::vector<std::string> events()
    {
        std::vector<std::string> names;
        for (const scene::Change& change : world->changes().take()) {
            if (change.kind == scene::ChangeKind::InstanceEventNoArgs)
                names.emplace_back(atoms.text(change.name));
        }
        return names;
    }
};

} // namespace

TEST_CASE("the pointer finds the element drawn on top")
{
    Fixture fixture;
    const InstanceId back = fixture.box(fixture.screen, 0.0f, 0.0f, 200.0f, 200.0f);
    const InstanceId front = fixture.box(fixture.screen, 50.0f, 50.0f, 50.0f, 50.0f);
    fixture.run();

    CHECK(ui::hitTest(*fixture.world, fixture.service, Vec2{10.0f, 10.0f}) == back);
    // The overlapping region belongs to whichever draws last, which is document
    // order at equal ZIndex -- the same rule the draw list uses, from the same
    // numbers.
    CHECK(ui::hitTest(*fixture.world, fixture.service, Vec2{60.0f, 60.0f}) == front);
    CHECK_FALSE(ui::hitTest(*fixture.world, fixture.service, Vec2{500.0f, 500.0f}).valid());
}

TEST_CASE("an invisible element takes no clicks")
{
    Fixture fixture;
    const InstanceId hidden = fixture.box(fixture.screen, 0.0f, 0.0f, 100.0f, 100.0f);
    fixture.world->uiObjects().find(hidden)->visible = false;
    fixture.run();

    // All three together, which `Visible`'s doc promises: an invisible element
    // that still swallowed clicks is the defect that sentence rules out.
    CHECK_FALSE(ui::hitTest(*fixture.world, fixture.service, Vec2{10.0f, 10.0f}).valid());
}

TEST_CASE("a clipped-away element takes no clicks either")
{
    Fixture fixture;
    const InstanceId strip = fixture.box(fixture.screen, 0.0f, 0.0f, 100.0f, 40.0f);
    fixture.world->uiObjects().find(strip)->clipsDescendants = true;
    const InstanceId overhang = fixture.box(strip, 50.0f, 0.0f, 200.0f, 40.0f);
    fixture.run();

    // Inside both: the child answers.
    CHECK(ui::hitTest(*fixture.world, fixture.service, Vec2{60.0f, 10.0f}) == overhang);
    // Past the parent's edge: nothing does. An element scrolled off the end of a
    // list must not answer a click that lands where it would have been.
    CHECK_FALSE(ui::hitTest(*fixture.world, fixture.service, Vec2{150.0f, 10.0f}).valid());
}

TEST_CASE("hover is a pair of edges rather than a state")
{
    Fixture fixture;
    const InstanceId button = fixture.box(fixture.screen, 0.0f, 0.0f, 100.0f, 100.0f);
    fixture.run();

    fixture.interact(Vec2{10.0f, 10.0f});
    CHECK(fixture.events() == std::vector<std::string>{"PointerEntered"});

    // Still inside: told once, not once a frame.
    fixture.interact(Vec2{20.0f, 20.0f});
    CHECK(fixture.events().empty());

    fixture.interact(Vec2{500.0f, 500.0f});
    CHECK(fixture.events() == std::vector<std::string>{"PointerExited"});
    (void)button;
}

TEST_CASE("Activated needs both ends of the press on one element")
{
    Fixture fixture;
    const InstanceId button = fixture.box(fixture.screen, 0.0f, 0.0f, 100.0f, 100.0f);
    fixture.run();

    fixture.interact(Vec2{10.0f, 10.0f}, true, false);
    (void)fixture.events();
    fixture.interact(Vec2{20.0f, 20.0f}, false, true);
    CHECK(fixture.events() == std::vector<std::string>{"Activated"});

    // Pressed on the button and released off it: cancelled, which is what
    // every UI does and what people rely on to change their minds.
    fixture.interact(Vec2{10.0f, 10.0f}, true, false);
    (void)fixture.events();
    fixture.interact(Vec2{500.0f, 500.0f}, false, true);
    const std::vector<std::string> after = fixture.events();
    CHECK(std::ranges::find(after, "Activated") == after.end());
    (void)button;
}

TEST_CASE("the UI reports whether it took the pointer")
{
    Fixture fixture;
    fixture.box(fixture.screen, 0.0f, 0.0f, 100.0f, 100.0f);
    fixture.run();

    // The flag `input` consumes the mouse codes on, so a button over the world
    // does not also shoot the gun.
    CHECK(fixture.interact(Vec2{10.0f, 10.0f}).pointerOverUi);
    CHECK_FALSE(fixture.interact(Vec2{500.0f, 500.0f}).pointerOverUi);
}

TEST_CASE("focus follows the press, and typing reaches the focused field")
{
    Fixture fixture;
    const InstanceId field = fixture.child("TextInput", fixture.screen);
    scene::UIObjectComponent* object = fixture.world->uiObjects().find(field);
    object->size = core::UDim2{core::UDim{0.0f, 200.0f}, core::UDim{0.0f, 30.0f}};
    fixture.run();

    fixture.interact(Vec2{10.0f, 10.0f}, true, false);
    CHECK(fixture.world->textInputs().find(field)->focused);

    fixture.interact(Vec2{10.0f, 10.0f}, false, false, "ab");
    CHECK(fixture.world->textLabels().find(field)->text == "ab");

    // Backspace removes a whole UTF-8 sequence, not a byte: dropping one byte of
    // a two-byte character leaves a string no renderer can read.
    //
    // **Through `setProperty` rather than into the component**, which is what
    // a script's own assignment does -- and it is what puts the caret at the
    // end of the new string (S6.7). Writing the component directly leaves the
    // caret pointing into text that no longer exists, which is a state nothing
    // outside a test can produce.
    REQUIRE(fixture.world->setProperty(field, fixture.world->atoms().intern("Text"),
                                       scene::Value{std::string("a\xC3\xA9")}) == scene::World::SetResult::Changed);
    fixture.interact(Vec2{10.0f, 10.0f}, false, false, {}, true);
    CHECK(fixture.world->textLabels().find(field)->text == "a");

    // A press elsewhere takes focus away, including a press on nothing. A field
    // that kept focus after the player clicked the world would go on eating
    // their keystrokes.
    fixture.interact(Vec2{600.0f, 400.0f}, true, false);
    CHECK_FALSE(fixture.world->textInputs().find(field)->focused);
    fixture.interact(Vec2{600.0f, 400.0f}, false, false, "zz");
    CHECK(fixture.world->textLabels().find(field)->text == "a");
}

TEST_CASE("Return submits and releases focus")
{
    Fixture fixture;
    const InstanceId field = fixture.child("TextInput", fixture.screen);
    fixture.world->uiObjects().find(field)->size = core::UDim2{core::UDim{0.0f, 200.0f}, core::UDim{0.0f, 30.0f}};
    fixture.run();

    fixture.interact(Vec2{10.0f, 10.0f}, true, false);
    (void)fixture.events();

    fixture.interact(Vec2{10.0f, 10.0f}, false, false, {}, false, true);
    CHECK_FALSE(fixture.world->textInputs().find(field)->focused);
    const std::vector<std::string> after = fixture.events();
    CHECK(std::ranges::find(after, "FocusLost") != after.end());
}

// --- The caret (S6.7) ---------------------------------------------------------
//
// **`TextInput`'s own doc has promised "typed text, backspace and a caret"
// since the class existed**, and the caret was the one it did not have: the
// editor appended and backspaced at the END, so a typo four characters back
// meant deleting everything after it.
//
// The field was here once with nothing moving it, and it was removed for that
// reason -- `inertcheck` found it, and the comment that replaced it said a real
// caret would arrive with the code that moves it. These are that code's cases.

namespace {

// A focused field with `seed` in it and the caret wherever focus left it.
[[nodiscard]] InstanceId focusedField(Fixture& fixture, std::string_view seed)
{
    const InstanceId field = fixture.child("TextInput", fixture.screen);
    fixture.world->uiObjects().find(field)->size = core::UDim2{core::UDim{0.0f, 200.0f}, core::UDim{0.0f, 30.0f}};
    fixture.run();
    fixture.interact(Vec2{10.0f, 10.0f}, true, false);
    if (!seed.empty())
        fixture.interact(Vec2{10.0f, 10.0f}, false, false, seed);
    return field;
}

} // namespace

TEST_CASE("the caret starts at the end of what is already there")
{
    // Which is what clicking into a field means everywhere: the caret goes after
    // the text and typing continues it.
    Fixture fixture;
    const InstanceId field = fixture.child("TextInput", fixture.screen);
    fixture.world->uiObjects().find(field)->size = core::UDim2{core::UDim{0.0f, 200.0f}, core::UDim{0.0f, 30.0f}};
    REQUIRE(fixture.world->setProperty(field, fixture.world->atoms().intern("Text"),
                                       scene::Value{std::string("hello")}) == scene::World::SetResult::Changed);
    fixture.run();

    fixture.interact(Vec2{10.0f, 10.0f}, true, false);
    CHECK(fixture.world->textInputs().find(field)->caret == 5);
}

TEST_CASE("typing inserts where the caret is, not at the end")
{
    // The whole feature. Without it a typo four characters back means deleting
    // everything after it.
    Fixture fixture;
    const InstanceId field = focusedField(fixture, "hello");

    ui::InteractionInput left;
    left.pointer = Vec2{10.0f, 10.0f};
    left.caretLeft = true;
    fixture.send(left);
    fixture.send(left);
    CHECK(fixture.world->textInputs().find(field)->caret == 3);

    fixture.interact(Vec2{10.0f, 10.0f}, false, false, "XY");
    CHECK(fixture.world->textLabels().find(field)->text == "helXYlo");
    CHECK(fixture.world->textInputs().find(field)->caret == 5);
}

TEST_CASE("backspace takes what is before the caret and delete takes what is after")
{
    // Two keys and two edits. With only one of them a caret can be used to
    // insert and nothing else.
    Fixture fixture;
    const InstanceId field = focusedField(fixture, "abcd");

    ui::InteractionInput left;
    left.pointer = Vec2{10.0f, 10.0f};
    left.caretLeft = true;
    fixture.send(left);
    fixture.send(left);
    REQUIRE(fixture.world->textInputs().find(field)->caret == 2);

    ui::InteractionInput back;
    back.pointer = Vec2{10.0f, 10.0f};
    back.backspace = true;
    fixture.send(back);
    CHECK(fixture.world->textLabels().find(field)->text == "acd");
    CHECK(fixture.world->textInputs().find(field)->caret == 1);

    ui::InteractionInput forward;
    forward.pointer = Vec2{10.0f, 10.0f};
    forward.forwardDelete = true;
    fixture.send(forward);
    CHECK(fixture.world->textLabels().find(field)->text == "ad");
    // Forward delete does NOT move the caret, which is what makes holding it
    // eat the rest of the line rather than walking backwards through it.
    CHECK(fixture.world->textInputs().find(field)->caret == 1);
}

TEST_CASE("Home and End go to the two ends")
{
    Fixture fixture;
    const InstanceId field = focusedField(fixture, "abcd");

    ui::InteractionInput home;
    home.pointer = Vec2{10.0f, 10.0f};
    home.caretHome = true;
    fixture.send(home);
    CHECK(fixture.world->textInputs().find(field)->caret == 0);

    ui::InteractionInput end;
    end.pointer = Vec2{10.0f, 10.0f};
    end.caretEnd = true;
    fixture.send(end);
    CHECK(fixture.world->textInputs().find(field)->caret == 4);
}

TEST_CASE("the caret steps whole characters, so it never lands inside one")
{
    // Bytes everywhere except where a step has to be a character. A caret inside
    // a UTF-8 sequence is an insertion that splits it, and a string no renderer
    // can read.
    Fixture fixture;
    const InstanceId field = focusedField(fixture, "a\xC3\xA9z");
    REQUIRE(fixture.world->textInputs().find(field)->caret == 4);

    ui::InteractionInput left;
    left.pointer = Vec2{10.0f, 10.0f};
    left.caretLeft = true;
    fixture.send(left); // past 'z'
    CHECK(fixture.world->textInputs().find(field)->caret == 3);
    fixture.send(left); // past the two-byte sequence, in one step
    CHECK(fixture.world->textInputs().find(field)->caret == 1);
}

TEST_CASE("the caret stops at both ends rather than running off them")
{
    Fixture fixture;
    const InstanceId field = focusedField(fixture, "ab");

    ui::InteractionInput left;
    left.pointer = Vec2{10.0f, 10.0f};
    left.caretLeft = true;
    for (int press = 0; press < 6; ++press)
        fixture.send(left);
    CHECK(fixture.world->textInputs().find(field)->caret == 0);

    // And a backspace at the start does nothing rather than eating a byte that
    // is not there.
    ui::InteractionInput back;
    back.pointer = Vec2{10.0f, 10.0f};
    back.backspace = true;
    fixture.send(back);
    CHECK(fixture.world->textLabels().find(field)->text == "ab");

    ui::InteractionInput right;
    right.pointer = Vec2{10.0f, 10.0f};
    right.caretRight = true;
    for (int press = 0; press < 6; ++press)
        fixture.send(right);
    CHECK(fixture.world->textInputs().find(field)->caret == 2);

    ui::InteractionInput forward;
    forward.pointer = Vec2{10.0f, 10.0f};
    forward.forwardDelete = true;
    fixture.send(forward);
    CHECK(fixture.world->textLabels().find(field)->text == "ab");
}

TEST_CASE("a script assigning Text puts the caret at the end")
{
    // The only answer that is always in range. A caret left where it was points
    // into a string that no longer exists: at best somewhere arbitrary, at worst
    // inside a UTF-8 sequence.
    Fixture fixture;
    const InstanceId field = focusedField(fixture, "abcd");

    ui::InteractionInput home;
    home.pointer = Vec2{10.0f, 10.0f};
    home.caretHome = true;
    fixture.send(home);
    REQUIRE(fixture.world->textInputs().find(field)->caret == 0);

    REQUIRE(fixture.world->setProperty(field, fixture.world->atoms().intern("Text"),
                                       scene::Value{std::string("a much longer value")}) ==
            scene::World::SetResult::Changed);
    CHECK(fixture.world->textInputs().find(field)->caret == 19);
}
