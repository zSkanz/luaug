#include "luaug/input/input.h"

#include "luaug/scene/world.h"

#include <algorithm>
#include <cmath>

namespace luaug::input {
namespace {

// `Enum.KeyCode`'s layout, as ranges rather than as ninety-four constants.
//
// The enum is generated from `api/defs/enums.api.luau` in exactly this order and
// numbered sequentially from 0, which is the file's own stated rule. These
// bounds are that rule written down where the resolver can use it, and
// `registerSceneTypes` checks them against the registered descriptor at boot --
// so a KeyCode item inserted in the middle is a boot failure rather than a
// binding that quietly names a different key.
constexpr i32 KeyboardFirst = 1;
constexpr i32 KeyboardCount = 63;

// The keyboard block IS `platform::Key`, item for item and in the same order,
// which is what makes `keyCodeOf` a subtraction rather than a table. Asserted
// rather than assumed: a key added to one enum and not the other would silently
// shift every gamepad code by one.
static_assert(static_cast<i32>(platform::Key::Count) == KeyboardCount + 1,
              "Enum.KeyCode's keyboard block and platform::Key must be the same list");
constexpr i32 MouseButtonFirst = KeyboardFirst + KeyboardCount; // 64
constexpr i32 MouseButtonCount = 5;
constexpr i32 MouseMovement = MouseButtonFirst + MouseButtonCount; // 69
constexpr i32 MouseWheel = MouseMovement + 1;                      // 70
constexpr i32 PadButtonFirst = MouseWheel + 1;                     // 71
constexpr i32 PadButtonCount = 15;
constexpr i32 PadAxisFirst = PadButtonFirst + PadButtonCount; // 86
constexpr i32 PadAxisCount = 6;
constexpr i32 LeftThumbstick = PadAxisFirst + PadAxisCount; // 92
constexpr i32 RightThumbstick = LeftThumbstick + 1;         // 93

// The axes, by name, so the two stick composites can find their halves.
constexpr i32 LeftStickX = PadAxisFirst;
constexpr i32 LeftStickY = PadAxisFirst + 1;
constexpr i32 RightStickX = PadAxisFirst + 2;
constexpr i32 RightStickY = PadAxisFirst + 3;

static_assert(RightThumbstick + 1 == static_cast<i32>(kKeyCodeCount),
              "the KeyCode ranges above must cover the whole enum with no gap");

[[nodiscard]] constexpr bool inRange(i32 value, i32 first, i32 count) noexcept
{
    return value >= first && value < first + count;
}

[[nodiscard]] i32 keyCodeOf(platform::Key key) noexcept
{
    const auto raw = static_cast<i32>(key);
    if (raw <= 0 || raw > KeyboardCount)
        return 0;
    // `platform::Key` counts from 1 after `Unknown`, and so does the keyboard
    // block of `Enum.KeyCode`, in the same order. The two tables are generated
    // from one list on purpose (api/defs/enums.api.luau's header says so).
    return KeyboardFirst + raw - 1;
}

[[nodiscard]] i32 keyCodeOf(platform::MouseButton button) noexcept
{
    const auto raw = static_cast<i32>(button);
    if (raw <= 0 || raw > MouseButtonCount)
        return 0;
    return MouseButtonFirst + raw - 1;
}

[[nodiscard]] i32 keyCodeOf(platform::GamepadButton button) noexcept
{
    const auto raw = static_cast<i32>(button);
    if (raw <= 0 || raw > PadButtonCount)
        return 0;
    return PadButtonFirst + raw - 1;
}

[[nodiscard]] i32 keyCodeOf(platform::GamepadAxis axis) noexcept
{
    const auto raw = static_cast<i32>(axis);
    if (raw <= 0 || raw > PadAxisCount)
        return 0;
    return PadAxisFirst + raw - 1;
}

[[nodiscard]] bool valid(i32 keyCode) noexcept
{
    return keyCode > 0 && keyCode < static_cast<i32>(kKeyCodeCount);
}

// Half deflection, which is what an analogue source bound to a `Bool` action
// counts as pressed past. Named rather than written twice, because the trigger
// path and the stick path both need it and a threshold that drifted apart
// between them would be a trigger that fires at a different point than the
// stick that mirrors it.
constexpr f32 AnalogPressThreshold = 0.5f;

} // namespace

DeviceType deviceOf(i32 keyCode) noexcept
{
    if (inRange(keyCode, PadButtonFirst, PadButtonCount) || inRange(keyCode, PadAxisFirst, PadAxisCount) ||
        keyCode == LeftThumbstick || keyCode == RightThumbstick) {
        return DeviceType::Gamepad;
    }
    // Everything else, `Unknown` included. There is no third answer to give:
    // a binding that names nothing is not a touch binding.
    return DeviceType::KeyboardMouse;
}

bool isAnalog(i32 keyCode) noexcept
{
    return keyCode == MouseMovement || keyCode == MouseWheel || inRange(keyCode, PadAxisFirst, PadAxisCount) ||
           keyCode == LeftThumbstick || keyCode == RightThumbstick;
}

// The four names no device event carries, so no `platform` table has them.
// Indexed by KeyCode minus the block's first value, which is why they are laid
// out as two pairs rather than as a map.
constexpr std::string_view AnalogNames[] = {"MouseMovement", "MouseWheel"};
constexpr std::string_view StickNames[] = {"LeftThumbstick", "RightThumbstick"};

i32 keyCodeFromName(std::string_view name) noexcept
{
    if (name.empty())
        return 0;

    if (const platform::Key key = platform::keyFromName(name); key != platform::Key::Unknown)
        return keyCodeOf(key);
    if (const platform::MouseButton button = platform::mouseButtonFromName(name);
        button != platform::MouseButton::Unknown) {
        return keyCodeOf(button);
    }
    if (const platform::GamepadButton button = platform::gamepadButtonFromName(name);
        button != platform::GamepadButton::Unknown) {
        return keyCodeOf(button);
    }
    if (const platform::GamepadAxis axis = platform::gamepadAxisFromName(name);
        axis != platform::GamepadAxis::Unknown) {
        return keyCodeOf(axis);
    }

    for (i32 index = 0; index < 2; ++index) {
        if (name == AnalogNames[index])
            return MouseMovement + index;
        if (name == StickNames[index])
            return LeftThumbstick + index;
    }
    return 0;
}

std::string_view keyCodeName(i32 keyCode) noexcept
{
    if (inRange(keyCode, KeyboardFirst, KeyboardCount))
        return platform::keyName(static_cast<platform::Key>(keyCode - KeyboardFirst + 1));
    if (inRange(keyCode, MouseButtonFirst, MouseButtonCount))
        return platform::mouseButtonName(static_cast<platform::MouseButton>(keyCode - MouseButtonFirst + 1));
    if (inRange(keyCode, PadButtonFirst, PadButtonCount))
        return platform::gamepadButtonName(static_cast<platform::GamepadButton>(keyCode - PadButtonFirst + 1));
    if (inRange(keyCode, PadAxisFirst, PadAxisCount))
        return platform::gamepadAxisName(static_cast<platform::GamepadAxis>(keyCode - PadAxisFirst + 1));
    if (keyCode == MouseMovement || keyCode == MouseWheel)
        return AnalogNames[keyCode - MouseMovement];
    if (keyCode == LeftThumbstick || keyCode == RightThumbstick)
        return StickNames[keyCode - LeftThumbstick];
    return {};
}

void InputSystem::pumpFrame(std::span<const platform::Event> events)
{
    for (const platform::Event& event : events) {
        switch (event.type) {
        case platform::EventType::KeyDown:
        case platform::EventType::KeyUp: {
            const i32 code = keyCodeOf(event.key);
            if (!valid(code))
                break;
            // A repeat is a key that is already down. Recording it as a fresh
            // press would make `Pressed` fire again every autorepeat interval,
            // which is a jump per repeat rather than a jump per press.
            m_state.held[static_cast<usize>(code)] = event.type == platform::EventType::KeyDown;
            m_state.lastDevice = DeviceType::KeyboardMouse;
            break;
        }
        case platform::EventType::MouseButtonDown:
        case platform::EventType::MouseButtonUp: {
            const i32 code = keyCodeOf(event.button);
            if (!valid(code))
                break;
            m_state.held[static_cast<usize>(code)] = event.type == platform::EventType::MouseButtonDown;
            m_state.lastDevice = DeviceType::KeyboardMouse;
            break;
        }
        case platform::EventType::MouseMoved:
            m_state.pointer = core::Vec2{event.pointerX, event.pointerY};
            // Accumulated, not sampled: several motion events arrive per frame,
            // and a tick that read only the last one would lose most of a fast
            // flick. Y is negated so that moving the mouse away from the player
            // is +Y, which is the direction the `Up` composite means.
            m_simPointerDelta = m_simPointerDelta + core::Vec2{event.pointerDeltaX, -event.pointerDeltaY};
            m_renderPointerDelta = m_renderPointerDelta + core::Vec2{event.pointerDeltaX, -event.pointerDeltaY};
            // Deliberately does NOT set `lastDevice`: a mouse nudged by a desk
            // bump would otherwise steal every prompt on screen from a gamepad
            // the player is holding.
            break;
        case platform::EventType::MouseWheel:
            m_simWheel = m_simWheel + core::Vec2{event.wheelX, event.wheelY};
            m_renderWheel = m_renderWheel + core::Vec2{event.wheelX, event.wheelY};
            m_state.lastDevice = DeviceType::KeyboardMouse;
            break;
        case platform::EventType::GamepadButtonDown:
        case platform::EventType::GamepadButtonUp: {
            const i32 code = keyCodeOf(event.gamepadButton);
            if (!valid(code))
                break;
            m_state.held[static_cast<usize>(code)] = event.type == platform::EventType::GamepadButtonDown;
            m_state.lastDevice = DeviceType::Gamepad;
            break;
        }
        case platform::EventType::GamepadAxisMoved: {
            const i32 code = keyCodeOf(event.gamepadAxis);
            if (!valid(code))
                break;
            m_state.axis[static_cast<usize>(code)] = event.axisValue;
            // Only a real deflection claims the device. A stick resting inside
            // its dead zone still emits events on most hardware, and letting
            // those set `lastDevice` would flip a HUD's prompts to gamepad
            // while nobody is touching one.
            if (std::abs(event.axisValue) > AnalogPressThreshold)
                m_state.lastDevice = DeviceType::Gamepad;
            break;
        }
        case platform::EventType::GamepadRemoved:
            // Every gamepad input goes to rest. The pad is gone; anything still
            // recorded as held would stay held forever.
            for (i32 code = PadButtonFirst; code < PadButtonFirst + PadButtonCount; ++code)
                m_state.held[static_cast<usize>(code)] = false;
            for (i32 code = PadAxisFirst; code < PadAxisFirst + PadAxisCount; ++code)
                m_state.axis[static_cast<usize>(code)] = 0.0f;
            break;
        case platform::EventType::WindowFocusGained:
            m_state.focused = true;
            break;
        case platform::EventType::WindowFocusLost:
            m_state.focused = false;
            break;
        default:
            break;
        }
    }
}

void InputSystem::setSnapshot(const DeviceState& state) noexcept
{
    m_state = state;
    // The deltas come from the snapshot rather than accumulating on top of it:
    // a replay hands the state a tick should see, and adding the live mouse to
    // it would make the replay depend on whether anybody moved the pointer.
    m_simPointerDelta = state.pointerDelta;
    m_renderPointerDelta = state.pointerDelta;
    m_simWheel = state.wheel;
    m_renderWheel = state.wheel;
}

namespace {

// One binding's contribution to its action, in the action's own currency.
struct Contribution
{
    bool pressed = false;
    core::Vec3 axis;
};

[[nodiscard]] bool digital(const DeviceState& state, const std::array<bool, kKeyCodeCount>& consumed, i32 code) noexcept
{
    if (!valid(code) || consumed[static_cast<usize>(code)])
        return false;
    if (isAnalog(code)) {
        // An analogue source on a digital question. `Enum.KeyCode`'s doc states
        // the answer rather than leaving it to be discovered: past half
        // deflection counts as pressed, because refusing the binding outright
        // would let a rebinding UI hand the player an unusable choice.
        if (code == LeftThumbstick)
            return std::abs(state.axis[LeftStickX]) > AnalogPressThreshold ||
                   std::abs(state.axis[LeftStickY]) > AnalogPressThreshold;
        if (code == RightThumbstick)
            return std::abs(state.axis[RightStickX]) > AnalogPressThreshold ||
                   std::abs(state.axis[RightStickY]) > AnalogPressThreshold;
        return std::abs(state.axis[static_cast<usize>(code)]) > AnalogPressThreshold;
    }
    return state.held[static_cast<usize>(code)];
}

// The signed contribution of a pair of composite keys: +1 for the positive one,
// -1 for the negative one, 0 for both or neither. Both-at-once cancelling is
// what makes holding A and D stand still rather than drift by whichever the
// engine happened to read last.
[[nodiscard]] f32 composite(const DeviceState& state, const std::array<bool, kKeyCodeCount>& consumed, i32 positive,
                            i32 negative) noexcept
{
    const f32 up = digital(state, consumed, positive) ? 1.0f : 0.0f;
    const f32 down = digital(state, consumed, negative) ? 1.0f : 0.0f;
    return up - down;
}

[[nodiscard]] core::Vec2 stick(const DeviceState& state, i32 code) noexcept
{
    if (code == LeftThumbstick) {
        // Y negated, because SDL reports a stick's Y positive DOWNWARD and the
        // `Up` composite means +Y. One convention reaches the game, and this is
        // the line that establishes it.
        return core::Vec2{state.axis[LeftStickX], -state.axis[LeftStickY]};
    }
    return core::Vec2{state.axis[RightStickX], -state.axis[RightStickY]};
}

} // namespace

namespace {

// Which `Enum.UserInputType` a `KeyCode` produces. Coarser than `deviceOf`,
// because these are the kinds a raw handler switches on rather than the families
// a prompt draws for.
[[nodiscard]] UserInputType userInputTypeOf(i32 keyCode) noexcept
{
    if (inRange(keyCode, KeyboardFirst, KeyboardCount))
        return UserInputType::Keyboard;
    if (keyCode == MouseMovement)
        return UserInputType::MouseMovement;
    if (keyCode == MouseWheel)
        return UserInputType::MouseWheel;
    if (inRange(keyCode, MouseButtonFirst, MouseButtonCount)) {
        // The first three get their own items because `== MouseButton1` is the
        // overwhelmingly common test; the fourth and fifth have no name in the
        // enum and report as the primary's neighbour rather than as `None`.
        switch (keyCode - MouseButtonFirst) {
        case 0:
            return UserInputType::MouseButton1;
        case 1:
            return UserInputType::MouseButton2;
        case 2:
            return UserInputType::MouseButton3;
        default:
            return UserInputType::MouseButton1;
        }
    }
    if (inRange(keyCode, PadButtonFirst, PadButtonCount) || inRange(keyCode, PadAxisFirst, PadAxisCount) ||
        keyCode == LeftThumbstick || keyCode == RightThumbstick)
        return UserInputType::Gamepad;
    return UserInputType::None;
}

// Whether the interface already took this input. Two claims, one per device
// family, and each one covers exactly the codes that family produces -- which is
// what stops a HUD button under the pointer from also eating the jump key.
[[nodiscard]] bool consumedByUi(i32 keyCode, bool pointerCaptured, bool keyboardCaptured) noexcept
{
    if (pointerCaptured &&
        (inRange(keyCode, MouseButtonFirst, MouseButtonCount) || keyCode == MouseMovement || keyCode == MouseWheel))
        return true;
    return keyboardCaptured && inRange(keyCode, KeyboardFirst, KeyboardCount);
}

} // namespace

bool InputSystem::isKeyDown(i32 keyCode) const noexcept
{
    // The same `digital`, with nothing consumed: a poll is about the device.
    static const std::array<bool, kKeyCodeCount> nothingConsumed{};
    return digital(m_state, nothingConsumed, keyCode);
}

std::span<const RawInputEvent> InputSystem::drainRawEvents() noexcept
{
    m_rawDrained.swap(m_rawEvents);
    m_rawEvents.clear();
    return m_rawDrained;
}

void InputSystem::collectRawEvents(core::Vec2 pointerDelta, core::Vec2 wheel)
{
    m_rawEvents.clear();

    const core::Vec3 pointer{m_state.pointer.x, m_state.pointer.y, 0.0f};

    // **Walked by KeyCode, ascending.** The order these fire in is observable --
    // a handler may write to the world -- so it has to come from something that
    // promises one (R10), and an array index is the cheapest promise there is.
    for (i32 code = 1; code < static_cast<i32>(kKeyCodeCount); ++code) {
        const auto slot = static_cast<usize>(code);
        // `digital` rather than `held`, so a trigger crossing half deflection
        // begins and ends like a button -- which is what `Enum.KeyCode`'s own
        // doc promises and what a `Bool` action already does with one.
        static const std::array<bool, kKeyCodeCount> nothingConsumed{};
        const bool held = digital(m_state, nothingConsumed, code);
        const bool was = m_hasPrevious && digital(m_previous, nothingConsumed, code);
        if (held == was)
            continue;

        const UserInputType kind = userInputTypeOf(code);
        const bool consumed =
            held ? consumedByUi(code, m_uiCapturedPointer, m_uiCapturedKeyboard) : m_beganConsumed[slot];
        if (held)
            m_beganConsumed[slot] = consumed;

        RawInputEvent event;
        event.phase = held ? RawInputEvent::Phase::Began : RawInputEvent::Phase::Ended;
        event.userInputType = kind;
        event.keyCode = code;
        event.position = pointer;
        event.uiConsumed = consumed;
        m_rawEvents.push_back(event);
    }

    // Motion, once per tick, with the delta ACCUMULATED since the last dispatch.
    // A handler that saw only the last device event would lose most of a fast
    // flick, which is the same reason the deltas are accumulated at all.
    if (pointerDelta.x != 0.0f || pointerDelta.y != 0.0f) {
        RawInputEvent event;
        event.phase = RawInputEvent::Phase::Changed;
        event.userInputType = UserInputType::MouseMovement;
        event.position = pointer;
        event.delta = core::Vec3{pointerDelta.x, pointerDelta.y, 0.0f};
        event.uiConsumed = m_uiCapturedPointer;
        m_rawEvents.push_back(event);
    }

    if (wheel.x != 0.0f || wheel.y != 0.0f) {
        RawInputEvent event;
        event.phase = RawInputEvent::Phase::Changed;
        event.userInputType = UserInputType::MouseWheel;
        // `z` is where the wheel lives, in both fields, so a handler reads one
        // component whichever it reached for.
        event.position = core::Vec3{m_state.pointer.x, m_state.pointer.y, wheel.y};
        event.delta = core::Vec3{wheel.x, 0.0f, wheel.y};
        event.uiConsumed = m_uiCapturedPointer;
        m_rawEvents.push_back(event);
    }

    // Gamepad axes, which have no press to begin or end: a stick that moved is
    // `InputChanged` with its deflection, and one resting at the same value
    // produces nothing at all.
    for (i32 code = PadAxisFirst; code < PadAxisFirst + PadAxisCount; ++code) {
        const auto slot = static_cast<usize>(code);
        const f32 value = m_state.axis[slot];
        const f32 was = m_hasPrevious ? m_previous.axis[slot] : 0.0f;
        if (value == was)
            continue;

        RawInputEvent event;
        event.phase = RawInputEvent::Phase::Changed;
        event.userInputType = UserInputType::Gamepad;
        event.keyCode = code;
        event.position = core::Vec3{value, 0.0f, 0.0f};
        event.delta = core::Vec3{value - was, 0.0f, 0.0f};
        m_rawEvents.push_back(event);
    }

    m_previous = m_state;
    m_hasPrevious = true;
}

void InputSystem::dispatch(scene::World& world, Rate rate)
{
    const core::NameAtom pressedAtom = world.atoms().intern("Pressed");
    const core::NameAtom releasedAtom = world.atoms().intern("Released");
    const core::NameAtom stateChangedAtom = world.atoms().intern("StateChanged");

    const bool simulation = rate == Rate::Simulation;
    const core::Vec2 pointerDelta = simulation ? m_simPointerDelta : m_renderPointerDelta;
    const core::Vec2 wheel = simulation ? m_simWheel : m_renderWheel;

    // Every context on this clock, INCLUDING the disabled ones. A disabled
    // context's actions have to be walked so that anything they were holding is
    // released: skipping them would leave a jump held true for the rest of the
    // session, which is what closing a menu mid-press would otherwise do.
    m_contexts.clear();
    world.inputContexts().forEach([&](core::InstanceId id, const scene::InputContextComponent& context) {
        if (context.rate == static_cast<i32>(rate) && !world.destroyed(id))
            m_contexts.emplace_back(context.priority, id);
    });

    // Stable, so two contexts at one priority keep the pool's order -- which is
    // a pure function of the operation sequence and therefore the same on every
    // run (R10). An unstable sort here would be a replay divergence that only
    // shows up once two contexts happen to tie.
    std::stable_sort(m_contexts.begin(), m_contexts.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });

    m_consumed.fill(false);

    // The UI's claim on the pointer, applied before any context resolves --
    // which is what makes it behave like the highest-priority sinking context
    // without being one. Mouse codes only: a key pressed while the pointer rests
    // over a HUD is still the game's.
    if (m_uiCapturedPointer) {
        for (i32 code = MouseButtonFirst; code < MouseButtonFirst + MouseButtonCount; ++code)
            m_consumed[static_cast<usize>(code)] = true;
        m_consumed[static_cast<usize>(MouseMovement)] = true;
        m_consumed[static_cast<usize>(MouseWheel)] = true;
    }

    // The keyboard half of the same claim: a focused `TextInput` eats the keys.
    // Without it a player typing `w` into a chat box walks forward, which is the
    // same defect the pointer flag fixes one device over.
    if (m_uiCapturedKeyboard) {
        for (i32 code = KeyboardFirst; code < KeyboardFirst + KeyboardCount; ++code)
            m_consumed[static_cast<usize>(code)] = true;
    }

    // The raw events (ADR 0041), collected HERE: after the UI's claims are known
    // and before any context resolves. They describe what the device did, so a
    // sinking context -- which is a fact about actions -- must not change them,
    // while the UI's claim -- which is a fact about who the input reached --
    // must.
    if (simulation)
        collectRawEvents(pointerDelta, wheel);

    for (const auto& [priority, contextId] : m_contexts) {
        const scene::InputContextComponent* context = world.inputContexts().find(contextId);
        if (context == nullptr)
            continue;

        for (core::InstanceId actionId = world.firstChild(contextId); actionId.valid();
             actionId = world.nextSibling(actionId)) {
            scene::InputActionComponent* action = world.inputActions().find(actionId);
            if (action == nullptr)
                continue;

            const auto type = static_cast<ActionType>(action->type);
            core::Vec3 value;
            bool pressed = false;

            // A disabled action, or one inside a disabled context, resolves to
            // NOTHING rather than being skipped. The distinction matters at the
            // moment of disabling: leaving the last value in place is how a key
            // released while a menu was open never reaches the game, and how
            // `Released` fails to fire for something a handler started.
            const bool live = context->enabled && action->enabled;
            if (!live) {
                // Nothing to compute; the zero value above is the answer.
            }
            else if (type == ActionType::ViewportPosition) {
                // The pointer's POSITION, which no binding names: an action of
                // this type answers where the cursor is, and a binding on it
                // would be a field with nothing to say.
                value = core::Vec3{m_state.pointer.x, m_state.pointer.y, 0.0f};
            }
            else if (type != ActionType::Direction3D) {
                for (core::InstanceId bindingId = world.firstChild(actionId); bindingId.valid();
                     bindingId = world.nextSibling(bindingId)) {
                    const scene::InputBindingComponent* binding = world.inputBindings().find(bindingId);
                    if (binding == nullptr)
                        continue;

                    const f32 scale = binding->scale;
                    switch (type) {
                    case ActionType::Bool:
                        pressed = pressed || digital(m_state, m_consumed, binding->keyCode);
                        break;
                    case ActionType::Direction1D: {
                        f32 amount = composite(m_state, m_consumed, binding->up, binding->down);
                        const i32 code = binding->keyCode;
                        if (valid(code) && !m_consumed[static_cast<usize>(code)]) {
                            if (code == MouseWheel)
                                amount += wheel.y;
                            else if (isAnalog(code))
                                amount += m_state.axis[static_cast<usize>(code)];
                            else if (m_state.held[static_cast<usize>(code)])
                                amount += 1.0f;
                        }
                        value.x += amount * scale;
                        break;
                    }
                    case ActionType::Direction2D: {
                        core::Vec2 amount{composite(m_state, m_consumed, binding->right, binding->left),
                                          composite(m_state, m_consumed, binding->up, binding->down)};
                        const i32 code = binding->keyCode;
                        if (valid(code) && !m_consumed[static_cast<usize>(code)]) {
                            if (code == MouseMovement)
                                amount = amount + pointerDelta;
                            else if (code == LeftThumbstick || code == RightThumbstick)
                                amount = amount + stick(m_state, code);
                            else if (code == MouseWheel)
                                amount = amount + wheel;
                        }
                        value.x += amount.x * scale;
                        value.y += amount.y * scale;
                        break;
                    }
                    case ActionType::Direction3D:
                    case ActionType::ViewportPosition:
                        break;
                    }
                }
            }

            // Deliberately NOT clamped. A key contributes 1, a stick its
            // deflection, and a mouse-motion binding contributes PIXELS -- so a
            // clamp to the unit range would make every look control unusable,
            // and a clamp that skipped mouse bindings would make the rule
            // depend on which key a binding happened to name.
            const bool changed = value != action->axis || pressed != action->pressed;
            action->axis = value;
            action->pressed = pressed;

            if (!changed)
                continue;

            if (type == ActionType::Bool) {
                world.changes().push(scene::Change{
                    scene::ChangeKind::InstanceEventNoArgs, actionId, {}, pressed ? pressedAtom : releasedAtom});
            }
            world.changes().push(scene::Change{scene::ChangeKind::InstanceEventNoArgs, actionId, {}, stateChangedAtom});
        }

        if (!context->sink || !context->enabled)
            continue;

        // Consumed AFTER the context resolved, so an action inside a sinking
        // context still reads its own input. Per key rather than per context: a
        // dialog that sinks Escape leaves W to whatever is underneath it.
        for (core::InstanceId actionId = world.firstChild(contextId); actionId.valid();
             actionId = world.nextSibling(actionId)) {
            const scene::InputActionComponent* action = world.inputActions().find(actionId);
            // A disabled action sinks nothing. The alternative is a dead action
            // silently eating a key for the rest of the session, with no way to
            // tell from the outside which context is doing it.
            if (action == nullptr || !action->enabled)
                continue;
            for (core::InstanceId bindingId = world.firstChild(actionId); bindingId.valid();
                 bindingId = world.nextSibling(bindingId)) {
                const scene::InputBindingComponent* binding = world.inputBindings().find(bindingId);
                if (binding == nullptr)
                    continue;
                for (const i32 code : {binding->keyCode, binding->up, binding->down, binding->left, binding->right}) {
                    if (valid(code))
                        m_consumed[static_cast<usize>(code)] = true;
                }
            }
        }
    }

    world.engineState().pointerPosition = m_state.pointer;
    world.engineState().lastInputDeviceType = static_cast<i32>(m_state.lastDevice);

    if (simulation) {
        m_simPointerDelta = core::Vec2{};
        m_simWheel = core::Vec2{};
    }
    else {
        m_renderPointerDelta = core::Vec2{};
        m_renderWheel = core::Vec2{};
    }
}

void InputSystem::dispatchSimTick(scene::World& world, u64)
{
    dispatch(world, Rate::Simulation);
}

void InputSystem::dispatchRenderRate(scene::World& world)
{
    dispatch(world, Rate::Render);
}

void InputSystem::releaseAll(scene::World& world)
{
    m_state.held.fill(false);
    m_state.axis.fill(0.0f);
    m_simPointerDelta = core::Vec2{};
    m_renderPointerDelta = core::Vec2{};
    m_simWheel = core::Vec2{};
    m_renderWheel = core::Vec2{};
    // Both rates, because a held key belongs to whichever context bound it and
    // losing focus releases it for all of them.
    dispatch(world, Rate::Simulation);
    dispatch(world, Rate::Render);
}

} // namespace luaug::input
