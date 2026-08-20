// The sanctioned crack in the platform wall.
//
// ADR 0004 keeps SDL inside this module -- with one exception it names, the
// SDL GPU RHI backend, which cannot create a swapchain without the real
// SDL_Window. Architecture.md §2 extends the same allowance to `app`'s glue,
// which is where the ImGui SDL3 backend will be fed.
//
// So rather than pretending otherwise with an opaque handle that the backend
// casts anyway, the access is explicit, named, and confined to a header whose
// filename says what including it means. The public headers next to this one
// stay SDL-free; including THIS one is a declaration that you are one of the
// two modules allowed to know.
#pragma once

#include "luaug/platform/window.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <span>

namespace luaug::platform {

[[nodiscard]] SDL_Window* nativeWindow(const Window& window) noexcept;

// The untranslated events from the most recent pumpEvents(), including the
// ones the engine does not model. Same lifetime rule as the translated span.
[[nodiscard]] std::span<const SDL_Event> rawEvents() noexcept;

} // namespace luaug::platform
