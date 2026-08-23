# The migration guide

Written for somebody with existing habits and no patience. Most of what you know
transfers; this page is the part that does not.

## What transfers, unchanged

The `Instance` tree. `game:GetService`. `Instance.new`, `Parent`, `Clone`,
`Destroy`, `FindFirstChild`, `WaitForChild`, `GetChildren`, `GetDescendants`,
`IsA`. Attributes. Tags. `Signal:Connect` and `:Disconnect`. `task.spawn`,
`task.defer`, `task.delay`, `task.wait`. `Vector3`, `CFrame`, `Color3`, `UDim2`.
`Enum`. `print` and `warn`. A `Part` with a `Size`, an `Anchored` and a `Color`.
`Touched`. `TweenService:Create`. A UI tree of frames and labels laid out in
`UDim2`.

If your muscle memory reaches for one of those, it is there and it works.

## Read these twice

### Signals are deferred-only

There is no immediate mode. A fire is enqueued and handlers run at the next
resumption point of the frame. One semantics rather than two, one queue, one
order — and a handler that yields never stalls the thing that fired.

Full contract: [Signals and connections](manual:concepts/signals). The reasoning:
[Signals are deferred-only](manual:why/deferred-signals).

### Two clocks, and the phase says which

`RunService.PreRender` is per render frame with a variable `dt`. `PreAnimation`,
`PreSimulation`, `PostSimulation` and `Heartbeat` are per **simulation tick**
with the fixed timestep. `task.wait` resumes on the simulation clock.

So `task.wait(1)` is exactly 60 ticks, everywhere, and a camera that follows the
world belongs on `Heartbeat` rather than `PreRender`.

### No ModuleScripts

Modules are plain files, required by string:

```luau
local Greeting = require("@shared/greeting")
```

No `require(instance)`, no instance to find first, no wait-then-require dance.
Real files the analyzer can follow.

### Children are not members

`workspace.Baseplate` raises. Use `FindFirstChild` or `WaitForChild`.

The reason is the typing story: an index that could resolve to a child is
untypeable under a fully strict surface. See
[Children are not members](manual:why/children-are-not-members).

### Units are metres

Not studs. Gravity is 9.81 m/s², a character is about two metres tall, and a
jump speed that felt right in studs is roughly **four times too large** here.

### Vector3 is the Luau vector

Components are lowercase — `v.x`, `v.y`, `v.z`. `v.Magnitude` and `v.Unit` keep
their capitals because those are the engine's. See
[Vector3 is the Luau vector](manual:why/vector).

### Streaming is a service

Not a property on the scene root. `StreamingService` has foci, radii, a budget
and events.

### Graphics settings are not a script's

A script cannot set shadow quality or render scale. Those belong to the machine
the game is being shown on. See
[Graphics settings belong to the player](manual:why/graphics-settings).

### A destroyed handle stops working

After the drain in which `Destroying` fired, every access to a destroyed
instance raises. It does not stay readable forever — the slot is reclaimed, and
a use-after-destroy is a keyed error rather than a silent read of a corpse.

## Habit by habit

### wait() → task.wait()

```luau
-- was
wait(1)
spawn(fn)
delay(2, fn)

-- now
task.wait(1)
task.spawn(fn)
task.delay(2, fn)
```

The globals do not exist. Neither does `tick()` — `RunService.SimTime` is the
clock, and `os.clock` is for profiling.

### UserInputService → the Input Action System

```luau
--!strict
local InputService = game:GetService("InputService")

local gameplay = Instance.new("InputContext")
gameplay.Parent = InputService

local jump = Instance.new("InputAction")
jump.Type = Enum.InputActionType.Bool
jump.Parent = gameplay

local key = Instance.new("InputBinding")
key.KeyCode = Enum.KeyCode.Space
key.Parent = jump

jump.Pressed:Connect(function()
    character:Jump()
end)
```

More setup than `UserInputService.InputBegan`, and it is rebindable, gamepad-
ready, promptable and replayable from the first line. The raw events still exist
for a debug key — see [Raw input](manual:input/raw).

### BindableEvent → Signal.new()

```luau
--!strict
local scored: Signal<number> = Signal.new()
scored:Connect(function(points: number) end)
scored:Fire(10)
```

A value, not an instance. Instances were the wrong shape for this.

### Humanoid → CharacterBody

One instance, not two. `WalkSpeed`, `JumpSpeed`, `Move`, `Jump`, `Grounded`,
`Landed`.

There is no `Health`, no `Died`, and no default animation set: those were a
game's rules living in the engine. `Jump` does not check `Grounded` either — a
guard is one line in your code, and putting it in the engine makes a double jump
impossible to write.

### PlayerGui → UIService

`ScreenGui.Parent = game:GetService("UIService")`. It takes the container's role
without taking a players service, because there is not one.

### rbxassetid:// → asset://

```luau
mesh.MeshContent = "asset://models/tree.glb"
```

A path relative to your project's content directory. Local files, an open
pipeline, and a build step you can run.

### DataStore → your own backend

There is no data store and no remote event. `net.request` is an HTTP client and
your backend is yours, in any language. See
[Talking to a backend](manual:guides/backend).

### Studio → an editor, and a terminal

`luaug edit` opens the visual editor: viewport, explorer, properties, content
browser, play/pause/step/stop, save, undo. `luaug dev` runs the project with hot
reload and no editor.

Scripts are files in your own editor, with a real language server.

## Where to look next

- [Every deliberate divergence](manual:roblox/divergences) — the full list
- [What is not here](manual:roblox/not-here) — the honest gaps
- [Your first world](manual:get-started/first-world)
