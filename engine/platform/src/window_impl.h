// Internal to the platform module: this is where `Window` -- incomplete in the
// public header on purpose -- actually gains an SDL type.
#pragma once

#include <SDL3/SDL_video.h>

#include "luaug/platform/window.h"

namespace luaug::platform
{

class Window
{
public:
    explicit Window(SDL_Window* handle) noexcept : handle_(handle) {}

    ~Window()
    {
        if (handle_ != nullptr)
            SDL_DestroyWindow(handle_);
    }

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    [[nodiscard]] SDL_Window* handle() const noexcept { return handle_; }

private:
    SDL_Window* handle_ = nullptr;
};

} // namespace luaug::platform
