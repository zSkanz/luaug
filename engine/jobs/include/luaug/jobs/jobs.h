// The engine's CPU work pool (architecture.md §2, L1).
//
// Two rules define this module, and both are contracts rather than advice:
//
//   1. **No blocking IO here.** A worker that blocks on a file read is a worker
//      that is not stealing work, and the pool has no way to know it. Async IO
//      is `platform::readFileAsync`, which owns its own queue; jobs DECODE what
//      IO delivers.
//   2. **Nothing about which worker ran what may become observable simulation
//      state** (R10). The pool makes no promise at all about execution order,
//      and offers `StableCommit` (commit.h) as the sanctioned way to get an
//      answer that does not depend on it: per-job buckets, a barrier, and a
//      merge in bucket-index order.
//
// An UNINITIALIZED pool is a serial pool: `schedule` runs the callable
// immediately on the calling thread and returns a handle that is already
// finished, and `parallelFor` walks its ranges in order. That is not a
// degraded mode to apologise for -- it is the mode every headless determinism
// run and every unit test wants, and having it be the default means no caller
// needs an `if (poolExists)` branch.
#pragma once

#include "luaug/core/types.h"

#include <span>
#include <type_traits>
#include <utility>

namespace luaug::jobs {

using core::u32;
using core::u64;
using core::u8;
using core::usize;

// What a job's results are allowed to influence. Carried in the API rather
// than in a comment because it is the one classification that has to survive
// being read by somebody who did not write the job: `SimVisible` work is work
// whose output reaches the world, and it is the only domain the stable-commit
// discipline is mandatory for.
//
// The pool does not schedule domains differently today. It records them, the
// debug overlay reports per-domain counts, and a future priority policy has a
// vocabulary to be written in.
enum class Domain : u8
{
    // Reaches the simulation. Results must be committed stably (commit.h).
    SimVisible,
    // Extraction, culling, upload preparation. Consumed by the frame being
    // drawn and never by the tick.
    Render,
    // Decode, transcode, materialisation preparation. Never the IO wait itself.
    AssetIo,
    // Editors, gates, offline tools. No ordering requirement of any kind.
    Tooling,
};

[[nodiscard]] const char* domainName(Domain domain) noexcept;

// A slot handle with a generation, so a handle outliving its job is detectable
// rather than dangerous: waiting on a recycled handle reports "finished",
// which is the truthful answer about the job it names.
struct JobHandle
{
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }

    [[nodiscard]] constexpr bool operator==(const JobHandle&) const noexcept = default;
};

// The most jobs that may be live at one time. A fixed pool because the
// alternative is allocating on the hot path, and because a runaway scheduler
// should hit a named limit rather than swallow the machine.
inline constexpr u32 MaxLiveJobs = 4096;

// The most workers the pool will start regardless of what the machine reports.
inline constexpr u32 MaxWorkers = 64;

// `workerCount == 0` asks for one worker per hardware thread minus the caller's
// own, clamped to at least one. Calling `init` twice without `shutdown` is a
// no-op, not an error: several subsystems may want the pool and none of them
// owns it.
void init(u32 workerCount = 0);

// Drains what is already scheduled, joins every worker, and returns the pool to
// the serial mode described at the top of this file.
void shutdown();

[[nodiscard]] bool initialized() noexcept;

// Zero when the pool is serial.
[[nodiscard]] u32 workerCount() noexcept;

// The raw entry point. `user` is not owned, not copied, and must outlive the
// job -- which is exactly why the templated overload below exists and is what
// callers should reach for.
using JobFn = void (*)(void* user) noexcept;

JobHandle schedule(const char* name, Domain domain, JobFn fn, void* user, std::span<const JobHandle> dependencies = {});

// Runs `[begin, end)` in `grain`-sized ranges. `rangeIndex` counts ranges from
// zero in ascending order of `begin`, and IS stable: range 3 covers the same
// elements on every run, on every machine, whichever worker happens to execute
// it. That is what makes it a legitimate index into a `StableCommit`.
using RangeFn = void (*)(void* user, usize begin, usize end, u32 rangeIndex) noexcept;

void parallelFor(const char* name, Domain domain, usize begin, usize end, usize grain, RangeFn fn, void* user);

// How many ranges `parallelFor` will produce for these arguments. Callers size
// a `StableCommit` with this, and the pool is not allowed to disagree with it:
// the partition is a function of the DATA, never of how many workers this
// machine has, or a bucket index would mean different things on two machines.
[[nodiscard]] constexpr u32 rangeCount(usize begin, usize end, usize grain) noexcept
{
    if (end <= begin || grain == 0) {
        return 0;
    }
    const usize count = ((end - begin) + grain - 1) / grain;
    return static_cast<u32>(count);
}

// Blocks until the job has run. The calling thread HELPS while it waits --
// it executes ready jobs rather than idling -- so a wait inside a job cannot
// deadlock the pool by holding a worker hostage.
void wait(JobHandle handle);
void waitAll(std::span<const JobHandle> handles);

[[nodiscard]] bool finished(JobHandle handle) noexcept;

// Counters for the debug overlay and for tests that need to prove work was
// actually distributed rather than quietly serialised. Monotonic since `init`.
struct Stats
{
    u64 scheduled = 0;
    u64 executed = 0;
    u64 stolen = 0;
    u64 executedByDomain[4] = {};
};

[[nodiscard]] Stats stats() noexcept;
void resetStats() noexcept;

namespace detail {

// Callables up to this size are stored inside the job slot. Larger ones are
// refused at compile time rather than silently heap-allocated: a job body that
// captures more than this is a job body that should be capturing a pointer to
// something the caller owns, and finding that out at the call site is cheaper
// than finding it out in a profile.
inline constexpr usize InlineStorageBytes = 56;

JobHandle scheduleErased(const char* name, Domain domain, JobFn invoke, void* payload, usize payloadSize,
                         usize payloadAlign, std::span<const JobHandle> dependencies);

void parallelForErased(const char* name, Domain domain, usize begin, usize end, usize grain, RangeFn invoke,
                       void* user);

} // namespace detail

// The overload callers should use: the callable is COPIED into the job slot, so
// there is no lifetime question to get wrong.
template <class Callable>
JobHandle schedule(const char* name, Domain domain, Callable&& callable, std::span<const JobHandle> dependencies = {})
{
    using Stored = std::decay_t<Callable>;
    static_assert(sizeof(Stored) <= detail::InlineStorageBytes,
                  "job body is too large to store inline; capture a pointer to caller-owned state instead");
    static_assert(std::is_nothrow_invocable_r_v<void, Stored&>,
                  "a job body must be callable as `void() noexcept`: the pool has no thread to throw on");
    // Trivially copyable, so the slot copy is a memcpy and the slot never has
    // to be destroyed. A job body that owns something is a lifetime question
    // spanning two threads, and the answer to it is to capture a pointer to
    // state the caller keeps alive across the `wait`.
    static_assert(std::is_trivially_copyable_v<Stored> && std::is_trivially_destructible_v<Stored>,
                  "a job body must be trivially copyable: capture pointers and scalars, not owning objects");

    Stored value{std::forward<Callable>(callable)};
    const JobFn invoke = [](void* payload) noexcept { (*static_cast<Stored*>(payload))(); };
    return detail::scheduleErased(name, domain, invoke, &value, sizeof(Stored), alignof(Stored), dependencies);
}

// `callable(begin, end, rangeIndex)`. Ranges are the same on every run; see
// `RangeFn`.
template <class Callable>
void parallelFor(const char* name, Domain domain, usize begin, usize end, usize grain, Callable&& callable)
{
    using Stored = std::remove_reference_t<Callable>;
    static_assert(std::is_nothrow_invocable_r_v<void, Stored&, usize, usize, u32>,
                  "a parallelFor body must be callable as `void(usize, usize, u32) noexcept`");

    const RangeFn invoke = [](void* user, usize rangeBegin, usize rangeEnd, u32 rangeIndex) noexcept {
        (*static_cast<Stored*>(user))(rangeBegin, rangeEnd, rangeIndex);
    };
    // The callable outlives the call because this function does not return
    // until every range has run, which is what lets it be borrowed rather than
    // copied per range.
    detail::parallelForErased(name, domain, begin, end, grain, invoke,
                              const_cast<void*>(static_cast<const void*>(std::addressof(callable))));
}

} // namespace luaug::jobs
