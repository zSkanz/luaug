#include "luaug/platform/crash.h"

#include <atomic>
#include <cstdio>
#include <string>

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <dbghelp.h>
// clang-format on
#else
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace luaug::platform
{
namespace
{

// Built once at install time and never touched again, because a fault handler
// may not allocate: the heap is one of the things that can be broken by the
// time it runs. `std::string` here is the storage, and the handler only ever
// reads `.c_str()`.
std::string& artifactPath() noexcept
{
    static std::string path;
    return path;
}

// A fault inside the fault handler is how a crash reporter turns one bad report
// into a hang. One shot, and the second fault takes the default path.
std::atomic<bool>& alreadyHandling() noexcept
{
    static std::atomic<bool> flag{false};
    return flag;
}

#ifdef _WIN32

LONG WINAPI writeMinidump(EXCEPTION_POINTERS* exception) noexcept
{
    if (alreadyHandling().exchange(true))
        return EXCEPTION_CONTINUE_SEARCH;

    const HANDLE file = ::CreateFileA(artifactPath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION information{};
        information.ThreadId = ::GetCurrentThreadId();
        information.ExceptionPointers = exception;
        information.ClientPointers = FALSE;

        // `WithIndirectlyReferencedMemory` rather than a full dump: it captures
        // what the stack's pointers point at, which is what makes a stale
        // pointer readable in a debugger, without writing the process's entire
        // address space to disk. A full dump of a host holding GPU resources is
        // hundreds of megabytes and nobody sends it.
        const auto type = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo);
        (void)::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file, type,
            exception != nullptr ? &information : nullptr, nullptr, nullptr);
        ::CloseHandle(file);
    }

    // `stderr` and not `core::log`: this runs after the fault, and calling back
    // up through the engine is how a handler stops being one.
    std::fprintf(stderr, "\n[crash] wrote %s\n", artifactPath().c_str());
    std::fflush(stderr);

    // Search on, so a debugger still breaks and the process still dies with the
    // exception code it faulted with. Swallowing it would turn a crash into a
    // silent exit, which is worse than the crash.
    return EXCEPTION_CONTINUE_SEARCH;
}

#else

// Everything below runs in a signal handler, so it uses only async-signal-safe
// calls: `open`, `write`, `close`, `_exit`, and re-raising. No `printf`, no
// allocation, no `std::string` construction -- the path was built at install
// time for exactly this reason.
void writeSignalNote(int signalNumber) noexcept
{
    if (alreadyHandling().exchange(true))
        return;

    const int file = ::open(artifactPath().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file >= 0)
    {
        const char* name = ::strsignal(signalNumber);
        static const char prefix[] = "luaug: died on signal ";
        (void)!::write(file, prefix, sizeof(prefix) - 1);
        if (name != nullptr)
            (void)!::write(file, name, ::strlen(name));
        (void)!::write(file, "\n", 1);
        ::close(file);
    }

    static const char note[] = "\n[crash] wrote the signal note beside the log\n";
    (void)!::write(STDERR_FILENO, note, sizeof(note) - 1);

    // Restore the default and re-raise, so the shell sees the real signal and a
    // core file is still produced where the system is configured to make one.
    ::signal(signalNumber, SIG_DFL);
    ::raise(signalNumber);
}

#endif

} // namespace

bool installCrashHandler(const std::filesystem::path& directory)
{
    const auto pid =
#ifdef _WIN32
        static_cast<unsigned long>(::GetCurrentProcessId());
#else
        static_cast<unsigned long>(::getpid());
#endif

    const std::filesystem::path artifact = directory / ("luaug-crash-" + std::to_string(pid) +
#ifdef _WIN32
        ".dmp"
#else
        ".txt"
#endif
    );
    artifactPath() = artifact.string();
    alreadyHandling().store(false);

#ifdef _WIN32
    ::SetUnhandledExceptionFilter(&writeMinidump);
    return true;
#else
    // The four that mean "this process is in an invalid state". `SIGINT` and
    // `SIGTERM` are deliberately absent: those are somebody asking the process
    // to stop, and a crash artifact for a Ctrl-C is noise.
    for (const int signalNumber : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS})
        ::signal(signalNumber, &writeSignalNote);
    return true;
#endif
}

std::filesystem::path crashArtifactPath()
{
    return std::filesystem::path(artifactPath());
}

} // namespace luaug::platform
