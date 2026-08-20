// The whole scheduling surface (api-design.md §3.2). There are no legacy
// globals: no `wait`, no `spawn`, no `delay`, no `tick` (divergence #2, R8).
//
// Two rules shape everything here:
//
//   1. **Timers run on the SimClock, in integer ticks.** `task.wait(1)` is
//      exactly 60 ticks at the default timestep, on every machine and every
//      run -- which is what makes a recorded replay reproduce (ADR 0025). A
//      deadline is a tick index, never a float compared against a float.
//
//   2. **The task-resume phase is one FIFO** ordered by `(deadline tick,
//      scheduling sequence)`, with `task.wait` resumptions and `task.delay`
//      callbacks interleaved. R10 forbids leaving that to container order, so
//      the order is maintained explicitly rather than inherited from a heap.
#pragma once

#include "luaug/core/types.h"
#include "luaug/script/binding.h"

#include <vector>

struct lua_State;

namespace luaug::script {

// A pending resumption: a coroutine parked by `task.wait` or a callback
// scheduled by `task.delay`. The two are one list because §3.2 says they
// interleave -- two things due on the same tick resume in the order they were
// scheduled, whichever call scheduled them.
struct TimerEntry
{
    u64 deadlineTick = 0;
    // Monotonic across the VM, so `(deadline, sequence)` is a total order and
    // ties never fall to whatever the container felt like.
    u64 sequence = 0;

    // The parked coroutine or the callback's thread, held by registry ref --
    // the only thing keeping either alive between now and the deadline.
    int threadRef = -1;

    // How many `task.delay` arguments are already sitting on the callback's own
    // coroutine, waiting to become its resume values. They live there rather
    // than in the deferred arena because the arena is drain-scoped and a timer
    // is not (see `taskDelay`).
    u32 argCount = 0;

    // `task.wait` resumes with the elapsed sim time and `task.delay` with the
    // caller's arguments, so the two differ at the resume rather than in the
    // list.
    bool wait = false;
    u64 scheduledTick = 0;
};

// Per-VM. Held by `VmContext` as a pointer and owned by `ScriptRuntime`.
class TaskScheduler
{
public:
    // Sorted by `(deadlineTick, sequence)` at insert, so resuming is walking
    // the front rather than searching. A heap would answer the same question
    // and would not keep the order visible in a debugger.
    std::vector<TimerEntry> timers;
    u64 nextSequence = 0;
};

// Installs the `task` library. Runs during boot with everything else.
void registerTasks(lua_State* L);

// Resumes everything due at or before `tick`, in `(deadline, sequence)` order.
// Between `PostSimulation` and `Heartbeat` (architecture.md §3); anything these
// resumptions defer drains at `Heartbeat`.
void resumeDueTimers(lua_State* L, u64 tick);

} // namespace luaug::script
