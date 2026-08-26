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
// **It writes a readable note beside the dump**, and that reverses an earlier
// decision here rather than extending it. This used to say it deliberately did
// not symbolize, on the reasoning that the dump is for a developer with a
// debugger. That reasoning assumed a debugger, and the machine this engine is
// developed on has none installed -- so a person hit a crash, sent the `.dmp`,
// and the only way to read it was to guess. The note carries the exception, its
// plain name, the faulting address where there is one, the `what()` of an
// uncaught C++ exception, and a symbolised stack.
//
// What it still deliberately does NOT do: upload, or attempt to continue. A
// handler that tries to recover from a corrupted process is a handler that
// turns one bad report into two.
#pragma once

#include <filesystem>

namespace luaug::platform {

// Installs the process-wide fault handler. Idempotent; a second call replaces
// the directory rather than stacking a second handler.
//
// On a fault the handler writes `luaug-crash-<pid>.dmp` (Windows minidump) or
// `luaug-crash-<pid>.txt` (POSIX signal note) into `directory`, then lets the
// default behaviour run -- so a debugger still breaks and an exit code still
// says what happened. On Windows it writes `luaug-crash-<pid>.txt` as well: the
// half a person can read.
//
// It also installs a `std::terminate` handler, because an uncaught C++
// exception is not a fault and reached neither of the paths above -- it
// produced a dump with no cause in it, and on POSIX nothing at all.
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

// The readable note beside it -- the same sentence, and for the same reason.
// On POSIX this is the artifact: there is no dump there, and the note is the
// whole of what a fault leaves behind.
[[nodiscard]] std::filesystem::path crashNotePath();

} // namespace luaug::platform
