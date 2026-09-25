#pragma once

// Running a tool and reading what it said (ADR 0091).
//
// The editor compiles a user's surface shader by running `shadercross`, the
// same program the engine's own build runs, rather than by linking a compiler
// into itself: a compiler that crashes on somebody's shader takes a process of
// its own down, not the editor, and the editor starts on a machine where the
// compiler's library is missing and says so when asked to compile.
//
// Blocking by design -- the caller is a worker thread -- and never on the
// render thread.

#include <string>
#include <vector>

namespace luaug::platform {

struct ProcessResult
{
    // False when the program could not be started at all.
    bool started = false;
    int exitCode = -1;
    // Standard output and standard error, interleaved as the program wrote them.
    std::string output;
};

[[nodiscard]] ProcessResult runProcess(const std::vector<std::string>& arguments);

} // namespace luaug::platform
