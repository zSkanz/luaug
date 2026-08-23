# task, and asking for time

`task` is the whole scheduling surface. There is no `wait`, no `spawn`, no
`delay` and no `tick` global — they were removed rather than renamed, and
nothing aliases them.

| Call | What it does |
|---|---|
| `task.spawn` | Resumes a function on a new coroutine **immediately**, synchronously, before returning. |
| `task.defer` | Enqueues a function on the deferred queue; it runs at the next resumption point. |
| `task.delay` | Resumes a function after a duration of simulation time. |
| `task.wait` | Yields, and resumes after a duration of simulation time. |
| `task.cancel` | Cancels a pending resumption. |

All five return or take a `thread`. The three schedulers **create the coroutine
themselves and hand it back** — passing an existing thread raises. Resuming a
coroutine you already hold is `coroutine.resume`'s job, and keeping the two
surfaces apart is what guarantees every scheduled thread is one the scheduler
can account for.

## The clock is the simulation clock

`task.wait(1)` is exactly 60 ticks at the default 1/60 timestep. On every
machine. On every run.

That is not an approximation that happens to be close — deadlines are computed
in **integer ticks**, by converting the duration against
`PhysicsService.FixedTimestep` and adding to the current tick index. It has to
be integer arithmetic: at `dt = 1/60`, the expression `1 / (1/60)` evaluates to
`60.000000000000007`, so a naive ceiling would yield 61 ticks and quietly
contradict the guarantee.

`task.wait` returns the elapsed simulation time as `ticks × FixedTimestep` — the
product, not a running sum of per-tick values, so an exact multiple comes back
exact and a long wait carries no accumulated drift.

`RunService.SimTime` is how you read that clock. There is no `tick()` because
there is no wall clock in simulation code; see
[Determinism](manual:concepts/determinism).

## spawn is the odd one out

```luau
task.spawn(function()
    print("this prints before the next line")
end)
print("second")
```

`task.spawn` is the **one non-deferred call in the library**, and deliberately
so: it runs the function on the spot, on a fresh coroutine, while the caller's
stack is still live.

An error inside it is contained — it does not propagate to the caller, and
`task.spawn` returns normally either way.

Everything else in this library is deferred, which means it is ordered against
signal fires in the single queue described in
[Signals and connections](manual:concepts/signals).

## Zero means the next tick

```luau
task.wait(0)         -- one tick from now
task.delay(0, fn)    -- fn runs one tick from now
task.delay(-1, fn)   -- clamps to zero: also one tick from now
```

`task.wait` must yield, and `task.delay` must not run before it returns, so "at
or after 0 seconds" cannot be satisfied by the phase you are standing in. A
negative duration clamps rather than raising — it is what subtracting two
elapsed values across a tick boundary produces, and raising on it would be a
trap rather than a diagnostic.

## Two things due on the same tick

The task-resume phase is one FIFO ordered by `(deadline tick, scheduling
sequence)`. An earlier deadline resumes first; two things due on the same tick
resume in the order they were scheduled, whichever call scheduled them, with
`task.wait` resumptions and `task.delay` callbacks interleaved.

`SimTime` is constant across a whole tick, drains included — so a
`task.delay(d, fn)` scheduled from a `PreSimulation` handler and one scheduled
from `Heartbeat` on the same tick come due on the same tick.

## Cancelling

```luau
local pending = task.delay(5, respawn)
if playerLeft then
    task.cancel(pending)
end
```

`task.cancel` takes away a **pending resumption**, and a queued `task.defer`
entry counts as one. Three cases raise `script.err.task_not_scheduled` and they
are all the same case: a finished thread, an already-cancelled thread, and the
currently running thread. None of them holds a pending resumption, and a pending
resumption is the only thing `cancel` can take away.

## A loop, done right

```luau
--!strict
local RunService = game:GetService("RunService")

RunService.Heartbeat:Connect(function(dt: number)
    -- runs once per simulation tick, with a fixed dt
end)
```

Prefer a phase signal to a `while true do task.wait() end` loop. The signal says
*which* phase you want to run in, it carries the correct `dt` for that phase,
and it is disconnectable. The loop says none of those things and cannot be
stopped from outside.

## task is a global, not a module

`task` is on the global table. `require("@std/task")` does not resolve in the
game VM — there is one scheduler and it is reached by name.

`synchronize` and `desynchronize` are reserved names: they are **absent**, not
present-and-erroring.
