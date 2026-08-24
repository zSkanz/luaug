// L1 -- the SDL3 wrapper (ADR 0004). This module and the SDL GPU RHI backend
// are the only places that touch SDL directly; nothing above them may see an
// SDL type, which is why the public headers here mention none.
//
// The SDL-facing backends get what they need through sdl_interop.h, which is
// deliberately the one header in this module that does include SDL.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::platform {

using core::u64;

// Declared in window.h. A reference is all `pickFolder` needs, and including
// that header here would make every consumer of the platform basics carry it.
class Window;

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

// Resident set size in bytes, or zero where the platform will not say.
//
// Here for one caller and it is worth naming: M7's gate is "peak memory under
// the declared ceiling" over a five-minute fly-through, and a streaming system
// that leaks is indistinguishable from one that works until you measure it.
// Zero rather than an error on an unsupported platform, because a soak report
// that cannot read memory should still report frame times.
//
// The number is the OS's, so it counts the whole process -- the GPU driver and
// the allocator's free lists included. It is a TREND instrument: what a soak
// asserts is that the curve flattens, not what the absolute figure is.
[[nodiscard]] u64 residentBytes() noexcept;

// `setThreadName` from architecture.md §2 is absent on purpose: SDL3 has no
// setter for it, only SDL_GetThreadName, so it would mean per-platform code
// with no way to test it. This note used to say it would land with `jobs`, and
// M7 shipped `jobs` without it -- because `jobs` is L1 like this module and may
// not include it, so the name would have to be set by `app` or by `jobs` making
// OS calls of its own. Neither is worth the #ifdefs for a string a profiler
// shows.

// The icon this executable carries in its own resources, as the bytes the
// resource holds -- a PNG for every entry `branding/icon/luaug.ico` contains.
// Empty where the platform has no such thing, which is everywhere but Windows
// today (roadmap M8: the macOS and Linux halves are a bundle's `Info.plist` and
// a `.desktop` entry, and both belong to a packaging step).
//
// **Read from the artifact rather than from a file beside it**, which is the
// whole point: a game built with `luaug build` has that resource replaced with
// its own, so this returns the GAME's icon in a packaged build and the engine's
// mark in a development one, with no path to configure and nothing to install.
//
// The bytes are returned undecoded because decoding is `asset`'s (L2) and this
// is L1. `app` is the layer that can see both.
[[nodiscard]] std::vector<std::byte> applicationIconBytes();

// The identity Windows groups taskbar buttons and pinned shortcuts by. Without
// it a pinned shortcut loses its icon and two games built with this engine
// group under one button, because the shell falls back to the executable path.
//
// Reverse-DNS, from `[project] id`. A no-op on every other platform, and a
// no-op for an empty id.
void setApplicationId(std::string_view id);

struct Paths
{
    // Directory holding the running executable.
    std::filesystem::path executableDir;
    // Where the engine's own content (message catalogs, compiled shaders) is
    // staged next to the binary -- the same shape a packaged build uses.
    std::filesystem::path contentDir;
    // Where this USER's own state goes: the launcher's project list, and
    // whatever else outlives one project and one installation. Per user and per
    // machine, from `SDL_GetPrefPath`, and created by SDL on the way out.
    //
    // **Empty when the platform has nowhere to put it**, which is a real answer
    // rather than a failure: a caller with no user directory keeps its state in
    // memory for one session, and every one of them is a convenience. Nothing
    // the engine needs to RUN lives here.
    std::filesystem::path userDir;
    // Where this user keeps documents, which is where the launcher offers to
    // put a new project. Distinct from `userDir` and not derivable from it: one
    // is where an application hides its own state and the other is where a
    // person looks for their work. SDL's own header says of this folder that it
    // "is a good place to save a user's projects".
    //
    // Empty where the platform has no such notion, and the launcher then leaves
    // its location field blank rather than seeding it with somewhere wrong.
    std::filesystem::path documentsDir;
};

// Both members are ABSOLUTE on every desktop tier. On Android they are
// deliberately RELATIVE -- `.` and `content` -- because an APK's content is a
// set of zip entries served by AAssetManager and SDL only routes a relative
// path there. Anything that opens a file under contentDir must therefore go
// through platform::readFile (file.h), which SDL resolves per platform;
// std::filesystem and fopen find nothing inside a package.
//
// `userDir` arrived with its first consumer, which is what this comment used to
// say would happen: the launcher's project list (ADR 0055). The cache directory
// architecture.md §2 also names is still not here, for the same reason it was
// not before -- the bytecode and shader caches live under the build root per §8,
// and a path nobody uses is a path nobody has checked.
[[nodiscard]] const Paths& paths();

// Starts another program and does not wait for it. Used by the launcher to
// start the editor on the project somebody chose (ADR 0055), which is a
// relaunch rather than a load: everything a project decides is resolved at boot.
//
// `args[0]` is the executable. The child inherits this process's streams, so
// what it logs appears where the launcher's did -- which is what makes a failure
// to start the editor visible at all.
//
// True when the child was created. That is all it can mean: a program that
// starts and then exits nonzero has still started, and nothing here waits to
// find out.
[[nodiscard]] bool startDetached(const std::vector<std::string>& args);

// Whether this build can show the system's own folder picker. False on a
// platform with none, and false in a build with `SDL_DIALOG` off -- callers ask
// so they can say WHY the button did nothing, rather than doing nothing.
[[nodiscard]] bool canPickFolder();

// Shows the system folder picker and calls `done` with what was chosen, or with
// an empty path when the person cancelled or the picker could not be shown.
//
// **Asynchronous, and the callback arrives on the event thread**: SDL delivers
// the result while `pumpEvents` runs, so a caller must keep pumping and must not
// assume the callback has happened by the time this returns. `window` is the
// parent, so the dialog is modal to it on the platforms that can do that.
void pickFolder(Window& window, std::string_view startIn, std::function<void(std::filesystem::path)> done);
// The same, for FILES: the system's own open dialog, with `allowMany` deciding
// whether more than one can be chosen. `done` receives what was picked, and an
// empty list for a cancel or a picker that could not be shown.
//
// Asynchronous on the same terms `pickFolder` states: the callback arrives while
// `pumpEvents` runs, so a caller must keep pumping.
//
// **No filters.** The engine reads a mesh, a texture, a sound, a scene and a
// script, and a project may carry files it merely keeps beside them; a dialog
// that hid what somebody was pointing at would be answering a question the
// importer is better placed to answer -- and it answers it by copying the file
// and letting the browser say what kind it turned out to be.
void pickFiles(Window& window, std::string_view startIn, bool allowMany,
               std::function<void(std::vector<std::filesystem::path>)> done);

// What was dropped onto a window since the last `pumpEvents`, in the order the
// system reported it. Empty on nearly every frame.
//
// **A list of its own rather than a field on `Event`**, for the reason
// `rawEvents` is a list of its own: an `Event` is a POD copied for every mouse
// motion, and a string on it would be an allocation per frame paid for a thing
// that happens twice a session. Valid until the next pump.
[[nodiscard]] std::span<const std::string> droppedFiles() noexcept;

} // namespace luaug::platform
