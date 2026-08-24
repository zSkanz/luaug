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

using core::f32;
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

// Locks the pointer to this window, or lets it go.
//
// **Locked means CENTRED and hidden, not merely confined.** SDL's relative mode
// warps the cursor back to the middle of the window after every motion event and
// reports the motion instead of a position, which is the only arrangement that
// lets a look control keep turning past the edge of the screen. Confining a
// visible cursor to the window would stop at the edge and the camera would stop
// with it.
//
// False on a platform or a driver that will not do it. `InputService`'s own
// property records the WISH; whether it was granted is this call's answer.
[[nodiscard]] bool setPointerLocked(Window& window, bool locked);

// Whether the system cursor is drawn. Independent of the lock, because the two
// are separate wishes -- a strategy game hides the cursor during a cutscene
// without locking it (api-design.md §2.4).
void setPointerVisible(bool visible);

// Puts the pointer at a position in the window, in the same pixels an event
// reports.
//
// **It exists because SDL's relative mode moves the cursor and this engine does
// not want it to** (D063). While the pointer is held, SDL accumulates a logical
// position from the relative motion and warps the real cursor there on the way
// out -- so a look that turned the camera a full circle leaves the cursor
// against a window edge. Anchoring means remembering where it was when the hold
// began and putting it back, which is what every editor does and what a person
// expects: the cursor reappears under their hand, not where the camera took it.
void setPointerPosition(Window& window, f32 x, f32 y);

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

// The window's pixel density relative to its logical size: 2 on a doubled
// display, 1 where the two agree. What `UIService.DisplayScale` reports, and 1
// when the platform cannot say -- a game multiplying by zero is worse than one
// multiplying by an unscaled default (D055).
[[nodiscard]] f32 windowDisplayScale(const Window& window) noexcept;

// How far in from each window edge it is safe to draw, in PIXELS: left, top,
// right, bottom. Zero on a desktop window; a notch, a rounded corner or a
// system gesture bar is what makes it not.
struct WindowInsets
{
    i32 left = 0;
    i32 top = 0;
    i32 right = 0;
    i32 bottom = 0;
};

[[nodiscard]] WindowInsets windowSafeAreaInsets(const Window& window) noexcept;

// Where a window is and how big, in LOGICAL units -- the numbers a person set by
// dragging an edge, not the pixels a swapchain needs.
//
// Logical because that is what survives a move between displays: the same window
// on a doubled display has twice the pixels and is the same size to the person
// who sized it, and restoring the pixel count would double it (D055 is the same
// distinction from the other end).
struct WindowPlacement
{
    i32 x = 0;
    i32 y = 0;
    i32 width = 0;
    i32 height = 0;
    // Restored by MAXIMISING rather than by placing: a maximised window's
    // position and size are the ones it will have when somebody un-maximises it,
    // and writing those back as a plain geometry loses the state entirely.
    bool maximized = false;
};

[[nodiscard]] WindowPlacement windowPlacement(const Window& window) noexcept;

// Puts a window back where it was. A zero or negative size is ignored rather
// than applied, so a truncated or hand-edited file cannot produce a window
// nobody can find or grab.
//
// **Placed before it is maximised**, which is the order that matters: the
// restore geometry has to be set while the window is normal or un-maximising
// later lands somewhere the person never put it.
void setWindowPlacement(Window& window, const WindowPlacement& placement);

} // namespace luaug::platform
