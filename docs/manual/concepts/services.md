# Services

A service is a singleton that hangs directly off `game`. There is exactly one of
each in a world, nothing creates or destroys one, and every one of them owns a
system rather than a thing in the scene.

```luau
local RunService = game:GetService("RunService")
local TagService = game:GetService("TagService")
```

`DataModel.GetService` returns the service, creating it on first request and
returning the same instance every time after. `DataModel.FindService` asks
without creating: it returns `nil` for a service that has not been requested
yet, which is what a script wants when it is checking whether something is in
use rather than reaching for it.

`Instance.new("RunService")` raises. So does `Destroy` on a service. Both are
the same rule seen twice: a service is not a thing a script owns.

## Acquire once, at the top

```luau
--!strict
local RunService = game:GetService("RunService")

RunService.Heartbeat:Connect(function(dt: number)
    -- ...
end)
```

`game:GetService` in a hot loop is a string lookup per call for a value that
never changes. Bind it to a local at file scope — that is the idiom in every
example in this repository, and it also makes the code read as a declaration of
what the file depends on.

`workspace` is the one exception with a shorter spelling, because it is reached
constantly. `workspace` and `game:GetService("Workspace")` are the same
instance.

## The thirteen

| Service | Owns |
|---|---|
| `Workspace` | The 3D scene, and the spatial queries against it. |
| `RunService` | The frame loop: the phase signals, and the clock they run on. |
| `Lighting` | The sun, the sky, the fog and the exposure. |
| `PhysicsService` | The simulation tick grid, and the controls that are not per-part. |
| `StreamingService` | The streamed world: its foci, chunks and budgets. |
| `TagService` | Finding instances by tag, and being told when a tagged one appears. |
| `TweenService` | Property animation. |
| `InputService` | The Input Action System, and device-wide state. |
| `UIService` | The parent of every `ScreenGui`, and what the screen is. |
| `AudioService` | Mixing, and the listener. |
| `ScriptService` | The mount point for entry scripts. |
| `DebugService` | The debug overlay, and the engine's own instrumentation. |
| `HotReloadService` | The hot-reload loop. Development builds only. |

`HotReloadService` carries the **Development builds only** badge in the
reference: it is compiled out of a shipping build, so a script that reaches for
it unconditionally in a shipped game gets nothing. `DebugService` is present in
both.

## What is deliberately not a service

There is no `Players`, no `DataStoreService` and no `ReplicatedStorage`. The
first two belong to a hosted platform this engine is not; the third is a
multiplayer concept, and v1 has no replication. Persistence is your own backend,
reached with `@std/net` — see [Talking to a backend](manual:guides/backend).

Streaming is a service rather than a flag on the scene root, and that is a
deliberate divergence: streaming is a system with foci, budgets and events, and
a boolean on the scene root is not enough of an API to configure one.

## Where to look next

- [The frame, phase by phase](manual:concepts/frame) — what `RunService`'s
  signals actually mean
- [The reference](site:reference) — every service, with every member
