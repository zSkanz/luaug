// The frame pipeline's vocabulary (architecture.md §3), in `core` because
// several modules name a phase without any of them owning the schedule:
// `app::FrameScheduler` runs them, `script` drains the deferred queue at each
// one, and the profiler labels its zones with them.
#pragma once

#include "luaug/core/types.h"

namespace luaug::core {

// Order of declaration is order of execution within a frame, and code relies on
// that: the scheduler walks the sim phases in enum order. Adding a phase in the
// middle is therefore a behaviour change, not a bookkeeping one.
//
// `PreRender` runs once per rendered frame at a variable dt; everything from
// `PreAnimation` to `Heartbeat` runs once per *sim tick* at the fixed dt.
// Callers routinely assume every phase shares one rate; api-design.md §2.1
// documents the split loudly for that reason.
enum class Phase : u8
{
    // Mutations that must not happen mid-tick land here: hot-reload batches,
    // streaming materialisation, origin rebase, asset-ready callbacks.
    FrameStart,

    PreRender,

    // **The parallel windows are reserved and NOTHING runs in them** (S6.3).
    //
    // They sit where architecture.md §3 puts the seams -- B after the PreRender
    // drain, A after the PreSimulation drain -- and the tick fires neither.
    // There is no `ConnectParallel` to connect to (`datatypes.api.luau` calls
    // it "a reserved name, absent in v1") and no thread-safety checker: the
    // `ThreadSafety` annotations reach `ClassRegistry` and the api-dump and are
    // read by nothing.
    //
    // That is a deliberate reservation and not an oversight, and the earlier
    // version of this comment overstated it -- it said the windows existed "so
    // that the thread-safety checker has something real to name", which named a
    // thing that has never been built. What they are actually for is the ORDER:
    // the seams are where a later milestone would run actor handlers, and
    // fixing their position now means a phase added between them later cannot
    // silently move one.
    //
    // `phase_tests.cpp` holds the positions, so the reservation is a fact the
    // build checks rather than a sentence somebody has to believe.
    ParallelWindowB,

    PreAnimation,
    PreSimulation,
    ParallelWindowA,
    PostSimulation,

    // Where `task.wait` and `task.delay` resume, between PostSimulation and
    // Heartbeat, on the SimClock. Anything they defer drains at Heartbeat.
    TaskResume,

    Heartbeat,

    Count,
};

// For logs and profiler zones only. Never i18n-formatted: these are engine
// internals, not user-facing text (R3).
[[nodiscard]] const char* phaseName(Phase phase) noexcept;

} // namespace luaug::core
