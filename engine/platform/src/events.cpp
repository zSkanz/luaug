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
    default:
        return Key::Unknown;
    }
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
    default:
        // Everything else stays in the raw stream only. Dropping it here is
        // not a loss: the module models what the engine reacts to, and the
        // SDL-facing consumers read rawEvents().
        break;
    }
}

} // namespace

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
