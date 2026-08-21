// The sanctioned way for parallel work to produce an answer the simulation may
// read (R10, architecture.md §2's deterministic commit rule).
//
// The rule in one sentence: **per-job buffers, a barrier, then a merge in
// bucket-index order.** Never a shared container under a mutex -- that produces
// a correct result in an order decided by whichever worker got there first,
// which is precisely the thing a replay cannot reproduce.
//
//     jobs::StableCommit<Contact> contacts(ranges);
//     jobs::parallelFor("collect", jobs::Domain::SimVisible, 0, count, 256,
//         [&](usize begin, usize end, u32 range) noexcept {
//             for (usize i = begin; i < end; ++i) { ... contacts.bucket(range).push_back(c); }
//         });
//     for (const Contact& c : contacts.merged()) { world.apply(c); }
//
// `parallelFor` returning is the barrier; `merged()` is the stable pass. The
// result is bit-identical whichever order the ranges completed in, because a
// range's index -- and therefore its bucket -- is a property of the data and
// not of the schedule.
#pragma once

#include "luaug/core/types.h"

#include <cstddef>
#include <new>
#include <utility>
#include <vector>

namespace luaug::jobs {

using core::u32;
using core::usize;

// Buckets sit one per cache line. Two workers appending to two vectors whose
// control blocks share a line is the classic way a parallel loop gets slower
// than the serial one it replaced, and the fix costs padding rather than
// thought.
inline constexpr usize CommitBucketAlignment = 64;

template <class T>
class StableCommit
{
public:
    explicit StableCommit(u32 bucketCount) : m_buckets(bucketCount) {}

    [[nodiscard]] u32 bucketCount() const noexcept { return static_cast<u32>(m_buckets.size()); }

    // Unchecked on purpose: the index comes from `parallelFor`, which produced
    // it and the bucket count from the same range partition. A bounds check
    // here would be a check on the pool's own arithmetic, on the hot path, in
    // every job.
    [[nodiscard]] std::vector<T>& bucket(u32 index) noexcept { return m_buckets[index].values; }

    [[nodiscard]] const std::vector<T>& bucket(u32 index) const noexcept { return m_buckets[index].values; }

    [[nodiscard]] usize size() const noexcept
    {
        usize total = 0;
        for (const Bucket& bucket : m_buckets) {
            total += bucket.values.size();
        }
        return total;
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    // Buckets in index order, values in push order within a bucket. This is the
    // whole determinism guarantee, and it is a property of walking the array
    // rather than of anything the pool does.
    template <class Fn>
    void forEach(Fn&& fn) const
    {
        for (const Bucket& bucket : m_buckets) {
            for (const T& value : bucket.values) {
                fn(value);
            }
        }
    }

    [[nodiscard]] std::vector<T> merged() const
    {
        std::vector<T> result;
        result.reserve(size());
        for (const Bucket& bucket : m_buckets) {
            result.insert(result.end(), bucket.values.begin(), bucket.values.end());
        }
        return result;
    }

    // Keeps each bucket's capacity, which is the point of holding one of these
    // across frames rather than building it per tick.
    void clear() noexcept
    {
        for (Bucket& bucket : m_buckets) {
            bucket.values.clear();
        }
    }

private:
    struct alignas(CommitBucketAlignment) Bucket
    {
        std::vector<T> values;
    };

    std::vector<Bucket> m_buckets;
};

} // namespace luaug::jobs
