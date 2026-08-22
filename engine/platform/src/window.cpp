#include "luaug/platform/window.h"

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/platform.h"
#include "luaug/platform/sdl_interop.h"

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
