# The tick is fixed

The simulation runs at a fixed timestep — 1/60 of a second by default,
configurable from 1/240 to 1/30. Rendering runs at whatever rate the display
manages, and interpolates between the two most recent ticks.

One render frame runs zero or more simulation ticks.

## Why not just use frame time

Because "just multiply by `dt`" is only correct for the simplest integrations,
and stops being correct exactly where it matters:

- **Physics is not linear in `dt`.** A solver stepped at 8 ms and one stepped at
  33 ms do not converge to the same answer, and a stack of boxes that is stable
  on a fast machine sinks into the floor on a slow one.
- **A frame rate is a property of the machine**, so a variable timestep makes
  the *simulation* a property of the machine. Two players get different games.
- **Nothing replays.** A recorded input log is only meaningful against a known
  sequence of steps.

A fixed tick makes the simulation independent of how fast the picture is being
drawn. That is the whole idea, and everything else on this page follows from it.

## What it buys

**`task.wait(1)` is exactly 60 ticks**, on every machine and every run.
Deadlines are computed in integer ticks rather than by a float comparison over
seconds — which is not pedantry: at `dt = 1/60`, the expression `1 / (1/60)`
evaluates to `60.000000000000007`, and a naive ceiling gives 61.

**A phase handler receives the fixed `dt`**, identically, every tick. Code that
uses it is correct at 30 Hz and at 240 Hz without a branch.

**A world that reproduces**, which is the guarantee
[Determinism](manual:concepts/determinism) states precisely and which the engine
verifies by hashing the simulation and replaying recorded input.

**Rollback becomes possible later** rather than being a rewrite. Multiplayer is
a later phase, and it does not have to relitigate the foundation.

## What it costs

**Two clocks to keep straight.** A handler on `RunService.PreRender` runs at a
rate that depends on the machine; the other four phases do not. Writing
simulation state from a render-rate handler is a hole in the guarantee, and it
is the mistake this design makes easy to make and easy to name.

**A slow machine falls behind.** Up to four ticks run in one frame; past that
the accumulator is clamped and a warning logged, rather than letting the
simulation spiral trying to catch up. A machine that cannot keep up runs the
world in slow motion instead of exploding — which is the better failure.

**Interpolation is not free.** Rendering between ticks is what stops a 60 Hz
world looking stuttery on a 144 Hz display, and it means what you see is very
slightly behind what is simulated.

## Which phase to be in

That question has a real answer, and it is the practical form of this page:

| Doing | Phase |
|---|---|
| Applying a force or intent | `RunService.PreSimulation` |
| Reading where physics put something | `RunService.PostSimulation` |
| General per-tick gameplay | `RunService.Heartbeat` |
| Moving a camera | `RunService.PreRender`, or `Heartbeat` if it follows the world |

The last row has a subtlety worth the sentence: a camera that follows a
simulated thing belongs on `Heartbeat`, because a camera advancing every frame
against a world advancing every tick reads as the whole world vibrating.

## Changing it

```luau
--!strict
local PhysicsService = game:GetService("PhysicsService")
PhysicsService.FixedTimestep = 1 / 120
```

Values outside 1/240 to 1/30 are **refused**, not clamped. A write takes effect
at the next frame start rather than mid-tick — the accumulator, the timer wheel
and the solver all read it, and changing it between two of those reads inside
one frame is a class of bug worth designing out.

## Where to look next

- [The frame, phase by phase](manual:concepts/frame)
- [Determinism](manual:concepts/determinism)
- [task, and asking for time](manual:concepts/task)
