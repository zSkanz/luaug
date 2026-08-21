#include "luaug/input/input.h"
#include "luaug/input/scene_types.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>

// scene's generated header, through the include directory `luaug_scene` exports.
// input's own is reached by a relative path from its source; a test does not
// need it, because `registerSceneTypes` is the public way in.
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "class_descriptors.gen.h"

namespace {

namespace core = luaug::core;
namespace input = luaug::input;
namespace platform = luaug::platform;
namespace scene = luaug::scene;

using core::InstanceId;

// `Enum.KeyCode` values, by name, so a case reads as the binding a script would
// write. Taken from the generated enum at construction rather than hard-coded:
// the point of these tests is the resolver, and a case that spelled 4 where it
// meant "D" would go wrong silently when the enum moved.
struct Fixture
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    std::optional<scene::World> world;
    input::InputSystem system;

    Fixture()
    {
        scene::generated::registerClasses(classes, atoms);
        input::registerSceneTypes(classes, atoms);
        scene::generated::registerEnums(enums, atoms);
        world.emplace(classes, enums, atoms, 1u);
    }

    [[nodiscard]] core::i32 keyCode(const char* name) const
    {
        const scene::EnumDescriptor* descriptor = enums.find(scene::generated::KeyCodeEnumId);
        REQUIRE(descriptor != nullptr);
        for (const scene::EnumItemDesc& item : descriptor->items) {
            if (atoms.text(item.name) == name)
                return item.value;
        }
        FAIL("no KeyCode item named ", name);
        return 0;
    }

    [[nodiscard]] InstanceId make(const char* className)
    {
        const scene::ClassId id = classes.findId(atoms.intern(className));
        REQUIRE(id != scene::InvalidClass);
        return world->create(id);
    }

    InstanceId context(float priority = 0.0f, bool sink = false)
    {
        const InstanceId id = make("InputContext");
        scene::InputContextComponent* component = world->inputContexts().find(id);
        REQUIRE(component != nullptr);
        component->priority = priority;
        component->sink = sink;
        return id;
    }

    InstanceId action(InstanceId parent, input::ActionType type)
    {
        const InstanceId id = make("InputAction");
        REQUIRE_FALSE(world->setParent(id, parent).has_value());
        scene::InputActionComponent* component = world->inputActions().find(id);
        REQUIRE(component != nullptr);
        component->type = static_cast<core::i32>(type);
        return id;
    }

    InstanceId binding(InstanceId parent)
    {
        const InstanceId id = make("InputBinding");
        REQUIRE_FALSE(world->setParent(id, parent).has_value());
        REQUIRE(world->inputBindings().find(id) != nullptr);
        return id;
    }

    void press(const char* keyName)
    {
        platform::Event event;
        event.type = platform::EventType::KeyDown;
        event.key = platform::keyFromName(keyName);
        REQUIRE(event.key != platform::Key::Unknown);
        const platform::Event events[] = {event};
        system.pumpFrame(events);
    }

    void release(const char* keyName)
    {
        platform::Event event;
        event.type = platform::EventType::KeyUp;
        event.key = platform::keyFromName(keyName);
        REQUIRE(event.key != platform::Key::Unknown);
        const platform::Event events[] = {event};
        system.pumpFrame(events);
    }

    [[nodiscard]] const scene::InputActionComponent& state(InstanceId id) const
    {
        const scene::InputActionComponent* component = world->inputActions().find(id);
        REQUIRE(component != nullptr);
        return *component;
    }

    // The names of the events enqueued since the last call, in raise order.
    [[nodiscard]] std::vector<std::string> drainEvents()
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

TEST_CASE("a KeyCode belongs to the device its range says")
{
    // The ranges in input.cpp are `Enum.KeyCode`'s layout written where the
    // resolver can use it, so this is the case that goes red if an item is
    // inserted in the middle of the enum without the ranges moving with it.
    Fixture fixture;
    CHECK(input::deviceOf(fixture.keyCode("Unknown")) == input::DeviceType::KeyboardMouse);
    CHECK(input::deviceOf(fixture.keyCode("W")) == input::DeviceType::KeyboardMouse);
    CHECK(input::deviceOf(fixture.keyCode("MouseLeft")) == input::DeviceType::KeyboardMouse);
    CHECK(input::deviceOf(fixture.keyCode("MouseMovement")) == input::DeviceType::KeyboardMouse);
    CHECK(input::deviceOf(fixture.keyCode("ButtonSouth")) == input::DeviceType::Gamepad);
    CHECK(input::deviceOf(fixture.keyCode("LeftTrigger")) == input::DeviceType::Gamepad);
    CHECK(input::deviceOf(fixture.keyCode("RightThumbstick")) == input::DeviceType::Gamepad);

    CHECK_FALSE(input::isAnalog(fixture.keyCode("W")));
    CHECK_FALSE(input::isAnalog(fixture.keyCode("ButtonSouth")));
    CHECK(input::isAnalog(fixture.keyCode("MouseMovement")));
    CHECK(input::isAnalog(fixture.keyCode("LeftTrigger")));
    CHECK(input::isAnalog(fixture.keyCode("LeftThumbstick")));
}

TEST_CASE("a Bool action follows the key its binding names")
{
    Fixture fixture;
    const InstanceId context = fixture.context();
    const InstanceId jump = fixture.action(context, input::ActionType::Bool);
    const InstanceId binding = fixture.binding(jump);
    fixture.world->inputBindings().find(binding)->keyCode = fixture.keyCode("Space");

    fixture.system.dispatchSimTick(*fixture.world, 1);
    CHECK_FALSE(fixture.state(jump).pressed);
    (void)fixture.drainEvents();

    fixture.press("Space");
    fixture.system.dispatchSimTick(*fixture.world, 2);
    CHECK(fixture.state(jump).pressed);
    // Two facts, in this order: the specific one and the general one. A handler
    // connected to either is told once.
    CHECK(fixture.drainEvents() == std::vector<std::string>{"Pressed", "StateChanged"});

    // Held rather than pressed again. `Pressed` is an edge, and an autorepeat
    // that re-fired it would be a jump per repeat instead of a jump per press.
    fixture.system.dispatchSimTick(*fixture.world, 3);
    CHECK(fixture.state(jump).pressed);
    CHECK(fixture.drainEvents().empty());

    fixture.release("Space");
    fixture.system.dispatchSimTick(*fixture.world, 4);
    CHECK_FALSE(fixture.state(jump).pressed);
    CHECK(fixture.drainEvents() == std::vector<std::string>{"Released", "StateChanged"});
}

TEST_CASE("a Direction2D action reads its four composite keys")
{
    Fixture fixture;
    const InstanceId context = fixture.context();
    const InstanceId move = fixture.action(context, input::ActionType::Direction2D);
    const InstanceId binding = fixture.binding(move);
    scene::InputBindingComponent* keys = fixture.world->inputBindings().find(binding);
    keys->up = fixture.keyCode("W");
    keys->down = fixture.keyCode("S");
    keys->left = fixture.keyCode("A");
    keys->right = fixture.keyCode("D");

    fixture.press("W");
    fixture.press("D");
    fixture.system.dispatchSimTick(*fixture.world, 1);
    CHECK(fixture.state(move).axis.x == doctest::Approx(1.0));
    CHECK(fixture.state(move).axis.y == doctest::Approx(1.0));

    // Both halves of an axis at once cancel. Reading whichever the engine
    // happened to see last would make holding A and D drift sideways, which is
    // the classic version of this bug.
    fixture.press("A");
    fixture.system.dispatchSimTick(*fixture.world, 2);
    CHECK(fixture.state(move).axis.x == doctest::Approx(0.0));
    CHECK(fixture.state(move).axis.y == doctest::Approx(1.0));

    // Scale is a multiplier and a negative one inverts the axis, which is what
    // a settings screen's "invert Y" writes.
    keys->scale = -2.0f;
    fixture.system.dispatchSimTick(*fixture.world, 3);
    CHECK(fixture.state(move).axis.y == doctest::Approx(-2.0));
}

TEST_CASE("a sinking context hides the keys it names and no others")
{
    Fixture fixture;

    // A menu above gameplay. Both bind Escape; only gameplay binds W.
    const InstanceId gameplay = fixture.context(0.0f, false);
    const InstanceId walk = fixture.action(gameplay, input::ActionType::Bool);
    fixture.world->inputBindings().find(fixture.binding(walk))->keyCode = fixture.keyCode("W");
    const InstanceId cancel = fixture.action(gameplay, input::ActionType::Bool);
    fixture.world->inputBindings().find(fixture.binding(cancel))->keyCode = fixture.keyCode("Escape");

    const InstanceId menu = fixture.context(10.0f, true);
    const InstanceId close = fixture.action(menu, input::ActionType::Bool);
    fixture.world->inputBindings().find(fixture.binding(close))->keyCode = fixture.keyCode("Escape");

    fixture.press("Escape");
    fixture.press("W");
    fixture.system.dispatchSimTick(*fixture.world, 1);

    // The menu got the Escape; gameplay did not.
    CHECK(fixture.state(close).pressed);
    CHECK_FALSE(fixture.state(cancel).pressed);
    // Sinking is per input rather than per context: the menu never named W, so
    // it cannot hide it. A context that swallowed everything would make a HUD
    // under a dialog impossible.
    CHECK(fixture.state(walk).pressed);

    // Disabling the menu hands Escape back the very next tick, which is what
    // closing a menu has to do.
    fixture.world->inputContexts().find(menu)->enabled = false;
    fixture.system.dispatchSimTick(*fixture.world, 2);
    CHECK(fixture.state(cancel).pressed);
    CHECK_FALSE(fixture.state(close).pressed);
}

TEST_CASE("priority orders fallthrough and rate does not")
{
    Fixture fixture;

    // A `Render`-rate context is not dispatched by a sim tick at all, however
    // high its priority: the two are different questions (ADR 0039), and a
    // priority that also selected a clock would make this pair inexpressible.
    const InstanceId look = fixture.context(100.0f, true);
    fixture.world->inputContexts().find(look)->rate = static_cast<core::i32>(input::Rate::Render);
    const InstanceId aim = fixture.action(look, input::ActionType::Bool);
    fixture.world->inputBindings().find(fixture.binding(aim))->keyCode = fixture.keyCode("MouseLeft");

    const InstanceId gameplay = fixture.context(0.0f, false);
    const InstanceId fire = fixture.action(gameplay, input::ActionType::Bool);
    fixture.world->inputBindings().find(fixture.binding(fire))->keyCode = fixture.keyCode("MouseLeft");

    platform::Event click;
    click.type = platform::EventType::MouseButtonDown;
    click.button = platform::MouseButton::Left;
    const platform::Event events[] = {click};
    fixture.system.pumpFrame(events);

    fixture.system.dispatchSimTick(*fixture.world, 1);
    // The high-priority sinking context did NOT consume it, because it was not
    // dispatched: it is on the other clock.
    CHECK(fixture.state(fire).pressed);
    CHECK_FALSE(fixture.state(aim).pressed);

    fixture.system.dispatchRenderRate(*fixture.world);
    CHECK(fixture.state(aim).pressed);
}

TEST_CASE("a disabled action consumes nothing, even inside a sinking context")
{
    Fixture fixture;
    const InstanceId modal = fixture.context(10.0f, true);
    const InstanceId ignored = fixture.action(modal, input::ActionType::Bool);
    fixture.world->inputBindings().find(fixture.binding(ignored))->keyCode = fixture.keyCode("Space");
    fixture.world->inputActions().find(ignored)->enabled = false;

    const InstanceId gameplay = fixture.context(0.0f, false);
    const InstanceId jump = fixture.action(gameplay, input::ActionType::Bool);
    fixture.world->inputBindings().find(fixture.binding(jump))->keyCode = fixture.keyCode("Space");

    fixture.press("Space");
    fixture.system.dispatchSimTick(*fixture.world, 1);

    CHECK_FALSE(fixture.state(ignored).pressed);
    // Disabling an action hands its input back rather than swallowing it. The
    // opposite -- a disabled action that still sinks -- is a dead context that
    // silently eats a key for the rest of the session.
    CHECK(fixture.state(jump).pressed);
}

TEST_CASE("losing focus releases everything that was held")
{
    Fixture fixture;
    const InstanceId context = fixture.context();
    const InstanceId walk = fixture.action(context, input::ActionType::Bool);
    fixture.world->inputBindings().find(fixture.binding(walk))->keyCode = fixture.keyCode("W");

    fixture.press("W");
    fixture.system.dispatchSimTick(*fixture.world, 1);
    REQUIRE(fixture.state(walk).pressed);
    (void)fixture.drainEvents();

    fixture.system.releaseAll(*fixture.world);
    CHECK_FALSE(fixture.state(walk).pressed);
    // And it TELLS the game, rather than going quiet: a handler that started
    // something on `Pressed` needs its `Released` or the something never stops.
    const std::vector<std::string> events = fixture.drainEvents();
    CHECK(std::ranges::find(events, "Released") != events.end());
}

TEST_CASE("an analogue source on a Bool action presses past half deflection")
{
    Fixture fixture;
    const InstanceId context = fixture.context();
    const InstanceId shoot = fixture.action(context, input::ActionType::Bool);
    fixture.world->inputBindings().find(fixture.binding(shoot))->keyCode = fixture.keyCode("RightTrigger");

    platform::Event axis;
    axis.type = platform::EventType::GamepadAxisMoved;
    axis.gamepadAxis = platform::GamepadAxis::RightTrigger;

    axis.axisValue = 0.25f;
    const platform::Event light[] = {axis};
    fixture.system.pumpFrame(light);
    fixture.system.dispatchSimTick(*fixture.world, 1);
    CHECK_FALSE(fixture.state(shoot).pressed);

    axis.axisValue = 0.75f;
    const platform::Event heavy[] = {axis};
    fixture.system.pumpFrame(heavy);
    fixture.system.dispatchSimTick(*fixture.world, 2);
    CHECK(fixture.state(shoot).pressed);
}

TEST_CASE("a stick reports up as +Y, the direction the Up composite means")
{
    Fixture fixture;
    const InstanceId context = fixture.context();
    const InstanceId move = fixture.action(context, input::ActionType::Direction2D);
    fixture.world->inputBindings().find(fixture.binding(move))->keyCode = fixture.keyCode("LeftThumbstick");

    // SDL reports a stick's Y positive DOWNWARD. One convention reaches the
    // game, and this case is what says which one.
    platform::Event axis;
    axis.type = platform::EventType::GamepadAxisMoved;
    axis.gamepadAxis = platform::GamepadAxis::LeftY;
    axis.axisValue = -1.0f;
    const platform::Event events[] = {axis};
    fixture.system.pumpFrame(events);
    fixture.system.dispatchSimTick(*fixture.world, 1);

    CHECK(fixture.state(move).axis.y == doctest::Approx(1.0));
}

TEST_CASE("a Direction3D action reports zero, which is what v1 promises")
{
    Fixture fixture;
    const InstanceId context = fixture.context();
    const InstanceId move = fixture.action(context, input::ActionType::Direction3D);
    scene::InputBindingComponent* keys = fixture.world->inputBindings().find(fixture.binding(move));
    keys->up = fixture.keyCode("W");
    keys->right = fixture.keyCode("D");

    fixture.press("W");
    fixture.press("D");
    fixture.system.dispatchSimTick(*fixture.world, 1);

    // Declared and not driveable: no binding in api-design.md §2.4's list names
    // three axes. The zero vector rather than a guess at what the caller meant,
    // and this case is what stops the guess being added quietly later.
    CHECK(fixture.state(move).axis.x == doctest::Approx(0.0));
    CHECK(fixture.state(move).axis.y == doctest::Approx(0.0));
    CHECK(fixture.state(move).axis.z == doctest::Approx(0.0));
}

TEST_CASE("a resting stick does not steal the prompts from the keyboard")
{
    Fixture fixture;

    platform::Event drift;
    drift.type = platform::EventType::GamepadAxisMoved;
    drift.gamepadAxis = platform::GamepadAxis::LeftX;
    drift.axisValue = 0.05f;
    const platform::Event resting[] = {drift};
    fixture.system.pumpFrame(resting);
    CHECK(fixture.system.snapshot().lastDevice == input::DeviceType::KeyboardMouse);

    drift.axisValue = 0.9f;
    const platform::Event pushed[] = {drift};
    fixture.system.pumpFrame(pushed);
    CHECK(fixture.system.snapshot().lastDevice == input::DeviceType::Gamepad);

    // And pointer MOTION does not claim the device either: a desk bump would
    // otherwise flip every prompt on screen while the player holds a pad.
    platform::Event moved;
    moved.type = platform::EventType::MouseMoved;
    moved.pointerDeltaX = 3.0f;
    const platform::Event nudge[] = {moved};
    fixture.system.pumpFrame(nudge);
    CHECK(fixture.system.snapshot().lastDevice == input::DeviceType::Gamepad);
}

// --- The raw event surface (ADR 0041) ----------------------------------------
//
// Every case below is about the one property that made this surface allowable at
// all: these events come out of the IAS's own dispatch. Not from the OS, not on
// the wall clock, and not before the UI has said what it took.

namespace {

// The raw events one `Simulation` dispatch produced, drained.
[[nodiscard]] std::vector<input::RawInputEvent> rawOf(Fixture& fixture)
{
    const std::span<const input::RawInputEvent> events = fixture.system.drainRawEvents();
    return std::vector<input::RawInputEvent>(events.begin(), events.end());
}

[[nodiscard]] platform::Event mouseButton(platform::EventType type)
{
    platform::Event event;
    event.type = type;
    event.button = platform::MouseButton::Left;
    return event;
}

} // namespace

TEST_CASE("a key press and release produce InputBegan and InputEnded, once each")
{
    Fixture fixture;
    fixture.system.dispatchSimTick(*fixture.world, 1);
    CHECK(rawOf(fixture).empty());

    fixture.press("Space");
    fixture.system.dispatchSimTick(*fixture.world, 2);
    std::vector<input::RawInputEvent> began = rawOf(fixture);
    REQUIRE(began.size() == 1);
    CHECK(began[0].phase == input::RawInputEvent::Phase::Began);
    CHECK(began[0].userInputType == input::UserInputType::Keyboard);
    CHECK(began[0].keyCode == fixture.keyCode("Space"));
    CHECK_FALSE(began[0].uiConsumed);

    // Held is not begun. A surface that fired every tick a key was down would be
    // a polling loop wearing an event's clothes, and every handler would have to
    // filter it back out.
    fixture.system.dispatchSimTick(*fixture.world, 3);
    CHECK(rawOf(fixture).empty());

    fixture.release("Space");
    fixture.system.dispatchSimTick(*fixture.world, 4);
    std::vector<input::RawInputEvent> ended = rawOf(fixture);
    REQUIRE(ended.size() == 1);
    CHECK(ended[0].phase == input::RawInputEvent::Phase::Ended);
    CHECK(ended[0].keyCode == fixture.keyCode("Space"));
}

TEST_CASE("nothing is produced on the Render clock")
{
    // ADR 0041 puts these on `Simulation` so a handler that writes to the world
    // replays by construction. A render dispatch that also produced them would
    // fire one press twice, on two clocks, one of which a replay does not have.
    Fixture fixture;
    fixture.press("Space");
    fixture.system.dispatchRenderRate(*fixture.world);
    CHECK(rawOf(fixture).empty());

    fixture.system.dispatchSimTick(*fixture.world, 1);
    CHECK(rawOf(fixture).size() == 1);
}

TEST_CASE("the UI's claim reaches the event's second argument")
{
    Fixture fixture;
    fixture.system.setPointerCapturedByUi(true);

    const platform::Event events[] = {mouseButton(platform::EventType::MouseButtonDown)};
    fixture.system.pumpFrame(events);
    fixture.system.dispatchSimTick(*fixture.world, 1);

    std::vector<input::RawInputEvent> raw = rawOf(fixture);
    REQUIRE(raw.size() == 1);
    CHECK(raw[0].userInputType == input::UserInputType::MouseButton1);
    // The whole reason the flag is the SECOND argument rather than something a
    // handler has to go and ask for: a click on a button must not also fire the
    // gun, and the only way a handler can know is if it is told.
    CHECK(raw[0].uiConsumed);
}

TEST_CASE("a keyboard press is the game's even while the pointer is over the UI")
{
    // The pointer's claim covers the mouse codes and nothing else. A health bar
    // under the cursor eating the jump key is the defect this asserts against.
    Fixture fixture;
    fixture.system.setPointerCapturedByUi(true);
    fixture.press("Space");
    fixture.system.dispatchSimTick(*fixture.world, 1);

    std::vector<input::RawInputEvent> raw = rawOf(fixture);
    REQUIRE(raw.size() == 1);
    CHECK_FALSE(raw[0].uiConsumed);
}

TEST_CASE("a focused TextInput takes the keyboard, and the action stops seeing it")
{
    Fixture fixture;
    const InstanceId context = fixture.context();
    const InstanceId jump = fixture.action(context, input::ActionType::Bool);
    const InstanceId binding = fixture.binding(jump);
    fixture.world->inputBindings().find(binding)->keyCode = fixture.keyCode("W");

    fixture.press("W");
    fixture.system.dispatchSimTick(*fixture.world, 1);
    CHECK(fixture.state(jump).pressed);

    // Focus moves into a text field. The key is still physically down, and the
    // action has to stop seeing it or a player typing `w` into a chat box walks
    // forward.
    fixture.system.setKeyboardCapturedByUi(true);
    fixture.system.dispatchSimTick(*fixture.world, 2);
    CHECK_FALSE(fixture.state(jump).pressed);
}

TEST_CASE("InputEnded reports what InputBegan reported, even if the UI let go first")
{
    // A press that started on a button is still that press when it is released,
    // and a handler pairing the two must not be told the release was the game's
    // when the press was not. Otherwise a drag off a button is half-handled.
    Fixture fixture;
    fixture.system.setPointerCapturedByUi(true);

    const platform::Event downEvents[] = {mouseButton(platform::EventType::MouseButtonDown)};
    fixture.system.pumpFrame(downEvents);
    fixture.system.dispatchSimTick(*fixture.world, 1);
    REQUIRE(rawOf(fixture).size() == 1);

    fixture.system.setPointerCapturedByUi(false);
    const platform::Event upEvents[] = {mouseButton(platform::EventType::MouseButtonUp)};
    fixture.system.pumpFrame(upEvents);
    fixture.system.dispatchSimTick(*fixture.world, 2);

    std::vector<input::RawInputEvent> raw = rawOf(fixture);
    REQUIRE(raw.size() == 1);
    CHECK(raw[0].phase == input::RawInputEvent::Phase::Ended);
    CHECK(raw[0].uiConsumed);
}

TEST_CASE("pointer motion is one InputChanged a tick, with the delta accumulated")
{
    Fixture fixture;
    platform::Event first;
    first.type = platform::EventType::MouseMoved;
    first.pointerX = 10.0f;
    first.pointerY = 20.0f;
    first.pointerDeltaX = 3.0f;
    platform::Event second = first;
    second.pointerX = 14.0f;
    second.pointerDeltaX = 4.0f;
    const platform::Event events[] = {first, second};
    fixture.system.pumpFrame(events);
    fixture.system.dispatchSimTick(*fixture.world, 1);

    std::vector<input::RawInputEvent> raw = rawOf(fixture);
    REQUIRE(raw.size() == 1);
    CHECK(raw[0].phase == input::RawInputEvent::Phase::Changed);
    CHECK(raw[0].userInputType == input::UserInputType::MouseMovement);
    // Seven and not four: a handler that saw only the last device event would
    // lose most of a fast flick, which is the same reason the deltas are
    // accumulated for actions.
    CHECK(static_cast<double>(raw[0].delta.x) == doctest::Approx(7.0));
    CHECK(static_cast<double>(raw[0].position.x) == doctest::Approx(14.0));
    // Motion has no beginning and no end, so it is never `Began` or `Ended`.
    CHECK(raw[0].keyCode == 0);
}

TEST_CASE("a resting pointer produces nothing at all")
{
    // Sixty ticks a second into a handler that has nothing to react to is the
    // cost a naive implementation pays forever.
    Fixture fixture;
    fixture.system.dispatchSimTick(*fixture.world, 1);
    (void)rawOf(fixture);
    for (core::u64 tick = 2; tick < 10; ++tick) {
        fixture.system.dispatchSimTick(*fixture.world, tick);
        CHECK(rawOf(fixture).empty());
    }
}

TEST_CASE("losing focus ends everything that was held")
{
    // A handler that pairs `InputBegan` with `InputEnded` must never leak a
    // press. An alt-tab that left W down is how a character keeps walking into a
    // wall with the window in the background.
    Fixture fixture;
    fixture.press("W");
    fixture.press("A");
    fixture.system.dispatchSimTick(*fixture.world, 1);
    REQUIRE(rawOf(fixture).size() == 2);

    fixture.system.releaseAll(*fixture.world);
    std::vector<input::RawInputEvent> raw = rawOf(fixture);
    REQUIRE(raw.size() == 2);
    CHECK(raw[0].phase == input::RawInputEvent::Phase::Ended);
    CHECK(raw[1].phase == input::RawInputEvent::Phase::Ended);
}

TEST_CASE("events come out in KeyCode order, which is an order something promises")
{
    // R10: an observable order has to come from a container that has one. Two
    // keys pressed in the same frame arrive from the OS in whatever order the
    // driver produced, and a replay must not depend on it.
    Fixture fixture;
    fixture.press("Z");
    fixture.press("A");
    fixture.system.dispatchSimTick(*fixture.world, 1);

    std::vector<input::RawInputEvent> raw = rawOf(fixture);
    REQUIRE(raw.size() == 2);
    CHECK(raw[0].keyCode < raw[1].keyCode);
    CHECK(raw[0].keyCode == fixture.keyCode("A"));
}

TEST_CASE("IsKeyDown reads the device and ignores what the UI took")
{
    // The opposite of the events' second argument, deliberately: a poll asks
    // what the hardware is doing. A caller who wants the UI-aware answer wants
    // an `InputAction`, which is also the one that can be rebound.
    Fixture fixture;
    CHECK_FALSE(fixture.system.isKeyDown(fixture.keyCode("Space")));

    fixture.press("Space");
    CHECK(fixture.system.isKeyDown(fixture.keyCode("Space")));
    fixture.system.setKeyboardCapturedByUi(true);
    CHECK(fixture.system.isKeyDown(fixture.keyCode("Space")));

    fixture.release("Space");
    CHECK_FALSE(fixture.system.isKeyDown(fixture.keyCode("Space")));
}
