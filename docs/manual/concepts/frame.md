# The frame, phase by phase

LuauG runs **two clocks**, and knowing which one a piece of your code is on is
the difference between a game that reproduces and one that does not.

- The **SimClock** is a fixed tick: 1/60 of a second by default, configurable
  from 30 to 240 through `PhysicsService.FixedTimestep`, with a `u64` tick
  counter. All simulation lives inside it.
- The **RenderClock** is variable: one step per frame the display actually
  produced, with whatever `dt` that took.

One render frame runs **zero or more** simulation ticks. A fast machine renders
several frames between ticks; a slow one runs several ticks in one frame, up to
four, after which the accumulator is clamped and a warning is logged rather than
letting the simulation spiral.

Rendering interpolates between the two most recent ticks, which is why a world
simulated at 60 Hz looks smooth on a 144 Hz display.

## The five phases

`RunService` exposes five signals. Four are per tick and one is per frame, and
that split is the single most important thing on this page.

| Signal | Rate | `dt` | Use it for |
|---|---|---|---|
| `RunService.PreRender` | Per render frame | Variable | Camera work, anything that must be right for *this* image |
| `RunService.PreAnimation` | Per sim tick | Fixed | Posing, before animation is sampled |
| `RunService.PreSimulation` | Per sim tick | Fixed | Applying forces and intent, before physics steps |
| `RunService.PostSimulation` | Per sim tick | Fixed | Reading where physics put things |
| `RunService.Heartbeat` | Per sim tick | Fixed | General per-tick gameplay |

A handler on a per-tick phase receives the **fixed** timestep, every time,
whatever the frame rate. A handler on `PreRender` receives real elapsed time and
must not change simulation state — writing world state from a variable-rate
phase is how a game stops reproducing.

## What happens in a frame

```text
1. Frame start (a safe point)
     apply the hot-reload batch, materialize streamed chunks (budgeted),
     apply a pending origin rebase, run asset-ready callbacks
2. Pump platform events into the input queue
3. Dispatch render-rate input (UI and camera contexts)
4. PreRender          -> drain the deferred queue
5. while accumulated time >= fixed timestep, up to four times:
     SimTick:
       a. dispatch sim-rate input (gameplay actions)
       b. PreAnimation      -> drain, then sample animation clips, then step tweens
       c. PreSimulation     -> drain
       d. step physics, drain contacts, enqueue Touched
       e. scene systems: transforms, character update, welds, day/night
       f. PostSimulation    -> drain
       g. task-resume: task.wait and task.delay come due here
       h. Heartbeat         -> drain
6. Script GC step, within the frame budget
7. Extract the world for rendering, interpolated between ticks
8. Render, composite UI and the debug shell, present
9. Update audio, close the profiler frame
```

Every phase marked "drain" runs its engine work and then drains the deferred
queue to fixpoint. That is what a *resumption point* is, and it is where every
signal handler in your game actually runs.

## Where to put what

**Reading input →** it is already dispatched for you. Gameplay actions fire at
sim rate; UI and camera contexts fire at render rate. You choose which by which
`InputContext` an action lives in — see
[Actions, bindings and contexts](manual:input/actions).

**Pushing a body →** `PreSimulation`. It is the last phase before physics steps,
so a force applied there is applied to this tick.

**Reading where a body ended up →** `PostSimulation`. Reading in
`PreSimulation` gives you last tick's answer.

**Moving the camera →** `PreRender`. The camera is a property of the image, not
of the simulation, and moving it at tick rate makes it stutter against an
interpolated world.

**Everything else →** `Heartbeat`. It is the general per-tick phase and it is
the right default.

## Pausing

`RunService.Pause` stops the simulation clock; `RunService.Resume` starts it
again; `RunService.IsPaused` reports which. Rendering continues while paused, so
the world is still on screen and `PreRender` still fires — a paused world is a
still world, not a black one.

## Headless

`luaug test` and every gate run the **identical scheduler** minus the platform,
render and audio steps, with ticks driven as fast as possible. That is not a
simplified mode written for tests: it is the same code path, which is what makes
a headless determinism replay evidence about the real engine.
