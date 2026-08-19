#pragma once

#include <span>

#include "luaug/core/types.h"

namespace luaug::platform
{

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
// Deliberately tiny: this is every key the engine itself reacts to today.
// General keyboard input belongs to the Input Action System (ADR 0029, M6),
// which maps device-level input to named actions -- growing a full keycode
// table here first would build the wrong half of that, and would have to be
// re-derived against the IAS anyway.
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
};

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
