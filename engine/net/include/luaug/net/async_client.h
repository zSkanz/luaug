// Blocking network calls, off the VM's thread (api-design.md §7).
//
// `@std/net.request` yields the calling coroutine and the engine resumes it when
// the answer is here. Something has to do the waiting, and it cannot be the
// thread running Luau: a two-second request on the VM thread is a two-second
// frame.
//
// **Its own threads rather than the job system's** (`jobs/jobs.h`). That pool is
// sized to the machine's cores and exists to keep them busy; a worker parked on
// a socket for two seconds is a core the frame's work cannot have. Threads that
// are ALLOWED to block are a different resource from threads that must not, and
// conflating them is how a job system acquires a mysterious stall.
//
// **Completions are taken, never delivered.** Nothing here calls back into the
// VM: the host takes finished tickets at a point the frame loop chooses, the
// same shape `ITransport::poll` has and for the same R10 reason. A callback
// firing off a worker thread would be game code entering at a wall-clock moment.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/types.h"
#include "luaug/net/http.h"

#include <memory>
#include <optional>

namespace luaug::net {

using core::u64;
using core::usize;

// One outstanding call. Zero is never a valid ticket, so a default-constructed
// one cannot accidentally name somebody else's request.
struct NetTicket
{
    u64 value = 0;
    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(NetTicket, NetTicket) noexcept = default;
};

struct NetResult
{
    HttpResponse response;
    std::optional<core::EngineError> error;
};

class AsyncClient
{
public:
    // Two workers by default. Not one -- a single worker makes two concurrent
    // requests serial, and a script that fires three at a page load would see
    // the third wait for both -- and not many, because each is a thread that
    // exists to be idle and this is a game engine rather than a crawler.
    explicit AsyncClient(usize workers = 2);
    ~AsyncClient();

    AsyncClient(const AsyncClient&) = delete;
    AsyncClient& operator=(const AsyncClient&) = delete;

    [[nodiscard]] NetTicket submit(HttpRequest request);

    // Non-blocking. Returns false while the call is still in flight, and takes
    // the result exactly once: a second `take` of the same ticket returns false.
    [[nodiscard]] bool take(NetTicket ticket, NetResult& out);

    // How many calls are submitted and not yet taken. The one number a debug
    // panel wants, and the one a test needs to know when to stop pumping.
    [[nodiscard]] usize outstanding() const;

    // Stops the workers and drops every result. Called when a world goes away:
    // the coroutines waiting on those tickets went with it, and a result nobody
    // will ever take is a leak with a very long fuse.
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace luaug::net
