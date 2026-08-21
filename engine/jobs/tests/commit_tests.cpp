#include "luaug/jobs/commit.h"
#include "luaug/jobs/jobs.h"

#include <doctest/doctest.h>
#include <vector>

using namespace luaug::jobs;
using luaug::core::u32;
using luaug::core::u64;
using luaug::core::usize;

namespace {

struct ScopedPool
{
    explicit ScopedPool(u32 workers) { init(workers); }

    ~ScopedPool() { shutdown(); }

    ScopedPool(const ScopedPool&) = delete;
    ScopedPool& operator=(const ScopedPool&) = delete;
};

// A value expensive enough to compute that ranges finish at visibly different
// times, and cheap enough that the suite stays fast. The skew is the point: a
// merge that depended on completion order would produce a different answer on
// most runs of the case below, rather than on one in a thousand.
u64 mix(u64 value, u64 rounds)
{
    for (u64 i = 0; i < rounds; ++i) {
        value ^= value >> 33;
        value *= 0xff51afd7ed558ccdull;
        value += i;
    }
    return value;
}

constexpr usize Total = 4096;
constexpr usize Grain = 64;

// Range 0 is a hundred times more expensive than its neighbours, so the bucket
// that must come FIRST in the merged output is the one that finishes LAST.
[[nodiscard]] u64 workFor(usize index, u32 rangeIndex)
{
    return mix(static_cast<u64>(index), rangeIndex == 0 ? 2000 : 20);
}

} // namespace

TEST_CASE("merged order is bucket order, whatever order the buckets were filled in")
{
    StableCommit<int> commit(4);

    // Filled deliberately out of order -- which is what a worker pool does to
    // you, and the one thing the merge is not allowed to notice.
    commit.bucket(2).push_back(20);
    commit.bucket(0).push_back(0);
    commit.bucket(3).push_back(30);
    commit.bucket(2).push_back(21);
    commit.bucket(0).push_back(1);

    const std::vector<int> merged = commit.merged();
    REQUIRE(merged.size() == 5);
    CHECK(merged[0] == 0);
    CHECK(merged[1] == 1);
    CHECK(merged[2] == 20);
    CHECK(merged[3] == 21);
    CHECK(merged[4] == 30);

    CHECK(commit.bucketCount() == 4);
    CHECK(commit.size() == 5);
    CHECK_FALSE(commit.empty());

    std::vector<int> visited;
    commit.forEach([&visited](int value) { visited.push_back(value); });
    CHECK(visited == merged);

    commit.clear();
    CHECK(commit.empty());
    CHECK(commit.bucketCount() == 4);
}

TEST_CASE("a stable commit over parallelFor is bit-identical to the serial answer")
{
    // The reference: the same computation, in order, on one thread.
    std::vector<u64> reference;
    reference.reserve(Total);
    for (u32 range = 0; range < rangeCount(0, Total, Grain); ++range) {
        const usize begin = static_cast<usize>(range) * Grain;
        const usize end = begin + Grain < Total ? begin + Grain : Total;
        for (usize i = begin; i < end; ++i) {
            reference.push_back(workFor(i, range));
        }
    }

    ScopedPool pool(4);

    // Repeated, because a merge that depended on completion order would agree
    // with the reference some of the time. Sixteen runs of a deliberately
    // skewed workload is enough that "it happened to be in order" stops being
    // an explanation.
    for (int attempt = 0; attempt < 16; ++attempt) {
        StableCommit<u64> commit(rangeCount(0, Total, Grain));
        parallelFor("stable", Domain::SimVisible, 0, Total, Grain,
                    [&commit](usize begin, usize end, u32 rangeIndex) noexcept {
                        std::vector<u64>& bucket = commit.bucket(rangeIndex);
                        for (usize i = begin; i < end; ++i) {
                            bucket.push_back(workFor(i, rangeIndex));
                        }
                    });

        const std::vector<u64> merged = commit.merged();
        REQUIRE(merged.size() == reference.size());
        CHECK(merged == reference);
    }
}

TEST_CASE("the same commit answers the same in serial mode and on the pool")
{
    const auto run = [] {
        StableCommit<u64> commit(rangeCount(0, Total, Grain));
        parallelFor("compare", Domain::SimVisible, 0, Total, Grain,
                    [&commit](usize begin, usize end, u32 rangeIndex) noexcept {
                        std::vector<u64>& bucket = commit.bucket(rangeIndex);
                        for (usize i = begin; i < end; ++i) {
                            bucket.push_back(workFor(i, rangeIndex));
                        }
                    });
        return commit.merged();
    };

    // Serial mode first: an uninitialized pool walks its ranges in order.
    shutdown();
    const std::vector<u64> serial = run();

    init(4);
    const std::vector<u64> parallel = run();
    shutdown();

    // A world that ticks with the pool up and a world that ticks headless with
    // it down have to agree, or R10's replay guarantee ends at the first job.
    CHECK(serial == parallel);
}
