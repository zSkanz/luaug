#pragma once

#include "luaug/core/error.h"
#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/core/types.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

namespace luaug::platform {

using core::i32;
using core::u32;

// Defined in the implementation. Keeping it incomplete here is what lets the
// header stay free of SDL types without resorting to a `void*` payload.
class Window;

struct WindowSize
{
    i32 width = 0;
    i32 height = 0;
};

struct WindowDesc
{
    // A window title is user-facing text, so it is a catalog key rather than a
    // string (R3). Used when `title` below is empty, which is every window the
    // engine opens for itself.
    core::TextKey titleKey;
    std::span<const core::I18nArg> titleArgs{};

    // The passthrough this struct reserved at M1 and M8 needed: a title
    // AUTHORED BY A GAME, from `[window] title` in its own project file. It is
    // not the engine's string and there is nothing to translate it against --
    // the same split `log()` and `logText()` already draw. Non-empty wins over
    // `titleKey`.
    std::string_view title{};

    i32 width = 1280;
    i32 height = 720;
    bool resizable = true;
    // Hidden windows still have a swapchain, which is how a machine with a
    // display can exercise the windowed path without one appearing.
    bool visible = true;
};

struct WindowDeleter
{
    void operator()(Window* window) const noexcept;
};

using WindowPtr = std::unique_ptr<Window, WindowDeleter>;

// Dresses a window in an already-decoded RGBA image, top row first.
//
// Decoded rather than encoded because the decoder lives in `asset` (L2) and
// this module is L1; `app` is the layer that can see both and is the one that
// calls this, with what `applicationIconBytes()` returned.
//
// False when the pixels do not describe an image, or where the platform has no
// per-window icon -- macOS takes its icon from the bundle and never from a
// window, and that is not a failure.
[[nodiscard]] bool setWindowIcon(Window& window, std::span<const std::byte> rgba, i32 width, i32 height);

// Null on failure, with `outError` filled when it is not null. Requires a
// successful init().
[[nodiscard]] WindowPtr createWindow(const WindowDesc& desc, core::EngineError* outError = nullptr);

// Free functions rather than members, because `Window` is incomplete here.
[[nodiscard]] u32 windowId(const Window& window) noexcept;

// The size of the drawable area in pixels, which is what a swapchain needs.
// On a scaled display this is not the logical window size.
[[nodiscard]] WindowSize windowPixelSize(const Window& window) noexcept;

} // namespace luaug::platform
