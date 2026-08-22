#include "luaug/platform/window.h"

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/platform.h"
#include "luaug/platform/sdl_interop.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_surface.h>
#include <span>
#include <string>

#include "window_impl.h"

namespace luaug::platform {

void WindowDeleter::operator()(Window* window) const noexcept
{
    delete window;
}

WindowPtr createWindow(const WindowDesc& desc, core::EngineError* outError)
{
    if (!isInitialized()) {
        if (outError != nullptr)
            *outError = core::makeError(LUAUG_TR("platform.err.window_failed"), {}, "platform::init() has not run");
        return {};
    }

    SDL_WindowFlags flags = 0;
    if (desc.resizable)
        flags |= SDL_WINDOW_RESIZABLE;
    if (!desc.visible)
        flags |= SDL_WINDOW_HIDDEN;

    const std::string title =
        desc.title.empty() ? core::engineCatalog().format(desc.titleKey, desc.titleArgs) : std::string(desc.title);

    SDL_Window* handle = SDL_CreateWindow(title.c_str(), desc.width, desc.height, flags);
    if (handle == nullptr) {
        if (outError != nullptr)
            *outError = core::makeError(LUAUG_TR("platform.err.window_failed"), {}, SDL_GetError());
        return {};
    }

    return WindowPtr(new Window(handle));
}

bool setPointerLocked(Window& window, bool locked)
{
    return SDL_SetWindowRelativeMouseMode(window.handle(), locked);
}

void setPointerVisible(bool visible)
{
    // Not per window: SDL's cursor is the process's. Failure is ignored because
    // there is nothing a game could do about it and nothing a player would see
    // beyond the cursor they already have.
    if (visible)
        (void)SDL_ShowCursor();
    else
        (void)SDL_HideCursor();
}

bool setWindowIcon(Window& window, std::span<const std::byte> rgba, i32 width, i32 height)
{
    if (width <= 0 || height <= 0 ||
        rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return false;
    }

    // `SDL_CreateSurfaceFrom` does not copy, and `SDL_SetWindowIcon` does --
    // SDL duplicates the pixels into its own storage -- so the surface and the
    // caller's bytes may both go away as soon as this returns.
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, const_cast<std::byte*>(rgba.data()), width * 4);
    if (surface == nullptr)
        return false;

    const bool ok = SDL_SetWindowIcon(window.handle(), surface);
    SDL_DestroySurface(surface);
    return ok;
}

u32 windowId(const Window& window) noexcept
{
    return SDL_GetWindowID(window.handle());
}

WindowSize windowPixelSize(const Window& window) noexcept
{
    WindowSize size;
    // The out-params are left untouched on failure, which only happens for an
    // invalid window; a zero size is then the honest answer rather than stale
    // numbers from a previous call.
    SDL_GetWindowSizeInPixels(window.handle(), &size.width, &size.height);
    return size;
}

SDL_Window* nativeWindow(const Window& window) noexcept
{
    return window.handle();
}

} // namespace luaug::platform
