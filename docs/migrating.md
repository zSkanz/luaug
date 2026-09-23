# Coming from Roblox — The LuauG Migration Guide

You already know most of this engine. `game:GetService`, `Instance.new`,
`Parent`, `FindFirstChild`, `:Connect`, `task.wait`, `Vector3`, `CFrame`,
`--!strict` — all of it works the way you expect, and the parts that do not are
a short list rather than a new language.

This guide is that list, and the reason for each item. It is written for
somebody with existing habits and no patience: §1 is what you can stop thinking
about, §2 is what will bite you, §3 is a cookbook to copy from.

LuauG is a clean-room implementation (ADR 0020). It borrows the *shape* of an
API a lot of people already know and shares no code, no assets and no branding
with it. Nothing here is a compatibility layer, and a script written for the
other platform will not run unmodified — which is what the list below is for.

---

## 1. What transfers directly

Your mental model survives.

```lua
--!strict
local RunService = game:GetService("RunService")

local part = Instance.new("Part")
part.Name = "Spinner"
part.Size = Vector3.new(4, 1, 4)
part.Color = Color3.fromRGB(120, 168, 214)
part.Anchored = true
part.Parent = workspace

local spin = 0
RunService.Heartbeat:Connect(function(dt: number)
    spin += dt
    part.CFrame = CFrame.fromEuler(0, spin, 0) + Vector3.new(0, 3, 0)
end)
```

Everything in that snippet means what you think it means:

- **`game` and `GetService`** — the same service locator, the same singletons.
- **The Instance tree** — `Instance.new(class)`, `Parent`, `FindFirstChild`,
  `WaitForChild`, `GetChildren`, `Destroy`, `Clone`.
- **Attributes and tags** — `SetAttribute`/`GetAttribute`,
  `TagService:AddTag`/`GetTagged`.
- **Signals** — `:Connect`, `:Once`, `:Wait`, `:Disconnect`,
  `GetPropertyChangedSignal`.
- **`task`** — `spawn`, `defer`, `delay`, `wait`, `cancel`.
- **The datatypes** — `Vector3`, `Vector2`, `CFrame`, `Color3`, `UDim`, `UDim2`,
  with the arithmetic you expect.
- **Tweens** — `TweenService:Create`, the same easing styles and directions.
- **`--!strict`** — except it is not optional here (ADR 0018).

---

## 2. Familiar but different — read this twice

### Signals are deferred-only

There is no immediate mode and no setting that switches one on. Every handler
runs at the next drain point in the frame, never inside the call that fired it.

```lua
local folder = Instance.new("Folder")
folder.ChildAdded:Connect(function(child)
    print("added", child.Name)  -- runs at the next drain, not here
end)

local part = Instance.new("Part")
part.Parent = folder
print("parented")               -- prints FIRST, always
```

**Why:** one semantics instead of two. Re-entrancy bugs that depend on which
mode a place is in stop existing, and a deferred queue is what keeps the
scheduler parallel-ready (ADR 0015).

**What it changes for you:** code that assumed a handler had already run by the
next line has to read the state instead. `:Wait()` still yields until it fires.

### Two clocks, and the phases say which one they are on

| Phase | Clock | Use it for |
|---|---|---|
| `PreRender` | render rate, variable | camera-rate work; **never fires headless** |
| `PreAnimation` | fixed tick | work that has to precede the step |
| `PreSimulation` | fixed tick | forces, intent, anything the solver should see |
| `PostSimulation` | fixed tick | reading what the step produced |
| `Heartbeat` | fixed tick | gameplay — the default place for a script's loop |

`task.wait` resumes on the simulation clock. The fixed tick is 60 Hz.

**The trap, and everybody hits it once:** a headless run has no render clock, so
a camera or a gameplay rule driven from `PreRender` does not run at all when a
test drives your game. Put gameplay on `Heartbeat`.

### No ModuleScripts — real files

```lua
-- there
local Util = require(ReplicatedStorage:WaitForChild("Util"))

-- here
local Util = require("@shared/util")
```

A module is a `.luau` file under `src/shared/` and `require` takes a string.
**Why it is better:** the analyzer resolves it as you type — full types,
go-to-definition, no wait-then-require dance, and no load-order class of bug.

### Children are not members

```lua
-- there
local base = workspace.Baseplate

-- here
local base = workspace:FindFirstChild("Baseplate")
-- or, if it may not exist yet:
local base = workspace:WaitForChild("Baseplate")
```

**Why:** an `__index` fallback to children cannot be typed. Under a 100%-strict
surface every dotted child access would be `any`, and the typing story would
leak out through the most-used syntax in the language. Members and children live
in separate namespaces here, and the error names which one you missed.

### Units are metres

A stud is not a unit here. Gravity is 9.81 m/s², a character is about two metres
tall, and a jump speed that felt right in studs is roughly **four times too
big**. If your character clears the whole course in one leap, that is why.

### `Vector3` IS the Luau vector

```lua
local v = vector.create(1, 2, 3)   -- the primitive constructor
local w = Vector3.new(1, 2, 3)     -- the same value
print(v.x, v.y, v.z)               -- lowercase
```

Components are lowercase because it is the native `vector` primitive and that is
what the language calls them. It is f32, which is why `BasePart.CFrame` carries
the full-precision position for a large world — `part.Position` is a view of it.

### Streaming is a service

```lua
local StreamingService = game:GetService("StreamingService")
StreamingService.LoadRadius = 700
StreamingService.MinRadius = 320
StreamingService:AddFocus(character)
```

Not properties on `Workspace`. A streamed-out instance reparents to nil and the
handle you held still resolves — it is a husk, and reading it is legal.

### Graphics settings belong to the player, not to the scene

`Lighting` describes the world. Shadow resolution, render scale and the post
chain describe the machine, and they live in `luaug.toml`'s `[graphics]` table
and on the host's command line — never in a property a scene can write. A place
that shipped its author's GPU budget to every player is what that rule exists to
prevent (ADR 0044).

---

## 3. Habit-by-habit cookbook

### `wait()` → `task.wait()`

| there | here |
|---|---|
| `wait(1)` | `task.wait(1)` |
| `spawn(f)` | `task.spawn(f)` |
| `delay(1, f)` | `task.delay(1, f)` |
| `tick()` | `os.clock()` |

The globals are not deprecated here; they were never defined. Naming one is a
strict-mode error at analysis time, which is the point.

### UserInputService → the Input Action System

There is one input model and it is rebindable and promptable by default. You do
not read a key in gameplay code.

```lua
local Gameplay = Instance.new("InputContext")
Gameplay.Parent = workspace

local Move = Instance.new("InputAction")
Move.Name = "Move"
Move.Type = Enum.InputActionType.Direction2D
Move.Parent = Gameplay

local keys = Instance.new("InputBinding")
keys.Up = Enum.KeyCode.W
keys.Down = Enum.KeyCode.S
keys.Left = Enum.KeyCode.A
keys.Right = Enum.KeyCode.D
keys.DisplayName = "WASD"
keys.Parent = Move

local stick = Instance.new("InputBinding")
stick.KeyCode = Enum.KeyCode.LeftThumbstick
stick.Parent = Move

RunService.Heartbeat:Connect(function()
    local axis = Move:GetState() :: Vector2
    -- ...
end)
```

Rebinding is a property write: `keys.KeyCode = Enum.KeyCode.J`. Persisting it is
your game's job — an `InputBinding` is an ordinary Instance and a settings screen
serializes it like any other state.

### BindableEvent → `Signal.new()`

```lua
local died: Signal<string> = Signal.new()
died:Connect(function(who: string) end)
died:Fire("player")
```

`Signal` is a **global datatype**, not a module to require and not an Instance to
parent: an Instance was the wrong shape for a typed callback list.
`BindableFunction` is a plain function. Delivery is deferred, like every other
signal in the engine, and handler errors are contained one at a time.

### Humanoid → `CharacterBody`

```lua
local character = Instance.new("CharacterBody")
character.Size = vector.create(1.4, 3.2, 1.4)
character.WalkSpeed = 7         -- metres per second
character.JumpSpeed = 6
character.AutoStepHeight = 0.6  -- the ledge it walks over rather than into
character.Parent = workspace

character:Move(direction)       -- intent, once per tick
character:Jump()
print(character.Grounded, character.State)
```

One instance instead of a Humanoid plus a root part plus a rig, with a direct
Jolt character controller underneath. There is no `Health`, no `Died` and no
default animation set: those were a game's rules living in the engine.

### PlayerGui → `UIService`

```lua
local screen = Instance.new("ScreenGui")
screen.Parent = game:GetService("UIService")
```

The tree, the layouts and the properties are the ones you know, with three
renames: `BackgroundColor3` is `BackgroundColor`, `TextColor3` is `TextColor`,
and `TextXAlignment`/`TextYAlignment` are `HorizontalAlignment`/
`VerticalAlignment` — the same names the layout objects already use.

### `rbxassetid://` → `asset://`

```lua
mesh.MeshContent = "asset://models/tree.gltf" :: Content
```

A path into your own project, content-addressed at build time. No upload, no
moderation queue, no id to lose.

### DataStore → your own backend

There is no built-in persistence service. `@std/net.request` talks to whatever
backend you write, in any language, and `save://` is a local file path. v1 ships
primitives rather than a hosted product it cannot host.

### Studio → VS Code and `luaug dev`

```
luaug new my-game
cd my-game
luaug dev      # runs the game with a watcher; save a file and the world rebuilds
```

Hot reload is a fast world restart (ADR 0024), under 500 ms, and
`HotReloadService:SaveState` is how you carry what matters across it. The debug
overlay is the inspector: explorer, properties, stats and a console.

---

## 4. The deliberate divergences, in full

Maintained in [`api-design.md`](api-design.md) §2.5 and reproduced here. Every
row is a decision with a reason rather than an omission.

| # | Roblox | LuauG | Why |
|---|---|---|---|
| 1 | Immediate/Deferred signal modes | Deferred-only | One semantics; predictable; parallel-ready |
| 2 | `wait`/`spawn`/`delay`/`tick` globals | `task.*` + `os.clock` only | Kill the footguns at birth |
| 3 | UserInputService/ContextActionService/Mouse | Input Action System only | One modern input model, rebindable/promptable by default |
| 4 | `RBXScriptSignal`/`RBXScriptConnection` | `Signal<T...>` / `Connection` | Legal + cleaner; generic-typed |
| 5 | BindableEvent/BindableFunction | `Signal.new()` / plain functions | Instances were the wrong shape for this |
| 6 | ModuleScript instances + `require(instance)` | Plain `.luau` files + require-by-string | Real filesystem modules; no WaitForChild-require dance; analyzer parity |
| 7 | `Instance.new(class, parent)` | Single-arg only | The parent-then-mutate perf wart, removed |
| 8 | `.Changed` catch-all event | `GetPropertyChangedSignal` / `AttributeChanged` only | The untypeable catch-all, removed |
| 9 | `Vector3.X/Y/Z` | canonical `x/y/z` | It IS the native `vector` primitive; matches the vector stdlib RFC and the builtin type |
| 10 | `CFrame.Angles` + `fromOrientation` + `fromEulerAnglesXYZ` | One `CFrame.fromEuler(..., order?)` | Three confusing spellings → one explicit one |
| 11 | `BackgroundColor3`, `TextColor3` | `BackgroundColor`, `TextColor` | The "3" suffix is a historical artifact |
| 12 | BrickColor, Region3, borders (`BorderSizePixel`) | Absent | Legacy warts |
| 13 | Humanoid + HumanoidRootPart | `CharacterBody : BasePart` | One instance, a direct Jolt character controller |
| 14 | PhysicalProperties + material-derived physics | `Friction`/`Restitution`/`Density` props | Direct, typed, no bundle object in v1 |
| 15 | Studs | SI meters/kg/seconds | glTF-native, physics-native |
| 16 | CameraType state machine | Fully scriptable Camera + `@luaug/camera` rigs | Code-first engine; no hidden controllers; NearPlane/FarPlane exposed |
| 17 | MouseEnter/MouseLeave, InputBegan on GUI | `PointerEntered`/`PointerExited`, `Activated`, IAS `UIButton` bindings | Device-neutral |
| 18 | `workspace.StreamingEnabled` + props | `StreamingService` | Streaming is a system, not scene-root state |
| 19 | SoundService/SoundGroup | `AudioService`/`AudioGroup` | Consistent Audio* naming |
| 20 | `rbxassetid://` | `asset://` project paths (content-addressed) | Open, local-first pipeline |
| 21 | Deprecated camelCase aliases (`:connect`) | Never existed | Strict, single spelling |
| 22 | Optional typing | 100% `--!strict`, fully typed defs, new solver | Non-negotiable quality bar |
| 23 | TextXAlignment/TextYAlignment | `HorizontalAlignment`/`VerticalAlignment` (shared enums w/ layout) | One alignment vocabulary |
| 24 | RemoteEvent/RemoteFunction | v1: none; `@std/net` primitives; replication reserved | Honest scope; portable backends (ADR 0012) |
| 25 | A destroyed instance stays readable forever | Handles stop resolving at the end of the drain in which `Destroying` fired; using one raises `script.err.instance_dead` | The ECS reclaims the slot (architecture §4). Use-after-destroy becomes a keyed error instead of a silent read of a corpse |
| 26 | Dot-access to children (`workspace.Baseplate`, `folder.ChildName`) | `FindFirstChild("Baseplate")` — or `WaitForChild`, which is what the habit really wanted | An `__index` fallback to children is untypeable under a 100%-strict surface (ADR 0018): the analyzer cannot know what a child name resolves to, so every such access would be `any` and the typing story would leak out through the most-used syntax in the language. It is also the habit that made `WaitForChild` load-bearing in the first place — a name that resolves or errors depending on load order. Members and children now live in separate namespaces, and `scene.err.unknown_member` (§2.2) says which one you missed |
| 27 | `AnimationTrack.IsPlaying` | `AnimationTrack.Playing` | §9's own rule: a boolean PROPERTY carries no `Is` prefix and a boolean METHOD does. `Sound.Playing` was already spelled this way, and one engine cannot have both |

This rename list is **frozen**: no further renames without a new row there, and
no runtime aliases, ever.

---

## 5. Not in v1, honestly

Each of these has an owner in the roadmap's post-v1 phases rather than a shrug.

| Missing | Where it went |
|---|---|
| Visual editor | Post-v1 phase 1, built on the engine (ADR 0017) |
| Particles (`ParticleEmitter`) | Post-v1 phase 2 — the most visible gap of the group, and the one with no workaround |
| Decals | Post-v1 phase 2, and **projected into the world** rather than parented to a face |
| Terrain | Post-v1 phase 2 — Jolt has a height field; what is open is the authoring question |
| `SurfaceGui`, billboards | Post-v1 phase 2 — the UI tree exists; putting it in world space does not |
| Rich text | Post-v1 phase 2 — the glyph cache is already keyed for it |
| Navmesh pathfinding | Post-v1 phase 3, over the Recast seam already vendored (ADR 0022) |
| 2D workflow | Post-v1 phase 3 |
| Multiplayer, RemoteEvent, replication | Post-v1 phase 4 — designed, approved, and every seam it needs is open |
| Mobile | Post-v1 phase 5 |
| Constraints beyond `Weld` | Not scheduled; the transform weld is what v1 has |
| `BasePart.Material` | Not shipped in v1 |

And two things that are present but thinner than they look: a `Sound` plays a
generated tone rather than a file, and `Enum.CollisionFidelity` round-trips while
every value collides as a box. The engine says so itself — a property it stores
and does not read is marked `Inert` in the inspector and the api-dump, and a gate
stops a new one appearing quietly.

---

## 6. Glossary

| There | Here |
|---|---|
| Place file | A project directory: `luaug.toml`, `src/`, `content/` |
| ModuleScript | A `.luau` file, required by path |
| Model | A prefab: `Instance:Clone()`, or a `.prefab.luau` |
| Humanoid | `CharacterBody` |
| PlayerGui | `UIService` |
| RBXScriptSignal | `Signal<T...>` |
| Studio | VS Code plus `luaug dev` |
| Toolbox | Your own `content/` directory |
| Team Create | Git |
| Publishing | `luaug build` — a folder you can send to somebody |

---

## Where to go next

- [`api-design.md`](api-design.md) — the whole public surface, with the
  reasoning. It is long, and it is the reference.
- [`../examples/10-open-world/`](../examples/10-open-world/) — the flagship: a
  streamed world, a character, a day/night cycle and a HUD, in one readable file.
- [`../examples/04-obby/`](../examples/04-obby/) — a game with a menu, moving
  platforms, sounds and a finish flag.
- [`architecture.md`](architecture.md) — what is underneath, if you want to know
  why the frame looks the way it does.
