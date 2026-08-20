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

    // The parallel windows exist in the enum from day one even though v1 runs
    // `ConnectParallel` handlers serially on the game VM, so that the
    // thread-safety checker has something real to name (architecture.md §3).
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
