#pragma once

#include "luaug/core/types.h"

#include <span>
#include <string_view>

namespace luaug::platform {

using core::i32;
using core::u16;
using core::u32;
using core::u8;

enum class EventType : u8
{
    Quit,
    WindowCloseRequested,
    WindowResized,
    KeyDown,
    KeyUp,
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

// One flat record rather than a tagged union: at five event types the union
// machinery would cost more to read than the unused fields cost to carry.
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
};

// Drains the OS queue and returns this frame's translated events. The span is
// owned by the module and stays valid until the next call, so a caller that
// wants to keep an event past the frame must copy it.
//
// Events SDL reports that the engine does not model yet are dropped here but
// remain visible through sdl_interop.h, which is how the ImGui backend gets
// the full stream without the engine pretending to model input it does not.
[[nodiscard]] std::span<const Event> pumpEvents();

} // namespace luaug::platform
