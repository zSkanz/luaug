#pragma once

#include "luaug/core/types.h"

#include <span>
#include <string_view>

namespace luaug::platform {

using core::f32;
using core::i32;
using core::u16;
using core::u32;
using core::u8;

enum class EventType : u8
{
    Quit,
    WindowCloseRequested,
    WindowResized,
    WindowFocusGained,
    WindowFocusLost,
    KeyDown,
    KeyUp,
    // The text a keystroke PRODUCES, which is not the key that produced it: a
    // dead key followed by a vowel is two key events and one text event, and an
    // IME is many key events and one. A text field reads these; an action
    // binding reads the key events. Conflating them is how a text box ends up
    // unable to type an accented character on a layout nobody tested.
    TextInput,
    MouseMoved,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    GamepadAdded,
    GamepadRemoved,
    GamepadButtonDown,
    GamepadButtonUp,
    GamepadAxisMoved,
};

// Physical keys, named by the US-layout legend the way scancodes are.
//
// It was tiny through M4 -- the F-keys and Escape, which was every key the
// engine itself reacted to -- and M5 grows it to a keyboard, because the
// milestone ships a character somebody has to be able to steer.
//
// It is still not a full keycode table, and that is deliberate: mouse buttons
// and gamepad inputs belong to the Input Action System (ADR 0029, M6), which
// maps device-level input to named actions. Growing them here first would build
// the wrong half of that and would have to be re-derived against it anyway.
enum class Key : u16
{
    Unknown = 0,
    Escape,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,

    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,

    Space,
    Return,
    Tab,
    Backspace,
    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    LeftAlt,
    RightAlt,

    Left,
    Right,
    Up,
    Down,

    // Not a key. The count is what sizes a keyboard snapshot, and having it
    // here is what stops that array from being a number somebody has to keep in
    // step by hand.
    Count,
};

// The US-layout legend, which is the name a script uses and the name this enum
// is written in. Empty for `Unknown` and for `Count`.
[[nodiscard]] std::string_view keyName(Key key) noexcept;

// The reverse, case-sensitive. `Key::Unknown` for a name no key carries.
[[nodiscard]] Key keyFromName(std::string_view name) noexcept;

// The mouse buttons the Input Action System can bind. SDL numbers them from 1
// and this enum does not: `Unknown` is 0 here so that a default-constructed
// `Event` names no button, which is the same discipline `Key::Unknown` follows.
enum class MouseButton : u8
{
    Unknown = 0,
    Left,
    Middle,
    Right,
    X1,
    X2,

    Count,
};

// The standard gamepad layout SDL maps every controller onto
// (`SDL_gamepad.h:152`). Named by POSITION rather than by legend -- `South`
// rather than `A` -- because the legend depends on the pad: the bottom face
// button is A on an Xbox pad, B on a Nintendo one and Cross on a PlayStation
// one, and a binding stored as "A" would move when somebody changed hardware.
// What the button is CALLED is a display concern, and it belongs to the prompt
// glyph rather than to the binding.
//
// The paddles, touchpad button and the `MISC` range are deliberately absent:
// they exist on a minority of hardware, and an enum item nothing can produce on
// the machine in front of you is a binding that silently never fires.
enum class GamepadButton : u8
{
    Unknown = 0,
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,

    Count,
};

// The six analogue axes of that same standard layout. Sticks report -1 to 1;
// triggers report 0 to 1, because SDL reports them over the positive half of
// its range and a trigger at rest reading -1 would be a resting input that
// looks like a held one.
enum class GamepadAxis : u8
{
    Unknown = 0,
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,

    Count,
};

// The longest UTF-8 sequence a single `TextInput` event carries, plus room for
// a terminator. SDL hands text as a pointer into memory it frees once the event
// is handled, so the bytes are COPIED here: an `Event` outlives the SDL event
// it came from by a whole frame, and a dangling `const char*` in a public
// struct is a use-after-free waiting for a slow frame.
inline constexpr u32 kMaxTextInputBytes = 32;

// One flat record rather than a tagged union: the union machinery would cost
// more to read than the unused fields cost to carry, and at sixteen event types
// that trade has not changed -- only the number of fields nobody reads on any
// given event has.
struct Event
{
    EventType type = EventType::Quit;

    // The window the event belongs to; 0 when it is not window-scoped.
    u32 windowId = 0;

    // KeyDown / KeyUp.
    Key key = Key::Unknown;
    bool repeat = false;

    // WindowResized -- the new drawable size in pixels.
    i32 width = 0;
    i32 height = 0;

    // MouseMoved, MouseButtonDown / MouseButtonUp -- the pointer in window
    // pixels, top-left origin, y growing downward.
    f32 pointerX = 0.0f;
    f32 pointerY = 0.0f;
    // MouseMoved -- the motion since the previous event, which is NOT the
    // difference of two positions once the pointer is locked: relative mode
    // keeps reporting motion the position cannot express.
    f32 pointerDeltaX = 0.0f;
    f32 pointerDeltaY = 0.0f;

    // MouseButtonDown / MouseButtonUp.
    MouseButton button = MouseButton::Unknown;

    // MouseWheel -- in wheel notches, positive right and positive away from the
    // user, with SDL's FLIPPED direction already undone.
    f32 wheelX = 0.0f;
    f32 wheelY = 0.0f;

    // Every gamepad event. SDL's instance id, which is unique for the life of
    // the connection and is NOT a player index -- unplugging and replugging one
    // pad produces a new id, which is why a binding is never stored against it.
    u32 gamepadId = 0;
    GamepadButton gamepadButton = GamepadButton::Unknown;
    GamepadAxis gamepadAxis = GamepadAxis::Unknown;
    // GamepadAxisMoved. -1..1 for a stick, 0..1 for a trigger. No dead zone is
    // applied here: a dead zone is a per-action processor (ADR 0029) and
    // applying one at the device would make it unremovable.
    f32 axisValue = 0.0f;

    // TextInput -- NUL-terminated UTF-8, copied rather than referenced.
    char text[kMaxTextInputBytes] = {};
};

// The US-layout legend for a mouse button, a gamepad button and a gamepad axis.
// Empty for `Unknown` and for `Count`, exactly as `keyName` is -- these three
// exist for the same reason it does: the recorded input stream the determinism
// gate replays is written in names, not in numbers, so that a trace stays
// readable and stays valid across a reordering of these enums.
[[nodiscard]] std::string_view mouseButtonName(MouseButton button) noexcept;
[[nodiscard]] MouseButton mouseButtonFromName(std::string_view name) noexcept;

[[nodiscard]] std::string_view gamepadButtonName(GamepadButton button) noexcept;
[[nodiscard]] GamepadButton gamepadButtonFromName(std::string_view name) noexcept;

[[nodiscard]] std::string_view gamepadAxisName(GamepadAxis axis) noexcept;
[[nodiscard]] GamepadAxis gamepadAxisFromName(std::string_view name) noexcept;

// Whether the platform layer delivers `TextInput` events for this window.
//
// Off by default, and that is SDL's rule rather than ours: a game that never
// asks pays no IME cost and, on a phone, gets no on-screen keyboard. `ui` turns
// it on while a `TextInput` element holds focus and off again when it does not.
void setTextInputEnabled(u32 windowId, bool enabled) noexcept;

// Drains the OS queue and returns this frame's translated events. The span is
// owned by the module and stays valid until the next call, so a caller that
// wants to keep an event past the frame must copy it.
//
// Events SDL reports that the engine does not model yet are dropped here but
// remain visible through sdl_interop.h, which is how the ImGui backend gets
// the full stream without the engine pretending to model input it does not.
[[nodiscard]] std::span<const Event> pumpEvents();

} // namespace luaug::platform
