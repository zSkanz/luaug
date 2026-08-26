#include "luaug/platform/crash.h"

#include <atomic>
#include <cstdio>
#include <exception>
#include <string>
#include <typeinfo>

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

namespace luaug::platform {
namespace {

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

// The readable half, beside the dump. Separate from `alreadyHandling` because
// the two artifacts are written by two different paths -- a fault filter and a
// terminate handler -- and an uncaught C++ exception can reach both. Whichever
// arrives first writes the note; the second does not overwrite it, because the
// first one is the one with the cause in it.
std::atomic<bool>& noteWritten() noexcept
{
    static std::atomic<bool> flag{false};
    return flag;
}

std::string& notePath() noexcept
{
    static std::string path;
    return path;
}

#ifdef _WIN32

// The exception codes worth naming. A number is a thing to look up; a name is a
// thing to read, and the difference decides whether the person who receives the
// note can act on it without a debugger.
const char* exceptionName(DWORD code) noexcept
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "access violation";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "array bounds exceeded";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "misaligned access";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        return "floating-point divide by zero";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "integer divide by zero";
    case EXCEPTION_STACK_OVERFLOW:
        return "stack overflow";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "privileged instruction";
    // Visual C++ own code for a `throw` nobody caught: "msc" in the low bytes.
    case 0xE06D7363u:
        return "an uncaught C++ exception";
    default:
        return nullptr;
    }
}

// Symbolised, one frame per line.
//
// **This allocates and takes a lock, and that is a considered trade.** The
// minidump is already on disk by the time this runs, so the worst case is a
// note that comes out short -- and the ordinary case is that somebody with no
// debugger installed can read what broke. This header used to say it
// deliberately did not symbolise, on the reasoning that a dump is for a
// developer with a debugger; on the machine this engine is built on there is no
// debugger, and that reasoning cost an hour the first time a person pasted a
// dump and asked what it said.
void writeStack(std::FILE* out, CONTEXT* context) noexcept
{
    const HANDLE process = ::GetCurrentProcess();
    ::SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    if (::SymInitialize(process, nullptr, TRUE) == FALSE)
        return;

    void* captured[62];
    USHORT frames = 0;
    STACKFRAME64 frame{};
    CONTEXT walking{};
    const bool walkable = context != nullptr;
    if (walkable) {
        walking = *context;
#if defined(_M_X64)
        frame.AddrPC.Offset = walking.Rip;
        frame.AddrFrame.Offset = walking.Rbp;
        frame.AddrStack.Offset = walking.Rsp;
#elif defined(_M_ARM64)
        frame.AddrPC.Offset = walking.Pc;
        frame.AddrFrame.Offset = walking.Fp;
        frame.AddrStack.Offset = walking.Sp;
#endif
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;
    }
    else {
        frames = ::RtlCaptureStackBackTrace(2, 62, captured, nullptr);
    }

    alignas(SYMBOL_INFO) char symbolStorage[sizeof(SYMBOL_INFO) + 512];
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;

    std::fprintf(out, "\nStack (innermost first):\n");
    for (int index = 0; index < 62; ++index) {
        DWORD64 address = 0;
        if (walkable) {
#if defined(_M_X64)
            const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_ARM64)
            const DWORD machine = IMAGE_FILE_MACHINE_ARM64;
#else
            const DWORD machine = IMAGE_FILE_MACHINE_UNKNOWN;
#endif
            if (::StackWalk64(machine, process, ::GetCurrentThread(), &frame, &walking, nullptr,
                              ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr) == FALSE) {
                break;
            }
            address = frame.AddrPC.Offset;
            if (address == 0)
                break;
        }
        else {
            if (index >= frames)
                break;
            address = reinterpret_cast<DWORD64>(captured[index]);
        }

        DWORD64 displacement = 0;
        const char* name = "<no symbol>";
        if (::SymFromAddr(process, address, &displacement, symbol) != FALSE)
            name = symbol->Name;

        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (::SymGetLineFromAddr64(process, address, &lineDisplacement, &line) != FALSE)
            std::fprintf(out, "  %-52s %s:%lu\n", name, line.FileName, static_cast<unsigned long>(line.LineNumber));
        else
            std::fprintf(out, "  %-52s 0x%llx\n", name, static_cast<unsigned long long>(address));
    }
}

#endif

// `headline` says WHAT happened in one sentence; the rest is evidence. Written
// once per process, by whichever of the two paths reaches it first.
void writeNote(const char* headline, void* exceptionPointers) noexcept
{
    if (notePath().empty() || noteWritten().exchange(true))
        return;

    // `fopen_s` where the compiler insists, because `engine/` is built with
    // warnings as errors and MSVC deprecates `fopen`. A crash handler is not the
    // place to reach for `_CRT_SECURE_NO_WARNINGS` and silence the whole file.
    std::FILE* out = nullptr;
#ifdef _MSC_VER
    if (::fopen_s(&out, notePath().c_str(), "wb") != 0)
        return;
#else
    out = std::fopen(notePath().c_str(), "wb");
#endif
    if (out == nullptr)
        return;

    std::fprintf(out, "%s\n", headline);

#ifdef _WIN32
    auto* exception = static_cast<EXCEPTION_POINTERS*>(exceptionPointers);
    CONTEXT* context = nullptr;
    if (exception != nullptr && exception->ExceptionRecord != nullptr) {
        const DWORD code = exception->ExceptionRecord->ExceptionCode;
        const char* named = exceptionName(code);
        std::fprintf(out, "Exception code: 0x%08lx%s%s\n", static_cast<unsigned long>(code),
                     named != nullptr ? " -- " : "", named != nullptr ? named : "");
        if (code == EXCEPTION_ACCESS_VIOLATION && exception->ExceptionRecord->NumberParameters >= 2) {
            const ULONG_PTR kind = exception->ExceptionRecord->ExceptionInformation[0];
            std::fprintf(out, "Tried to %s address 0x%llx\n", kind == 0 ? "read" : (kind == 1 ? "write" : "execute"),
                         static_cast<unsigned long long>(exception->ExceptionRecord->ExceptionInformation[1]));
        }
        context = exception->ContextRecord;
    }
    writeStack(out, context);
#else
    (void)exceptionPointers;
#endif

    std::fclose(out);
    std::fprintf(stderr, "[crash] wrote %s\n", notePath().c_str());
    std::fflush(stderr);
}

// **An uncaught C++ exception is not a fault**, and before this it produced a
// minidump with no cause in it and nothing else. `std::current_exception` is
// still live inside a terminate handler, so rethrowing it here is how the
// `what()` string reaches the person who has to read the report.
//
// **On MSVC this is the SECOND of the two paths, not the first**, and the gate
// proved it rather than the design predicting it: `__scrt_common_main_seh`
// wraps `main` in its own `__try`, so a throw nobody catches reaches the
// unhandled-exception filter and the process dies there -- `terminate` is never
// called. What the filter writes instead is the stack AT THE THROW SITE, with
// the file and line, which is the more actionable half; the `what()` string is
// what is given up.
//
// Recovering the message from inside the filter was considered and rejected: it
// means decoding MSVC's `ThrowInfo` and its catchable-type array out of the
// exception record and then making a virtual call through a pointer whose type
// was inferred from a mangled name. That is version-fragile, it is undefined if
// the inference is wrong, and it would run inside a handler where being wrong
// costs the report itself. The stack names the throw; that is enough to find it.
//
// This handler still earns its place: it is the path on POSIX, and it is the
// path on any thread whose exception does not travel through that wrapper.
void terminateHandler() noexcept
{
    std::string headline = "LuauG terminated on an uncaught exception.";
    if (const std::exception_ptr active = std::current_exception(); active != nullptr) {
        try {
            std::rethrow_exception(active);
        } catch (const std::exception& error) {
            headline = std::string("LuauG terminated on an uncaught ") + typeid(error).name() + ": " + error.what();
        } catch (...) {
            headline = "LuauG terminated on an uncaught exception that is not a std::exception.";
        }
    }
    writeNote(headline.c_str(), nullptr);

    // **Claim the fault path before aborting.** `std::abort` raises SIGABRT, and
    // on POSIX the signal handler writes ITS note to the same filename -- so
    // without this the sentence naming the exception would be overwritten by
    // "died on signal Aborted", which is true and useless.
    alreadyHandling().store(true);
    std::abort();
}

#ifdef _WIN32

LONG WINAPI writeMinidump(EXCEPTION_POINTERS* exception) noexcept
{
    if (alreadyHandling().exchange(true))
        return EXCEPTION_CONTINUE_SEARCH;

    const HANDLE file =
        ::CreateFileA(artifactPath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION information{};
        information.ThreadId = ::GetCurrentThreadId();
        information.ExceptionPointers = exception;
        information.ClientPointers = FALSE;

        // `WithIndirectlyReferencedMemory` rather than a full dump: it captures
        // what the stack's pointers point at, which is what makes a stale
        // pointer readable in a debugger, without writing the process's entire
        // address space to disk. A full dump of a host holding GPU resources is
        // hundreds of megabytes and nobody sends it.
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory |
                                                     MiniDumpWithThreadInfo);
        (void)::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file, type,
                                  exception != nullptr ? &information : nullptr, nullptr, nullptr);
        ::CloseHandle(file);
    }

    // `stderr` and not `core::log`: this runs after the fault, and calling back
    // up through the engine is how a handler stops being one.
    std::fprintf(stderr, "\n[crash] wrote %s\n", artifactPath().c_str());
    std::fflush(stderr);

    // The dump is safe on disk; everything from here is best-effort, and a note
    // that comes out short is still a note somebody can read without a debugger.
    writeNote("LuauG died on a fault.", exception);

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
    if (file >= 0) {
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
    notePath() = (directory / ("luaug-crash-" + std::to_string(pid) + ".txt")).string();
    alreadyHandling().store(false);
    noteWritten().store(false);

    // Both directions, because the two failures look nothing alike from inside:
    // a fault arrives at the filter below, and a `throw` nobody caught arrives
    // here. Before this, only the first produced any evidence at all.
    (void)std::set_terminate(&terminateHandler);

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

std::filesystem::path crashNotePath()
{
    return std::filesystem::path(notePath());
}

} // namespace luaug::platform
