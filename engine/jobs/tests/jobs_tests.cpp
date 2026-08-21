#include "luaug/jobs/jobs.h"

#include <atomic>
#include <cstring>
#include <doctest/doctest.h>
#include <vector>

using namespace luaug::jobs;
using luaug::core::u32;
using luaug::core::u64;
using luaug::core::usize;

namespace {

// Brings the pool up for one test and puts it back down, so a failing case
// cannot leave worker threads running into the next one.
struct ScopedPool
{
    explicit ScopedPool(u32 workers)
    {
        init(workers);
        resetStats();
    }

    ~ScopedPool() { shutdown(); }

    ScopedPool(const ScopedPool&) = delete;
    ScopedPool& operator=(const ScopedPool&) = delete;
};

// Deterministic busy work. Not a sleep: a test that sleeps is a test whose
// meaning changes with the machine, and what these cases need is only that
// different ranges take visibly different amounts of time.
u64 burn(u64 rounds)
{
    u64 accumulator = 1469598103934665603ull;
    for (u64 i = 0; i < rounds; ++i) {
        accumulator ^= i;
        accumulator *= 1099511628211ull;
    }
    return accumulator;
}

} // namespace

TEST_CASE("an uninitialized pool is a serial pool")
{
    // Independent of whatever ran before it: doctest orders cases by file, and
    // a case that depends on the pool being down should say so rather than
    // hope.
    shutdown();
    REQUIRE_FALSE(initialized());
    CHECK(workerCount() == 0);

    int ran = 0;
    const JobHandle handle = schedule("serial", Domain::Tooling, [&ran]() noexcept { ran += 1; });

    // Already done by the time schedule returned: that is the contract, and it
    // is what lets every headless run and every unit test skip the setup.
    CHECK(ran == 1);
    CHECK(finished(handle));
    wait(handle);
    CHECK(ran == 1);
}

TEST_CASE("every scheduled job runs exactly once")
{
    ScopedPool pool(4);
    CHECK(initialized());
    CHECK(workerCount() == 4);

    constexpr u32 count = 512;
    std::vector<std::atomic<u32>> ran(count);
    for (std::atomic<u32>& slot : ran) {
        slot.store(0);
    }

    std::vector<JobHandle> handles;
    handles.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        std::atomic<u32>* const slot = &ran[i];
        handles.push_back(schedule("unit", Domain::Tooling, [slot]() noexcept { slot->fetch_add(1); }));
    }
    waitAll(handles);

    for (u32 i = 0; i < count; ++i) {
        CHECK(ran[i].load() == 1);
    }
    CHECK(stats().executed == count);
}

TEST_CASE("a dependency runs before its dependent")
{
    ScopedPool pool(4);

    std::atomic<u32> order{0};
    std::atomic<u32> firstAt{0};
    std::atomic<u32> secondAt{0};

    const JobHandle first = schedule("first", Domain::Tooling, [&order, &firstAt]() noexcept {
        burn(200000);
        firstAt.store(order.fetch_add(1) + 1);
    });

    const JobHandle dependencies[] = {first};
    const JobHandle second = schedule(
        "second", Domain::Tooling, [&order, &secondAt]() noexcept { secondAt.store(order.fetch_add(1) + 1); },
        dependencies);

    wait(second);
    CHECK(firstAt.load() == 1);
    CHECK(secondAt.load() == 2);
}

TEST_CASE("a chain of dependencies runs in chain order")
{
    ScopedPool pool(4);

    constexpr u32 length = 32;
    std::atomic<u32> cursor{0};

    // Each link records its own index into a slot the previous link is
    // guaranteed to have already filled, so a broken edge shows up as a hole
    // rather than as a flake.
    std::vector<u32> recorded(length, 0xFFFFFFFFu);
    JobHandle previous{};
    for (u32 i = 0; i < length; ++i) {
        u32* const target = &recorded[i];
        std::atomic<u32>* const counter = &cursor;
        const JobHandle dependencies[] = {previous};
        const std::span<const JobHandle> deps =
            i == 0 ? std::span<const JobHandle>{} : std::span<const JobHandle>{dependencies};
        previous =
            schedule("link", Domain::Tooling, [target, counter]() noexcept { *target = counter->fetch_add(1); }, deps);
    }
    wait(previous);

    for (u32 i = 0; i < length; ++i) {
        CHECK(recorded[i] == i);
    }
}

TEST_CASE("parallelFor covers every element exactly once")
{
    ScopedPool pool(4);

    constexpr usize total = 10000;
    constexpr usize grain = 64;
    std::vector<std::atomic<u32>> touched(total);
    for (std::atomic<u32>& slot : touched) {
        slot.store(0);
    }

    std::atomic<u32> highestRange{0};
    parallelFor("cover", Domain::Render, 0, total, grain,
                [&touched, &highestRange](usize begin, usize end, u32 rangeIndex) noexcept {
                    for (usize i = begin; i < end; ++i) {
                        touched[i].fetch_add(1);
                    }
                    u32 previous = highestRange.load();
                    while (rangeIndex > previous && !highestRange.compare_exchange_weak(previous, rangeIndex)) {
                    }
                });

    for (usize i = 0; i < total; ++i) {
        CHECK(touched[i].load() == 1);
    }

    // The pool is not allowed to disagree with the partition callers size their
    // commit buckets from.
    CHECK(rangeCount(0, total, grain) == highestRange.load() + 1);
}

TEST_CASE("rangeCount is the partition parallelFor actually uses")
{
    CHECK(rangeCount(0, 0, 8) == 0);
    CHECK(rangeCount(0, 1, 8) == 1);
    CHECK(rangeCount(0, 8, 8) == 1);
    CHECK(rangeCount(0, 9, 8) == 2);
    CHECK(rangeCount(4, 20, 8) == 2);
    CHECK(rangeCount(0, 10, 0) == 0);
}

TEST_CASE("waiting inside a job does not deadlock the pool")
{
    // One worker, deliberately: if `wait` parked the thread instead of helping,
    // the inner job would have nobody left to run it and this case would hang.
    ScopedPool pool(1);

    std::atomic<u32> inner{0};
    const JobHandle outer = schedule("outer", Domain::Tooling, [&inner]() noexcept {
        const JobHandle nested = schedule("inner", Domain::Tooling, [&inner]() noexcept { inner.fetch_add(1); });
        wait(nested);
    });
    wait(outer);
    CHECK(inner.load() == 1);
}

TEST_CASE("shutdown runs what was already scheduled")
{
    std::atomic<u32> ran{0};
    {
        init(2);
        resetStats();
        for (u32 i = 0; i < 64; ++i) {
            (void)schedule("drain", Domain::AssetIo, [&ran]() noexcept {
                burn(1000);
                ran.fetch_add(1);
            });
        }
        shutdown();
    }
    // A pool that dropped queued work on the way out would make "did my
    // callback happen" depend on timing, and asset callbacks are how memory is
    // released.
    CHECK(ran.load() == 64);
    CHECK_FALSE(initialized());
}

TEST_CASE("more jobs than there are slots still all run")
{
    ScopedPool pool(4);

    std::atomic<u32> ran{0};
    // Well past MaxLiveJobs, scheduled in flight rather than in one batch:
    // slots are recycled on completion, and the fallback when they are not is
    // to run inline rather than to drop the work.
    for (u32 i = 0; i < MaxLiveJobs * 2; ++i) {
        const JobHandle handle = schedule("many", Domain::Tooling, [&ran]() noexcept { ran.fetch_add(1); });
        if ((i % 128) == 0) {
            wait(handle);
        }
    }
    // Nothing is left behind: shutdown drains, and the count is checked after.
    shutdown();
    CHECK(ran.load() == MaxLiveJobs * 2);
}

TEST_CASE("a handle whose job has been recycled reports finished")
{
    ScopedPool pool(2);

    const JobHandle handle = schedule("short", Domain::Tooling, []() noexcept {});
    wait(handle);
    CHECK(finished(handle));

    // Churn enough work to reuse the slot, then ask again. The honest answer
    // about the job the handle named is still "it finished".
    for (u32 i = 0; i < 4096; ++i) {
        wait(schedule("churn", Domain::Tooling, []() noexcept {}));
    }
    CHECK(finished(handle));
    wait(handle);
}

TEST_CASE("domain counters attribute work to the domain that asked for it")
{
    ScopedPool pool(2);

    for (u32 i = 0; i < 10; ++i) {
        wait(schedule("sim", Domain::SimVisible, []() noexcept {}));
    }
    for (u32 i = 0; i < 3; ++i) {
        wait(schedule("io", Domain::AssetIo, []() noexcept {}));
    }

    const Stats snapshot = stats();
    CHECK(snapshot.executedByDomain[static_cast<usize>(Domain::SimVisible)] == 10);
    CHECK(snapshot.executedByDomain[static_cast<usize>(Domain::AssetIo)] == 3);
    CHECK(snapshot.executedByDomain[static_cast<usize>(Domain::Render)] == 0);
    CHECK(snapshot.executed == 13);
}

TEST_CASE("domain names are stable")
{
    CHECK(std::strcmp(domainName(Domain::SimVisible), "sim") == 0);
    CHECK(std::strcmp(domainName(Domain::Render), "render") == 0);
    CHECK(std::strcmp(domainName(Domain::AssetIo), "asset") == 0);
    CHECK(std::strcmp(domainName(Domain::Tooling), "tooling") == 0);
}
