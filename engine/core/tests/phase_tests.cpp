#include <doctest/doctest.h>

#include <algorithm>
// doctest prints a failing comparison through operator<<, and phaseName hands
// back a C string that the checks below compare as a string_view.
#include <ostream>
#include <string_view>
#include <vector>

#include "luaug/core/phase.h"

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

    for (u32 raw = 0; raw < static_cast<u32>(Phase::Count); ++raw)
    {
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
