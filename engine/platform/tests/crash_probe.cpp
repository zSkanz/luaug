// A process that installs the crash handler and then dies on purpose, one of
// two ways.
//
// It exists because the handler's entire value is in what happens *after* a
// fault, and no in-process test can assert on that: a test that faults takes
// the test runner with it. So the assertion is made from outside, by
// `run_crash_gate.cmake`, which runs this and then looks for the artifact.
//
// Without it the handler would be a feature verified by reading it -- which is
// the shape of every "gate that passes while doing nothing" this project has
// found, and would be a particularly bad one here: the failure mode is that a
// human hits the crash the handler was written for and still has nothing to
// send.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

#include "luaug/platform/crash.h"

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fputs("usage: luaug_crash_probe <directory> [fault|throw]\n", stderr);
        return 2;
    }

    // **Two ways, because they are two different failures.** A fault arrives at
    // the unhandled-exception filter; a `throw` nobody caught arrives at
    // `std::terminate` and used to arrive nowhere at all, producing a dump with
    // no cause in it. A gate that only faulted would have proved half of it.
    const std::string_view mode = argc >= 3 ? std::string_view(argv[2]) : std::string_view("fault");
    if (mode != "fault" && mode != "throw") {
        std::fputs("crash probe: mode must be fault or throw\n", stderr);
        return 2;
    }

    if (!luaug::platform::installCrashHandler(std::filesystem::path(argv[1]))) {
        std::fputs("crash probe: the handler could not be installed\n", stderr);
        return 2;
    }

    // The path is printed so the driver asserts against the handler's own answer
    // rather than against a name the test guessed. A test that reconstructs the
    // filename would still pass if the handler wrote somewhere else entirely.
    std::printf("%s\n", luaug::platform::crashArtifactPath().string().c_str());
    std::printf("%s\n", luaug::platform::crashNotePath().string().c_str());
    std::fflush(stdout);

#ifdef _WIN32
    // No Windows Error Reporting dialog. The handler deliberately returns
    // CONTINUE_SEARCH so a debugger still breaks and the exit code is still the
    // exception code -- which under CTest, with a dialog, would mean a hang
    // instead of a result.
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif

    if (mode == "throw") {
        // The message is checked by the driver, so it has to be something no
        // other part of the note could produce by accident.
        throw std::runtime_error("crash probe threw this on purpose");
    }

    // A null store, through a volatile pointer so no compiler is entitled to
    // decide this is unreachable and delete the rest of the function with it.
    volatile int* nowhere = nullptr;
    *nowhere = 1;

    // Unreachable. If it is ever reached, the fault did not happen and the
    // driver's artifact check would be asserting nothing -- so it exits
    // non-zero-but-distinct rather than zero.
    std::fputs("crash probe: the fault did not happen\n", stderr);
    return 3;
}
