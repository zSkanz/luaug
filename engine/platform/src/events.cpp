#include "luaug/platform/event.h"
#include "luaug/platform/platform.h"
#include "luaug/platform/sdl_interop.h"

#include <algorithm>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace luaug::platform {
namespace {

// Main-thread only, like SDL's own event queue. The buffers are kept between
// frames so a steady-state frame does no allocation.
std::vector<SDL_Event> g_rawEvents;
// Paths dropped onto a window this pump. See `droppedFiles`.
std::vector<std::string> g_droppedFiles;
std::vector<Event> g_events;

// Written as an explicit table rather than arithmetic on the scancode range:
// the F-keys happen to be contiguous today, and a silent reordering upstream
// would turn a pin bump into a wrong-key bug that nothing would catch.
Key translateScancode(SDL_Scancode scancode) noexcept
{
    switch (scancode) {
    case SDL_SCANCODE_ESCAPE:
        return Key::Escape;
    case SDL_SCANCODE_F1:
        return Key::F1;
    case SDL_SCANCODE_F2:
        return Key::F2;
    case SDL_SCANCODE_F3:
        return Key::F3;
    case SDL_SCANCODE_F4:
        return Key::F4;
    case SDL_SCANCODE_F5:
        return Key::F5;
    case SDL_SCANCODE_F6:
        return Key::F6;
    case SDL_SCANCODE_F7:
        return Key::F7;
    case SDL_SCANCODE_F8:
        return Key::F8;
    case SDL_SCANCODE_F9:
        return Key::F9;
    case SDL_SCANCODE_F10:
        return Key::F10;
    case SDL_SCANCODE_F11:
        return Key::F11;
    case SDL_SCANCODE_F12:
        return Key::F12;
    case SDL_SCANCODE_A:
        return Key::A;
    case SDL_SCANCODE_B:
        return Key::B;
    case SDL_SCANCODE_C:
        return Key::C;
    case SDL_SCANCODE_D:
        return Key::D;
    case SDL_SCANCODE_E:
        return Key::E;
    case SDL_SCANCODE_F:
        return Key::F;
    case SDL_SCANCODE_G:
        return Key::G;
    case SDL_SCANCODE_H:
        return Key::H;
    case SDL_SCANCODE_I:
        return Key::I;
    case SDL_SCANCODE_J:
        return Key::J;
    case SDL_SCANCODE_K:
        return Key::K;
    case SDL_SCANCODE_L:
        return Key::L;
    case SDL_SCANCODE_M:
        return Key::M;
    case SDL_SCANCODE_N:
        return Key::N;
    case SDL_SCANCODE_O:
        return Key::O;
    case SDL_SCANCODE_P:
        return Key::P;
    case SDL_SCANCODE_Q:
        return Key::Q;
    case SDL_SCANCODE_R:
        return Key::R;
    case SDL_SCANCODE_S:
        return Key::S;
    case SDL_SCANCODE_T:
        return Key::T;
    case SDL_SCANCODE_U:
        return Key::U;
    case SDL_SCANCODE_V:
        return Key::V;
    case SDL_SCANCODE_W:
        return Key::W;
    case SDL_SCANCODE_X:
        return Key::X;
    case SDL_SCANCODE_Y:
        return Key::Y;
    case SDL_SCANCODE_Z:
        return Key::Z;
    case SDL_SCANCODE_0:
        return Key::Digit0;
    case SDL_SCANCODE_1:
        return Key::Digit1;
    case SDL_SCANCODE_2:
        return Key::Digit2;
    case SDL_SCANCODE_3:
        return Key::Digit3;
    case SDL_SCANCODE_4:
        return Key::Digit4;
    case SDL_SCANCODE_5:
        return Key::Digit5;
    case SDL_SCANCODE_6:
        return Key::Digit6;
    case SDL_SCANCODE_7:
        return Key::Digit7;
    case SDL_SCANCODE_8:
        return Key::Digit8;
    case SDL_SCANCODE_9:
        return Key::Digit9;
    case SDL_SCANCODE_SPACE:
        return Key::Space;
    case SDL_SCANCODE_RETURN:
        return Key::Return;
    case SDL_SCANCODE_TAB:
        return Key::Tab;
    case SDL_SCANCODE_BACKSPACE:
        return Key::Backspace;
    case SDL_SCANCODE_LSHIFT:
        return Key::LeftShift;
    case SDL_SCANCODE_RSHIFT:
        return Key::RightShift;
    case SDL_SCANCODE_LCTRL:
        return Key::LeftControl;
    case SDL_SCANCODE_RCTRL:
        return Key::RightControl;
    case SDL_SCANCODE_LALT:
        return Key::LeftAlt;
    case SDL_SCANCODE_RALT:
        return Key::RightAlt;
    case SDL_SCANCODE_LEFT:
        return Key::Left;
    case SDL_SCANCODE_RIGHT:
        return Key::Right;
    case SDL_SCANCODE_UP:
        return Key::Up;
    case SDL_SCANCODE_DOWN:
        return Key::Down;
    default:
        return Key::Unknown;
    }
}

// One table, walked in both directions. Written out rather than derived from
// the enumerator names, because a table a compiler cannot check is one a test
// has to: `platform_tests` walks every enumerator and requires a round trip
// through both functions.
//
// The digits are `Digit0` rather than `0`, which is the enumerator's name and
// not the key's legend. M6 changed them: these names, the mouse and gamepad
// names beside them, and `Enum.KeyCode`'s items are ONE spelling space -- a
// recorded input stream is written in it, and `input` resolves a name to a
// KeyCode by walking these tables. A legend that differed from the item name
// for ten of the ninety-four would have meant a second table to keep in step.
struct KeyNaming
{
    Key key;
    std::string_view name;
};

constexpr KeyNaming KeyNames[] = {
    {Key::Escape, "Escape"},
    {Key::F1, "F1"},
    {Key::F2, "F2"},
    {Key::F3, "F3"},
    {Key::F4, "F4"},
    {Key::F5, "F5"},
    {Key::F6, "F6"},
    {Key::F7, "F7"},
    {Key::F8, "F8"},
    {Key::F9, "F9"},
    {Key::F10, "F10"},
    {Key::F11, "F11"},
    {Key::F12, "F12"},
    {Key::A, "A"},
    {Key::B, "B"},
    {Key::C, "C"},
    {Key::D, "D"},
    {Key::E, "E"},
    {Key::F, "F"},
    {Key::G, "G"},
    {Key::H, "H"},
    {Key::I, "I"},
    {Key::J, "J"},
    {Key::K, "K"},
    {Key::L, "L"},
    {Key::M, "M"},
    {Key::N, "N"},
    {Key::O, "O"},
    {Key::P, "P"},
    {Key::Q, "Q"},
    {Key::R, "R"},
    {Key::S, "S"},
    {Key::T, "T"},
    {Key::U, "U"},
    {Key::V, "V"},
    {Key::W, "W"},
    {Key::X, "X"},
    {Key::Y, "Y"},
    {Key::Z, "Z"},
    {Key::Digit0, "Digit0"},
    {Key::Digit1, "Digit1"},
    {Key::Digit2, "Digit2"},
    {Key::Digit3, "Digit3"},
    {Key::Digit4, "Digit4"},
    {Key::Digit5, "Digit5"},
    {Key::Digit6, "Digit6"},
    {Key::Digit7, "Digit7"},
    {Key::Digit8, "Digit8"},
    {Key::Digit9, "Digit9"},
    {Key::Space, "Space"},
    {Key::Return, "Return"},
    {Key::Tab, "Tab"},
    {Key::Backspace, "Backspace"},
    {Key::LeftShift, "LeftShift"},
    {Key::RightShift, "RightShift"},
    {Key::LeftControl, "LeftControl"},
    {Key::RightControl, "RightControl"},
    {Key::LeftAlt, "LeftAlt"},
    {Key::RightAlt, "RightAlt"},
    {Key::Left, "Left"},
    {Key::Right, "Right"},
    {Key::Up, "Up"},
    {Key::Down, "Down"},
};

// Every gamepad SDL reports is opened, because an unopened one produces no
// events at all: SDL only sends button and axis events for a gamepad somebody
// holds a handle to. Kept as a flat list rather than a map -- a machine has a
// handful of pads, and a linear scan over four entries beats a hash of one.
std::vector<SDL_Gamepad*> g_gamepads;

[[nodiscard]] MouseButton translateMouseButton(Uint8 button) noexcept
{
    switch (button) {
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    case SDL_BUTTON_X1:
        return MouseButton::X1;
    case SDL_BUTTON_X2:
        return MouseButton::X2;
    default:
        return MouseButton::Unknown;
    }
}

// An explicit table for the same reason `translateScancode` is one: the two
// enumerations happen to agree in order today, and a silent reordering upstream
// would turn a pin bump into a wrong-button bug nothing would catch.
[[nodiscard]] GamepadButton translateGamepadButton(Uint8 button) noexcept
{
    switch (static_cast<SDL_GamepadButton>(button)) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return GamepadButton::South;
    case SDL_GAMEPAD_BUTTON_EAST:
        return GamepadButton::East;
    case SDL_GAMEPAD_BUTTON_WEST:
        return GamepadButton::West;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return GamepadButton::North;
    case SDL_GAMEPAD_BUTTON_BACK:
        return GamepadButton::Back;
    case SDL_GAMEPAD_BUTTON_GUIDE:
        return GamepadButton::Guide;
    case SDL_GAMEPAD_BUTTON_START:
        return GamepadButton::Start;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return GamepadButton::LeftStick;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return GamepadButton::RightStick;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return GamepadButton::LeftShoulder;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return GamepadButton::RightShoulder;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return GamepadButton::DpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return GamepadButton::DpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return GamepadButton::DpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return GamepadButton::DpadRight;
    default:
        // Paddles, the touchpad button and the MISC range: real buttons on a
        // minority of hardware, and deliberately not in our enum (event.h).
        return GamepadButton::Unknown;
    }
}

[[nodiscard]] GamepadAxis translateGamepadAxis(Uint8 axis) noexcept
{
    switch (static_cast<SDL_GamepadAxis>(axis)) {
    case SDL_GAMEPAD_AXIS_LEFTX:
        return GamepadAxis::LeftX;
    case SDL_GAMEPAD_AXIS_LEFTY:
        return GamepadAxis::LeftY;
    case SDL_GAMEPAD_AXIS_RIGHTX:
        return GamepadAxis::RightX;
    case SDL_GAMEPAD_AXIS_RIGHTY:
        return GamepadAxis::RightY;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        return GamepadAxis::LeftTrigger;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        return GamepadAxis::RightTrigger;
    default:
        return GamepadAxis::Unknown;
    }
}

// SDL reports a stick over the whole signed range and a trigger over the
// positive half of it. Dividing both by 32767 would make a released trigger
// read -1, so the two are normalized differently -- and the asymmetry lives
// here, once, rather than in every caller that reads an axis.
[[nodiscard]] float normalizeAxis(GamepadAxis axis, Sint16 raw) noexcept
{
    constexpr float PositiveRange = 32767.0f;
    if (axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger)
        return std::clamp(static_cast<float>(raw) / PositiveRange, 0.0f, 1.0f);
    // -32768 divided by 32767 is slightly past -1, which is why this clamps
    // rather than trusting the division: an axis that can read -1.00003 makes
    // every "is this exactly -1" comparison downstream wrong once in a while.
    return std::clamp(static_cast<float>(raw) / PositiveRange, -1.0f, 1.0f);
}

struct MouseButtonNaming
{
    MouseButton button;
    std::string_view name;
};

constexpr MouseButtonNaming MouseButtonNames[] = {
    {MouseButton::Left, "MouseLeft"}, {MouseButton::Middle, "MouseMiddle"}, {MouseButton::Right, "MouseRight"},
    {MouseButton::X1, "MouseX1"},     {MouseButton::X2, "MouseX2"},
};

struct GamepadButtonNaming
{
    GamepadButton button;
    std::string_view name;
};

// Prefixed, because these names share a namespace with the key legends in the
// recorded input stream and in `Enum.KeyCode`: "Start" alone would be a key on
// some keyboard somewhere, and "South" alone means nothing to a reader.
constexpr GamepadButtonNaming GamepadButtonNames[] = {
    {GamepadButton::South, "ButtonSouth"},
    {GamepadButton::East, "ButtonEast"},
    {GamepadButton::West, "ButtonWest"},
    {GamepadButton::North, "ButtonNorth"},
    {GamepadButton::Back, "ButtonBack"},
    {GamepadButton::Guide, "ButtonGuide"},
    {GamepadButton::Start, "ButtonStart"},
    {GamepadButton::LeftStick, "ButtonLeftStick"},
    {GamepadButton::RightStick, "ButtonRightStick"},
    {GamepadButton::LeftShoulder, "ButtonLeftShoulder"},
    {GamepadButton::RightShoulder, "ButtonRightShoulder"},
    {GamepadButton::DpadUp, "DpadUp"},
    {GamepadButton::DpadDown, "DpadDown"},
    {GamepadButton::DpadLeft, "DpadLeft"},
    {GamepadButton::DpadRight, "DpadRight"},
};

struct GamepadAxisNaming
{
    GamepadAxis axis;
    std::string_view name;
};

constexpr GamepadAxisNaming GamepadAxisNames[] = {
    {GamepadAxis::LeftX, "LeftStickX"},        {GamepadAxis::LeftY, "LeftStickY"},
    {GamepadAxis::RightX, "RightStickX"},      {GamepadAxis::RightY, "RightStickY"},
    {GamepadAxis::LeftTrigger, "LeftTrigger"}, {GamepadAxis::RightTrigger, "RightTrigger"},
};

void openGamepad(SDL_JoystickID which)
{
    if (SDL_Gamepad* pad = SDL_OpenGamepad(which); pad != nullptr)
        g_gamepads.push_back(pad);
}

void closeGamepad(SDL_JoystickID which)
{
    const auto found = std::find_if(g_gamepads.begin(), g_gamepads.end(),
                                    [which](SDL_Gamepad* pad) { return SDL_GetGamepadID(pad) == which; });
    if (found == g_gamepads.end())
        return;
    SDL_CloseGamepad(*found);
    g_gamepads.erase(found);
}

void translate(const SDL_Event& raw, std::vector<Event>& out)
{
    switch (raw.type) {
    case SDL_EVENT_QUIT: {
        Event event;
        event.type = EventType::Quit;
        out.push_back(event);
        break;
    }
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
        Event event;
        event.type = EventType::WindowCloseRequested;
        event.windowId = raw.window.windowID;
        out.push_back(event);
        break;
    }
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        // Pixels rather than the logical size, because the consumer that
        // matters is the swapchain.
        Event event;
        event.type = EventType::WindowResized;
        event.windowId = raw.window.windowID;
        event.width = raw.window.data1;
        event.height = raw.window.data2;
        out.push_back(event);
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const Key key = translateScancode(raw.key.scancode);
        if (key == Key::Unknown)
            break;

        Event event;
        event.type = raw.type == SDL_EVENT_KEY_DOWN ? EventType::KeyDown : EventType::KeyUp;
        event.windowId = raw.key.windowID;
        event.key = key;
        event.repeat = raw.key.repeat;
        out.push_back(event);
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST: {
        Event event;
        event.type =
            raw.type == SDL_EVENT_WINDOW_FOCUS_GAINED ? EventType::WindowFocusGained : EventType::WindowFocusLost;
        event.windowId = raw.window.windowID;
        out.push_back(event);
        break;
    }
    case SDL_EVENT_TEXT_INPUT: {
        if (raw.text.text == nullptr)
            break;
        Event event;
        event.type = EventType::TextInput;
        event.windowId = raw.text.windowID;
        // Truncated at a byte boundary rather than at a codepoint one, and that
        // is safe only because the buffer is larger than any single input event
        // SDL produces: the cap exists to bound the struct, not to split text.
        const std::size_t length = std::min(std::strlen(raw.text.text), std::size_t{kMaxTextInputBytes - 1});
        std::memcpy(event.text, raw.text.text, length);
        event.text[length] = '\0';
        out.push_back(event);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        Event event;
        event.type = EventType::MouseMoved;
        event.windowId = raw.motion.windowID;
        event.pointerX = raw.motion.x;
        event.pointerY = raw.motion.y;
        event.pointerDeltaX = raw.motion.xrel;
        event.pointerDeltaY = raw.motion.yrel;
        out.push_back(event);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const MouseButton button = translateMouseButton(raw.button.button);
        if (button == MouseButton::Unknown)
            break;
        Event event;
        event.type = raw.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EventType::MouseButtonDown : EventType::MouseButtonUp;
        event.windowId = raw.button.windowID;
        event.button = button;
        event.pointerX = raw.button.x;
        event.pointerY = raw.button.y;
        out.push_back(event);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        Event event;
        event.type = EventType::MouseWheel;
        event.windowId = raw.wheel.windowID;
        // SDL reports FLIPPED for a natural-scrolling trackpad and documents
        // the fix as multiplying by -1. Undone here so that every consumer sees
        // one convention, which is what a binding stored in a save file needs.
        const float sign = raw.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        event.wheelX = raw.wheel.x * sign;
        event.wheelY = raw.wheel.y * sign;
        event.pointerX = raw.wheel.mouse_x;
        event.pointerY = raw.wheel.mouse_y;
        out.push_back(event);
        break;
    }
    case SDL_EVENT_GAMEPAD_ADDED: {
        openGamepad(raw.gdevice.which);
        Event event;
        event.type = EventType::GamepadAdded;
        event.gamepadId = static_cast<u32>(raw.gdevice.which);
        out.push_back(event);
        break;
    }
    case SDL_EVENT_GAMEPAD_REMOVED: {
        closeGamepad(raw.gdevice.which);
        Event event;
        event.type = EventType::GamepadRemoved;
        event.gamepadId = static_cast<u32>(raw.gdevice.which);
        out.push_back(event);
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const GamepadButton button = translateGamepadButton(raw.gbutton.button);
        if (button == GamepadButton::Unknown)
            break;
        Event event;
        event.type =
            raw.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? EventType::GamepadButtonDown : EventType::GamepadButtonUp;
        event.gamepadId = static_cast<u32>(raw.gbutton.which);
        event.gamepadButton = button;
        out.push_back(event);
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const GamepadAxis axis = translateGamepadAxis(raw.gaxis.axis);
        if (axis == GamepadAxis::Unknown)
            break;
        Event event;
        event.type = EventType::GamepadAxisMoved;
        event.gamepadId = static_cast<u32>(raw.gaxis.which);
        event.gamepadAxis = axis;
        event.axisValue = normalizeAxis(axis, raw.gaxis.value);
        out.push_back(event);
        break;
    }
    default:
        // Everything else stays in the raw stream only. Dropping it here is
        // not a loss: the module models what the engine reacts to, and the
        // SDL-facing consumers read rawEvents().
        break;
    }
}

} // namespace

std::string_view keyName(Key key) noexcept
{
    for (const KeyNaming& naming : KeyNames) {
        if (naming.key == key)
            return naming.name;
    }
    return {};
}

Key keyFromName(std::string_view name) noexcept
{
    // Case-sensitive, because the legend is the name: "w" is not a key on any
    // keyboard, and accepting it would make "Space" and "space" two spellings
    // of one thing in an API that has no other case-insensitive lookup.
    for (const KeyNaming& naming : KeyNames) {
        if (naming.name == name)
            return naming.key;
    }
    return Key::Unknown;
}

std::string_view mouseButtonName(MouseButton button) noexcept
{
    for (const MouseButtonNaming& naming : MouseButtonNames) {
        if (naming.button == button)
            return naming.name;
    }
    return {};
}

MouseButton mouseButtonFromName(std::string_view name) noexcept
{
    for (const MouseButtonNaming& naming : MouseButtonNames) {
        if (naming.name == name)
            return naming.button;
    }
    return MouseButton::Unknown;
}

std::string_view gamepadButtonName(GamepadButton button) noexcept
{
    for (const GamepadButtonNaming& naming : GamepadButtonNames) {
        if (naming.button == button)
            return naming.name;
    }
    return {};
}

GamepadButton gamepadButtonFromName(std::string_view name) noexcept
{
    for (const GamepadButtonNaming& naming : GamepadButtonNames) {
        if (naming.name == name)
            return naming.button;
    }
    return GamepadButton::Unknown;
}

std::string_view gamepadAxisName(GamepadAxis axis) noexcept
{
    for (const GamepadAxisNaming& naming : GamepadAxisNames) {
        if (naming.axis == axis)
            return naming.name;
    }
    return {};
}

GamepadAxis gamepadAxisFromName(std::string_view name) noexcept
{
    for (const GamepadAxisNaming& naming : GamepadAxisNames) {
        if (naming.name == name)
            return naming.axis;
    }
    return GamepadAxis::Unknown;
}

void setTextInputEnabled(u32 windowId, bool enabled) noexcept
{
    SDL_Window* window = SDL_GetWindowFromID(static_cast<SDL_WindowID>(windowId));
    if (window == nullptr)
        return;
    if (enabled)
        (void)SDL_StartTextInput(window);
    else
        (void)SDL_StopTextInput(window);
}

std::span<const Event> pumpEvents()
{
    g_rawEvents.clear();
    g_events.clear();
    g_droppedFiles.clear();

    SDL_Event raw;
    while (SDL_PollEvent(&raw)) {
        g_rawEvents.push_back(raw);
        // **Dropped paths go in a list of their own**, not on an `Event`. An
        // `Event` is a POD copied for every mouse motion and a string on it
        // would be an allocation per frame paid for a thing that happens twice
        // a session. SDL owns `drop.data` until the event is consumed, so the
        // copy happens here rather than being deferred to whoever reads it.
        if (raw.type == SDL_EVENT_DROP_FILE && raw.drop.data != nullptr)
            g_droppedFiles.emplace_back(raw.drop.data);
        translate(raw, g_events);
    }

    return g_events;
}

std::span<const SDL_Event> rawEvents() noexcept
{
    return g_rawEvents;
}

std::span<const std::string> droppedFiles() noexcept
{
    return g_droppedFiles;
}

} // namespace luaug::platform
