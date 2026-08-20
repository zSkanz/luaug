// The crash handler `architecture.md` §app has promised since M0.
//
// It exists because this project's verification model is a human running the
// engine by hand, and that human has reported defects from memory. One of them
// took the host down while values were being dragged in the inspector; the
// captured log held the two lines an ordinary successful run prints, because
// `core::log` already flushes per line and the process died without reaching
// any C++ error path. That is the signature of an access violation, and it is
// the case a file sink cannot help with at all -- **the handler is the only
// piece that runs after the fault and before the process is gone.**
//
// It lives in `platform` for the reason the SDL seam does: it is per-OS code,
// and nothing above should have to know which OS it is on.
//
// What it deliberately does NOT do: symbolize, upload, or attempt to continue.
// The dump is for a developer with a debugger, and a handler that tries to
// recover from a corrupted process is a handler that turns one bad report into
// two.
#pragma once

#include <filesystem>

namespace luaug::platform {

// Installs the process-wide fault handler. Idempotent; a second call replaces
// the directory rather than stacking a second handler.
//
// On a fault the handler writes `luaug-crash-<pid>.dmp` (Windows minidump) or
// `luaug-crash-<pid>.txt` (POSIX signal note) into `directory`, then lets the
// default behaviour run -- so a debugger still breaks and an exit code still
// says what happened.
//
// `directory` is passed in rather than resolved here for the same reason
// `core::openLogFile`'s path is: whoever is running decides where their
// artifacts land, and a handler that picked its own would put them where the
// person cannot find them.
//
// Returns false when the platform support is unavailable, which is not fatal.
[[nodiscard]] bool installCrashHandler(const std::filesystem::path& directory);

// The path the handler WOULD write to, so a host can print it at startup. A
// crash artifact nobody knows the name of is a crash artifact nobody sends.
[[nodiscard]] std::filesystem::path crashArtifactPath();

} // namespace luaug::platform
