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
    fixture.world->textLabels().find(field)->text = "a\xC3\xA9";
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
