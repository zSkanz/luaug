#include "luaug/platform/async_io.h"

#include <SDL3/SDL_asyncio.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace luaug::platform {
namespace {

struct Request
{
    // Set once at submit and read on the pump thread afterwards.
    std::string path;
    IoPriority priority = IoPriority::Normal;
    IoCallback callback;

    IoStatus status = IoStatus::Unknown;
    std::vector<std::byte> bytes;

    u32 generation = 0;
    bool allocated = false;
    bool inFlight = false;
    // Cancelled while SDL still had it: the completion arrives, the buffer is
    // freed, and the slot is released without anybody being told.
    bool abandoned = false;

    // The order this request was submitted in. Ties inside a priority band
    // break by submission order rather than by slot number, so the queue is
    // FIFO within a band and cannot starve an old request behind a new one of
    // equal urgency.
    u64 sequence = 0;
};

struct Service
{
    std::mutex mutex;
    SDL_AsyncIOQueue* queue = nullptr;
    bool initialized = false;

    // The submitter, and why it exists (D038).
    //
    // `SDL_LoadFileAsync` OPENS THE FILE SYNCHRONOUSLY and only the read is
    // asynchronous. On a fast local disk that open is microseconds and nobody
    // notices; on a bind-mounted container filesystem it measured five to a
    // hundred and forty milliseconds -- on the frame thread, inside the
    // streaming pump. An "async IO" service whose submit path can block for a
    // tenth of a second is not one, and the machines where that bites -- a
    // network share, a phone's storage, a cold cache -- are exactly the ones a
    // streaming world exists for.
    //
    // So admission moved off the caller's thread entirely. `readFileAsync`
    // queues and returns; this thread picks the most urgent request, RELEASES
    // the mutex, and does the blocking open; `pumpIo` only ever harvests.
    std::thread submitter;
    std::condition_variable wake;
    bool stopping = false;
    u32 maxInFlight = 4;
    u64 nextSequence = 1;

    std::array<Request, MaxIoRequests> requests{};
    std::vector<u32> freeSlots;
    // Slot indices waiting for a place in flight. Kept unsorted and scanned on
    // admission: it is at most a few hundred entries, admission happens a
    // handful of times a frame, and a scan is cheaper to keep correct than a
    // heap whose keys change under `setIoPriority`.
    std::vector<u32> queued;

    u32 inFlight = 0;

    IoStats stats;

    // The safety net, and nothing more. `platform::shutdown` is what SHOULD
    // stop this service and does; this exists because a `std::thread` still
    // joinable when its destructor runs calls `std::terminate`, and a process
    // that merely forgot to shut down deserves a leak rather than a crash
    // report. It touches no SDL on purpose: by the time a static destructor
    // runs, SDL may already be gone.
    ~Service()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        if (submitter.joinable()) {
            submitter.join();
        }
    }
};

Service& service()
{
    static Service instance;
    return instance;
}

// Caller holds the lock.
void releaseSlotLocked(Service& s, u32 slot)
{
    Request& request = s.requests[slot];
    request.path.clear();
    request.callback = {};
    request.bytes.clear();
    request.bytes.shrink_to_fit();
    request.allocated = false;
    request.inFlight = false;
    request.abandoned = false;
    request.status = IoStatus::Unknown;
    request.generation += 1;
    if (request.generation == 0) {
        request.generation = 1;
    }
    s.freeSlots.push_back(slot);
}

// Caller holds the lock. The most urgent queued slot, or `MaxIoRequests` when
// there is nothing to admit.
[[nodiscard]] u32 pickLocked(Service& s)
{
    if (s.inFlight >= s.maxInFlight || s.queued.empty()) {
        return MaxIoRequests;
    }

    usize bestAt = 0;
    for (usize i = 1; i < s.queued.size(); ++i) {
        const Request& candidate = s.requests[s.queued[i]];
        const Request& best = s.requests[s.queued[bestAt]];
        const bool moreUrgent = candidate.priority < best.priority;
        const bool sameBandButOlder = candidate.priority == best.priority && candidate.sequence < best.sequence;
        if (moreUrgent || sameBandButOlder) {
            bestAt = i;
        }
    }

    const u32 slot = s.queued[bestAt];
    s.queued.erase(s.queued.begin() + static_cast<std::ptrdiff_t>(bestAt));

    // Counted as in flight from the moment it is PICKED rather than from the
    // moment SDL takes it. The open below happens outside the lock, so without
    // this a second pick would admit more work than the budget allows while the
    // first is still opening -- and bounding exactly that is what the budget is.
    s.requests[slot].inFlight = true;
    s.inFlight += 1;
    return slot;
}

// The submitter thread. See `Service::submitter`.
void submitLoop()
{
    Service& s = service();
    for (;;) {
        u32 slot = MaxIoRequests;
        std::string path;
        {
            std::unique_lock<std::mutex> lock(s.mutex);
            s.wake.wait(lock, [&s] { return s.stopping || (s.inFlight < s.maxInFlight && !s.queued.empty()); });
            if (s.stopping) {
                return;
            }
            slot = pickLocked(s);
            if (slot >= MaxIoRequests) {
                continue;
            }
            path = s.requests[slot].path;
        }

        // OUTSIDE the lock, and that is the whole point of the thread: the open
        // inside `SDL_LoadFileAsync` blocks, and a caller waiting on this mutex
        // meanwhile would be waiting on a disk it never asked about.
        void* const userdata = reinterpret_cast<void*>(static_cast<std::uintptr_t>(slot));
        const bool issued = SDL_LoadFileAsync(path.c_str(), s.queue, userdata);

        {
            const std::lock_guard<std::mutex> lock(s.mutex);
            if (issued) {
                s.stats.issued += 1;
                continue;
            }
            // Rolled back: `pickLocked`'s optimistic in-flight count has to come
            // off again, or a failing path leaks the budget one request at a
            // time until nothing can be issued at all.
            Request& request = s.requests[slot];
            request.inFlight = false;
            if (s.inFlight > 0) {
                s.inFlight -= 1;
            }
            request.status = IoStatus::Failed;
            s.stats.failed += 1;
        }
        s.wake.notify_one();
    }
}

} // namespace

bool initIo(u32 maxInFlight)
{
    Service& s = service();
    const std::lock_guard<std::mutex> lock(s.mutex);
    if (s.initialized) {
        return true;
    }

    s.queue = SDL_CreateAsyncIOQueue();
    if (s.queue == nullptr) {
        return false;
    }

    s.maxInFlight = std::max(1u, maxInFlight);
    s.freeSlots.clear();
    s.freeSlots.reserve(MaxIoRequests);
    for (u32 i = MaxIoRequests; i > 0; --i) {
        const u32 slot = i - 1;
        Request& request = s.requests[slot];
        request.generation = 1;
        request.allocated = false;
        request.status = IoStatus::Unknown;
        s.freeSlots.push_back(slot);
    }
    s.queued.clear();
    s.inFlight = 0;
    s.nextSequence = 1;
    s.stopping = false;
    s.initialized = true;

    // Started last, so it cannot observe a half-built service.
    s.submitter = std::thread(submitLoop);
    return true;
}

void shutdownIo()
{
    Service& s = service();

    // Two phases, and the lock is not held for the second: SDL's completion
    // wait can take as long as the disk does, and a caller blocked on this
    // mutex meanwhile would be blocked for no reason.
    SDL_AsyncIOQueue* queue = nullptr;
    u32 outstanding = 0;
    {
        const std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.initialized) {
            return;
        }
        for (const u32 slot : s.queued) {
            releaseSlotLocked(s, slot);
        }
        s.queued.clear();
        // Set under the mutex the submitter waits on, which is D037's rule and
        // not a style choice: a flag flipped outside it can be missed between
        // the predicate being evaluated and the wait being entered, and the
        // thread then sleeps through the shutdown and hangs the join below.
        s.stopping = true;
        queue = s.queue;
        outstanding = s.inFlight;
    }
    s.wake.notify_all();

    // Joined BEFORE the drain: a submitter still running could hand SDL another
    // read while the drain below is counting down to zero, and the queue would
    // be destroyed under it.
    if (s.submitter.joinable()) {
        s.submitter.join();
    }
    s.submitter = std::thread();

    // Re-read after the join. A request the submitter admitted between the two
    // locks above is in flight now and was not a moment ago, and draining one
    // fewer than SDL holds is how its buffers leak.
    {
        const std::lock_guard<std::mutex> lock(s.mutex);
        outstanding = s.inFlight;
    }

    // Everything SDL already has must be collected before the queue can be
    // destroyed, or its buffers leak and its threads write into a freed queue.
    //
    // A false return is NOT the end of the drain: upstream documents that more
    // than one waiting thread may wake for a single task and that the call may
    // return false spuriously (`SDL_asyncio.h:458-466`). Treating that as "no
    // more work" would free the queue under a task still in flight, so the
    // bound is on consecutive empty waits rather than on the first one.
    u32 collected = 0;
    u32 emptyWaits = 0;
    while (collected < outstanding && emptyWaits < 16) {
        SDL_AsyncIOOutcome outcome{};
        if (SDL_WaitAsyncIOResult(queue, &outcome, 1000)) {
            if (outcome.buffer != nullptr) {
                SDL_free(outcome.buffer);
            }
            collected += 1;
            emptyWaits = 0;
        }
        else {
            emptyWaits += 1;
        }
    }

    {
        const std::lock_guard<std::mutex> lock(s.mutex);
        for (u32 slot = 0; slot < MaxIoRequests; ++slot) {
            if (s.requests[slot].allocated) {
                releaseSlotLocked(s, slot);
            }
        }
        SDL_DestroyAsyncIOQueue(s.queue);
        s.queue = nullptr;
        s.inFlight = 0;
        s.initialized = false;
    }
}

bool isIoInitialized() noexcept
{
    Service& s = service();
    const std::lock_guard<std::mutex> lock(s.mutex);
    return s.initialized;
}

IoRequest readFileAsync(const std::filesystem::path& path, IoPriority priority, IoCallback callback)
{
    Service& s = service();
    const std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.initialized || s.freeSlots.empty()) {
        return {};
    }

    const u32 slot = s.freeSlots.back();
    s.freeSlots.pop_back();

    Request& request = s.requests[slot];
    request.allocated = true;
    request.status = IoStatus::Pending;
    request.priority = priority;
    request.callback = std::move(callback);
    request.sequence = s.nextSequence++;
    // UTF-8 rather than the narrow native encoding, for the reason file.h
    // gives: SDL converts UTF-8 back to wide on Windows, and `string()` would
    // hand MSVC's ANSI code page a path it cannot encode.
    const std::u8string utf8 = path.u8string();
    request.path.assign(reinterpret_cast<const char*>(utf8.c_str()), utf8.size());

    s.queued.push_back(slot);

    // The submitter is woken rather than the open being done here. That is the
    // whole of D038: this function is called from the frame thread inside the
    // streaming pump, and it must not be able to block on a filesystem.
    s.wake.notify_one();

    return IoRequest{slot, request.generation};
}

void pumpIo()
{
    Service& s = service();

    // Collected outside the lock and fired outside it: a callback that queues
    // another read would otherwise deadlock on this mutex, and queuing another
    // read from a completion is the whole shape of a streaming pipeline.
    struct Completed
    {
        IoRequest handle;
        IoStatus status;
        std::vector<std::byte> bytes;
        IoCallback callback;
    };
    std::vector<Completed> completed;
    bool wakeSubmitter = false;

    {
        const std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.initialized) {
            return;
        }

        SDL_AsyncIOOutcome outcome{};
        while (SDL_GetAsyncIOResult(s.queue, &outcome)) {
            const u32 slot = static_cast<u32>(reinterpret_cast<std::uintptr_t>(outcome.userdata));
            if (slot >= MaxIoRequests) {
                if (outcome.buffer != nullptr) {
                    SDL_free(outcome.buffer);
                }
                continue;
            }

            Request& request = s.requests[slot];
            request.inFlight = false;
            if (s.inFlight > 0) {
                s.inFlight -= 1;
            }

            const bool ok = outcome.result == SDL_ASYNCIO_COMPLETE;
            if (ok && outcome.buffer != nullptr) {
                const usize size = static_cast<usize>(outcome.bytes_transferred);
                if (!request.abandoned) {
                    request.bytes.resize(size);
                    if (size > 0) {
                        std::memcpy(request.bytes.data(), outcome.buffer, size);
                    }
                }
                s.stats.bytesRead += size;
            }
            if (outcome.buffer != nullptr) {
                // SDL_LoadFileAsync allocates with SDL_malloc and documents
                // that the caller frees it; the copy above is what lets the
                // free happen here rather than at some later take.
                SDL_free(outcome.buffer);
            }

            if (request.abandoned) {
                s.stats.cancelled += 1;
                releaseSlotLocked(s, slot);
                continue;
            }

            request.status = ok ? IoStatus::Ready : IoStatus::Failed;
            if (ok) {
                s.stats.completed += 1;
            }
            else {
                s.stats.failed += 1;
            }

            if (request.callback) {
                completed.push_back(Completed{IoRequest{slot, request.generation}, request.status,
                                              std::move(request.bytes), std::move(request.callback)});
                releaseSlotLocked(s, slot);
            }
        }

        // A completion freed a place in flight, so there may be room for the
        // next queued read. Notified under the lock the predicate reads, which
        // is the rule D037 was about.
        if (!s.queued.empty()) {
            wakeSubmitter = true;
        }
    }
    if (wakeSubmitter) {
        s.wake.notify_one();
    }

    for (Completed& entry : completed) {
        entry.callback(entry.handle, entry.status, std::move(entry.bytes));
    }
}

IoStatus ioStatus(IoRequest request) noexcept
{
    Service& s = service();
    const std::lock_guard<std::mutex> lock(s.mutex);
    if (request.index >= MaxIoRequests) {
        return IoStatus::Unknown;
    }
    const Request& slot = s.requests[request.index];
    if (slot.generation != request.generation || !slot.allocated) {
        return IoStatus::Unknown;
    }
    return slot.status;
}

bool takeIoResult(IoRequest request, std::vector<std::byte>& out)
{
    Service& s = service();
    const std::lock_guard<std::mutex> lock(s.mutex);
    if (request.index >= MaxIoRequests) {
        return false;
    }
    Request& slot = s.requests[request.index];
    if (slot.generation != request.generation || !slot.allocated || slot.status != IoStatus::Ready) {
        return false;
    }
    out = std::move(slot.bytes);
    releaseSlotLocked(s, request.index);
    return true;
}

void cancelIo(IoRequest request)
{
    Service& s = service();
    const std::lock_guard<std::mutex> lock(s.mutex);
    if (request.index >= MaxIoRequests) {
        return;
    }
    Request& slot = s.requests[request.index];
    if (slot.generation != request.generation || !slot.allocated) {
        return;
    }

    if (slot.inFlight) {
        // SDL_AsyncIO has no cancel. The read finishes, `pumpIo` frees its
        // buffer and releases the slot without telling anybody -- which is the
        // honest version of "cancelled" for work already started.
        slot.abandoned = true;
        slot.callback = {};
        return;
    }

    const auto at = std::find(s.queued.begin(), s.queued.end(), request.index);
    if (at != s.queued.end()) {
        s.queued.erase(at);
    }
    s.stats.cancelled += 1;
    releaseSlotLocked(s, request.index);
}

void setIoPriority(IoRequest request, IoPriority priority)
{
    Service& s = service();
    const std::lock_guard<std::mutex> lock(s.mutex);
    if (request.index >= MaxIoRequests) {
        return;
    }
    Request& slot = s.requests[request.index];
    if (slot.generation != request.generation || !slot.allocated || slot.inFlight) {
        return;
    }
    slot.priority = priority;
}

IoStats ioStats() noexcept
{
    Service& s = service();
    const std::lock_guard<std::mutex> lock(s.mutex);
    IoStats out = s.stats;
    out.inFlight = s.inFlight;
    out.queued = static_cast<u32>(s.queued.size());
    out.ready = 0;
    for (const Request& request : s.requests) {
        if (request.allocated && request.status == IoStatus::Ready) {
            out.ready += 1;
        }
    }
    return out;
}

void resetIoStats() noexcept
{
    Service& s = service();
    const std::lock_guard<std::mutex> lock(s.mutex);
    s.stats = IoStats{};
}

} // namespace luaug::platform
