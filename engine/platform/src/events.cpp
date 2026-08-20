#include "luaug/platform/event.h"
#include "luaug/platform/sdl_interop.h"

#include <vector>

namespace luaug::platform {
namespace {

// Main-thread only, like SDL's own event queue. The buffers are kept between
// frames so a steady-state frame does no allocation.
std::vector<SDL_Event> g_rawEvents;
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
// the enumerator names, because the legend and the identifier differ for the
// digits -- `Digit0` is legend "0" -- and because a table a compiler cannot
// check is one a test has to: `platform_tests` walks every enumerator and
// requires a round trip through both functions.
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
    {Key::Digit0, "0"},
    {Key::Digit1, "1"},
    {Key::Digit2, "2"},
    {Key::Digit3, "3"},
    {Key::Digit4, "4"},
    {Key::Digit5, "5"},
    {Key::Digit6, "6"},
    {Key::Digit7, "7"},
    {Key::Digit8, "8"},
    {Key::Digit9, "9"},
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

std::span<const Event> pumpEvents()
{
    g_rawEvents.clear();
    g_events.clear();

    SDL_Event raw;
    while (SDL_PollEvent(&raw)) {
        g_rawEvents.push_back(raw);
        translate(raw, g_events);
    }

    return g_events;
}

std::span<const SDL_Event> rawEvents() noexcept
{
    return g_rawEvents;
}

} // namespace luaug::platform
