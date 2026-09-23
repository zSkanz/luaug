#pragma once

namespace luaug::platform {

// **Asking a running engine to stop from outside it**: Ctrl+C in its terminal,
// SIGTERM from a process manager (`docker stop`, systemd, a CI runner's
// cancel), or the console being closed on Windows.
//
// A dedicated server is the one run meant to go until it is stopped, and it
// has no window to close. Without this, the stop was the operating system's
// default -- the process killed where it stood -- so a game's `BindToClose`
// handlers, which exist to save what a match was holding, never ran on the one
// kind of run that most needs them.
//
// **The first request asks; a second insists.** The first sets a flag the frame
// loop reads, and the engine then closes as if a script had called
// `game:Shutdown()`, grace period and all. A second, while the first is still
// being honoured, ends the process at once with the conventional status for an
// interrupted program: a handler that hangs must not leave the operator unable
// to stop what they started.
void installStopSignals();

// Whether a stop has been asked for since `installStopSignals` (or the last
// `clearStopRequest`). Safe to read from any thread.
[[nodiscard]] bool stopRequested() noexcept;

// Forgets a request, so the next one asks rather than insists. For tests, and
// for a host that has honoured a request and carries on.
void clearStopRequest() noexcept;

} // namespace luaug::platform
