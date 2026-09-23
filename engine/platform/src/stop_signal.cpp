#include "luaug/platform/stop_signal.h"

#include <atomic>
#include <csignal>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace luaug::platform {
namespace {

// Lock-free, so a signal handler may touch it (the one thing a handler may do
// besides `_Exit`).
std::atomic<bool> requested{false};
static_assert(std::atomic<bool>::is_always_lock_free);

// 128 + SIGINT: what a shell reports for a program interrupted from its
// terminal, and what a process manager reads as "stopped, not crashed".
constexpr int InterruptedStatus = 130;

void request() noexcept
{
    if (requested.exchange(true))
        std::_Exit(InterruptedStatus);
}

void onSignal(int)
{
    request();
}

#ifdef _WIN32
// Ctrl+C reaches the C runtime's SIGINT as well; Ctrl+Break and closing the
// console reach only this. Answering TRUE keeps the process alive to honour
// the request -- for a close, Windows allows a few seconds before it ends the
// process anyway.
BOOL WINAPI onConsoleEvent(DWORD event)
{
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        request();
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

} // namespace

void installStopSignals()
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#ifdef _WIN32
    SetConsoleCtrlHandler(onConsoleEvent, TRUE);
#endif
}

bool stopRequested() noexcept
{
    return requested.load();
}

void clearStopRequest() noexcept
{
    requested.store(false);
}

} // namespace luaug::platform
