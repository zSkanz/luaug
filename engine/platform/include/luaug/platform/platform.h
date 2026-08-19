// L1 -- the SDL3 wrapper (ADR 0004). This module and the SDL GPU RHI backend
// are the only places that touch SDL directly; nothing above them may see an
// SDL type, which is why the public headers here mention none.
//
// The SDL-facing backends get what they need through sdl_interop.h, which is
// deliberately the one header in this module that does include SDL.
#pragma once

#include <filesystem>
#include <optional>

#include "luaug/core/error.h"
#include "luaug/core/types.h"

namespace luaug::platform
{

using core::u64;

struct InitOptions
{
    // Selects SDL's offscreen video driver, so a machine with no display --
    // a CI runner, a future dedicated server -- still brings up a video
    // subsystem and can own a GPU device to read pixels back from (the M1
    // headless harness). Without it, SDL_Init fails outright on such a machine.
    //
    // Requested rather than forced: SDL gives an SDL_VIDEO_DRIVER environment
    // variable precedence over a hint set this way, which is how a developer
    // overrides the choice without a rebuild.
    bool headless = false;
};

// Brings up the subsystems the engine delegates to SDL. Idempotent: a second
// call is a no-op, so a test that needs a window does not have to know whether
// the host already ran. Because it is a no-op, the options of the FIRST
// successful call are the ones in effect.
[[nodiscard]] std::optional<core::EngineError> init(const InitOptions& options = {});

// Safe to call without a successful init(). Windows must be destroyed first --
// SDL tears their backing resources down here, and a Window outliving this is
// a dangling handle.
void shutdown();

[[nodiscard]] bool isInitialized() noexcept;

// Monotonic, nanoseconds, arbitrary epoch. For frame pacing, profiling and
// timeouts. Valid before init(), which SDL's own tick clock is not.
//
// **Simulation code must never call this** (R10, ADR 0025): a tick that reads
// the wall clock is a tick whose result depends on how fast the machine ran,
// which breaks replay and any future rollback. The fixed-tick scheduler passes
// a fixed dt down precisely so nothing below it needs the real time.
[[nodiscard]] u64 nowNs() noexcept;

// `setThreadName` from architecture.md §2 is absent on purpose: M1 is
// single-threaded, SDL3 has no setter for it (only SDL_GetThreadName), so it
// would mean per-platform code with no caller and no way to test it. It lands
// with `jobs`, which is the first module that will have threads to name.

struct Paths
{
    // Directory holding the running executable.
    std::filesystem::path executableDir;
    // Where the engine's own content (message catalogs, compiled shaders) is
    // staged next to the binary -- the same shape a packaged build uses.
    std::filesystem::path contentDir;
};

// The user-data and cache directories architecture.md §2 also names are not
// here yet: nothing in M1 writes to either (the bytecode and shader caches
// live under the build root per §8), and a path nobody uses is a path nobody
// has checked. They arrive with their first consumer.
[[nodiscard]] const Paths& paths();

} // namespace luaug::platform
