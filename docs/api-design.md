# LuauG v1 — Scripting API & Developer Experience

This is the authoritative design for everything a game developer touches. The
native core serving it is defined in [`architecture.md`](architecture.md).
Conformance test specs are written from THIS document (never from the
implementation, never from probing Roblox — rule R7). Divergence discovered
during implementation goes through an ADR + an edit here in the same commit.

---

## 1. Global environment & module model

### 1.1 The three-tier surface — "world = globals, libraries = modules"

| Tier | What lives there | Rationale |
|---|---|---|
| **Globals** | The world model: `game`, `workspace`, `script`, `Instance`, datatypes (`Vector2`, `Vector3`, `CFrame`, `Color3`, `UDim`, `UDim2`, `Rect`, `TweenInfo`, `RaycastParams`, `Random`, `Signal`), `Enum`, plus the Luau builtins listed in full below | Roblox muscle memory: you never require Vector3 |
| **`@std/…`** | The cross-runtime stdlib (Lute-compatible surface, §7): `@std/json`, `@std/net`, `@std/fs`, `@std/path`, `@std/task`, `@std/stringext`, `@std/tableext`, … This is the scope, not the state: §7 says which of them the game VM registers, and today it is one | The convergence bet (ADR 0030): utility code runs unchanged on Roblox/Lute/LuauG |
| **`@luaug/…`** | Engine-provided optional Luau libraries (not core world): `@luaug/camera` (third-person/orbit rigs), `@luaug/testing` (engine-aware test helpers) | Keeps the global surface small; optional things are opt-in |

**Two `@luaug/…` modules named in earlier drafts of this table are not shipped
and this is the record of why** (M8). `@luaug/signal` was to be a pure-Luau
Signal; `Signal` is a global datatype with a `new` constructor, so the module
would have been a second implementation of a type the engine already hands every
script, with its own semantics to keep in step. `@luaug/imgui` was to be
dev-only custom debug panels: the ImGui overlay exists and is the engine's
(ADR 0011), and what is missing is a Luau binding to it — a real feature with a
real surface, and one nothing in v1 calls. Both are additive to reinstate.

**The builtin globals, in full** — the list is exhaustive, and it ends without
an ellipsis on purpose: a name that is not on it, is not one of the world
globals or datatypes above, and is not removed below simply does not exist.
Functions: `assert`, `error`, `print`, `warn`, `pcall`, `xpcall`, `select`,
`next`, `pairs`, `ipairs`, `rawget`, `rawset`, `rawequal`, `rawlen`,
`getmetatable`, `setmetatable`, `tonumber`, `tostring`, `type`, `typeof`,
`unpack`, `require`, `gcinfo`, plus the `_VERSION` string. (`collectgarbage`
was listed here and is **not** a Luau global — `lbaselib.cpp` defines neither it
nor `warn`. `gcinfo` covers the same ground, and GC pacing is the engine's job
per architecture §3, not a script's. `warn` stays on the list because the engine
installs it; `collectgarbage` came off because nothing does.) Libraries: `task` (§3.2), `vector`,
`buffer`, `bit32`, `math`, `table`, `string`, `coroutine`, `utf8`, `debug`,
and `os` — which carries `os.clock`, `os.time` and `os.date` and nothing else,
so `os.difftime`, `os.getenv`, `os.remove` and the rest of the process-facing
surface are absent rather than stubbed.

**Removed globals (deliberate, no aliases):** `wait`, `spawn`, `delay`,
`tick`, `time`, `elapsedTime`, `loadstring`, `getfenv`/`setfenv`, `newproxy`,
`shared`; `_G` exists but is frozen-empty and lint-flagged — a write to it
**raises** (Luau frozen-table semantics), so it never gains a key and is not a
back channel between scripts. `io` does not exist in the game VM, headless
runs included.

Absence is a property of the VM, not something a script can observe: naming an
undeclared global is itself a strict-mode error (ADR 0018), so a conformance
spec cannot legally reference a removed name and a `:: any` cast around one
would test the cast. The removals are enforced by the generated definitions
(§5) at analysis time and by a C++ test that inspects the sandboxed global
table directly.

### 1.2 Service acquisition

`game:GetService("TweenService")` — lazy singleton creation, typed via
string-singleton overloads so the return type is exact. A general
`(name: string) → Instance` overload sits beside them for the non-literal
case; it costs the exact type, which is the honest trade rather than a reason
to forbid the call. An unknown name raises `scene.err.unknown_service`.
`game:FindService(name)` returns `Instance?` and creates nothing. Services are
children of `game` once created: `Workspace`, `ScriptService` and `Lighting`
exist from boot — the `workspace` global and the script mount point (§3) need
the first two, and the renderer reads the third every frame whether or not a
script ever asks for it — and every other service is created on its first
`GetService`. That third entry is a correction rather than a design: `Lighting`
was lazy through M4, the host cached its id before anything created it, and the
renderer spent the milestone lighting every scene with the struct defaults. A
service the engine itself reads on a schedule cannot be one that exists only if
someone asks. The `workspace`
global is the canonical handle for the `Workspace` service; no other service
gets a global. That is a fact about the generated definitions, so its
observable form is simply that every other service is reached through `game`
and is an ordinary child of it.

### 1.3 `.luaurc` for a generated user project

```json
{
    "languageMode": "strict",
    "lint": { "*": true },
    "lintErrors": true,
    "typeErrors": true,
    "aliases": {
        "shared": "src/shared",
        "pkg": "luau_packages",
        "std": "~/.lute/typedefs/1.0.0/std",
        "lute": "~/.lute/typedefs/1.0.0/lute"
    }
}
```

- **`std` and `lute` ARE `.luaurc` aliases**, and this section said they were
  not. The design was that `@std/…` and `@luaug/…` would resolve only through
  `luarequire_registermodule` at runtime and through `luau-lsp`
  `require.directoryAliases` in the editor, keeping the user `.luaurc` clean.
  What the template actually scaffolds points `std` and `lute` at the pinned
  Lute typedefs on disk, which is what makes a `@std` require analyze at all —
  the `.luaug/types/std/` stubs the `directoryAliases` name are §5's artifact
  (3), and nothing generates them.
- `@luaug/…` still resolves only at runtime, and only for the modules the engine
  mounts as content (`@luaug/camera`, `@luaug/testing`).
- `@self`, `./`, `../`, `init.luau` behave per the Luau require-by-string
  spec. pesde would append its own per-dependency aliases; nothing scaffolds a
  `pesde.toml` today (§4).

### 1.4 Engine version pinning as users see it

- `rokit.toml` pins the toolchain — reproducible by deliberate edit rather than
  by whatever was latest. The scaffolded one pins `lute`, `luau-lsp` and
  `stylua`; **it does not pin `luaug`**, because the CLI is not distributed
  through rokit and finds the engine binaries beside its own install (ADR 0054)
  rather than resolving a version.
- `luaug.toml` `[project] engine = "0.1"` records the engine series a project
  targets. **Nothing compares it to the runtime**, so the CLI warning this line
  promised does not exist, and neither does the `luaug setup` that was to
  regenerate `.luaug/types/` against it — `luaug new` writes those definitions
  once, at scaffold time (§4).
- At runtime: `game.EngineVersion: string` and `game.LuauVersion: string`
  (read-only).
- Type defs and the api-dump are versioned artifacts of each engine release.
  (The docs JSON this line also named was dropped in M3 — see §5's
  generated-artifact list.)

---

## 2. v1 API surface

### 2.1 Services (the complete list — 12 + 1 dev-only)

**`Workspace`** (global `workspace`) — 3D scene root.
- Props: `Gravity: vector` (SI, default `(0, -9.81, 0)`), `CurrentCamera: Camera`
- Methods: `Raycast(origin, direction, params?) → RaycastResult?`,
  `Spherecast(origin, radius, direction, params?)`,
  `GetBodiesInBox(cframe, size, params?) → {BasePart}`
- **The direction is not normalised: its length is how far the cast reaches.**
  `direction * 100` is a hundred-metre ray, and `RaycastResult.Distance` is
  therefore a distance rather than a fraction. A tie between two surfaces at the
  same distance resolves the same way on every run (R10) — a query whose answer
  depends on traversal order is a replay divergence waiting for a body count to
  change.

**`RunService`** — frame loop.
- Props: `SimTime: number` (read-only) — SimClock time in seconds at the
  current tick, constant for the whole tick and advancing by
  `PhysicsService.FixedTimestep` between ticks. This is the clock simulation
  code reads: R10 forbids the wall clock and there is no `tick()`, while
  `os.clock` is for profiling and never for gameplay.
- Events (in-frame order, all `(dt: number)`): `PreRender` (render-rate,
  variable dt), `PreAnimation`, `PreSimulation`, `PostSimulation`, `Heartbeat`
  (fixed-tick, per architecture §3 — this rate split is documented loudly).
  **`PreRender` never fires in a headless run** (`luaug test`, the
  determinism harness): headless mode is the same scheduler minus the render
  steps, and `PreRender` is one of them. It stays connectable so shared code
  need not branch on it, but a handler connected to it in a headless process
  is a handler that will not run.
- Methods: `Pause()`, `Resume()`, `IsPaused() → boolean` (world pause;
  render/debug keep running). Pausing stops the SimClock, so `SimTime` stops
  advancing and no `task.wait`/`task.delay` timer comes due while paused —
  which is why `SimTime` is readable at all: pause would otherwise only be
  observable by yielding, and yielding is exactly what a paused world does not
  let you finish. Both calls are idempotent: `Pause()` on a paused world and
  `Resume()` on a running one are no-ops, not errors.

**`InputService`** — host for the Input Action System (§2.4) + device state.
- Props: `PointerLocked: boolean`, `PointerVisible: boolean`,
  `LastInputDeviceType: Enum.InputDeviceType`
- Methods: `GetPointerPosition() → Vector2`, `IsKeyDown(keyCode) → boolean`,
  `SetVirtualState(keyCode, value)` — the second drives one of the `Virtual` key
  codes from something that is not hardware, a HUD button or an on-screen stick
- Events: `InputDeviceChanged(deviceType)`, `WindowFocusChanged(focused)`, plus
  `InputBegan`, `InputChanged` and `InputEnded`, each carrying an `InputObject`
  (`UserInputType`, `KeyCode`, `Position`, `Delta`) and a second argument saying
  whether the UI already consumed it
- **The raw events are ADR 0041 and they amend ADR 0029's "only input model"
  clause**, which §2.4's own heading still states. They are fed from the IAS's
  dispatch rather than from the OS, on the `Simulation` clock, after the UI has
  taken what it took — so a handler that writes to the world is replayable by
  construction, and a recorded stream still sees every input a game reads. The
  IAS stays the recommended path for a shipped game, because it is what a
  rebinding screen can enumerate and what binds a key and a gamepad button to one
  action; these are the direct, familiar, unrebindable option beside it. The
  `@luaug/input` sugar module M6 scoped was dropped in the same decision:
  `IsKeyDown` plus the events make the simple case cheap, and shipping both would
  be two answers to one question.

**`TweenService`** — `Create(instance, tweenInfo, goals: {[string]: any}) →
Tween`; `GetValue(alpha, easingStyle, easingDirection) → number`. Easing enum
set identical to Roblox's so tutorials transfer.

**`AudioService`** — mixing + listener. Props: `MasterVolume: number`.
Methods: `PlayLocal(content) → Sound` (fire-and-forget 2D). The listener is
`Workspace.CurrentCamera`. `AudioGroup` instances are the mixing buses.

**`UIService`** — parent of `ScreenGui` instances (the PlayerGui role) +
screen metrics: `SafeAreaInsets: Rect` (read), `DisplayScale: number` (read).

**`Lighting`** — day/night + environment. Props: `ClockTime: number` (0–24),
`GeographicLatitude`, `Ambient: Color3`, `Brightness: number`,
`FogColor: Color3`, `FogStart: number`, `FogEnd: number`,
`ExposureCompensation: number`, `SunDirection: vector` (read, derived). **The
`Sky` child class named here was never built**, and what stands in its place is
not a smaller version of it: the sky is analytic, derived from `ClockTime`,
`GeographicLatitude` and `FogColor`, and the renderer prefilters that same sky as
the environment the image-based lighting reads. There is no `SkyboxContent`, so
an HDRI or a cubemap cannot be supplied at all — which is right outdoors and
wrong underground, and is a gap rather than a design.

Three of those changed meaning at M7.5 when the renderer gained image-based
lighting, and the change is worth stating because a scene authored against the
old ones will look different:

- **`Ambient` is no longer the whole environment.** It was applied flat to both
  the diffuse and the specular lobe, which `pbr.hlsl` itself called "the
  degenerate case of the split-sum approximation where the environment is one
  colour". There is a real environment now — the sky, prefiltered by roughness —
  so `Ambient` is ADDED to the diffuse lobe only, and keeps its documented
  meaning: a stand-in for bounced light there is still none of.
- **`FogColor` still says what distance fades towards**, and the sky's horizon
  band still borrows it. What is derived on top is elevation: the whole sky
  scales towards black at night and the horizon warms as the sun drops.
- **`Brightness` is the sun's strength and no longer its colour.** The sun has a
  colour now, derived from its elevation rather than authored, so a low sun casts
  warm light where it used to cast white.

`ExposureCompensation` is in EV stops, on top of the exposure the renderer
measures from the frame itself: zero means whatever it measured, +1 is twice the
light and -1 is half. It is not clamped -- a scene that deliberately blows out is
making a picture rather than a mistake.

**`PhysicsService`** — the sim tick grid, plus physics controls beyond
per-part props.
- `FixedTimestep: number` (default 1/60) — the sim tick rate, and therefore
  the grid every timing guarantee in §3.2 is expressed against. It ships
  before any physics does, because it is a scheduler property that physics
  merely names: express durations as multiples of it and code stays correct at
  30 Hz or 240 Hz. **Writable from M5**, and a write takes effect at the next
  FrameStart safe point rather than mid-tick — the accumulator, the timer wheel
  and the solver all read it, and a value that changed between two of those
  reads inside one frame is a class of bug worth designing out. A read gives
  back what was last written, so the property round-trips immediately and takes
  effect one frame later. Values outside 1/240 to 1/30 are refused rather than
  clamped.
- Collision groups (M5): `RegisterCollisionGroup(name)` — idempotent, so a
  script may register at file scope and survive a hot reload;
  `CollisionGroupSetCollidable(a, b, collidable)` — symmetric, because a
  one-way collision is not something a solver can express;
  `GetRegisteredCollisionGroups()` — a fresh array in registration order,
  `Default` first. An unregistered name on `BasePart.CollisionGroup` is an
  error rather than a silent fallback: the failure mode of a typo is a wall
  players walk through.

**`StreamingService`** — StreamingEnabled-modeled, as a dedicated service
(streaming is a system, not scene-root state).
- Props: `Enabled: boolean`, `LoadRadius: number`, `MinRadius: number`,
  `PauseOutsideLoadedArea: boolean`
- **A radius pair per size class** (ADR 0053): `StructureMinRadius`,
  `StructureLoadRadius`, `TerrainMinRadius`, `TerrainLoadRadius`, all zero by
  default and meaning *follow the base pair*. `MinRadius` and `LoadRadius` are
  the DETAIL class, which is what makes a world authored before size classes
  existed behave exactly as it did. A mountain and a pebble stop sharing a
  distance, which is the choice one grid forces and always resolves badly in
  one direction.
- Methods: `AddFocus(instance)`, `RemoveFocus(instance)`,
  `LoadAreaAsync(position, radius)`
- Events: `AreaLoaded(position, radius)`, `InstanceStreamedOut(instance)`
  (streamed-out = reparent to nil, exactly the Roblox contract).
- Per-model control: `Model.StreamingMode: Enum.StreamingMode`
  (Nonatomic/Atomic/Persistent). The grid decides WHEN something becomes
  eligible and the model decides WHAT comes with it, and that is the whole of
  what a person says about a world they built by hand.
- **`PauseOutsideLoadedArea` is the authoritative world's.** A replica cannot
  pause a simulation it does not own; it holds its camera and shows that it is
  loading (ADR 0053, decided ahead of the multiplayer phase so that phase does
  not rediscover it).
- **A path into `Workspace` may be nil**, and `TagService` is the documented way
  around it. `GetInstanceAddedSignal` and `GetInstanceRemovedSignal` are exactly
  what fires as cells arrive and leave, and a streamed instance carries the tags
  it was authored with.

**`TagService`** — the CollectionService pattern with a clearer name:
`GetTagged(tag) → {Instance}`, `GetInstanceAddedSignal(tag) →
Signal<Instance>`, `GetInstanceRemovedSignal(tag) → Signal<Instance>`,
`GetAllTags()`. (Add/Has/Remove live on `Instance`.) `GetAllTags` is the set of
tags **currently carried by at least one instance**, not every name ever seen,
and it updates synchronously with `AddTag`/`RemoveTag` — a tag disappears the
moment its last carrier drops it, even though the signals that report the drop
are deferred like everything else. `GetTagged` returns a fresh array the
caller owns (§3.1).

**`DebugService`** — ImGui overlay + instrumentation (present in shipped
builds with the overlay off unless enabled).
- Overlay: `OverlayVisible: boolean`, `ShowPanel(name)`, `HidePanel(name)`
  (built-ins: "Stats", "Scene", "Log", "Streaming", "Physics")
- Gizmos (dev): `DrawLine(a, b, color?)`, `DrawBox(cframe, size, color?)`,
  `DrawSphere(position, radius, color?)` (per-frame). Headless there is no
  renderer and all three are **silent no-ops** — they must not raise, so debug
  drawing left in shared code cannot fail a headless test.
- Stats: `GetStat(name) → number` ("FPS", "FrameTimeMs", "PhysicsBodies",
  "InstanceCount", "DrawCalls", "LuaMemoryKB", …), `SetCustomStat(name, value)`.
  `GetStat` on an unregistered name and `ShowPanel`/`HidePanel` on an unknown
  panel raise `scene.err.unknown_stat`; `SetCustomStat` with a non-number
  value raises like any typed-argument mismatch. A misspelt stat is a bug in
  the caller, and a debug surface that returns 0 for it hides that bug in the
  one place people are already confused.
- Log capture: `MessageOut: Signal<string, Enum.LogLevel>`. `print(s)` and
  `warn(s)` each produce exactly **one** deferred fire carrying the text
  verbatim, at `Enum.LogLevel.Info` and `Enum.LogLevel.Warning` respectively.
  Engine messages arrive pre-formatted with their key in the §6 form
  (`[scene.err.parent_cycle] …`) — contained handler errors (§3.1) and the
  re-entrancy cap's dropped-fire log among them — so a handler matching on a
  key substring sees engine output and one matching prose sees script output.

**`ScriptService`** — the mount point for entry scripts (§3): every
`src/scripts/**/*.luau` file becomes a `Script` child of it, with
subdirectories as `Folder`s. No properties or methods of its own in v1; the
tree *is* the API.

**`HotReloadService`** — dev builds only (§3).

**`KeyboardService` existed for exactly one milestone and is gone.** M5 shipped
it as a `DevOnly` scaffold — direct keyboard polling — because that milestone
had a character somebody had to steer and the Input Action System did not land
until M6. The `DevOnly` tag was the point: it meant a shipping build never
contained it, so its removal would be structural rather than a promise. M6
removed it, and the class is absent from the IDL, from the generated
definitions, from the api-dump and from the binary. ADR 0029's "the only input
model" became a property of the code, and then ADR 0041 amended the clause
itself — `InputService` above carries a raw event surface, fed from the IAS's own
pipeline rather than from a second path into the OS.

The scaffold's one lasting consequence is a naming one, and it is worth knowing
if you read a recorded input stream from M5: the key names it used are the same
names `Enum.KeyCode`'s items carry, because `platform`, the recorded stream and
the enum are deliberately one spelling space. The digits changed spelling
(`"0"` became `Digit0`) when that space was unified.

**Three services this section declared were never built, and the count above is
the count that exists.** Saying so here is cheaper than a reader finding out at
`game:GetService`. `AssetService` was M7's Luau surface over the asset pipeline —
`LoadModelAsync`, `PreloadAsync`, `Exists` — and the pipeline shipped without it:
content reaches the world through a property that names it (`MeshPart.MeshContent`,
`Material.ColorMap`), through a stamp (§2.6) and through the streaming manager, so
nothing in v1 ever had to ask for a load. `LocalizationService` was §6's in-game
half; the catalog format, the key discipline and the engine-side formatter all
shipped, and nothing loads a *game's* catalog — §6 still describes the service and
that description is a design, not a report. `WindowService` had no caller either:
`[window] title` and `[window] size` in `luaug.toml` are read at boot (§4), and
nothing changes either at runtime. None of the three is in the IDL, the generated
definitions, the api-dump or the binary, so `game:GetService("AssetService")`
raises `scene.err.unknown_service` exactly as any other name that is not a service
does. All three are additive to build, and `Enum.WindowMode` went with the third
of them (§2.3).

**Reserved meanings, not implemented in v1** (do not squat them): the service
names `Players`, `NetworkService`, `ReplicationService` and
`NavigationService`, which name no class at all in v1; and `Enum.RunContext`,
which *is* declared and does carry `Client` and `Server` (§2.3) — the items
exist, nothing reads them, and a `Script` runs identically whatever its
`RunContext` says. Reserving a meaning is not the same as withholding a name:
these are reserved so that v1 code cannot come to mean something else by them
once the client/server split ships.

**Networking is not a service in v1**: `require("@std/net")` (§7) is the
network surface, so backend code is portable to Lute verbatim (ADR 0012). What
the game VM actually registers is one function — `request`, the HTTP client — and
§7's table says which of that module's other members are still design.

### 2.2 Instance class hierarchy

```
Instance (abstract)
├─ DataModel (game)
├─ <all services above>
├─ Folder
├─ BaseScript (abstract)       -- Source: string -- the Luau this instance carries. It is data
│  │                           -- on the instance rather than a path to a file, which is what
│  │                           -- makes a script something you can create, copy, put inside a
│  │                           -- stamp and save in a scene like anything else (ADR 0050)
│  ├─ Script                   -- Luau that RUNS: Enabled, and every enabled Script in the
│  │                           -- world starts on its own coroutine when the world does (§3)
│  └─ ModuleScript             -- Luau that is REQUIRED: require(module) evaluates it once and
│                              -- every later require of the same instance gives the same value
├─ PVInstance (abstract)       -- anything with a place in the world: PivotOffset: CFrame,
│  │                           -- GetPivot() -> CFrame, PivotTo(cf)
│  ├─ BasePart (abstract)      -- CFrame, Position, Orientation (degrees, YXZ), Size,
│  │  │                        -- Anchored, CanCollide, CanQuery, Transparency, Color,
│  │  │                        -- Material, CollisionGroup, Friction, Restitution, Density,
│  │  │                        -- LinearVelocity/AngularVelocity (read), ApplyImpulse(v),
│  │  │                        -- Touched/TouchEnded signals
│  │  │                        -- (`Material` was the one member of this list M5 did
│  │  │                        --  not ship, on the rule that a type-checked no-op
│  │  │                        --  looks more like a working API than a missing
│  │  │                        --  member does. It ships now, and as a `Material?`
│  │  │                        --  reference rather than the enum this line once
│  │  │                        --  meant: `Enum.Material` does not exist)
│  │  ├─ Part                  -- Shape: Enum.PartShape (Block/Ball/Cylinder/Capsule/Wedge)
│  │  ├─ MeshPart              -- MeshContent: Content, CollisionFidelity: Enum.CollisionFidelity
│  │  └─ CharacterBody         -- Jolt character controller (capsule): Move(direction: vector),
│  │                           -- Jump(), WalkSpeed, JumpSpeed, MaxSlopeAngle, AutoStepHeight,
│  │                           -- Grounded (read), State: Enum.CharacterState, Landed signal.
│  │                           -- Two characters BLOCK each other; neither pushes the
│  │                           -- other, and knockback is a game rule the game writes.
│  │                           -- CollisionGroup decides the pair, this one included
│  ├─ Model                    -- PrimaryPart, GetExtentsSize(), StreamingMode
│  └─ Camera                   -- CFrame, FieldOfView, NearPlane, FarPlane, ViewportSize (read),
│                              -- WorldToViewportPoint(), ViewportPointToRay(). No CameraType.
├─ Material                    -- a surface: Color, Transparency, ColorMap, NormalMap,
│                              -- MetallicRoughnessMap, Emissive/EmissiveMap, Metalness,
│                              -- Roughness, NormalScale, AlphaMode, AlphaCutoff, DoubleSided.
│                              -- Point a BasePart at one; BasePart.Color multiplies it, and
│                              -- what a project keeps in content/ is a STAMP of one (§2.6)
├─ Attachment                  -- CFrame (relative to parent BasePart), WorldCFrame (read)
│  └─ Bone                     -- a joint of a skinned mesh: JointName, JointIndex, Transform
├─ Constraint (abstract)       -- Attachment0, Attachment1, Enabled, CollideConnected
│  ├─ HingeConstraint          -- LimitsEnabled, LowerAngle, UpperAngle
│  ├─ BallSocketConstraint     -- LimitsEnabled, UpperAngle, TwistLimit
│  └─ FixedConstraint          -- no members of its own
├─ Ragdoll                     -- Enabled, and it owns nothing else: a ragdoll IS parts, Bones
│                              -- and constraints, each an instance you can see and move, and
│                              -- this is the flag that says to drive the pose from them
├─ Weld / WeldConstraint       -- rigid attachment (ships in M5): Part0, Part1, Enabled;
│                              -- Weld carries explicit C0/C1, WeldConstraint captures the
│                              -- relative transform when it becomes active. A TRANSFORM
│                              -- weld: the welded part is driven from its anchor and is
│                              -- not independently simulated, and the solver is not
│                              -- involved -- a CharacterBody is a CharacterVirtual rather
│                              -- than a Body, so no constraint could reach it anyway
├─ PointLight / SpotLight      -- child of BasePart/Attachment (the Roblox attach model),
│                              -- Color, Brightness, Range, Enabled, (Spot: Angle), and
│                              -- Shadows, which is stored, reported faithfully and acted on
│                              -- by nothing: the sun is this release's only caster, and it is
│                              -- the last property the IDL still marks `Inert` (§5)
├─ Sound                       -- Content, Playing, Looped, Volume, PlaybackSpeed, TimePosition,
│                              -- RollOffMinDistance/MaxDistance (3D iff parented to a BasePart),
│                              -- Play()/Pause()/Stop(), Ended/Loaded signals, Group: AudioGroup?
├─ AudioGroup                  -- mixing bus: Volume. A sibling of Sound and not a subclass of
│                              -- it: a Sound NAMES its bus through `Group`
├─ AnimationPlayer             -- under a Model/MeshPart with a skinned mesh (ships in M6):
│  │                           -- LoadAnimation(content) → AnimationTrack
│  └─ AnimationTrack (non-Instance handle) -- Play(fadeTime?), Stop(fadeTime?), Looped,
│                              -- Speed, Weight, Playing, Length, TimePosition, Ended signal
│                              -- v1 scope: glTF clip playback + linear blending; no state
│                              -- machines, no IK (roadmap M6)
├─ InputContext / InputAction / InputBinding   (§2.4)
├─ ScreenGui                   -- Enabled, DisplayOrder, ScreenInsets. The screen root a UI tree
│                              -- is parented UNDER; it extends Instance, not UIObject
├─ UIObject (abstract)         -- Position/Size: UDim2, AnchorPoint: Vector2, Rotation,
│  │                           -- BackgroundColor: Color3, BackgroundTransparency, Visible,
│  │                           -- ZIndex, LayoutOrder, AutomaticSize, ClipsDescendants,
│  │                           -- AbsolutePosition/AbsoluteSize (read),
│  │                           -- Activated, PointerEntered, PointerExited signals
│  ├─ Frame
│  ├─ TextLabel                -- Text, TextColor, TextSize, Font: Content,
│  │  │                        -- HorizontalAlignment, VerticalAlignment, TextWrapped, TextScaled
│  │  │                        -- (no RichText)
│  │  ├─ TextButton            -- a TextLabel that is clickable; no members of its own
│  │  └─ TextInput             -- PlaceholderText, Focused/FocusLost signals
│  ├─ ImageLabel               -- Image: Content, ImageColor, ScaleType, SliceCenter: Rect
│  │  └─ ImageButton           -- an ImageLabel that is clickable; no members of its own
│  └─ ScrollFrame              -- CanvasSize, CanvasPosition, ScrollBarThickness
└─ UI modifiers, each extending Instance and acting on the UIObject it is parented to:
   UIListLayout (FillDirection, Padding: UDim, HorizontalAlignment, VerticalAlignment,
   SortOrder, Wraps), UIPadding, UICorner
```

Layout is computed directly -- two passes over each dirty `ScreenGui` -- and no
solver is exposed or vendored. It was to have been Clay; ADR 0040 records why a
`UDim2` placement turned out to be arithmetic rather than a constraint problem.
**Not here (documented honestly):**
Terrain, ParticleEmitter, SurfaceGui/billboards, RichText and video. **The solver
joints came off this list.** It read "every constraint except the rigid weld —
no `HingeConstraint`, `SpringConstraint` or `Motor6D`, and no solver joint of any
kind", and `HingeConstraint`, `BallSocketConstraint` and `FixedConstraint` now
ship over `Attachment` pairs, and a `Ragdoll` is assembled from them rather than
owning bodies of its own. `SpringConstraint` and `Motor6D` are still absent.

**`Instance` base members:** `Name`, `Parent`; `ClassName` (read-only); tree:
`FindFirstChild`, `FindFirstChildOfClass`, `FindFirstChildWhichIsA`,
`FindFirstAncestor`, `FindFirstAncestorOfClass`, `GetChildren`,
`GetDescendants`, `WaitForChild(name, timeout?)`, `IsA`, `IsAncestorOf`,
`IsDescendantOf`, `Clone`, `Destroy`; attributes: `GetAttribute`,
`SetAttribute`, `GetAttributes`, `GetAttributeChangedSignal(name)`,
`AttributeChanged`; tags: `AddTag`, `RemoveTag`, `HasTag`, `GetTags`; signals:
`ChildAdded`, `ChildRemoved`, `DescendantAdded`, `DescendantRemoving`,
`AncestryChanged`, `Destroying`; `GetPropertyChangedSignal(name)`.
`Instance.new(className)` — single argument, typed per class name. There is
no `Changed` catch-all and no parent second argument. Sibling name
duplication is fully supported (`FindFirstChild` returns the first in child
order — ADR 0026). `typeof` of any instance is `"Instance"`, whatever the
class; the class is `ClassName` (§2.3).

**Child order is parenting order.** Children are ordered by the order in which
they were parented. A newly parented child goes last among its siblings;
re-parenting an existing child unlinks it from its old parent's list and
appends it to the new one's, so a re-parent is also a move to the end; and
assigning an instance's *current* parent again changes nothing at all — it is
not a reorder. There is no reorder API and no implicit sort: the tree's order
is exactly what the code that built it produced, which is what makes it
reproducible across runs (R10). **Document order** is that child order taken
depth-first, preorder — each child immediately followed by its own subtree.
`GetDescendants` returns document order, and it is the same order the whole
Find family tie-breaks on (ADR 0026).

**The Find family.** `FindFirstChild(name)`, `FindFirstChildOfClass(className)`
and `FindFirstChildWhichIsA(className)` search **direct children only**: there
is no `recursive` parameter in v1, and depth is a `GetDescendants` loop.
`OfClass` matches the exact `ClassName`, so `FindFirstChildOfClass("BasePart")`
never finds a `Part`; `WhichIsA` matches through `IsA`, so it does, and it
accepts abstract base names. When several children match, all three return the
first in document order. `FindFirstAncestor(name)` and
`FindFirstAncestorOfClass(className)` start at `Parent` and walk upward,
returning the nearest match; the instance itself is never a candidate. All five
return `Instance?` — not finding something is not an error. `IsA(className)` is
a **total** predicate: a string naming no class returns `false` rather than
raising, because its job is to test names you do not trust. `IsAncestorOf` and
`IsDescendantOf` read strictly — an instance is neither its own ancestor nor
its own descendant, so both return `false` for self.

**`WaitForChild(name, timeout?)`** parks the calling coroutine until a child of
that name **exists** under this instance, and returns it. The contract is about
the state, not about the event that produced it: a sibling *renamed* into the
awaited name satisfies a waiter exactly as a newly parented child does, and a
matching child already present returns immediately without yielding. With a
`timeout`, expiry returns `nil` — no error, no warning — and the timeout is
SimClock seconds like every other timer (§3.2), so it is a whole number of
ticks. Without one the wait is unbounded, and after 5 sim-seconds the engine
logs the familiar "infinite yield possible" warning
(`scene.warn.wait_for_child`, visible on `DebugService.MessageOut`) and keeps
waiting. The timeout form never warns, however long its timeout: you said how
long you were prepared to wait.

**Construction, read-only members, and what raises.** `Instance.new(className)`
takes one argument and returns an unparented instance whose `Name` is its
`ClassName` (`Instance.new("Folder").Name == "Folder"`); no v1 class declares a
different default name. An unknown class name raises `scene.err.unknown_class`;
a class tagged `Abstract`, `Service`, `NotCreatable` or `DevOnly` in the IDL
(§5) raises `scene.err.not_creatable` — which is what `Instance.new("BasePart")`
and `Instance.new("Workspace")` each get. `Instance.new("Script")` was the third
example on that line, on the rule that a script existed only because a file did;
ADR 0050 reversed it, and `Script` and `ModuleScript` are created from the same
call as everything else (§3). Passing a second
argument is a **type** error in the generated definitions, and that is where
divergence #7 is enforced; at runtime the extra argument is ignored, and in
particular the instance is not parented to it. Assigning a read-only member
such as `ClassName` raises `scene.err.read_only_property`. Assigning `Parent`
on a destroyed instance raises `scene.err.parent_locked` and leaves the tree
unchanged (§3.1). Reading or writing a member the class does not have raises
`scene.err.unknown_member`: a typo is an error, not a `nil`. That is what makes
divergence #8 testable — asking for `.Changed` does not quietly hand back
nothing, it says the member does not exist.

**What the signals carry.** `ChildAdded(child)`, `ChildRemoved(child)`,
`DescendantAdded(descendant)` and `DescendantRemoving(descendant)` each carry
the one instance. `AncestryChanged(instance, newParent)` carries the instance
whose ancestry changed and its new parent, `nil` when it was unparented.
`Destroying()` and the signal returned by `GetPropertyChangedSignal(name)`
carry **no** arguments — the new value is read off the instance, which keeps
the signal's type independent of the property's and keeps one signal correct
when a property's type changes. `AttributeChanged(name)` carries the attribute
name as a string, so `AttributeChanged:Wait()` returns that name. The order in
which one engine operation raises several of these is §3.1's.
`GetPropertyChangedSignal(name)` returns the *same* `Signal` object every time
for a given property on a given instance, so two calls yield two connections on
one signal; a name that is not a property of the class raises
`scene.err.unknown_property`. `GetAttributeChangedSignal(name)` accepts any
name, because an attribute that has never been set is a perfectly reasonable
thing to wait for.

**Attributes** are per-instance values independent of the class's properties.
An attribute may hold a `string`, `number`, `boolean`, `vector`, `CFrame` or
`Color3`, and `Vector2`, `UDim`, `UDim2` and `Rect` as those datatypes ship;
`SetAttribute` with a table, an `Instance` or a function raises
`scene.err.attribute_type` and leaves the attribute unset — a rejected write
does not clear a previous value. `SetAttribute(name, nil)` removes the
attribute. Attribute names, and tag names, are non-empty case-sensitive
strings: `"speed"` and `"Speed"` are two different attributes, and an empty
name raises `scene.err.invalid_name`. `GetAttributes` returns a fresh table the
caller owns.

**Tags** are pure instance state with no relationship to the tree: a
nil-parented instance carrying `"Checkpoint"` is still returned by
`TagService:GetTagged("Checkpoint")`. `AddTag` for a tag already present and
`RemoveTag` for one that is absent are no-ops. A non-string tag raises
`scene.err.invalid_name`, as does an empty one. `Destroy` removes every tag
from the instance and fires the corresponding `GetInstanceRemovedSignal(tag)`
deferred, exactly as it does `Destroying` — and the handle still resolves for
the duration of that drain (divergence #25), so a removal handler can still
read the corpse's `Name` before it goes. `GetTags` returns a fresh array.

**`Clone`** deep-copies the subtree — children, properties, attributes and tags
at every depth — and returns the copy unparented. The copies are independent:
setting an attribute or a tag on the clone does not touch the source.
Object-valued properties pointing *inside* the cloned subtree
(`Model.PrimaryPart` is the one that catches people) are rewired to the
corresponding clone at any depth; references pointing *outside* it are
preserved as they are and keep pointing at the original instance. See §2.6.

#### The pivot, and what `PivotOffset` is for

Everything with a place in the world derives from **`PVInstance`**, and it exists
so that "move this so it sits *there*" is one call with one meaning rather than
three. `BasePart`, `Model` and `Camera` are the three; `Attachment` is not one,
because its `CFrame` is relative to its parent and it is not positional in this
sense.

- **`PivotOffset: CFrame`** says where the pivot sits relative to the object.
  The identity puts it at the object's own centre, which is the default and the
  behaviour anything that ignores this property gets.
- **`GetPivot() -> CFrame`** is that pivot in world space.
- **`PivotTo(target)`** moves the object so its pivot lands on `target`. A
  `Model` moves every part beneath it by the same transform, so relative layout
  is preserved; a `BasePart` or a `Camera` moves itself.

**`PivotOffset` is the whole point of the API.** Without it, `PivotTo(cf)` is
`CFrame = cf` under a longer name and nothing can hinge: set the offset to a
door's hinge edge and `PivotTo` rotates the door about that edge.

A `Model` takes its pivot from its `PrimaryPart` — including that part's own
`PivotOffset`, so assigning a primary part means more than picking a position.
With no primary part it falls back to the centre of the box `GetExtentsSize`
reports, unrotated: a group of parts has no orientation to inherit, and an
identity fallback would move a model built far from the origin by its whole
distance the first time anything pivoted it.

**It is not a centre of mass.** When physics arrives it will have its own notion
of where a body turns about, and joining the two would make hinging a door
change how it falls.

### 2.3 Datatypes

| Type | Shape |
|---|---|
| `Vector3` | IS the native `vector` primitive (ADR 0013). Canonical fields **`x` `y` `z` (lowercase)**; the full official `vector` stdlib (`vector.create/magnitude/normalize/dot/cross/angle/lerp/floor/ceil/abs/sign/clamp/min/max`, `vector.zero/one`), with `vector.angle(a, b, axis?)` taking the RFC's optional third argument for a signed angle about that axis. `Vector3.new(x?, y?, z?)` — a missing component is 0. The `Vector3` global = the vector library + `new` (constant-folded via vectorCtor/vectorLib compile options): every library function is reachable under both names and `Vector3.zero == vector.zero`, but it is a superset table and no table identity is promised. Convenience metatable methods: `:Dot`, `:Cross`, `:Lerp`, `:Angle`; `.Magnitude`/`.Unit` via `__index` (documented as slower; prefer `vector.magnitude`). Operators are the primitive's own, and all of them: `+`, `-` (binary and unary), `*` and `/` with a scalar on either side or component-wise with another vector, `//`, `%`, and `==`. Precision: f32 — millimetre-exact to roughly ±8 km, degrading to ~16 mm resolution at ±131 km, where the exponent range ends (ADR 0013 addendum); `CFrame` is the f64 source of truth (below). |
| `Vector2` | Engine userdata: `X`, `Y`, `Magnitude`, `Unit`, `:Dot`, `:Lerp`; `Vector2.new(x, y)`, `.zero`, `.one`. |
| `CFrame` | Carries the **f64 translation** (the world-precision source of truth; ADR 0014). `CFrame.new(x?, y?, z?)`, `CFrame.new(position: vector)`, `CFrame.lookAt(pos, target, up?)`, `CFrame.fromEuler(rx, ry, rz, order: Enum.RotationOrder? = YXZ)` (the ONE euler constructor), `CFrame.fromAxisAngle(axis, angle)`, `CFrame.fromQuaternion(pos, qx, qy, qz, qw)`, `CFrame.fromMatrix(pos, r, u, b)`, `CFrame.identity`. Props: `Position` (Vector3 — the f32 rounding of the f64 translation, at every magnitude), `Rotation` (the same basis with a zero translation, so a pure translation's `Rotation` is the identity), `RightVector`, `UpVector`, `LookVector` (−Z). Methods: `Inverse`, `Lerp`, `Orthonormalize`, `ToWorldSpace`, `ToObjectSpace`, `PointToWorldSpace`, `PointToObjectSpace`, `VectorToWorldSpace`, `VectorToObjectSpace`, `ToEuler(order?)`, `ToAxisAngle`, `ToQuaternion` — semantics below the table. Operators: `CFrame * CFrame`, `CFrame * vector` (transforms a **point**: the translation is applied), and `==`, which is exact and component-wise, an identity test rather than a tolerance. Guideline: gameplay math at open-world scale — past a few kilometres from the origin — must go through CFrame (f64-preserving), not Position. |
| `Color3` | `new(r?, g?, b?)` — nominally 0–1, each channel defaulting to 0, and **not clamped**: values outside the range are legal and meaningful (HDR emissive, tint multipliers over 1), so clamping, where it is wanted, belongs to the consumer. `fromRGB(r, g, b)` (0–255), `fromHSV(h, s, v)` / `:ToHSV()` — all three components 0–1, so `fromHSV(1/3, 1, 1)` is green — `fromHex`; fields `R G B`; `:Lerp`, `:ToHSV`, `:ToHex`, and `==` (exact, component-wise). `:ToHex()` returns six lowercase hex digits with no leading `#` (`"ff8800"`); `fromHex` accepts `#rrggbb`, `rrggbb`, `#rgb` and `rgb`, case-insensitively, so the round trip is defined in both directions and neither call has to guess what the other meant. Anything else raises `script.err.color_hex_invalid`. No BrickColor. |
| `UDim` / `UDim2` | `UDim.new(scale, offset)` with fields `Scale`, `Offset`; `UDim2.new(xs, xo, ys, yo)`, `UDim2.fromScale`, `UDim2.fromOffset`; `X: UDim`, `Y: UDim`. |
| `Rect` | `Rect.new(min: Vector2, max: Vector2)`; `Min`, `Max`, `Width`, `Height`. |
| `TweenInfo` | `TweenInfo.new(time, easingStyle?, easingDirection?, repeatCount?, reverses?, delayTime?)` — enum params also accept string literals ("Quad") via typed unions. |
| `Signal<T...>` / `Connection` | THE signal types (never "RBXScriptSignal"). `Signal:Connect(fn) → Connection`, `:Once(fn)`, `:Wait() → T...`; `Connection:Disconnect()`, `.Connected`. `Disconnect` is idempotent: a second call is a no-op and `.Connected` stays `false`. Deferred-only (ADR 0015), ordering per §3.1. User-creatable: `Signal.new()` with `:Fire(...)`, `:Destroy()` — replaces BindableEvent/BindableFunction. `Signal.new()` is generic and its pack is inferred from the `Fire`/`Connect` sites; annotate it (`Signal<string>`, `Signal<()>`) where inference has nothing to work from, such as an array element type. `ConnectParallel` reserved, not in v1. |
| `RaycastParams` / `RaycastResult` | `RaycastParams.new { Filter = {Instance}, FilterType = Enum.RaycastFilterType.Exclude, CollisionGroup = "Default" }` (table constructor); result: `Instance`, `Position`, `Normal`, `Distance`. Both are read-only once built: a params object mutated between two casts is a question that means something different depending on when the engine looked at it. The filter covers a named instance's **descendants**, so filtering a `Model` filters its parts, and each word means what it says at the edges — an empty `Exclude` filter hits everything and an empty `Include` filter hits nothing. `CollisionGroup` is the empty string for "any group". **`RaycastResult` still carries no `Material`**, and the reason it was given here has since gone: `BasePart.Material` ships (§2.2) and this field did not arrive with it. The rule that kept it out stands on its own — a field reporting a value nothing sets is worse than one that is absent, and nothing sets this one. Note for `--!strict` callers: Luau table types are invariant, so `Filter = { part }` needs `:: { Instance }` — the annotation a `{Instance}` field costs. |
| `Random` | `Random.new(seed?)`: `NextNumber(min?, max?)`, `NextInteger(min, max)`, `NextUnitVector()`, `Clone()`. `NextNumber()` is [0, 1) and `NextNumber(min, max)` is [min, max) — half-open, like every other range in the engine; `NextInteger(min, max)` is inclusive at **both** ends, which is the one place the engine is not half-open and the reason it is spelled out. `min > max`, or a non-integer bound to `NextInteger`, raises `script.err.random_range`. `NextUnitVector` is uniform over the sphere, not merely unit length. The seed is any number, truncated toward zero. Deterministic streams (R10) — see the note below the table. |
| `Tween` | The handle `TweenService:Create` returns: `Play()`, `Pause()`, `Cancel()`; `Instance: Instance?`, `TweenInfo`, `PlaybackState: Enum.PlaybackState` (read), `Completed: Signal<Enum.PlaybackState>`. Not creatable on its own — there is no `Tween.new`, because a tween without the service that steps it would be a handle to nothing. |
| `AnimationTrack` | The handle `AnimationPlayer:LoadAnimation` returns (§2.2). |
| `InputObject` | The read-only snapshot `InputService`'s raw events carry (§2.1, ADR 0041): `UserInputType`, `KeyCode`, `Position`, `Delta`. A snapshot and not a live object, so holding one past its handler tells you what happened rather than what is happening. |
| `Content` | A type alias of `string` in v1 (`asset://…`, `save://…` URIs); reserved to become opaque later. It is a real exported type name, generated into `engine.d.luau` (§5), so `local c: Content = "asset://models/tree.glb"` type-checks — which is what makes the alias worth having before it becomes opaque. |
| `Enum` | Global `Enum` namespace; `EnumItem` = `Name`, `Value`, `EnumType` — and `EnumType` is the enum **object**, not its name as a string, so `Enum.PartShape.Ball.EnumType == Enum.PartShape`. `Enum.X:GetEnumItems()` returns a **fresh** array on every call, in declaration order (fresh so a caller may sort it; ordered because R10 forbids container order reaching observable order). The declared enums, in full: `EasingStyle` (Linear, Sine, Quad, Cubic, Quart, Quint, Exponential, Circular, Back, Bounce, Elastic), `EasingDirection`, `KeyCode` (keys + mouse + gamepad buttons), `InputActionType` (Bool, Direction1D, Direction2D, Direction3D, ViewportPosition), `InputDeviceType` (KeyboardMouse, Gamepad, Touch), `InputRate` (Simulation, Render — ADR 0039), `PartShape`, `CollisionFidelity` (Default, Hull, Box, Precise), `RotationOrder` (XYZ, XZY, YXZ, YZX, ZXY, ZYX — all six permutations; YXZ wherever an `order` parameter is omitted), `RaycastFilterType` (Include, Exclude), `StreamingMode` (Nonatomic, Atomic, Persistent), `PlaybackState`, `CharacterState` (Grounded, Airborne), `AutomaticSize`, `FillDirection`, `HorizontalAlignment`, `VerticalAlignment`, `SortOrder`, `ScaleType` (Stretch, Slice, Tile), `LogLevel` (Trace, Debug, Info, Warning, Error — ascending severity, and `Value` orders them), `RunContext` (Client, Server — declared and carrying both items in v1, but nothing reads them; §2.1), `AlphaMode` (Opaque, Mask, Blend — glTF's three, how a `Material` reads the alpha channel of its colour), `UserInputType` (the device an `InputObject` came from; §2.1). **Two names left this list rather than joining it**: `Material` is a class now and not an enum (§2.2), and `WindowMode` went with the `WindowService` that was never built (§2.1). |

**What `typeof` returns.** `typeof(Vector3.new(1, 2, 3))` is **`"vector"`** —
Vector3 *is* the VM primitive (divergence #9, §9), which is why every signature
in this document spells the type lowercase; there is no `"Vector3"` type name
to test against, and a type guard that looks for one never matches. Every
instance is `"Instance"` whatever its class — the class is `ClassName`. A
`Signal` is `"Signal"` and a `Connection` is `"Connection"`, never
`"RBXScriptSignal"`/`"RBXScriptConnection"` (divergence #4). An enum item is
`"EnumItem"`, an enum object such as `Enum.PartShape` is `"Enum"`, and the
`Enum` global itself is `"Enums"` — three names for three different things, and
the plural is the one people forget. The remaining datatypes answer with their
own names: `"Vector2"`, `"CFrame"`, `"Color3"`, `"UDim"`, `"UDim2"`, `"Rect"`,
`"TweenInfo"`, `"Tween"`, `"AnimationTrack"`, `"InputObject"`,
`"RaycastParams"`, `"RaycastResult"`, `"Random"`. `Content` is a `string` in v1
and answers `"string"`.

Reading a member a datatype does not have raises
`script.err.unknown_member`, exactly as on an instance (§2.2): `c.r` on a
`Color3` and `cf.position` on a `CFrame` are errors, not `nil`. The renames in
§2.5 are frozen, and this is what makes them enforceable — the old spelling does
not quietly return nothing, it says so.

**The convenience metatable members are runtime-only.** `.Magnitude`, `.Unit`,
`:Dot`, `:Cross`, `:Lerp` and `:Angle` work, and the analyzer cannot see them:
`vector` is a Luau *builtin* type, and a definitions file cannot augment one —
`declare extern type vector with …` is accepted and then loses to the builtin
(U-54). So `--!strict` code reaches them only through a cast, which makes the
`vector.*` library form the one to write — and §2.3 already recommends it for
speed. The members stay because they work and because code ported from another
Luau runtime expects them; they are documented here as the slower *and*
untypeable form rather than quietly dropped.

**The one exception is a vector's own components, and it is not ours to make.**
`v.X`, `v.Y` and `v.Z` return the same numbers as `v.x`, `v.y` and `v.z`,
because the interpreter answers a single-character index on a vector *inside
`LOP_GETTABLEKS`*, case-insensitively, before any metatable is consulted
(`VM/src/lvmexecute.cpp:619-635` at the 0.734 pin). No metatable the engine
installs can be reached for those six names, so the rule stated here until
2026-08-20 — that `v.X` raises — described something the VM does not permit.
Lowercase is still the canonical spelling and the only one the type definitions
declare, so `v.X` remains a type error under `--!strict`; it is a runtime error
in one place fewer than the rest of this section. Every other name on a vector,
`v.Nope` and `v.XY` included, raises through the engine's metatable as
documented.

**Handedness and rotation conventions.** The world is **right-handed, Y-up,
−Z forward** — glTF's convention, which is why the importer needs no axis
conversion (divergence #15) and why every rotation sign in this section is what
it is. `LookVector` is the basis's −Z column, `UpVector` its +Y and
`RightVector` its +X, so `RightVector:Cross(UpVector) == -LookVector`.
Rotations follow the right-hand rule about their axis, and
`CFrame.fromEuler(rx, ry, rz, order)` applies them as **intrinsic** rotations
in the named order — each about the axes the previous one produced. A +π/2
rotation about Y therefore takes `LookVector` from `(0, 0, -1)` to
`(-1, 0, 0)`. `Enum.RotationOrder` declares all six permutations and `YXZ` is
the default wherever an `order` parameter is omitted, `ToEuler(order?)`
included. `ToEuler` returns `(rx, ry, rz)` in `fromEuler`'s parameter order;
`ToAxisAngle` returns `(axis: vector, angle: number)` with a unit axis;
`ToQuaternion` returns `(qx, qy, qz, qw)` — w last, matching
`fromQuaternion`'s tail. Each is the inverse of its constructor.

**The six space-conversion methods, algebraically.** `self` is the frame the
conversion is relative to; these are definitions, not descriptions, and an
implementation that disagrees with a right-hand side disagrees with the
document:

```
self:ToWorldSpace(cf)        == self * cf
self:ToObjectSpace(cf)       == self:Inverse() * cf
self:PointToWorldSpace(v)    == self * v
self:PointToObjectSpace(v)   == self:Inverse() * v
self:VectorToWorldSpace(v)   == self.Rotation * v
self:VectorToObjectSpace(v)  == self.Rotation:Inverse() * v
```

The `Point*` pair moves positions and so applies the translation; the `Vector*`
pair moves directions and applies rotation only. That is the whole distinction,
and it is why a surface normal run through `PointToWorldSpace` comes out wrong
by exactly the frame's position.

**`fromMatrix`, `Orthonormalize`, `Lerp`, `lookAt`.**
`CFrame.fromMatrix(pos, r, u, b)` takes the right, up and **back** axes — back,
not forward, so `LookVector == -b` — and accepts axes that are neither unit
length nor mutually perpendicular: it stores what it is given, because a
constructor that silently repaired its input would hide the bug that produced
it. `Orthonormalize` is the explicit repair. It preserves the translation and
treats `LookVector` as authoritative: the look direction is normalised,
`UpVector` is made perpendicular to it and normalised, and `RightVector` is
whatever their cross product requires. `Lerp(goal, alpha)` interpolates the
translation linearly and **slerps** the rotation, so the halfway frame sits at
an equal angle from each end; `alpha` is not clamped, and values outside [0, 1]
extrapolate. `CFrame.lookAt(pos, target, up?)` defaults `up` to `(0, 1, 0)`;
where the direction from `pos` to `target` is degenerate (zero length) the result
is the **identity rotation at `pos`** rather than
a frame full of NaN — a camera pointed at itself should stop moving, not poison
every value it touches for the rest of the run. **An `up` parallel to the
direction is NOT that case** (D144): the direction is perfectly well defined and
only the roll about it is undetermined, so a roll is chosen — `(0, 0, 1)`, or
`(1, 0, 0)` where the direction is itself mostly Z — and the direction is
honoured. This used to answer with the identity too, which threw away the part
the caller had asked for and aimed every spotlight, camera and turret pointed
straight up or straight down along −Z instead.

**`Random` and R10.** A seeded generator is reproducible: the same seed
produces the same stream for the same engine build on the same platform, which
is the level-B guarantee recorded replays rest on (ADR 0025 — replays store
seeds, not draws). `Random.new()` with no seed draws a non-deterministic seed
from the host and is therefore **not legal in simulation code**: it is exactly
the unseeded RNG R10 forbids, and `luaug check` flags it. Use it for cosmetics
that never reach the world hash, or seed it from something the simulation
already knows. `Clone()` copies the stream position, so the clone continues the
same sequence independently of the original.

### 2.4 Input Action System (ADR 0029; the raw surface beside it is ADR 0041, §2.1)

- `InputContext : Instance` — `Enabled: boolean`, `Priority: number`,
  `Sink: boolean`, `Rate: Enum.InputRate`; children are InputActions. Parented
  anywhere (convention: under the Script that owns it).
- `InputAction : Instance` — `Type: Enum.InputActionType`,
  `Enabled: boolean`; `GetState() → boolean | number | Vector2 | vector`;
  signals `Pressed`, `Released` (Bool), `StateChanged` (all types).
- `InputBinding : Instance` (child of an InputAction) —
  `KeyCode: Enum.KeyCode`, composites `Up/Down/Left/Right: Enum.KeyCode`
  (Direction1D and Direction2D), `Scale: number`, `DisplayName: string` (a
  localization key is allowed), `Image: Content`, `DeviceType:
  Enum.InputDeviceType` (read-only, derived from `KeyCode`).
- **`UIButton` was declared here and never built.** It was to bind an action to
  an on-screen `ImageButton` or `TextButton` directly. What ships instead is
  `InputService:SetVirtualState(keyCode, value)` against one of the `Virtual`
  key codes (§2.1): the button's own `Activated` handler drives the state, and
  the binding stays an ordinary `KeyCode` binding rather than a second kind of
  binding with a second resolution rule. `examples/04-obby`'s HUD jump button is
  that path's proving caller.
- `InputAction:GetPreferredBinding(deviceType?) → InputBinding?` for prompt
  glyphs ("Press [E]").

**`Priority` orders fallthrough; `Rate` picks the clock, and they are different
questions** (ADR 0039). A `Simulation` context — the default — is dispatched on
the sim tick, where R10 holds and the input replay can see it; a `Render` one is
dispatched per rendered frame, for camera look and UI, and is *not* part of the
recorded input stream, because a render frame is not a unit the replay has. A
gameplay decision taken from a `Render` action is frame-rate-dependent by
construction. Deriving the rate from `Priority` instead would make a
low-priority render-rate context inexpressible and would let somebody change an
action's determinism class by re-tuning a number about layering.

**The enum-typed members are total rather than optional**, also ADR 0039. The
engine's property value domain is a closed set in which only an `Instance` can
be nil, so `Enum.KeyCode` carries an explicit `Unknown` item — that is what
"unbound" means — and `DeviceType` is derived from `KeyCode` rather than stored,
so a binding cannot claim to be a gamepad binding for the `W` key. Every
member `InputBinding` actually carries is total; the one optional member the
rule was stated against was `UIButton`, which is not there.

**`StateChanged` carries no arguments**, like `Destroying` and the signal from
`GetPropertyChangedSignal`, and the handler reads `GetState()`. Same reason as
in §2.2: it keeps the signal's type independent of the value's — and here also
because the queue between the engine and the VM carries POD facts, so an
argument would have to be rebuilt at drain time rather than captured at fire
time (§3.1). Input dispatches once per tick, so the value the handler reads is
the value that caused the fire.

**Runtime rebinding is a property write** — `binding.KeyCode = Enum.KeyCode.J`
— and persistence is the game's: an `InputBinding` is an ordinary Instance and a
settings screen serializes it like any other state. The engine ships no
save/load pair for bindings in v1.

### 2.5 Deliberate divergences vs Roblox (the migration guide reuses this table)

| # | Roblox | LuauG | Why |
|---|---|---|---|
| 1 | Immediate/Deferred signal modes | Deferred-only | One semantics; predictable; parallel-ready |
| 2 | `wait`/`spawn`/`delay`/`tick` globals | `task.*` + `os.clock` only | Kill the footguns at birth |
| 3 | UserInputService/ContextActionService/Mouse | The Input Action System, plus `InputService`'s raw events beside it | One modern input model, rebindable and promptable by default, and it stays the recommended path for a shipped game. **"Only" stopped being true at ADR 0041**: `InputBegan`/`InputChanged`/`InputEnded` and `IsKeyDown` exist because reading one key cost a context, an action, a binding and the parenting between them. They are fed from the IAS's own dispatch rather than from a second path into the OS, which is what keeps a replay able to see every input a game reads (§2.1) |
| 4 | `RBXScriptSignal`/`RBXScriptConnection` | `Signal<T...>` / `Connection` | Legal + cleaner; generic-typed |
| 5 | BindableEvent/BindableFunction | `Signal.new()` / plain functions | Instances were the wrong shape for this |
| 6 | ModuleScript instances + `require(instance)` | Both: `.luau` files by string, and `ModuleScript` instances by reference | **Reversed by ADR 0050**, and the row stays because half of what it argued for is still true — a file module is a real filesystem module with analyzer parity, and `src/shared/` is still exactly that. What the divergence cost was everything a script could not be while its identity was a file: it could not go inside a stamp, could not be copied with the thing it belongs to, and could not be created the way every other instance is. So `Source` became a property, `ModuleScript` came back, and `require` accepts one. Only a `ModuleScript` — requiring a `Script` would run it a second time somewhere else, which is precisely what the two classes exist to keep apart |
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
| 17 | MouseEnter/MouseLeave, InputBegan on GUI | `PointerEntered`/`PointerExited`, `Activated`, and a button that drives an action through `InputService:SetVirtualState` | Device-neutral. The `UIButton` binding this row named was never built (§2.4); the virtual key code is what took its place |
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

This rename list is **frozen**: no further renames without a new row here, and
no runtime aliases, ever.

### 2.6 Stamps — reusable pre-built instance trees

LuauG supports the Unity-prefab / Roblox-model workflow — authoring an
instance tree once (with children, properties, attributes, tags) and
instantiating it many times. **The thing is called a `Stamp`, and the name was
chosen against the obvious ones** (ADR 0049): `Prefab` is Unity's, `Blueprint`
is Unreal's, `Model` is Roblox's and an instanced `Scene` is Godot's, and
borrowing one imports a mental model this engine has not agreed to — somebody
who reads "prefab" goes looking for nested variants and an override list that
grows forever. A stamp is also a noun and a verb: you stamp one into the world,
the instance is stamped, and what it came from is its stamp.

1. **`Instance:Clone()`** (M2): any instance tree can be kept as a template
   (parented to `nil` or a storage Folder) and deep-cloned on demand, with
   internal references fixed up. This is the classic Roblox pattern and works
   for trees built in code or loaded from assets. A clone knows nothing about
   where it came from, which is the whole difference from the next one.
2. **`Instance.stamp(name, linked?)`** — a source file under
   `content/stamps/<name>.stamp.json`, placed into the world. **A stamp file is
   a scene of one subtree**: the same writer, the same reader and the same four
   correctness rules `.scene.json` has, over a root that is one instance instead
   of `Workspace`. Writing a second format would have meant two definitions of
   "everything about a subtree", and they would disagree the first time somebody
   added a property. Placing one is **two verbs**, and the second argument picks
   between them: **linked** — the default — writes the instance as its mark plus
   what differs, so everything it does not override comes from the file on every
   load and editing the file changes every instance with it; **a copy**
   (`linked = false`) is made by the stamp and owes it nothing afterwards.

**A property write on a linked instance is an override** (ADR 0051, which
reverses ADR 0049's break-on-edit rule). It stays on that instance, is recorded
under the path inside the stamp it applies to, and survives the source changing.
A *structural* change is not an override and is not recorded as one: adding or
removing a child is a different thing from setting a parameter, and a format that
carried it would be inventing added-and-removed-object machinery nobody has
designed here. So the save writes such an instance in full, drops its mark, and
counts it — nothing is lost, the world is exactly what it was, and the instance
is its own from then on. The alternative was a save that refuses, and a save that
refuses is a save that loses work.

**The `.prefab.luau` authoring format this section specified was never built**,
and neither was the `PrefabDef` asset the pipeline was to compile it into, nor
the `AssetService` that would have instantiated one (§2.1). What replaced them is
not a second format but the absence of one: a stamp is a scene, a scene is
`.scene.json`, and the editor writes both. **The general scene serialization
deferred below as risk §10.6 is therefore the thing that shipped** — ADR 0047
made a project's authored world a file, and boot loads it before the scripts run
(§3).

---

## 3. Script execution model (v1)

**One process, one game VM** (architecture §5). Model: **a script is an ordinary
instance carrying its own source** (ADR 0050), reached either by mounting a file
or by being in the world the scene describes; no client/server folders yet.

- **A script's source is a property.** `BaseScript` is abstract and carries
  `Source: string`; `Script` **runs** and `ModuleScript` is **required**, and
  both are creatable from the same `Instance.new` every other class uses.
  This section said the opposite until ADR 0050 — a `Script` was `NotCreatable`
  because it existed only where a file did — and the reason that rule went is
  worth keeping: a script whose identity is a file cannot go inside a stamp,
  cannot be copied with the thing it belongs to, cannot live in a content
  library, and cannot be created the way everything else is.
- **Mounting:** at boot, every `src/scripts/**/*.luau` file becomes a `Script`
  instance under `game:GetService("ScriptService")` (subdirectories become
  `Folder`s), with the file's text as its `Source`. Scripts have
  `Enabled: boolean` and `RunContext` (reserved enum; unset in v1). Files under
  `src/shared/` are still required by string and still never appear in the tree.
  **The mount is no longer the only way to have a script**: one saved in a scene
  or placed from a stamp is the same instance the mount would have made, and
  neither knows about the other.
- **`require` accepts an instance**, and only a `ModuleScript`: requiring a
  `Script` would run it a second time somewhere else, which is exactly what the
  two classes exist to keep apart. The cache is keyed by the instance and has the
  same three states a path-keyed module has — a value, a failure that is
  re-raised rather than re-run, and "being evaluated right now", which is a
  cycle.
- **`Script.Enabled` decides whether that script's threads are resumed, and
  nothing else** (ADR 0059). False when scripts start means this one never
  starts; it is still mounted and still in the tree, and no coroutine is created
  for it. Writing it afterwards takes effect at the next deferred drain — this
  section said a write after boot had *no* effect in v1, and that limit is gone.
  **False to true starts it**, on its own coroutine, file scope running now
  against the world as it is: a start and not a resume, so a script that ran, was
  disabled and is enabled again runs its file scope a second time. **True to
  false stops resumption**, and only that: a queued `task.defer`, a `task.delay`
  coming due and a signal fire are discarded when they come up, and a thread
  already executing runs to its next yield, because a coroutine cannot be
  preempted. **Connections are not disconnected** and nothing the script built is
  removed — they stop being invoked because nothing resumes the threads that
  would run them. Anything larger would decide whether a connection dies with the
  thing its closure captured, and a property setter is the worst place to answer
  that. Several scripts enabled by one tick start in document order.
- **Conformance specs** are mounted the same way. They live outside
  `src/scripts/**` (§4), but the headless runner mounts each
  `tests/conformance/**/*.spec.luau` file as an entry `Script`: `script` is
  bound to it, `script:IsDescendantOf(game:GetService("ScriptService"))` is
  true, and the runner calls the suite's `run()` **after** `game.Loaded` has
  fired. So a `game.Loaded:Connect` made at file scope does run, and every spec
  observes a fully booted world rather than a half-built one.
- **Lifecycle:** engine init → load `luaug.toml` + asset manifest → build the
  DataModel + mount scripts → **load the boot scene** named by `[project] scene`
  → start each Script on its own coroutine via `task.defer` in deterministic
  path-sorted order → the boot drain → first frame. The scene step is ADR 0047's
  and this section gained it there: the world a project *starts* with is a file,
  and it is loaded before a line of that project's behaviour runs, so a script's
  file scope sees the authored world instead of an empty one. The boot drain
  advances no clock — `SimTime` is still zero when it ends.
  **Mounting and starting are two steps, and the editor does only the first**
  (ADR 0058): a project opened in `luaug edit` shows its scripts and does not
  run them, play starts them, and stop tears the VM down so a second play is the
  same as the first. Every other host -- a game, `luaug dev`, headless, the
  conformance runner, a replay -- starts them at boot as written above.
  `game.Loaded: Signal` fires after all entry scripts have had their first
  resumption; `game:BindToClose(fn)` (with a capped timeout) and
  `game:Shutdown()` for exit.
- **The `script` global** = the instance the code being run IS. In a `Script`
  that is the running Script; **inside a `ModuleScript` it is that
  ModuleScript**, which is what makes a module a place you can put things --
  `script:FindFirstChild("Inner")` asks the module's own children. A module that
  is not an instance at all (`@luaug/…`) has `script = nil` rather than the
  requirer's, because handing it somebody else's is the same mistake in a
  quieter place. Name/Parent/attributes work; use attributes for per-script
  config.
- **Module caching & errors:** standard Luau Require semantics — one
  evaluation per module per VM, cyclic requires per spec, a module that errors
  propagates to its requirer and the failure is cached. An error in one
  Script's coroutine kills only that coroutine; the traceback goes to the
  console + DebugService Log; other scripts are unaffected.
- **Future client/server path (documented now, shipped later):** the blessed
  v1 multiplayer-ish pattern is a **sibling backend project run on Lute**
  (`backend/` using `@std/net.serve`), sharing `src/shared` via the alias —
  the @std convergence story keeping process boundaries honest from day one.
  `src/client`/`src/server` folder names and `Enum.RunContext` are reserved;
  the CLI warns if they exist in v1.

### 3.1 Deferred execution: one queue, one order

Signals are deferred-only (ADR 0015). This section is the contract that makes
"deferred" mean something specific enough to test — conformance specs are
written against it, and the engine's world hash depends on every rule here.

**One queue.** There is a single deferred queue. Everything that defers enters
it in the order it was raised, with no priority and no per-signal queues:
engine-raised signal fires (`ChildAdded`, `GetPropertyChangedSignal`, …),
script-raised fires (`Signal.new()` + `:Fire(...)`), and `task.defer`
callbacks all share it. This is what makes the relative order of a script's own
`:Fire()` and the `ChildAdded` caused by its `part.Parent = x` well defined.

**Resumption points.** The queue drains at the resumption points of the frame
pipeline (architecture §3): `PreRender` at render rate, then per sim tick
`PreAnimation`, `PreSimulation`, `PostSimulation`, and `Heartbeat`. Each
resumption point runs its engine phase, then drains. `task` timers resume in
their own phase between `PostSimulation` and `Heartbeat`; anything they defer
drains at `Heartbeat`.

**A drain runs to fixpoint.** Handlers that fire further signals append to the
same queue and the drain continues until the queue is empty — a drain does not
snapshot the queue and stop at its original end.

**A drain does not block on a yielding handler.** A handler that yields —
`task.wait`, a `:Wait()`, any yielding call — is left parked and the drain
moves straight on to the next entry; the parked coroutine resumes later on its
own terms, like any other. If a drain blocked, a single `task.wait(5)` in one
listener would stall every other listener of every other signal and make drain
duration unbounded, which is the exact property the fixed tick exists to
prevent.

**Order within one operation.** "In the order it was raised" needs a rule for
the several fires that one engine operation raises. A re-parent — the first
parenting of a fresh instance and an unparenting included — raises, in this
order:

1. `ChildRemoved` on the old parent,
2. `DescendantRemoving` on the old ancestors, nearest first,
3. `ChildAdded` on the new parent,
4. `DescendantAdded` on the new ancestors, nearest first,
5. `AncestryChanged` on the moved instance, then on each of its descendants in
   document order.

Leaving is fully observed before arriving, and the subtree is told last, once
its new ancestry is already true of it. Steps 1–2 are absent when there was no
old parent and steps 3–4 when there is no new one.

A `SetAttribute` that changes something raises two fires, and they are ordered
too: the signal from `GetAttributeChangedSignal(name)` first, then the class's
own `AttributeChanged`. The named signal is the narrower subscription — it asked
about *this* attribute — while the catch-all is what routes, and routing after
the specific fact has been observed is the order that composes. A property write
raises one fire, because there is no catch-all `Changed` to pair it with
(divergence #8).

**A write enqueues only when something changed.** Assigning a property or an
attribute the value it already holds enqueues nothing.
`GetPropertyChangedSignal` and `AttributeChanged` are past-tense facts about a
*change*, and an unconditional enqueue would make the 10k-parts benchmark
(architecture §4) pathological for no semantic gain. Equality is the value's
own `==`. There is no coalescing in the other direction: three distinct writes
before one drain produce three fires, in write order, not one — though since
the fire carries no value (§2.2), all three handlers read whatever the property
settled on.

**What a fire captures.** Enqueuing a fire records its arguments and the
identity of the connection list at that moment. Two consequences, and they are
the two people actually rely on:

- A connection made **after** the fire does not run for it. It was not
  listening when the thing happened.
- A connection disconnected **before it is invoked** does not run — including a
  disconnect performed by an earlier handler in the same drain. `:Disconnect()`
  is reliable, not advisory.

**Connection order is guaranteed**: handlers of one fire run in the order they
were connected. (Relying on order *between different signals* is still wrong —
that is queue order, above.) `:Once` disconnects on invocation, so a signal
fired twice before a drain invokes a `:Once` handler exactly once.

**`:Wait()`** parks the calling coroutine as a one-shot registration made at the
moment of the call, resumed by the next fire it is eligible for, at that fire's
position in the drain, returning that fire's arguments. Within that fire it
behaves exactly like a one-shot connection made at the same instant, and so it
takes its place among the connections **by registration order**: a `:Wait()`
registered after handler A and before handler B resumes after A runs and before
B does. This is stated rather than left unspecified because everything that
observes a signal at all leans on it.

**Re-entrancy is capped at 10.** Every queue entry carries a depth: entries
raised outside any handler have depth 0, and one raised by a handler running at
depth *d* has depth *d*+1. An entry that would exceed depth 10 is **dropped**
and logs `script.err.reentrancy_limit`, which surfaces on
`DebugService.MessageOut` like every other engine keyed message. Depths 0
through 10 all run, so a handler that re-fires its own signal is invoked
**exactly 11 times** and the twelfth fire is the one dropped; an
A-fires-B-fires-A loop splits those eleven 6/5. The cap counts everything on
the queue, `task.defer` callbacks included: a callback that defers again
carries its raiser's depth exactly as a fire does, and terminates at the same
generation. A fires-only cap would make a self-deferring callback an unbounded
drain — not a wrong number, a hang. This bounds signal loops deterministically,
and because it is a generation depth rather than a call-stack depth it does not
fire spuriously on a wide fan-out.

**Errors are contained.** Each handler runs on its own coroutine; an error in
one handler does not stop the other handlers of that fire, does not stop the
drain, and does not stop the script that fired. It goes to the console and
`DebugService.MessageOut` with its traceback. Containment is identical for
`task.defer` and `task.delay` callbacks — they are queue entries like any other
— and for `task.spawn`, whose function is resumed synchronously while the
caller's stack is still live and whose error still does **not** propagate to
that caller: `task.spawn` returns normally either way. A handler that errors
stays connected, because an error is a fact about one invocation and not a
disconnect; a `:Once` handler that errors has still been consumed.

**`Destroy` and queued fires.** The tree mutation is **synchronous**: when
`Destroy` returns, `Parent` is already nil, the instance is already absent from
its former parent's `GetChildren`, and its children have been destroyed
recursively. Only the signals are deferred. `Destroy` enqueues `Destroying`,
then closes the instance's other signals — queued fires for them find no live
connections and invoke nothing. `Parent` locks to nil; assigning it afterwards
raises `scene.err.parent_locked` and changes nothing. The instance handle stops
resolving at the end of the drain in which `Destroying` fired (divergence #25),
so until then the corpse is still usable: `Connect` still succeeds (the handler
simply does not run for the already-enqueued fire, which captured the
connection list before that connection existed), and a second `Destroy` is a
no-op that enqueues nothing and does not fire `Destroying` twice. After that
drain, each of those raises `script.err.instance_dead`. `GetChildren` and
`GetDescendants` return fresh arrays, so destroying during iteration over one is
safe; the array may simply contain instances that are gone by the time you reach
them. `Signal:Destroy()` on a script-created signal follows the same rule:
already-queued fires find no live connections and invoke nothing, and every
`Connection` reports `Connected == false` once the drain ends.

**Arguments are captured, not copied.** Tables and instances are passed by
reference: mutating a table between `:Fire(t)` and the drain is visible to the
handlers. Pass values you do not intend to change.

### 3.2 `task`

The whole scheduling surface; there are no legacy globals (divergence #2).

| Call | Semantics |
|---|---|
| `task.spawn(fn, ...)` | Resumes `fn` on a new coroutine **immediately**, synchronously, before returning. The one non-deferred call, and deliberately so. Returns that thread. |
| `task.defer(fn, ...)` | Enqueues `fn` on the deferred queue (§3.1) — it runs at the next resumption point, ordered against signal fires by when it was raised. Returns the thread it will resume. |
| `task.delay(duration, fn, ...)` | Resumes `fn` at the first task-resume phase at or after `duration` seconds of **SimClock** time. Returns the thread it will resume. |
| `task.wait(duration?)` | Yields; resumes at the first task-resume phase at or after `duration` seconds of SimClock time (default: the next tick). Returns the elapsed sim time, which is a whole number of ticks and therefore ≥ `duration`. |
| `task.cancel(thread)` | Cancels a pending resumption — a queued `task.defer` entry counts as one, so cancelling the thread `task.defer` returned stops its callback from running at the next drain. A thread with no pending resumption raises `script.err.task_not_scheduled`. |

**All three schedulers take a function, never a thread.** `task.spawn`,
`task.defer` and `task.delay` create the coroutine themselves and hand it back
— the returned thread is the one `coroutine.running()` reports inside the
callback, and it is the handle `task.cancel` takes. Passing an existing thread
raises. Resuming a coroutine you already hold is `coroutine.resume`'s job, and
keeping the two surfaces apart is what guarantees every scheduled thread is one
the scheduler created and can therefore account for.

**What counts as "not scheduled" for `task.cancel`.** A thread that has
finished, a thread already cancelled, and the thread currently running all
raise `script.err.task_not_scheduled`: none of the three holds a pending
resumption, and a pending resumption is the only thing `cancel` can take away.

Timers run on the SimClock, not the wall clock: `task.wait(1)` is exactly 60
ticks at the default 1/60 timestep, on every machine and every run. That is
what makes recorded replays reproduce (ADR 0025), and it is why there is no
`tick()` — `RunService.SimTime` (§2.1) is how a script reads that clock.

**Deadlines are computed in integer ticks**, never by a float ceil over
seconds: `duration` is converted to a whole number of ticks against
`FixedTimestep` and added to the current tick index, and the resumption happens
at that tick. This is load-bearing rather than an implementation note. At
`dt = 1/60`, `1 / (1/60)` evaluates to `60.000000000000007`, so a naive ceil
yields 61 ticks and contradicts the guarantee in the paragraph above.
`task.wait` returns `ticks × FixedTimestep` — the product, not a running sum of
per-tick dt values, so an exact multiple of the timestep comes back exact and
long waits carry no accumulated drift.

**Zero and negative durations.** `task.wait(0)` and `task.delay(0, fn)` both
mean the *next* task-resume phase, one tick away: `wait` must yield and `delay`
must not run before returning, so "at or after 0 seconds" cannot be satisfied by
the phase you are standing in. A negative duration clamps to zero and means the
same thing — it is what subtracting two elapsed values across a tick boundary
produces, and raising on it would be a trap rather than a diagnostic.

**The task-resume phase is one FIFO**, ordered by `(deadline tick, scheduling
sequence)`, with `task.wait` resumptions and `task.delay` callbacks interleaved
in it: an earlier deadline resumes first, and two things due on the same tick
resume in the order they were scheduled, whichever call scheduled them. R10
forbids leaving this to container order, so it is a rule rather than an accident
of the timer wheel. Anything those resumptions defer drains at `Heartbeat`
(§3.1). `SimTime` is constant across a whole tick, drains included, so a
`task.delay(d, fn)` scheduled from a `PreSimulation` handler and one scheduled
from `Heartbeat` on the same tick come due on the same tick.

**Hot reload (`luaug dev` — ADR 0024):**
- **Code change → fast world restart:** tear down the game VM, rebuild the
  DataModel, re-run scripts. GPU resources, imported assets, streamed chunks,
  and the window survive → target **< 500 ms** (a hard perf requirement).
- **State bag:** `HotReloadService` (dev-only): `PreReload: Signal`,
  `PostReload: Signal`, `SaveState(key: string, value: any)` (json-able or
  buffer), `LoadState(key) → any?`, `IsReload(): boolean`. The engine
  auto-preserves `Workspace.CurrentCamera.CFrame` and any instance tagged
  `"PreserveOnReload"` (so the character just stays put in the demo).
  The two event names are `Pre*`/`Post*` and `IsReload` is a **method** because
  §9's own lints rejected the alternatives: a boolean property may not carry an
  `Is` prefix, and an event must be a past-tense fact or a `Pre*`/`Post*` phase.
  The rules were right and the first spelling was not.
- **Asset change → in-place swap** — designed here and **not implemented**.
  Textures, meshes and audio were to hot-swap without a VM restart on a
  content-hash change pushed over the dev WebSocket. The message type exists in
  the protocol and the engine answers it with `dev.err.not_implemented` rather
  than ignoring it, which is the honest half: a verb that is reserved says so
  where a verb that is silently dropped looks like a bug in the watcher. Editing
  an asset today needs the code-change path, which is the full VM restart above.
- Transport (ADR 0035): the dev server (Lute, `@lute/fs.watch` + an `@std/net`
  WebSocket **server** on the `[dev] port`) launches the runtime, and the
  runtime **connects out to it as a WebSocket client** and receives
  `{script-changed | asset-changed | eval}` messages over that connection.
  **Only the dev server listens** — the engine opens no port in any profile, and
  the client half is compiled into dev builds only. The same server serves the
  dev server's other clients (the hot-reload gate test, the overlay console, a
  future editor) and relays between them and the engine. `eval` is reserved the
  same way `asset-changed` is and is answered the same way: running arbitrary
  source in a live world touches R4 and needs its own design, so the dev console
  it was to power does not evaluate anything.
- **Reload ordering:** the state bag and the `PreserveOnReload` instances are
  captured before the VM is destroyed and restored into the fresh world
  **before** the new entry scripts are deferred, so a script that looks for what
  it left behind finds it already there. `IsReload` is how it knows to look
  rather than re-create.

---

## 4. User project anatomy

**Config format: `luaug.toml`** (consistent with rokit.toml/pesde.toml;
comments; static). Sections: `[project]` name, id (reverse-DNS), version,
**`scene`** — the world a run starts with, relative to `content/` (ADR 0047,
§3's lifecycle) — `engine = "0.1"`, `icon`; `[window]` title, size; `[dev]`
port; `[assets]` extra source dirs, import options; `[permissions]` net_serve,
fs_paths (§7); `[memory]` optional script-heap hard cap and budget overrides;
`[graphics]` the quality family (below); `[build]` targets, bytecode opt level.

**Five of those sections are read and three are not.** What reaches the engine or
the CLI is `[project]` `name`, `id`, `icon` and `scene`; `[window]` `title` and
`size`; `[dev]` `port`; `[assets]` `content`, which names the directory
`build-assets` compiles; and the whole of `[graphics]`. **`[permissions]`,
`[memory]` and `[build]` are parsed by nothing**, and neither is `[project]
version` — the permission model belongs to the `@std` surface §7 has not
finished, the heap cap belongs to a budget the engine enforces from its own
defaults, and the build target is a flag on `luaug build` rather than a table. A
key nothing reads is not a default: a project that sets one is setting nothing.

**`[graphics]` — the quality family (M8, ADR 0044).** These are *engine*
settings and not `Lighting` properties: `Lighting` describes the world and
travels with the scene, while these describe the machine it is being shown on.
A script cannot write them, deliberately — a scene must not decide the player's
GPU budget.

```toml
[graphics]
quality = "high"          # low | medium | high | ultra -- a named set of every field below
render_scale = 1.0        # 0.5 to 1.0; the world renders at this fraction, the UI does not
shadow_resolution = 1024  # one cascade's tile, in texels; the atlas is two tiles by two
shadow_cascades = 4       # 0 through 4; 0 is "the sun casts no shadow"
shadow_distance = 120.0   # metres
light_budget = 256        # how many lights one frame may carry
bloom = true
ambient_occlusion = true
anti_aliasing = true
auto_exposure = true
```

Three layers, each overriding the one before: the preset, then this table, then
the host's own flags (`--quality=`, `--render-scale=`, `--no-bloom`, …). `high`
is exactly what the engine ships with, so a project that says nothing gets it.

**`[project] icon`** is a project-relative path to a PNG or a multi-size `.ico`.
The dev host sets it on its window; `luaug build` embeds it in the packaged
artifact, because a game built with this engine wears its own face rather than
the engine's.

**`luaug new my-game` template:**
```
my-game/
├─ luaug.toml
├─ .luaurc                  -- strict mode + shared/pkg/std/lute aliases (§1.3)
├─ rokit.toml               -- lute, luau-lsp, stylua pinned (§1.4)
├─ stylua.toml
├─ .vscode/
│  ├─ settings.json         -- luau-lsp: platform.type=standard (Roblox defs OFF),
│  │                        -- types.definitionFiles=[.luaug/types/engine.d.luau],
│  │                        -- require.directoryAliases for @std/ and @luaug/ stubs
│  └─ extensions.json       -- johnnymorganz.luau-lsp, stylua
├─ content/
│  └─ scenes/main.scene.json -- the world the project starts with (§2.6, ADR 0047)
├─ src/
│  ├─ scripts/main.luau     -- entry Script (--!strict)
│  └─ shared/greeting.luau  -- modules, require("@shared/...")
├─ assets/i18n/en.json
├─ tests/example.test.luau
├─ .luaug/                  -- generated (gitignored): types/, cache/, manifest
└─ .gitignore
```

Three lines of that tree were written before the things in them existed and are
corrected here rather than left as a scaffold nobody gets. **`pesde.toml` is not
scaffolded** — `luaug add` does not exist (below), so an empty manifest for a
package manager the CLI cannot drive would be furniture. **There is no
`types.documentationFiles` setting**: the docs JSON it named was dropped in M3
and the hover text rides inside the definitions file (§5), which the template's
own settings say in a comment beside the line. And **`assets/` holds the i18n
catalog and nothing else**: models, textures and audio live under `content/`,
which is what the engine mounts and what `build-assets` compiles, and there is no
`prefabs/` directory because a stamp is a file under `content/stamps/`.

**Two test conventions, and they are not one suite.** A *user project* puts its
tests in `tests/**/*.test.luau`, and `lute test` runs them pure — the starter
template scaffolds one. The *engine's own conformance suite* — the specs written
from this document, which decide whether an implementation is LuauG at all —
lives at `tests/conformance/**/*.spec.luau` and is run only by the headless
runtime, which mounts each file as an entry Script (§3). The different extension
is the point: the two are globbed by different runners, and a conformance spec
that matched the user pattern would be picked up by `lute test`, where there is
no engine to conform to and every case fails for the wrong reason.

**What `luaug test` does is the second of those and only the second.** It runs a
suite of `*.spec.luau` on the headless engine and emits TAP, defaulting to
`tests/conformance`; the `--engine` flag this section named does not exist,
because there is no non-engine mode of the command for it to switch off. A user
project's own `*.test.luau` files therefore run under `lute test` and not under
`luaug`, which is the half of the two-runner story that shipped.

**CLI command set.** The `luaug` CLI is a set of Lute scripts run by the pinned
`lute` rather than a `lute compile`d binary — that was M3's decision and this row
was written before it — and it launches the separate native runtime binary. The
table is the command set the CLI's own `--help` prints, which is the one that
exists:

| Command | Wraps |
|---|---|
| `luaug new <name>` | scaffold from a template. `--template` exists and `starter` is the only value it accepts |
| `luaug dev [path]` | run the project with a watcher attached: a saved file rebuilds the world (asset watcher/importer + the hot-reload server, §3.2) |
| `luaug edit [path]` | run the project with the editor UI in place of the debug overlay (ADR 0046) |
| `luaug build [path]` | The distributable folder: the host binary under the game's name and wearing its icon, the engine's content beside it, and the game in `game/` — which the player mounts when given no script. Ships Luau **source** rather than bytecode; ADR 0045 says why, and amends this row |
| `luaug build-assets` | compile `content/` into a pack and a manifest — glTF 2.0 canonical → runtime formats (engine mesh, KTX2/BCn textures, ogg/wav), with a crypto-digest cache in `.luaug/cache` |
| `luaug test [path]` | run the conformance suite on the headless engine |
| `luaug check [path]` | `luau-lsp analyze` with the generated settings/defs (CI-ready) + StyLua check + i18n lint |
| `luaug fmt [path]` | StyLua |

**Five commands in this table were designed and never built**, and none of them
is load-bearing for anything above. `luaug run` is `luaug dev` without the
watcher, and the watcher costs nothing a run cares about. `luaug asset
import|list|hash` became one command, `build-assets`, with no sub-verbs and no
`assimp` front end in a shipped build. `luaug setup` was the `lute setup`
pattern; `luaug new` copies the definitions into `.luaug/types/` at scaffold time
instead, so nothing has ever needed to ask for them again — and it is also why
there is no command that would regenerate them after an engine upgrade.
`luaug add` needs the package manager `pesde.toml` was scaffolded for, and
`luaug doctor` is named by the comment in the template's own `luaug.toml` and
answers nothing today.

Zero-config onboarding = `luaug new` + open VS Code: defs, hover documentation,
aliases and formatter preconfigured; nothing to install manually beyond
`rokit install`. **The `@std`/`@luaug` aliases are the one part of that promise
still owed** — the settings point at `.luaug/types/std/` and
`.luaug/types/luaug/`, and nothing generates them (§5, artifact 3).

---

## 5. Type definitions & docs pipeline

**Single source of truth: a typed Luau IDL** — `api/defs/*.api.luau` data
files (strict-typed against `api/schema.luau`), evaluated by a Lute codegen
app (`api/generator/`). Why Luau over JSON/YAML: comments, composition/reuse,
runs on our own toolchain, schema-validated by the type checker itself.

Each class entry declares: `name`, `extends`, `tags` (Service | Abstract |
NotCreatable | DevOnly), `properties` (name, type, readOnly, default,
threadSafety = Safe | ReadSafe | Unsafe, docKey), `methods` (params/returns,
overloads, yields → enforces the `Async` suffix), `events` (signal param
types), plus top-level `enums` and `datatypes`. Doc text is authored inline in
English with an auto-derived key (`api.part.anchored`), exported into the en
catalog. The same files drive the C++ side (architecture §4): generated
property getter/setter tables, method dispatch glue, enum registration, and
thread-safety assertions.

**Generated artifacts per engine release** (each gated in CI, though not all
the same way — the two shapes are below):
1. `runtime/types/engine.d.luau` — `declare extern type` for every
   class/datatype + global declarations (`game`, `workspace`, `script`,
   `Instance.new` string-singleton overloads). Never `declare class`. **The doc
   text rides in this file** as `---` comments above each declaration, from the
   same IDL string that produces the declaration.
2. ~~`.luaug/types/engine-docs.json` — the luau-lsp documentation file.~~
   **Dropped in M3.** It is an LSP-only setting with no `luau-lsp analyze`
   flag, so nothing in the gate can prove a generated one is even shaped right,
   and MASTER_PROMPT §9 forbids shipping a format we cannot verify. The `---`
   comments in (1) carry the same text with one artifact instead of two and the
   existing freshness gate already covering it.
3. `.luaug/types/std/**` and `.luaug/types/luaug/**` — typed stub modules for
   `@std`/`@luaug` (editor resolution via `require.directoryAliases`).
4. `api/api-dump.json` — versioned, machine-readable; CI diffs it to force
   changelog entries and catch accidental API breaks. It records what a script
   can observe and deliberately nothing else: no backend accessor names (R17),
   and **no doc prose**, which rides in (1) and would bury a removed method
   under a reflowed paragraph. Ordered by name rather than by declaration
   order, so moving a class between `.api.luau` files produces no diff at all
   and an added member produces one in a single place.
5. `docs/api/**` — the markdown reference. One page per class, plus a page
   each for the datatypes, the enums and the libraries, plus an index: 56 class
   pages and four others at the current definitions. A page lists what its class
   **declares** and links to its base rather than flattening what it inherits,
   which is the api-dump's rule in (4) and holds here for the same reason —
   flattening turns one added member on `Instance` into a diff on every page in
   the directory.
6. The **documentation site** — the manual authored under `docs/manual/**` and a
   reference page for every class, datatype, enum and library, emitted as one
   self-contained static site by `api/generator/gen_site.luau` and built by
   `scripts/docs.ps1` / `scripts/docs.sh`. It is §8's "Docs site outline", and
   its table of contents is authored in `api/generator/site/nav.luau` rather
   than discovered, because the order of a manual is most of what a manual is.

**Built so far:** (1) since M2, freshness-gated, carrying the doc comments since
M3; (4) since M4, freshness-gated the same way; (5) since M8; (6) since
2026-08-26. (2) is dropped. **(3) is the one artifact declared here and still not
generated** — `luaug new` copies (1) into a scaffolded project's `.luaug/types/`
and writes no `std/` or `luaug/` stubs beside it, so the
`require.directoryAliases` in the template's `.vscode/settings.json` name
directories that are not there.

**Two shapes of gate, because the two outputs are read differently.** Both live
in `scripts/gates/luau-check.sh`. (5) is checked in, so its stage — *generated
API reference is fresh*, which runs `lute api/generator/gen_reference.luau` —
copies `docs/api/` aside, re-runs the generator and diffs the **directory**. The
copy is taken BEFORE the generator runs, like every other freshness check in that
file, so a hand edit is caught rather than overwritten and then reported clean. A
directory rather than a file list, because a class removed from the IDL has to
take its page with it, and a stale page is exactly the documentation that
outlives what it describes.

(6) is deliberately **not** checked in, and there is therefore no freshness diff
to run: a site is read after somebody built it, so the build IS the read and a
stale copy cannot exist. Checking it in would buy a diff nobody reads across tens
of thousands of generated lines and cost churn on every edit to a doc string. So
its stage — *the documentation site builds*, which runs
`lute api/generator/gen_site.luau "--out=$site_out"` into a temporary directory —
is that exit code and nothing else. What it catches is not staleness but a
**reference that no longer resolves**: every `api:` link in the manual, every code
sample's file name and every page the navigation declares is checked against the
IDL and the filesystem, and the run exits non-zero listing what it could not
resolve. It also audits the published prose, because a doc string that cites a
design section, a decision record or an internal milestone is prose written for
somebody maintaining the engine on a page written for somebody using it.

The paths in (1), (4) and (5) are the real ones. (1) and (4) were written here as
`.luaug/types/...` and (5) as `docs/reference/**`, and none of the three was
corrected as its generator landed — which is the stale-spec bug MASTER_PROMPT §5
names, three times in one list. `.luaug/` is per-project generated state, and all
three of these are repository artifacts that ship with the engine.

Naming-rule lints run inside the generator (§9) as a CI gate.

---

## 6. i18n system

- **Catalog format: flat JSON per locale** — `i18n/en.json` for the engine,
  `tools/cli/i18n/en.json` for the CLI, `assets/i18n/en.json` for games. JSON
  because C++, Luau, and translation tools all consume it trivially. Values
  are template strings with `{param}` placeholders; plural values are objects
  keyed by CLDR category (`{"one": "...", "other": "..."}`, selected by a
  `count` param). English-only at launch; **adding a locale = adding a file**
  — enforced by never concatenating user-facing strings in code (R3).
- **Key naming:** dotted, area-first: `engine.physics.err.invalid_shape`,
  `engine.stream.info.area_loaded`, `cli.dev.watching`, `api.part.anchored`
  (docs). Games use their own namespace (`game.*` suggested).
- **C++ → localized message path:** engine code never embeds English; it
  raises `(key, params)` records → the core formatter resolves against loaded
  catalogs (fallback: locale → `en` → raw-key echo + a one-time dev warning)
  → surfaced to console/overlay/`error()`. Errors reaching Luau are
  pre-formatted strings prefixed with the key
  (`[engine.assets.err.not_found] …`) so tests match on keys, not prose.
- **In-game `LocalizationService` — designed here and never built** (§2.1). The
  three halves that did ship are the ones the engine itself needs: the catalog
  format, the key discipline R3 enforces, and the C++ formatter above. What is
  missing is the Luau surface, so a game today has a catalog under
  `assets/i18n/` that nothing loads. The design stands as written:
  `Locale: string` (BCP-47, settable),
  `SystemLocale: string` (read), `LocaleChanged: Signal<string>`,
  `Translate(key: string, params: {[string]: any}?) → string` (a missing key
  echoes the key + a dev log), `LoadCatalog(locale: string, content: Content)`
  (DLC/mods). v1 stops here; ICU-grade formatting, dates, and gender are
  reserved extensions. `TextLabel.Text` accepts plain strings only in v1 —
  auto-localized UI is future work; `InputBinding.DisplayName` accepts a key.

---

## 7. @std implementation scope for v1 (ADR 0030)

**Registered in the game VM, today: one module and one function.**
`require("@std/net")` resolves and exports `request`, the HTTP client; nothing
else on this table is reachable from a script. The table is therefore the *scope*
this section committed to and not a report of what a game can call, and the third
column says which is which. `@luaug/testing` and `@luaug/camera` do ship, as
Luau content the engine mounts.

| Module | Game runtime? | v1 notes / sandbox |
|---|---|---|
| `@std/task` | **Not registered.** `task` is a global and the module name does not resolve | The design was table identity, not merely the same surface: `require("@std/task") == task`, because two tables would be two schedulers and only one of them owns the queue in §3.1. spawn/defer/delay/wait/cancel; synchronize/desynchronize are reserved names, meaning absent — not present-and-erroring |
| `@std/json` | **Not registered** | encode/decode |
| `@std/path` | **Not registered** | pure |
| `@std/stringext`, `@std/tableext` | **Not registered** | pure |
| `@std/net` | **Yes — `request` only** | `request` (HTTP client) is built and parks the calling coroutine on a worker, resuming at a frame safe point. The WS client, `serve` (HTTP+WS server) and raw sockets are design: shipped builds were to require `[permissions] net_serve = true` in luaug.toml, and nothing reads that key yet |
| `@std/fs` | **Not registered** | Paths were to be URI-rooted: `asset://` (read-only mounted content) and `save://` (per-user writable dir) in shipped builds; dev mode project-root read/write; raw OS paths only behind an `--allow-fs` dev flag / `[permissions] fs_paths`. A game VM today has no filesystem at all, which is the safe end of that design to be unfinished at |
| `@std/test` | **Not registered** | The real runner is Lute's. What the engine actually provides headless is `@luaug/testing`, which `tests/conformance/**` is written against |
| `@std/io` | No (game VM) | Headless/dev console only |
| `@std/process`, `@std/luau` | **Tooling-only** | Never in the game VM (security; `@std/luau` = a loadstring-equivalent) |

**The shared conformance suite runs on the LuauG runtime and not on Lute.**
`tests/conformance/**/*.spec.luau` is driven by `luaug test`, in the gate and in
CI, and there is no second run of those files under `lute` — the insurance policy
on the convergence bet is written down here and has never been collected. It is
also what would have caught this table going stale: a spec that required
`@std/json` would have failed the first time the suite ran without it, and no
spec ever required one.

---

## 8. Examples & docs plan

**One template, and the other two became examples.** `luaug new` has exactly one
target — `starter`, the §4 tree — and what it scaffolds moved with ADR 0047: the
lit part its one script used to spawn is **authored in the scene** now, and the
script finds it with `WaitForChild` and spins it on `Heartbeat`. That is the
split the template exists to teach, and a file scope that built a world would
teach the opposite. The two below were written here as templates and shipped
under `examples/` instead, and the difference is not cosmetic: a template is
scaffolded into somebody's own project and has to stay small enough to read,
while an example is a project this repository keeps building.

1. `starter` — the `luaug new` target: one script, one shared module, one scene,
   one test.
2. `examples/04-obby` (M6) — the idiom set, and a game rather than a
   demonstration: a course of `Part`s, platforms moved by a tweened `CFrame`, a
   `ScreenGui` + `UIListLayout` menu that arrives by tweening a `UDim2`, a HUD
   button driving a real `InputAction` through `InputService:SetVirtualState`,
   `Sound` on the SimClock, and a skinned glTF playing one `AnimationTrack` on
   the character. The Tags-and-`Signal.new` checkpoint pattern this row promised
   is not in it, and neither are localized strings — that catalog needs the
   `LocalizationService` §6 never got.
3. `examples/10-open-world` (M8, the flagship) — a streamed world with the
   **character** as the focus rather than the camera; third-person
   `CharacterBody` + a `@luaug/camera` rig; `MinRadius` and
   `PauseOutsideLoadedArea` doing the work when the ground has not arrived; one
   `Lighting.ClockTime` driving sky, shadow direction, fog, reflections and
   exposure together; ambient `Sound`; and hot reload that puts the character
   back where it was standing, through `HotReloadService:SaveState`. Two things
   this row promised are not in it: `AudioGroup` mixing, and the `backend/` Lute
   app sharing `@shared` code over `@std/net` WS — the second cannot exist until
   the WS client does (§7).

**The docs site is built** (§5, artifact 6), and its outline is authored in
`api/generator/site/nav.luau` rather than here, because a table of contents in
two places is a table of contents that disagrees with itself. What that file now
declares is wider than the sketch this line carried: Get started · Core concepts
· Building a world · Rendering and lighting · Physics · Input · User interface ·
Audio · Animation · Assets and streaming · Guides · **Why it works this way** —
the decisions a person will trip over, each with the reasoning that settled it —
and **Coming from Roblox**, which is the keystone and is three pages rather than
one: the migration guide, every deliberate divergence (§2.5), and what is not
here. [`coming-from-roblox.md`](coming-from-roblox.md) is still the repository's
own copy of that argument. The reference half is generated from the IDL and not
from the api-dump: the dump carries no doc prose (§5, artifact 4) and could not
produce a reference page if it wanted to.

---

## 9. Naming & style conventions

**The rule, in one sentence: if you index it off an object, it is PascalCase; if
you index it off a module or namespace, it is camelCase.** That is the Roblox
convention, and LuauG keeps it (ADR 0034).

- **Object members are PascalCase** — properties, methods and events reached
  through an instance or a value: `part.Name`, `part:Destroy()`,
  `cf:Inverse()`, `v.Magnitude`, `signal:Connect(fn)`. Methods are called with
  `:`. Services end in `Service` except `Workspace` and `Lighting`. Yielding
  methods end in `Async`. Events are past-tense facts (`Landed`, `Ended`,
  `ChildAdded`) or `Pre*/Post*` phases. Boolean properties have no `Is` prefix
  (`Anchored`, `Enabled`); boolean methods do (`IsA`, `IsPaused`). No
  abbreviations except UI.
- **Module and namespace functions are camelCase**, constructors and factories
  included: `Instance.new(className)`, `CFrame.new`, `CFrame.fromEuler`,
  `Color3.fromRGB`, `Signal.new`, `task.spawn`, `vector.create`, and whatever a
  `@std/*` or `@luaug/*` module exports. Namespace constants are lowercase too:
  `CFrame.identity`, `vector.zero`. A namespace is not an object, and the
  casing says so at the call site.
- **Type, class and namespace names themselves are PascalCase**: `Instance`,
  `CFrame`, `Color3`, `Vector3`, `Signal`.
- **Two rules carry named exception lists**, and the generator's lints hold both
  (`api/schema.luau`). A yielding method's name ends in `Async` *unless it
  begins with `Wait`*: the suffix exists to warn that a call parks when the name
  does not say so, and `WaitForChild` already says so. Events are past-tense
  facts or `Pre*`/`Post*` phases *except* `Heartbeat` and `MessageOut`, whose
  names are frozen here. Adding to either list is an edit someone makes in the
  schema, deliberately — which is the difference between an exception and a
  loophole.
- **There are no camelCase aliases for object members** (`:connect` never
  existed — divergence #21), and no PascalCase aliases for module functions.
  One spelling each, always.
- The single documented exception is that the native `vector` fields are
  `x`/`y`/`z` (divergence #9): they are fields of a VM primitive, like
  `string`, not members of an object we define.
- **Identifier casing inside our own Luau files** — engine runtime, tooling,
  examples, templates and specs alike. The public rules above govern what a
  *user* types; these govern what a file looks like:
  - **Module-level variables and constants are PascalCase, with no
    underscores.** There is no `SCREAMING_SNAKE_CASE` in this codebase: a
    constant is not a different kind of name, only a different kind of value.
  - **Locals and inner functions inside a function body are camelCase.**
  - Module-level *functions* — helpers and exported alike — follow the public
    rule and stay camelCase, because a module is not an object.
  - Types are PascalCase.
- **User code:** `--!strict` everywhere (also `.luaurc` languageMode strict),
  StyLua formatting, luau-lsp lints on.
- **Enforcement:** the generator's schema validator encodes every rule above
  as CI-failing lints on the IDL (regex + structural checks: `Async` iff
  yields, the event tense list, no `Get` prefix on properties, singular enum
  names), and `scripts/gates/luau-check.sh` runs it as
  `lute api/generator/check.luau`. `luaug check` gates the rest — it is
  `luau-lsp analyze` and `stylua --check` in one command, over the whole
  repository including every example and template, under the pinned luau-lsp.
  There is no `luaug fmt --check`: `fmt` rewrites and `check` reports, and the
  gate calls `check`.

---

## 10. Risks & open questions (one recommendation each)

1. **`vector` field typing (x/y/z) under luau-lsp custom platform.** The
   builtin new-solver `vector` type is lowercase; mapping the primitive to a
   custom extern type may have edges. **Rec:** committed to lowercase
   (divergence #9); run a week-one spike in M3 — if clean, uppercase sugar
   could be added later, never the reverse. (UNCONFIRMED U-14.)
2. **Full-VM-restart hot reload may feel heavy for UI tuning.** **Rec:** hold
   the < 500 ms budget as a hard perf gate; invest in the state bag +
   `PreserveOnReload`; consider per-module hot swap only post-v1 if the
   budget fails (ADR 0024).
3. **@std convergence depends on Lute surface stability.** **Rec:** pin Lute
   via rokit; the shared conformance suite (§7) is a v1 deliverable, not an
   afterthought; wrap divergences behind our stubs. **Open, and the most
   overdue item on this list.** Lute is pinned. The suite was never run against
   it, and the surface it would have compared has one module in it (§7) — so the
   bet is neither collected nor lost, which is the state a bet should not be in
   after a release.
4. ~~**No ModuleScript breaks the "model with scripts inside" habit.**~~
   **Closed by ADR 0050, in the other direction.** `ModuleScript` exists, a
   script's `Source` is a property, and a script inside a model is exactly what
   a stamp carries — so the habit works rather than needing a recipe. What the
   asset importer does with a script embedded in a glTF is a separate question
   and not this one.
5. **IAS is new even to Roblox devs (2026).** **Rec:** the obby and five
   copy-paste IAS recipes in the migration guide are launch blockers. The obby
   shipped as `examples/04-obby` rather than as a template (§8), and ADR 0041
   took the other half of this risk out by making one key readable without a
   context, an action and a binding.
6. ~~**No general scene serialization format**~~ (code-only worlds strain as
   content grows). **Closed by ADR 0047**, and the recommendation was right
   about the shape of the answer if not the timing: it was not invented under
   deadline, it was invented when a person tried to edit a world and asked how
   to save it. `.scene.json` is the format, a stamp is the same format over one
   subtree (§2.6), and `[project] scene` is what a run loads before its scripts
   start (§3). `.prefab.luau` was never built and is not what covered this.
7. **Single-VM v1 code may assume shared memory and break under a future
   client/server split.** **Rec:** never ship `IsServer()`-style stubs in v1;
   push the sibling-backend-on-Lute pattern hard in docs; reserve
   `RunContext` and the folder names.
8. **Ensemble similarity to Roblox naming despite generic words** (Part,
   Workspace, ScreenGui). **Rec:** pre-launch legal review of the full
   api-dump identifier list (ADR 0020); the deliberate renames stand as
   evidence of independent design; nominative use only in docs.
9. **Luau 0.734 pin vs the fast-moving solver/lsp.** **Rec:** one Luau +
   luau-lsp pair pinned per engine release, defs regenerated and shipped
   together; upgrades are engine releases, never silent.
10. **UI property renames trade familiarity for cleanliness.** **Rec:** the
    rename list is frozen at §2.5; no runtime aliases ever; defs-driven
    completion + the migration table carry discovery.
