#include "luaug/platform/async_io.h"

#include <SDL3/SDL_asyncio.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
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

// Caller holds the lock. Hands the highest-priority queued request to SDL,
// repeating until the in-flight budget is full or nothing is left.
void admitLocked(Service& s)
{
    while (s.inFlight < s.maxInFlight && !s.queued.empty()) {
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

        Request& request = s.requests[slot];
        // The slot index is the userdata SDL hands back. It is an index rather
        // than a pointer because the slot array never moves and an index
        // survives being compared against a generation.
        void* const userdata = reinterpret_cast<void*>(static_cast<std::uintptr_t>(slot));
        if (!SDL_LoadFileAsync(request.path.c_str(), s.queue, userdata)) {
            request.status = IoStatus::Failed;
            s.stats.failed += 1;
            continue;
        }

        request.inFlight = true;
        s.inFlight += 1;
        s.stats.issued += 1;
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
    s.initialized = true;
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
        queue = s.queue;
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
    admitLocked(s);

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

        admitLocked(s);
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
