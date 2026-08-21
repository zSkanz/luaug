#include "luaug/jobs/jobs.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace luaug::jobs {
namespace {

// Two lock domains, on purpose.
//
//   * `g_mutex` guards the job GRAPH -- slot allocation, dependency counters,
//     the dependent lists and completion. It is taken at schedule and at
//     completion, which is twice per job, and never while a job body runs.
//   * each worker's `queue` has its own mutex, so the steady state of a worker
//     -- pop, run, pop -- never touches the graph lock at all.
//
// A single lock over both would be simpler and would serialise every pop
// against every schedule, which is the shape that makes a pool slower than the
// loop it replaced. A lock-free deque would be faster still and is a
// measurement away; the queues are behind an interface here so that swapping
// one in is a file rather than a redesign.
constexpr u32 InvalidSlot = 0xFFFFFFFFu;

// Ranges beyond this many per `parallelFor` are scheduled in waves rather than
// all at once, so a very fine grain cannot exhaust the slot pool. The partition
// itself is untouched: wave boundaries change WHEN a range runs, never which
// elements it covers.
constexpr u32 MaxRangesPerWave = 1024;

struct Job
{
    JobFn invoke = nullptr;
    alignas(16) std::array<std::byte, detail::InlineStorageBytes> storage{};
    void* externalUser = nullptr;
    bool usesStorage = false;

    const char* name = "";
    Domain domain = Domain::Tooling;

    u32 generation = 0;
    u32 remainingDependencies = 0;
    bool finished = false;
    bool allocated = false;

    // Slots that cannot run until this one has. Small in practice; a vector
    // rather than an intrusive list because the graph lock already serialises
    // every touch of it and the allocation happens off the hot path.
    std::vector<u32> dependents;
};

struct Worker
{
    std::mutex mutex;
    std::deque<u32> queue;
    std::thread thread;
};

struct Pool
{
    std::mutex mutex;
    std::condition_variable wake;

    std::array<Job, MaxLiveJobs> jobs{};
    std::vector<u32> freeSlots;

    std::vector<std::unique_ptr<Worker>> workers;
    std::atomic<bool> running{false};
    std::atomic<u32> readyCount{0};
    std::atomic<u32> nextEnqueueWorker{0};
    std::atomic<u32> nextStealWorker{0};

    std::atomic<u64> scheduled{0};
    std::atomic<u64> executed{0};
    std::atomic<u64> stolen{0};
    std::array<std::atomic<u64>, 4> executedByDomain{};
};

Pool& pool()
{
    static Pool instance;
    return instance;
}

// Which worker the calling thread is, or `InvalidSlot` for the main thread and
// anything else outside the pool. Used to push new work onto the local queue
// (locality, and no contention) and to let `wait` help without stealing from
// itself.
thread_local u32 t_workerIndex = InvalidSlot;

[[nodiscard]] u32 allocateSlot()
{
    Pool& p = pool();
    if (p.freeSlots.empty()) {
        return InvalidSlot;
    }
    const u32 slot = p.freeSlots.back();
    p.freeSlots.pop_back();

    Job& job = p.jobs[slot];
    job.allocated = true;
    job.finished = false;
    job.remainingDependencies = 0;
    job.dependents.clear();
    return slot;
}

// The generation moves on RELEASE rather than on acquire, which is what makes a
// handle to a finished job answer truthfully: a caller holding the old
// generation sees a mismatch and reads it as "finished", which is exactly what
// happened to the job it named.
void releaseSlot(u32 slot)
{
    Pool& p = pool();
    Job& job = p.jobs[slot];
    job.allocated = false;
    job.invoke = nullptr;
    job.externalUser = nullptr;
    job.usesStorage = false;
    job.dependents.clear();
    job.generation += 1;
    if (job.generation == 0) {
        job.generation = 1;
    }
    p.freeSlots.push_back(slot);
}

void enqueue(u32 slot)
{
    Pool& p = pool();
    const usize workerCount = p.workers.size();
    if (workerCount == 0) {
        return;
    }

    // A job scheduled from inside a job goes on the local queue: it is almost
    // always the continuation of what that worker was just doing, and the data
    // it touches is in that core's cache.
    usize target = t_workerIndex;
    if (target >= workerCount) {
        target = p.nextEnqueueWorker.fetch_add(1, std::memory_order_relaxed) % workerCount;
    }

    {
        Worker& worker = *p.workers[target];
        const std::lock_guard<std::mutex> lock(worker.mutex);
        worker.queue.push_back(slot);
    }

    // Incremented UNDER THE GRAPH LOCK, and that is the whole of D037.
    //
    // `readyCount` is read by the wait predicate of a `condition_variable`
    // whose mutex is `p.mutex`, and a condition variable requires the
    // predicate's state to change under that same mutex. It did not. A waiter
    // evaluates the predicate while holding `p.mutex`, sees zero, and only THEN
    // atomically releases the lock and registers on the variable -- and an
    // enqueue landing in that window incremented an atomic nothing was
    // serialising against and notified a variable nobody had registered on yet.
    // The wake-up was lost and a worker slept forever with work in its own
    // queue.
    //
    // The comment that used to be here was right about a DIFFERENT window --
    // between a failed steal and the wait -- and concluded that a job "cannot be
    // missed", which is the sentence that made this take a hung CI run to find.
    //
    // The cost is one uncontended graph-lock acquisition per enqueue. The graph
    // lock is already taken twice per job; this is the third, and it buys the
    // only thing that makes the sleep safe.
    {
        const std::lock_guard<std::mutex> lock(p.mutex);
        p.readyCount.fetch_add(1, std::memory_order_release);
    }
    p.wake.notify_all();
}

[[nodiscard]] u32 popLocal(u32 workerIndex)
{
    Pool& p = pool();
    if (workerIndex >= p.workers.size()) {
        return InvalidSlot;
    }
    Worker& worker = *p.workers[workerIndex];
    const std::lock_guard<std::mutex> lock(worker.mutex);
    if (worker.queue.empty()) {
        return InvalidSlot;
    }
    // LIFO from the owner's end: the most recently pushed job is the one whose
    // inputs are still warm. Thieves take from the other end, where the oldest
    // and coldest work is.
    const u32 slot = worker.queue.back();
    worker.queue.pop_back();
    p.readyCount.fetch_sub(1, std::memory_order_acq_rel);
    return slot;
}

[[nodiscard]] u32 steal(u32 self)
{
    Pool& p = pool();
    const usize count = p.workers.size();
    if (count == 0) {
        return InvalidSlot;
    }

    // A worker starts looking at its neighbour; a thread that is not a worker
    // at all -- the main thread inside `wait` -- starts somewhere rotating, so
    // that helping does not always fall on the same victim.
    const usize start = self < count ? self : p.nextStealWorker.fetch_add(1, std::memory_order_relaxed) % count;
    for (usize i = 1; i <= count; ++i) {
        const usize victim = (start + i) % count;
        if (self < count && victim == self) {
            continue;
        }
        Worker& worker = *p.workers[victim];
        const std::lock_guard<std::mutex> lock(worker.mutex);
        if (worker.queue.empty()) {
            continue;
        }
        const u32 slot = worker.queue.front();
        worker.queue.pop_front();
        p.readyCount.fetch_sub(1, std::memory_order_acq_rel);
        p.stolen.fetch_add(1, std::memory_order_relaxed);
        return slot;
    }
    return InvalidSlot;
}

void complete(u32 slot)
{
    Pool& p = pool();
    std::vector<u32> ready;
    {
        const std::lock_guard<std::mutex> lock(p.mutex);
        Job& job = p.jobs[slot];
        job.finished = true;
        for (const u32 dependent : job.dependents) {
            Job& other = p.jobs[dependent];
            if (other.remainingDependencies > 0 && --other.remainingDependencies == 0) {
                ready.push_back(dependent);
            }
        }
        releaseSlot(slot);
    }

    // Outside the graph lock: enqueue takes a queue lock, and holding two locks
    // in one order here would put an ordering constraint on every other pair.
    for (const u32 dependent : ready) {
        enqueue(dependent);
    }
    p.wake.notify_all();
}

void execute(u32 slot)
{
    Pool& p = pool();
    Job& job = p.jobs[slot];

    const JobFn invoke = job.invoke;
    void* const payload = job.usesStorage ? static_cast<void*>(job.storage.data()) : job.externalUser;
    const Domain domain = job.domain;

    invoke(payload);

    p.executed.fetch_add(1, std::memory_order_relaxed);
    p.executedByDomain[static_cast<usize>(domain)].fetch_add(1, std::memory_order_relaxed);
    complete(slot);
}

// One unit of help. Returns false when there was nothing to run, which is the
// caller's cue to sleep rather than spin.
[[nodiscard]] bool tryRunOne()
{
    const u32 self = t_workerIndex;
    u32 slot = popLocal(self);
    if (slot == InvalidSlot) {
        slot = steal(self);
    }
    if (slot == InvalidSlot) {
        return false;
    }
    execute(slot);
    return true;
}

void workerLoop(u32 index)
{
    t_workerIndex = index;
    Pool& p = pool();

    while (p.running.load(std::memory_order_acquire)) {
        if (tryRunOne()) {
            continue;
        }

        std::unique_lock<std::mutex> lock(p.mutex);
        p.wake.wait(lock, [&p] {
            return p.readyCount.load(std::memory_order_acquire) > 0 || !p.running.load(std::memory_order_acquire);
        });
    }

    // Drain on the way out, so a `shutdown` racing the last few jobs does not
    // leave a dependent forever unsatisfied and a waiter forever parked.
    while (tryRunOne()) {
    }
    t_workerIndex = InvalidSlot;
}

[[nodiscard]] bool handleFinished(JobHandle handle)
{
    if (handle.index >= MaxLiveJobs) {
        // The serial-mode handle, and any handle from a pool that was never
        // initialized: the work has already happened.
        return true;
    }
    Pool& p = pool();
    const Job& job = p.jobs[handle.index];
    return job.generation != handle.generation || job.finished;
}

} // namespace

const char* domainName(Domain domain) noexcept
{
    switch (domain) {
    case Domain::SimVisible:
        return "sim";
    case Domain::Render:
        return "render";
    case Domain::AssetIo:
        return "asset";
    case Domain::Tooling:
        return "tooling";
    }
    return "unknown";
}

void init(u32 workerCount)
{
    Pool& p = pool();
    if (p.running.load(std::memory_order_acquire)) {
        return;
    }

    if (workerCount == 0) {
        const u32 hardware = std::thread::hardware_concurrency();
        // One fewer than the machine has, because the thread calling `wait`
        // helps: reserving a core for it would leave the pool one short of the
        // machine while that thread is busy anyway.
        workerCount = hardware > 1 ? hardware - 1 : 1;
    }
    workerCount = std::min(workerCount, MaxWorkers);

    {
        const std::lock_guard<std::mutex> lock(p.mutex);
        p.freeSlots.clear();
        p.freeSlots.reserve(MaxLiveJobs);
        // Descending, so the first allocations come off slot 0 upward -- a
        // detail that matters only for how a debugger reads, and costs nothing.
        for (u32 i = MaxLiveJobs; i > 0; --i) {
            const u32 slot = i - 1;
            Job& job = p.jobs[slot];
            job.generation = 1;
            job.allocated = false;
            job.finished = false;
            p.freeSlots.push_back(slot);
        }
    }

    p.running.store(true, std::memory_order_release);
    p.workers.reserve(workerCount);
    for (u32 i = 0; i < workerCount; ++i) {
        p.workers.push_back(std::make_unique<Worker>());
    }
    for (u32 i = 0; i < workerCount; ++i) {
        p.workers[i]->thread = std::thread(workerLoop, i);
    }
}

void shutdown()
{
    Pool& p = pool();
    if (!p.running.load(std::memory_order_acquire)) {
        return;
    }

    // Everything already scheduled runs. A pool that dropped queued work on
    // shutdown would make "did my callback happen" depend on timing, and the
    // asset system's callbacks are how memory gets released.
    while (tryRunOne()) {
    }

    // Under the graph lock, and this is the half of D037 that actually hung the
    // build. `running` is the other half of the worker's wait predicate, and it
    // was stored without `p.mutex` -- so a worker that had evaluated the
    // predicate (still running, nothing ready) but had not yet registered on the
    // variable missed the notify and slept through the shutdown. The main thread
    // then blocked in `join` forever.
    //
    // The hung process was exactly two threads, both waiting, both at zero CPU:
    // this thread inside `join` and one worker inside `wake.wait`. That shape is
    // what named the bug.
    {
        const std::lock_guard<std::mutex> lock(p.mutex);
        p.running.store(false, std::memory_order_release);
    }
    p.wake.notify_all();

    for (const std::unique_ptr<Worker>& worker : p.workers) {
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
    }
    p.workers.clear();
    p.readyCount.store(0, std::memory_order_release);
}

bool initialized() noexcept
{
    return pool().running.load(std::memory_order_acquire);
}

u32 workerCount() noexcept
{
    Pool& p = pool();
    return p.running.load(std::memory_order_acquire) ? static_cast<u32>(p.workers.size()) : 0u;
}

namespace detail {

JobHandle scheduleErased(const char* name, Domain domain, JobFn invoke, void* payload, usize payloadSize,
                         usize payloadAlign, std::span<const JobHandle> dependencies)
{
    Pool& p = pool();
    p.scheduled.fetch_add(1, std::memory_order_relaxed);

    // Serial mode, and the fallback when the slot pool is exhausted. Running
    // here rather than refusing is the honest behaviour: the caller asked for
    // work to happen, and a dropped job is a bug that surfaces somewhere else.
    const auto runInline = [&]() noexcept {
        // The caller's own copy, which is alive for the duration of this call
        // and is exactly what "runs on the calling thread" means.
        invoke(payload);
        p.executed.fetch_add(1, std::memory_order_relaxed);
        p.executedByDomain[static_cast<usize>(domain)].fetch_add(1, std::memory_order_relaxed);
        return JobHandle{MaxLiveJobs, 0xFFFFFFFFu};
    };

    if (payloadAlign > 16 || payloadSize > InlineStorageBytes) {
        return runInline();
    }
    if (!p.running.load(std::memory_order_acquire)) {
        return runInline();
    }

    u32 slot = InvalidSlot;
    u32 generation = 0;
    bool readyNow = false;
    {
        const std::lock_guard<std::mutex> lock(p.mutex);
        slot = allocateSlot();
        if (slot != InvalidSlot) {
            Job& job = p.jobs[slot];
            job.invoke = invoke;
            job.usesStorage = true;
            job.externalUser = nullptr;
            job.name = name;
            job.domain = domain;
            std::memcpy(job.storage.data(), payload, payloadSize);

            for (const JobHandle& dependency : dependencies) {
                if (dependency.index >= MaxLiveJobs) {
                    continue;
                }
                Job& other = p.jobs[dependency.index];
                if (other.generation != dependency.generation || other.finished) {
                    continue;
                }
                other.dependents.push_back(slot);
                job.remainingDependencies += 1;
            }
            readyNow = job.remainingDependencies == 0;
            generation = job.generation;
        }
    }

    if (slot == InvalidSlot) {
        return runInline();
    }

    const JobHandle handle{slot, generation};
    if (readyNow) {
        enqueue(slot);
    }
    return handle;
}

void parallelForErased(const char* name, Domain domain, usize begin, usize end, usize grain, RangeFn invoke, void* user)
{
    const u32 ranges = rangeCount(begin, end, grain);
    if (ranges == 0) {
        return;
    }

    Pool& p = pool();
    const auto runRange = [&](u32 index) noexcept {
        const usize rangeBegin = begin + static_cast<usize>(index) * grain;
        const usize rangeEnd = std::min(rangeBegin + grain, end);
        invoke(user, rangeBegin, rangeEnd, index);
    };

    if (!p.running.load(std::memory_order_acquire) || ranges == 1) {
        for (u32 index = 0; index < ranges; ++index) {
            runRange(index);
        }
        p.scheduled.fetch_add(ranges, std::memory_order_relaxed);
        p.executed.fetch_add(ranges, std::memory_order_relaxed);
        p.executedByDomain[static_cast<usize>(domain)].fetch_add(ranges, std::memory_order_relaxed);
        return;
    }

    struct Context
    {
        RangeFn invoke;
        void* user;
        usize begin;
        usize end;
        usize grain;
    };
    Context context{invoke, user, begin, end, grain};

    std::vector<JobHandle> handles;
    handles.reserve(std::min<u32>(ranges, MaxRangesPerWave));

    for (u32 waveStart = 0; waveStart < ranges; waveStart += MaxRangesPerWave) {
        const u32 waveEnd = std::min(waveStart + MaxRangesPerWave, ranges);
        handles.clear();
        for (u32 index = waveStart; index < waveEnd; ++index) {
            handles.push_back(schedule(name, domain, [ctx = &context, index]() noexcept {
                const usize rangeBegin = ctx->begin + static_cast<usize>(index) * ctx->grain;
                const usize rangeEnd = std::min(rangeBegin + ctx->grain, ctx->end);
                ctx->invoke(ctx->user, rangeBegin, rangeEnd, index);
            }));
        }
        waitAll(handles);
    }
}

} // namespace detail

JobHandle schedule(const char* name, Domain domain, JobFn fn, void* user, std::span<const JobHandle> dependencies)
{
    struct Trampoline
    {
        JobFn fn;
        void* user;
    };
    Trampoline trampoline{fn, user};
    const JobFn invoke = [](void* payload) noexcept {
        const Trampoline* const call = static_cast<const Trampoline*>(payload);
        call->fn(call->user);
    };
    return detail::scheduleErased(name, domain, invoke, &trampoline, sizeof(Trampoline), alignof(Trampoline),
                                  dependencies);
}

void parallelFor(const char* name, Domain domain, usize begin, usize end, usize grain, RangeFn fn, void* user)
{
    detail::parallelForErased(name, domain, begin, end, grain, fn, user);
}

void wait(JobHandle handle)
{
    Pool& p = pool();
    for (;;) {
        {
            const std::lock_guard<std::mutex> lock(p.mutex);
            if (handleFinished(handle)) {
                return;
            }
        }

        // Help rather than idle. This is also what makes a `wait` inside a job
        // safe: the waiting worker keeps draining the pool instead of holding
        // one of its threads hostage.
        if (tryRunOne()) {
            continue;
        }

        std::unique_lock<std::mutex> lock(p.mutex);
        p.wake.wait(
            lock, [&p, handle] { return handleFinished(handle) || p.readyCount.load(std::memory_order_acquire) > 0; });
    }
}

void waitAll(std::span<const JobHandle> handles)
{
    for (const JobHandle& handle : handles) {
        wait(handle);
    }
}

bool finished(JobHandle handle) noexcept
{
    Pool& p = pool();
    const std::lock_guard<std::mutex> lock(p.mutex);
    return handleFinished(handle);
}

Stats stats() noexcept
{
    Pool& p = pool();
    Stats out;
    out.scheduled = p.scheduled.load(std::memory_order_relaxed);
    out.executed = p.executed.load(std::memory_order_relaxed);
    out.stolen = p.stolen.load(std::memory_order_relaxed);
    for (usize i = 0; i < p.executedByDomain.size(); ++i) {
        out.executedByDomain[i] = p.executedByDomain[i].load(std::memory_order_relaxed);
    }
    return out;
}

void resetStats() noexcept
{
    Pool& p = pool();
    p.scheduled.store(0, std::memory_order_relaxed);
    p.executed.store(0, std::memory_order_relaxed);
    p.stolen.store(0, std::memory_order_relaxed);
    for (std::atomic<u64>& counter : p.executedByDomain) {
        counter.store(0, std::memory_order_relaxed);
    }
}

} // namespace luaug::jobs
