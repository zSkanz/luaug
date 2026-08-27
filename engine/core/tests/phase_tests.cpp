#include <algorithm>
#include <doctest/doctest.h>
// doctest prints a failing comparison through operator<<, and phaseName hands
// back a C string that the checks below compare as a string_view.
#include "luaug/core/phase.h"

#include <ostream>
#include <string_view>
#include <vector>

using luaug::core::Phase;
using luaug::core::phaseName;
using luaug::core::u32;
using luaug::core::u8;
using luaug::core::usize;

TEST_CASE("every phase has a name, and no two share one")
{
    // These strings label profiler zones and log lines. A duplicate would not
    // fail anything -- it would merge two phases' costs in every capture taken
    // from then on, which is the kind of wrong number that gets acted on.
    std::vector<std::string_view> seen;

    for (u32 raw = 0; raw < static_cast<u32>(Phase::Count); ++raw) {
        const std::string_view name = phaseName(static_cast<Phase>(static_cast<u8>(raw)));

        CHECK_FALSE(name.empty());
        CHECK(std::find(seen.begin(), seen.end(), name) == seen.end());
        seen.push_back(name);
    }

    CHECK(seen.size() == static_cast<usize>(Phase::Count));
}

TEST_CASE("names match the enumerators they label")
{
    // Spot-checked rather than exhaustive: the point is that the string is the
    // enumerator's own spelling, so a zone in a capture greps back to the phase.
    CHECK(phaseName(Phase::FrameStart) == std::string_view{"FrameStart"});
    CHECK(phaseName(Phase::TaskResume) == std::string_view{"TaskResume"});
    CHECK(phaseName(Phase::Heartbeat) == std::string_view{"Heartbeat"});
}

TEST_CASE("Count and out-of-range values do not read as a phase")
{
    const std::string_view sentinel = phaseName(Phase::Count);
    CHECK_FALSE(sentinel.empty());

    for (u32 raw = 0; raw < static_cast<u32>(Phase::Count); ++raw)
        CHECK(phaseName(static_cast<Phase>(static_cast<u8>(raw))) != sentinel);

    // A value cast in from outside the enum -- a corrupt profiler record, a
    // stale serialized index -- has to read as broken, not as FrameStart.
    CHECK(phaseName(static_cast<Phase>(u8{200})) == sentinel);
}

TEST_CASE("declaration order is frame order")
{
    // phase.h is explicit that the scheduler walks the enum in order, so the
    // order is behaviour. Pinning it here means inserting a phase in the middle
    // fails a test instead of silently rescheduling the frame.
    CHECK(Phase::FrameStart < Phase::PreRender);
    CHECK(Phase::PreRender < Phase::ParallelWindowB);
    CHECK(Phase::ParallelWindowB < Phase::PreAnimation);
    CHECK(Phase::PreAnimation < Phase::PreSimulation);
    CHECK(Phase::PreSimulation < Phase::ParallelWindowA);
    CHECK(Phase::ParallelWindowA < Phase::PostSimulation);
    // task.wait and task.delay resume between PostSimulation and Heartbeat, so
    // anything they defer still drains in the same tick.
    CHECK(Phase::PostSimulation < Phase::TaskResume);
    CHECK(Phase::TaskResume < Phase::Heartbeat);
    CHECK(Phase::Heartbeat < Phase::Count);
}

// --- The reserved windows, and where they sit (S6.3) -------------------------
//
// **Nothing runs in either.** There is no `ConnectParallel` to connect to and
// no thread-safety checker; what they reserve is the ORDER, so that a phase
// added later cannot silently move a seam a future milestone will need.
//
// Held here rather than described in a comment because a comment is what the
// old one was, and it claimed both mechanisms already worked.

TEST_CASE("the parallel windows sit at the seams architecture.md names")
{
    // Window B after the PreRender drain, window A after the PreSimulation
    // drain. The arithmetic is the point: a phase inserted between `PreRender`
    // and `ParallelWindowB` moves the seam, and this is what notices.
    CHECK(static_cast<u8>(Phase::ParallelWindowB) == static_cast<u8>(Phase::PreRender) + 1);
    CHECK(static_cast<u8>(Phase::ParallelWindowA) == static_cast<u8>(Phase::PreSimulation) + 1);
}

TEST_CASE("the tick's order is the one the frame pipeline documents")
{
    // The whole sequence, in one place, so a reordering has to change a test
    // that says what the order is FOR rather than a number nobody reads.
    CHECK(static_cast<u8>(Phase::FrameStart) < static_cast<u8>(Phase::PreRender));
    CHECK(static_cast<u8>(Phase::PreRender) < static_cast<u8>(Phase::PreAnimation));
    CHECK(static_cast<u8>(Phase::PreAnimation) < static_cast<u8>(Phase::PreSimulation));
    CHECK(static_cast<u8>(Phase::PreSimulation) < static_cast<u8>(Phase::PostSimulation));
    // `task.wait` and `task.delay` resume between PostSimulation and Heartbeat,
    // on the SimClock -- which is what makes a wait deterministic.
    CHECK(static_cast<u8>(Phase::PostSimulation) < static_cast<u8>(Phase::TaskResume));
    CHECK(static_cast<u8>(Phase::TaskResume) < static_cast<u8>(Phase::Heartbeat));
    CHECK(static_cast<u8>(Phase::Heartbeat) < static_cast<u8>(Phase::Count));
}
