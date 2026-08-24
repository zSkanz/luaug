#include "luaug/platform/window.h"

#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/platform.h"
#include "luaug/platform/sdl_interop.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#if defined(_WIN32)
// clang-format off
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// clang-format on
#endif

#include <span>
#include <string>

#include "window_impl.h"

namespace luaug::platform {

namespace {

// **Windows 11 rounds every window's corners, and this engine draws into them.**
//
// The rounding is the shell's frame, not ours: the editor's dockspace, the
// launcher's panel and a game's framebuffer all reach the edge of the client
// area, so a rounded corner clips content the APPLICATION drew rather than
// softening chrome the SYSTEM drew. The shell's own theme has had a rounding of
// zero from the day it was data (ADR 0056); this is the half of that decision
// the window manager owns.
//
// Asked for through `dwmapi.dll` at runtime rather than by linking it: the
// attribute arrived in Windows 11 and does not exist on 10, so a link-time
// dependency would be a hard requirement bought for a preference, and the
// version check is the call failing. Every failure here is silent and
// survivable -- a rounded corner is a cosmetic loss and refusing to open a
// window is not.
void squareTheCorners([[maybe_unused]] SDL_Window* handle)
{
#if defined(_WIN32)
    void* hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(handle), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (hwnd == nullptr)
        return;

    // DWMWA_WINDOW_CORNER_PREFERENCE and DWMWCP_DONOTROUND, by value: the
    // enumerators are only declared by a recent Windows SDK, and this file is
    // compiled against whichever one the machine has.
    constexpr int kCornerPreference = 33;
    constexpr int kDoNotRound = 1;

    using SetAttribute = long(__stdcall*)(void*, unsigned long, const void*, unsigned long);
    HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll");
    if (dwm == nullptr)
        return;
    if (const auto set =
            reinterpret_cast<SetAttribute>(reinterpret_cast<void*>(::GetProcAddress(dwm, "DwmSetWindowAttribute")));
        set != nullptr) {
        const int preference = kDoNotRound;
        (void)set(hwnd, kCornerPreference, &preference, sizeof(preference));
    }
    // Left loaded: the window outlives this call and DWM is in every process
    // that has one anyway, so unloading would be returning a reference the
    // system had already given us.
#endif
}

} // namespace

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

    squareTheCorners(handle);

    return WindowPtr(new Window(handle));
}

bool setPointerLocked(Window& window, bool locked)
{
    return SDL_SetWindowRelativeMouseMode(window.handle(), locked);
}

void setPointerPosition(Window& window, f32 x, f32 y)
{
    SDL_WarpMouseInWindow(window.handle(), x, y);
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

WindowPlacement windowPlacement(const Window& window) noexcept
{
    WindowPlacement placement;
    SDL_GetWindowPosition(window.handle(), &placement.x, &placement.y);
    SDL_GetWindowSize(window.handle(), &placement.width, &placement.height);
    placement.maximized = (SDL_GetWindowFlags(window.handle()) & SDL_WINDOW_MAXIMIZED) != 0;
    return placement;
}

void setWindowPlacement(Window& window, const WindowPlacement& placement)
{
    if (placement.width > 0 && placement.height > 0) {
        (void)SDL_SetWindowSize(window.handle(), placement.width, placement.height);
        (void)SDL_SetWindowPosition(window.handle(), placement.x, placement.y);
    }
    if (placement.maximized)
        (void)SDL_MaximizeWindow(window.handle());
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

f32 windowDisplayScale(const Window& window) noexcept
{
    // Zero is SDL's failure answer, and a scale of zero would multiply every
    // measurement a game makes to nothing. One is the honest fallback: it says
    // "logical pixels are device pixels", which is what an unscaled display is.
    const float scale = SDL_GetWindowDisplayScale(window.handle());
    return scale > 0.0f ? scale : 1.0f;
}

WindowInsets windowSafeAreaInsets(const Window& window) noexcept
{
    WindowInsets insets;

    // SDL reports the safe area as a RECTANGLE inside the window; what a UI
    // wants is how much each edge takes away, so it is converted here rather
    // than in four places downstream.
    SDL_Rect safe{};
    if (!SDL_GetWindowSafeArea(window.handle(), &safe))
        return insets;

    WindowSize size;
    SDL_GetWindowSize(window.handle(), &size.width, &size.height);
    if (size.width <= 0 || size.height <= 0)
        return insets;

    insets.left = safe.x;
    insets.top = safe.y;
    insets.right = size.width - (safe.x + safe.w);
    insets.bottom = size.height - (safe.y + safe.h);
    if (insets.right < 0)
        insets.right = 0;
    if (insets.bottom < 0)
        insets.bottom = 0;
    return insets;
}

SDL_Window* nativeWindow(const Window& window) noexcept
{
    return window.handle();
}

} // namespace luaug::platform
