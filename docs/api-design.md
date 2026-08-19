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
| **Globals** | The world model: `game`, `workspace`, `script`, `Instance`, datatypes (`Vector2`, `Vector3`, `CFrame`, `Color3`, `UDim`, `UDim2`, `Rect`, `TweenInfo`, `RaycastParams`, `Random`, `Signal`), `Enum`, plus Luau builtins (`task`, `vector`, `buffer`, `math`, `table`, `string`, `coroutine`, `utf8`, `os` limited to clock/time/date, `print`, `warn`, `error`, `assert`, `pcall`/`xpcall`, `require`, `typeof`, `tostring`, …) | Roblox muscle memory: you never require Vector3 |
| **`@std/…`** | The cross-runtime stdlib (Lute-compatible surface, §7): `@std/json`, `@std/net`, `@std/fs`, `@std/path`, `@std/task`, `@std/stringext`, `@std/tableext`, … | The convergence bet (ADR 0030): utility code runs unchanged on Roblox/Lute/LuauG |
| **`@luaug/…`** | Engine-provided optional Luau libraries (not core world), PascalCase like everything else LuauG defines: `@luaug/camera` (third-person/orbit rigs), `@luaug/signal` (pure-Luau Signal for shared code), `@luaug/imgui` (dev-only custom debug panels), `@luaug/testing` (engine-aware test helpers) | Keeps the global surface small; optional things are opt-in |

**Removed globals (deliberate, no aliases):** `wait`, `spawn`, `delay`,
`tick`, `elapsedTime`, `loadstring`, `getfenv`/`setfenv`, `newproxy`,
`shared`; `_G` exists but is frozen-empty and lint-flagged. `io` does not
exist in the game VM.

### 1.2 Service acquisition

`game:GetService("TweenService")` — lazy singleton creation, typed via
string-singleton overloads so the return type is exact.
`game:FindService(name)` returns `Instance?`. Services are children of `game`
once created. The `workspace` global is the canonical handle for the
`Workspace` service; no other service gets a global.

### 1.3 `.luaurc` for a generated user project

```json
{
  "languageMode": "strict",
  "aliases": {
    "shared":  "src/shared",
    "pkg":     "luau_packages"
  }
}
```

- `@std/…` and `@luaug/…` are **not** user `.luaurc` aliases: at runtime they
  resolve via `luarequire_registermodule`; in the editor/CI they resolve via
  generated `luau-lsp` `require.directoryAliases` pointing at typed stubs in
  `.luaug/types/` (the `lute setup` pattern, mirrored by `luaug setup`). This
  keeps the user `.luaurc` clean and avoids runtime/analyzer conflicts.
- `@self`, `./`, `../`, `init.luau` behave per the Luau require-by-string
  spec. pesde appends its own per-dependency aliases when used.

### 1.4 Engine version pinning as users see it

- `rokit.toml` pins the `luaug` CLI (and `lute`, `luau-lsp`, `stylua`) —
  reproducible toolchain.
- `luaug.toml` `[project] engine = "0.1.3"` pins the runtime; the CLI uses
  exactly that runtime and `luaug setup` regenerates `.luaug/types/` to match.
- At runtime: `game.EngineVersion: string` and `game.LuauVersion: string`
  (read-only).
- Type defs, docs JSON, and the api-dump are versioned artifacts of each
  engine release; a mismatch produces a CLI warning.

---

## 2. v1 API surface

### 2.1 Services (the complete v1 list — 15 + 1 dev-only)

**`Workspace`** (global `workspace`) — 3D scene root.
- Props: `Gravity: vector` (SI, default `(0, -9.81, 0)`), `CurrentCamera: Camera`
- Methods: `Raycast(origin, direction, params?) → RaycastResult?`,
  `Spherecast(origin, radius, direction, params?)`,
  `GetBodiesInBox(cframe, size, params?) → {BasePart}`

**`RunService`** — frame loop.
- Events (in-frame order, all `(dt: number)`): `PreRender` (render-rate,
  variable dt), `PreAnimation`, `PreSimulation`, `PostSimulation`, `Heartbeat`
  (fixed-tick, per architecture §3 — this rate split is documented loudly).
- Methods: `Pause()`, `Resume()`, `IsPaused() → boolean` (world pause;
  render/debug keep running).

**`InputService`** — host for the Input Action System (§2.4) + device state.
- Props: `PointerLocked: boolean`, `PointerVisible: boolean`,
  `LastInputDeviceType: Enum.InputDeviceType`
- Methods: `GetPointerPosition() → Vector2`
- Events: `InputDeviceChanged(deviceType)`, `WindowFocusChanged(focused)`

**`TweenService`** — `Create(instance, tweenInfo, goals: {[string]: any}) →
Tween`; `GetValue(alpha, easingStyle, easingDirection) → number`. Easing enum
set identical to Roblox's so tutorials transfer.

**`AudioService`** — mixing + listener. Props: `MasterVolume: number`.
Methods: `PlayLocal(content) → Sound` (fire-and-forget 2D). The listener is
`Workspace.CurrentCamera`. `AudioGroup` instances are the mixing buses.

**`AssetService`** — content loading over the content-addressed pipeline.
- `LoadModelAsync(content: Content) → Model`, `PreloadAsync(contents:
  {Content})`, `Exists(content) → boolean`. See §2.6 (prefabs).

**`LocalizationService`** — §6. `Locale`, `SystemLocale`,
`Translate(key, params?)`, `LoadCatalog(locale, content)`, `LocaleChanged`.

**`UIService`** — parent of `ScreenGui` instances (the PlayerGui role) +
screen metrics: `SafeAreaInsets: Rect` (read), `DisplayScale: number` (read).

**`Lighting`** — day/night + environment. Props: `ClockTime: number` (0–24),
`GeographicLatitude`, `Ambient: Color3`, `Brightness: number`,
`FogColor: Color3`, `FogStart: number`, `FogEnd: number`,
`SunDirection: vector` (read, derived). Child class: `Sky`
(`SkyboxContent: Content` HDRI/cubemap, `SunAngularSize`).

**`PhysicsService`** — physics controls beyond per-part props:
`RegisterCollisionGroup(name)`, `CollisionGroupSetCollidable(a, b,
collidable)`, `GetRegisteredCollisionGroups()`, `FixedTimestep: number`
(default 1/60 — also the sim tick rate).

**`StreamingService`** — StreamingEnabled-modeled, as a dedicated service
(streaming is a system, not scene-root state).
- Props: `Enabled: boolean`, `LoadRadius: number`, `MinRadius: number`,
  `PauseOutsideLoadedArea: boolean`
- Methods: `AddFocus(instance)`, `RemoveFocus(instance)`,
  `LoadAreaAsync(position, radius)`
- Events: `AreaLoaded(position, radius)`, `InstanceStreamedOut(instance)`
  (streamed-out = reparent to nil, exactly the Roblox contract).
- Per-model control: `Model.StreamingMode: Enum.StreamingMode`
  (Default/Atomic/Persistent).

**`TagService`** — the CollectionService pattern with a clearer name:
`GetTagged(tag) → {Instance}`, `GetInstanceAddedSignal(tag) →
Signal<Instance>`, `GetInstanceRemovedSignal(tag) → Signal<Instance>`,
`GetAllTags()`. (Add/Has/Remove live on `Instance`.)

**`WindowService`** — desktop-first, no Roblox analog: `Title: string`,
`Mode: Enum.WindowMode` (Windowed/Fullscreen/Borderless), `Size: Vector2`,
`VSync: boolean`; Events: `Resized(size)`.

**`DebugService`** — ImGui overlay + instrumentation (present in shipped
builds with the overlay off unless enabled).
- Overlay: `OverlayVisible: boolean`, `ShowPanel(name)`, `HidePanel(name)`
  (built-ins: "Stats", "Scene", "Log", "Streaming", "Physics")
- Gizmos (dev): `DrawLine(a, b, color?)`, `DrawBox(cframe, size, color?)`,
  `DrawSphere(position, radius, color?)` (per-frame)
- Stats: `GetStat(name) → number` ("FPS", "FrameTimeMs", "PhysicsBodies",
  "InstanceCount", "DrawCalls", "LuaMemoryKB", …), `SetCustomStat(name, value)`
- Log capture: `MessageOut: Signal<string, Enum.LogLevel>`

**`ScriptService`** — the mount point for entry scripts (§3): every
`src/scripts/**/*.luau` file becomes a `Script` child of it, with
subdirectories as `Folder`s. No properties or methods of its own in v1; the
tree *is* the API.

**`HotReloadService`** — dev builds only (§3).

**Reserved names, not present in v1** (do not squat these meanings):
`Players`, `NetworkService`, `ReplicationService`, `NavigationService`,
`Enum.RunContext` values `Client`/`Server`.

**Networking is not a service in v1**: `require("@std/net")` (§7) is the
socket/HTTP/WS surface, so backend code is portable to Lute verbatim
(ADR 0012).

### 2.2 Instance class hierarchy (v1 minimum)

```
Instance (abstract)
├─ DataModel (game)
├─ <all services above>
├─ Folder
├─ Script                      -- entry-point code (§3); there is NO ModuleScript class
├─ BasePart (abstract)         -- CFrame, Position, Orientation (degrees, YXZ), Size,
│  │                           -- Anchored, CanCollide, CanQuery, Transparency, Color,
│  │                           -- Material, CollisionGroup, Friction, Restitution, Density,
│  │                           -- LinearVelocity/AngularVelocity (read), ApplyImpulse(v),
│  │                           -- Touched/TouchEnded signals
│  ├─ Part                     -- Shape: Enum.PartShape (Block/Ball/Cylinder/Capsule/Wedge)
│  ├─ MeshPart                 -- MeshContent: Content, CollisionFidelity: Enum.CollisionFidelity
│  └─ CharacterBody            -- Jolt character controller (capsule): Move(direction: vector),
│                              -- Jump(), WalkSpeed, JumpSpeed, MaxSlopeAngle, AutoStepHeight,
│                              -- Grounded (read), State: Enum.CharacterState, Landed signal
├─ Model                       -- PrimaryPart, GetPivot()/PivotTo(cf), GetExtentsSize(),
│                              -- StreamingMode
├─ Attachment                  -- CFrame (relative to parent BasePart), WorldCFrame (read)
├─ Camera                      -- CFrame, FieldOfView, NearPlane, FarPlane, ViewportSize (read),
│                              -- WorldToViewportPoint(), ViewportPointToRay(). No CameraType.
├─ PointLight / SpotLight      -- child of BasePart/Attachment (the Roblox attach model),
│                              -- Color, Brightness, Range, (Spot: Angle), Shadows: boolean
├─ Sky                         -- under Lighting
├─ Sound                       -- Content, Playing, Looped, Volume, PlaybackSpeed, TimePosition,
│  │                           -- RollOffMinDistance/MaxDistance (3D iff parented to a BasePart),
│  │                           -- Play()/Pause()/Stop(), Ended/Loaded signals, Group: AudioGroup?
│  └─ AudioGroup               -- mixing bus: Volume
├─ AnimationPlayer             -- under a Model/MeshPart with a skinned mesh (ships in M6):
│  │                           -- LoadAnimation(content) → AnimationTrack
│  └─ AnimationTrack (non-Instance handle) -- Play(fadeTime?), Stop(fadeTime?), Looped,
│                              -- Speed, Weight, IsPlaying, Ended signal
│                              -- v1 scope: glTF clip playback + linear blending; no state
│                              -- machines, no IK (roadmap M6)
├─ InputContext / InputAction / InputBinding   (§2.4)
└─ UI classes:
   ScreenGui                   -- Enabled, DisplayOrder, ScreenInsets
   └─ UIObject (abstract)      -- Position/Size: UDim2, AnchorPoint: Vector2, Rotation,
      │                        -- BackgroundColor: Color3, BackgroundTransparency, Visible,
      │                        -- ZIndex, LayoutOrder, AutomaticSize, ClipsDescendants,
      │                        -- AbsolutePosition/AbsoluteSize (read),
      │                        -- Activated, PointerEntered, PointerExited signals
      ├─ Frame
      ├─ TextLabel             -- Text, TextColor, TextSize, Font: Content,
      │                        -- HorizontalAlignment, VerticalAlignment, TextWrapped, TextScaled
      │                        -- (no RichText in v1)
      ├─ TextButton
      ├─ TextInput             -- Text, PlaceholderText, Focused/FocusLost signals
      ├─ ImageLabel            -- Image: Content, ImageColor, ScaleType, SliceCenter: Rect
      ├─ ImageButton
      ├─ ScrollFrame           -- CanvasSize, CanvasPosition, ScrollBarThickness
      └─ modifiers: UIListLayout (FillDirection, Padding: UDim, HorizontalAlignment,
                    VerticalAlignment, SortOrder, Wraps), UIPadding, UICorner
```

Clay is the internal layout solver behind UDim2 + UIListLayout +
AutomaticSize; it is never exposed. **Not in v1 (documented honestly):**
Terrain, ParticleEmitter, constraints beyond the character controller,
SurfaceGui/billboards, RichText, video.

**`Instance` base members:** `Name`, `Parent`, `ClassName` (read-only); tree:
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
order — ADR 0026).

### 2.3 Datatypes

| Type | Shape |
|---|---|
| `Vector3` | IS the native `vector` primitive (ADR 0013). Canonical fields **`x` `y` `z` (lowercase)** — they belong to the VM primitive, not to us (§9). The `Vector3` global is LuauG's and is therefore fully PascalCase: `Vector3.New` (constant-folded via the vectorCtor/vectorLib compile options), `Vector3.Create`, `Vector3.Magnitude`, `Vector3.Normalize`, `Vector3.Dot`, `Vector3.Cross`, `Vector3.Angle`, `Vector3.Lerp`, `Vector3.Floor`, `Vector3.Ceil`, `Vector3.Abs`, `Vector3.Sign`, `Vector3.Clamp`, `Vector3.Min`, `Vector3.Max`, `Vector3.Zero`, `Vector3.One`. Convenience metatable methods `:Dot`, `:Cross`, `:Lerp`, `:Angle` and `.Magnitude`/`.Unit` via `__index` (documented as slower; prefer the library form). The official lowercase `vector` stdlib remains available beside it, unchanged, for code that must also run under Lute. Precision: f32 — exact to ~±131 km; `CFrame` is the f64 source of truth (below). |
| `Vector2` | Engine userdata: `X`, `Y`, `Magnitude`, `Unit`, `:Dot`, `:Lerp`; `Vector2.New(x, y)`, `.Zero`, `.One`. |
| `CFrame` | Carries the **f64 translation** (the world-precision source of truth; ADR 0014). `CFrame.New(x?, y?, z?)`, `CFrame.LookAt(pos, target, up?)`, `CFrame.FromEuler(rx, ry, rz, order: Enum.RotationOrder? = YXZ)` (the ONE euler constructor), `CFrame.FromAxisAngle(axis, angle)`, `CFrame.FromQuaternion(pos, qx, qy, qz, qw)`, `CFrame.FromMatrix(pos, r, u, b)`, `CFrame.Identity`. Props: `Position` (Vector3, f32 convenience), `Rotation`, `RightVector`, `UpVector`, `LookVector` (−Z). Methods: `Inverse`, `Lerp`, `Orthonormalize`, `ToWorldSpace`, `ToObjectSpace`, `PointToWorldSpace`, `PointToObjectSpace`, `VectorToWorldSpace`, `VectorToObjectSpace`, `ToEuler(order?)`, `ToAxisAngle`, `ToQuaternion`. Operators: `CFrame * CFrame`, `CFrame * vector`. Guideline: gameplay math beyond ±131 km must go through CFrame (f64-preserving), not Position. |
| `Color3` | `Color3.New(r, g, b)` 0–1, `FromRGB`, `FromHSV`, `FromHex("#rrggbb")`; `R G B`; `:Lerp`, `:ToHSV`, `:ToHex`. No BrickColor. |
| `UDim` / `UDim2` | `UDim.New(scale, offset)`; `UDim2.New(xs, xo, ys, yo)`, `UDim2.FromScale`, `UDim2.FromOffset`; `X: UDim`, `Y: UDim`. |
| `Rect` | `Rect.New(min: Vector2, max: Vector2)`; `Min`, `Max`, `Width`, `Height`. |
| `TweenInfo` | `TweenInfo.New(time, easingStyle?, easingDirection?, repeatCount?, reverses?, delayTime?)` — enum params also accept string literals ("Quad") via typed unions. |
| `Signal<T...>` / `Connection` | THE signal types (never "RBXScriptSignal"). `Signal:Connect(fn) → Connection`, `:Once(fn)`, `:Wait() → T...`; `Connection:Disconnect()`, `.Connected`. Deferred-only (ADR 0015), ordering per §3.1. User-creatable: `Signal.New()` with `:Fire(...)`, `:Destroy()` — replaces BindableEvent/BindableFunction. `ConnectParallel` reserved, not in v1. |
| `RaycastParams` / `RaycastResult` | `RaycastParams.New { Filter = {Instance}, FilterType = Enum.RaycastFilterType.Exclude, CollisionGroup = "Default" }` (table constructor); result: `Instance`, `Position`, `Normal`, `Distance`, `Material`. |
| `Random` | `Random.New(seed?)`: `NextNumber(min?, max?)`, `NextInteger(min, max)`, `NextUnitVector()`, `Clone()`. Deterministic streams (R10). |
| `Content` | A type alias of `string` in v1 (`asset://…`, `save://…` URIs); reserved to become opaque later. |
| `Enum` | Global `Enum` namespace; `EnumItem` = `Name`, `Value`, `EnumType`; `Enum.X:GetEnumItems()`. v1 enums: `EasingStyle` (Linear, Sine, Quad, Cubic, Quart, Quint, Exponential, Circular, Back, Bounce, Elastic), `EasingDirection`, `KeyCode` (keys + mouse + gamepad buttons), `InputActionType` (Bool, Direction1D, Direction2D, Direction3D, ViewportPosition), `InputDeviceType` (KeyboardMouse, Gamepad, Touch), `PartShape`, `Material` (small v1 set), `CollisionFidelity` (Default, Hull, Box, Precise), `RotationOrder`, `RaycastFilterType` (Include, Exclude), `StreamingMode` (Default, Atomic, Persistent), `PlaybackState`, `CharacterState` (Grounded, Airborne), `AutomaticSize`, `FillDirection`, `HorizontalAlignment`, `VerticalAlignment`, `SortOrder`, `ScaleType` (Stretch, Slice, Tile), `WindowMode`, `LogLevel`, `RunContext` (reserved values Client, Server). |

### 2.4 Input Action System (the only input path — ADR 0029)

- `InputContext : Instance` — `Enabled: boolean`, `Priority: number`,
  `Sink: boolean`; children are InputActions. Parented anywhere (convention:
  under the Script that owns it).
- `InputAction : Instance` — `Type: Enum.InputActionType`,
  `Enabled: boolean`; `GetState() → boolean | number | Vector2 | vector`;
  signals `Pressed`, `Released` (Bool), `StateChanged(newValue)` (all types).
- `InputBinding : Instance` (child of an InputAction) —
  `KeyCode: Enum.KeyCode?`, composites `Up/Down/Left/Right: Enum.KeyCode?`
  (Direction2D), `Scale: number`, `UIButton: ImageButton | TextButton?`,
  `DisplayName: string` (a localization key is allowed), `Image: Content?`,
  `DeviceType: Enum.InputDeviceType?` (per-device preferred binding).
- `InputAction:GetPreferredBinding(deviceType?) → InputBinding?` for prompt
  glyphs ("Press [E]").

### 2.5 Deliberate divergences vs Roblox (the migration guide reuses this table)

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
| 26 | `Instance.new`, `CFrame.new`, `Color3.fromRGB`, `CFrame.identity` | `Instance.New`, `CFrame.New`, `Color3.FromRGB`, `CFrame.Identity` | One casing rule for everything LuauG defines, constructors and constants included (§9, ADR 0034). Roblox's lowercase `new` is a Lua-era inheritance, not a design |

This rename list is **frozen**: no further renames without a new row here, and
no runtime aliases, ever.

### 2.6 Prefabs — reusable pre-built instance trees

LuauG supports the Unity-prefab / Roblox-model workflow — authoring an
instance tree once (with children, properties, attributes, tags) and
instantiating it many times — through three composable mechanisms:

1. **`Instance:Clone()`** (M2): any instance tree can be kept as a template
   (parented to `nil` or a storage Folder) and deep-cloned on demand, with
   internal references fixed up. This is the classic Roblox pattern and works
   for trees built in code or loaded from assets.
2. **`AssetService:LoadModelAsync("asset://…")`** (M4+): loads an imported
   asset — a glTF hierarchy or a compiled prefab — as a ready `Model` tree
   (MeshParts, lights, attachments as children). Load once, then `Clone()`
   per instance; `PreloadAsync` warms it.
3. **`.prefab.luau` — the v1 authoring format**: a `--!strict` Luau module
   returning a declarative, typed tree description (class, properties,
   attributes, tags, children — validated against the same API schema as
   everything else). The asset pipeline compiles these to `PrefabDef` assets:
   instantiable at runtime through `AssetService`, and bakeable into streamed
   chunk payloads by the world importer (architecture §10).

   ```luau
   --!strict
   return prefab.define "Tree" {
       class = "Model",
       children = {
           trunk = { class = "MeshPart", MeshContent = "asset://models/trunk.glb",
                     Anchored = true, tags = { "Climbable" } },
           leaves = { class = "MeshPart", MeshContent = "asset://models/leaves.glb" },
       },
   }
   ```

   Being plain Luau, prefabs are code-first (fits the no-editor v1), diffable,
   and parameterizable. When the visual editor arrives (phase 2), "save as
   prefab" writes exactly this format — the Unity-style drag-and-save flow
   lands on an already-shipping foundation. A general *scene serialization*
   format beyond prefabs (whole-world save files) remains deliberately
   deferred (risk §10.6): glTF + prefabs + spawner code cover v1.

---

## 3. Script execution model (v1)

**One process, one game VM** (architecture §5). Model: **entry scripts + file
modules**, no client/server folders yet.

- **Mounting:** at boot, every `src/scripts/**/*.luau` file becomes a `Script`
  instance under `game:GetService("ScriptService")` (subdirectories become
  `Folder`s). Scripts have `Enabled: boolean` and `RunContext` (reserved enum;
  unset in v1). Modules (everything else, canonically `src/shared/`) never
  appear in the tree — they are required by string. Dynamic script creation
  (`Instance.new("Script")` + source) is not supported in v1.
- **Lifecycle:** engine init → load `luaug.toml` + asset manifest → build the
  DataModel + mount scripts → start each Script on its own coroutine via
  `task.defer` in deterministic path-sorted order → first frame.
  `game.Loaded: Signal` fires after all entry scripts have had their first
  resumption; `game:BindToClose(fn)` (with a capped timeout) and
  `game:Shutdown()` for exit.
- **The `script` global** = the running `Script` instance (Name/Parent/
  attributes work; use attributes for per-script config).
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
position in the drain, returning that fire's arguments.

**Re-entrancy is capped at 10.** Every fire carries a depth: fires raised
outside any handler have depth 0, and a fire raised by a handler running at
depth *d* has depth *d*+1. A fire that would exceed depth 10 is **dropped** and
logs `script.err.reentrancy_limit`. This bounds signal loops (A fires B fires A)
deterministically; it is a generation depth, not a call-stack depth, so it does
not fire spuriously on a wide fan-out.

**Errors are contained.** Each handler runs on its own coroutine; an error in
one handler does not stop the other handlers of that fire, does not stop the
drain, and does not stop the script that fired. It goes to the console and
`DebugService.MessageOut` with its traceback.

**`Destroy` and queued fires.** `Destroy` enqueues `Destroying`, then closes the
instance's other signals — queued fires for them find no live connections and
invoke nothing. `Parent` locks to nil, children are destroyed recursively, and
the instance handle stops resolving at the end of the drain in which
`Destroying` fired (divergence #25). `GetChildren` and `GetDescendants` return
fresh arrays, so destroying during iteration over one is safe; the array may
simply contain instances that are gone by the time you reach them.

**Arguments are captured, not copied.** Tables and instances are passed by
reference: mutating a table between `:Fire(t)` and the drain is visible to the
handlers. Pass values you do not intend to change.

### 3.2 `task`

The whole scheduling surface; there are no legacy globals (divergence #2).

| Call | Semantics |
|---|---|
| `task.spawn(fn, ...)` | Resumes `fn` on a new coroutine **immediately**, synchronously, before returning. The one non-deferred call, and deliberately so. |
| `task.defer(fn, ...)` | Enqueues `fn` on the deferred queue (§3.1) — it runs at the next resumption point, ordered against signal fires by when it was raised. |
| `task.delay(duration, fn, ...)` | Resumes `fn` at the first task-resume phase at or after `duration` seconds of **SimClock** time. |
| `task.wait(duration?)` | Yields; resumes at the first task-resume phase at or after `duration` seconds of SimClock time (default: the next tick). Returns the elapsed sim time, which is a whole number of ticks and therefore ≥ `duration`. |
| `task.cancel(thread)` | Cancels a pending resumption. Cancelling a thread that is not scheduled is an error. |

Timers run on the SimClock, not the wall clock: `task.wait(1)` is exactly 60
ticks at the default 1/60 timestep, on every machine and every run. That is
what makes recorded replays reproduce (ADR 0025), and it is why there is no
`tick()`.

**Hot reload (`luaug dev` — ADR 0024):**
- **Code change → fast world restart:** tear down the game VM, rebuild the
  DataModel, re-run scripts. GPU resources, imported assets, streamed chunks,
  and the window survive → target **< 500 ms** (a hard perf requirement).
- **State bag:** `HotReloadService` (dev-only): `BeforeReload: Signal`,
  `AfterReload: Signal`, `SaveState(key: string, value: any)` (json-able or
  buffer), `LoadState(key) → any?`, `IsReload: boolean`. The engine
  auto-preserves `Workspace.CurrentCamera.CFrame` and any instance tagged
  `"PreserveOnReload"` (so the character just stays put in the demo).
- **Asset change → in-place swap:** textures/meshes/audio hot-swap without a
  VM restart (content-hash change pushed over the dev WebSocket).
- Transport: the dev server (Lute, `@lute/fs.watch` + `@std/net` WebSocket)
  pushes `{script-changed | asset-changed | eval}` messages to the runtime on
  a localhost port; `eval` powers the dev console in the overlay.

---

## 4. User project anatomy

**Config format: `luaug.toml`** (consistent with rokit.toml/pesde.toml;
comments; static). Sections: `[project]` name, id (reverse-DNS), version,
`engine = "0.1"`; `[window]` title, size; `[dev]` port; `[assets]` extra
source dirs, import options; `[permissions]` net_serve, fs_paths (§7);
`[memory]` optional script-heap hard cap and budget overrides; `[build]`
targets, bytecode opt level.

**`luaug new my-game` template:**
```
my-game/
├─ luaug.toml
├─ .luaurc                  -- strict mode + @shared/@pkg aliases (§1.3)
├─ rokit.toml               -- luaug, lute, luau-lsp, stylua pinned
├─ pesde.toml               -- empty deps; `luaug add` manages it
├─ stylua.toml
├─ .vscode/
│  ├─ settings.json         -- luau-lsp: platform.type=standard (Roblox defs OFF),
│  │                        -- types.definitionFiles=[.luaug/types/engine.d.luau],
│  │                        -- types.documentationFiles=[.luaug/types/engine-docs.json],
│  │                        -- require.directoryAliases for @std/ and @luaug/ stubs
│  └─ extensions.json       -- johnnymorganz.luau-lsp, stylua
├─ src/
│  ├─ scripts/main.luau     -- entry Script (--!strict)
│  └─ shared/               -- modules, require("@shared/...")
├─ assets/
│  ├─ models/  textures/  audio/  prefabs/
│  └─ i18n/en.json
├─ tests/example.test.luau
├─ .luaug/                  -- generated (gitignored): types/, cache/, manifest
└─ .gitignore
```

**CLI command set** (the `luaug` CLI is a Lute app compiled with
`lute compile`; it launches the separate native runtime binary):

| Command | Wraps |
|---|---|
| `luaug new [template]` | scaffold (`starter`, `obby`, `openworld-demo`) |
| `luaug dev` | asset watcher/importer (@lute/fs.watch) + WS hot-reload server (@std/net) + runtime in dev mode w/ overlay |
| `luaug run` | runtime, no watch |
| `luaug build --target win64` | Luau bytecode compile (O2), content-addressed asset pack, single-folder/exe output |
| `luaug asset import\|list\|hash` | assimp offline → glTF 2.0 canonical → runtime formats (engine mesh, KTX2/BCn textures, ogg/wav); crypto-digest cache in `.luaug/cache` |
| `luaug test [--engine]` | `lute test` for pure `tests/**/*.test.luau`; `--engine` boots the headless runtime exposing the same @std/test-compatible runner with the engine API available |
| `luaug check` | `luau-lsp analyze` with the generated settings/defs (CI-ready) + StyLua check + i18n lint |
| `luaug fmt` | StyLua |
| `luaug setup` | regenerate `.luaug/types/` + `.vscode` config for the pinned engine version (the `lute setup` pattern) |
| `luaug add <pkg>` | pesde wrapper (installs to `luau_packages`, maintains aliases) |
| `luaug doctor` | toolchain/version diagnosis |

Zero-config onboarding = `luaug new` + open VS Code: defs, docs, aliases,
formatter all preconfigured; nothing to install manually beyond
`rokit install`.

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

**Generated artifacts per engine release** (all diff-checked in CI):
1. `.luaug/types/engine.d.luau` — `declare extern type` for every
   class/datatype + global declarations (`game`, `workspace`, `script`,
   `Instance.new` string-singleton overloads). Never `declare class`.
2. `.luaug/types/engine-docs.json` — the luau-lsp documentation file
   (hover/completion docs).
3. `.luaug/types/std/**` and `.luaug/types/luaug/**` — typed stub modules for
   `@std`/`@luaug` (editor resolution via `require.directoryAliases`).
4. `api-dump.json` — versioned, machine-readable; CI diffs it to force
   changelog entries and catch accidental API breaks.
5. `docs/reference/**` — markdown reference pages.

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
- **In-game `LocalizationService`:** `Locale: string` (BCP-47, settable),
  `SystemLocale: string` (read), `LocaleChanged: Signal<string>`,
  `Translate(key: string, params: {[string]: any}?) → string` (a missing key
  echoes the key + a dev log), `LoadCatalog(locale: string, content: Content)`
  (DLC/mods). v1 stops here; ICU-grade formatting, dates, and gender are
  reserved extensions. `TextLabel.Text` accepts plain strings only in v1 —
  auto-localized UI is future work; `InputBinding.DisplayName` accepts a key.

---

## 7. @std implementation scope for v1 (ADR 0030)

| Module | Game runtime? | v1 notes / sandbox |
|---|---|---|
| `@std/task` | Yes — IS the global `task` | spawn/defer/delay/wait/cancel; synchronize/desynchronize reserved |
| `@std/json` | Yes | encode/decode |
| `@std/path` | Yes | pure |
| `@std/stringext`, `@std/tableext` | Yes | pure |
| `@std/net` | Yes | `request` (HTTP client) + WS client: always available. `serve` (HTTP+WS server) and raw sockets: dev mode always; shipped builds require `[permissions] net_serve = true` in luaug.toml |
| `@std/fs` | Yes, virtualized | Paths are URI-rooted: `asset://` (read-only mounted content) and `save://` (per-user writable dir) in shipped builds. Dev mode: project-root read/write. Raw OS paths only behind an `--allow-fs` dev flag / `[permissions] fs_paths` |
| `@std/test` | Shim only | The real runner is Lute's; engine headless mode (`luaug test --engine`) provides a compatible surface so the same `*.test.luau` files run with the engine API |
| `@std/io` | No (game VM) | Headless/dev console only |
| `@std/process`, `@std/luau` | **Tooling-only** | Never in the game VM (security; `@std/luau` = a loadstring-equivalent) |

A shared **conformance test suite runs against both Lute and the LuauG
runtime in CI** — the insurance policy on the convergence bet.

---

## 8. Examples & docs plan

**Templates/examples (each a working `luaug new` target):**
1. `starter` — the §4 tree; one script spawning a lit part, one shared
   module, one test.
2. `obby` — teaches the idiom set: parts via code, checkpoints via Tags +
   `TagService.GetInstanceAddedSignal`, respawn via `CharacterBody`, jump via
   an IAS `InputAction`, TweenService platforms, `Signal.new` for game
   events, a ScreenGui HUD, localized strings, a prefab.
3. `openworld-demo` (flagship) — chunked glTF terrain under
   `StreamingService` with the character as focus; third-person
   `CharacterBody` + a `@luaug/camera` rig; IAS contexts (`gameplay` vs
   `menu`, Sink/Priority shown); day/night driving `Lighting.ClockTime` in
   `PostSimulation`; ambient `Sound` + `AudioGroup` mixing; ImGui
   streaming/stats panels; the hot-reload workflow with `"PreserveOnReload"`
   on the character; an optional `backend/` Lute app sharing `@shared` code
   over `@std/net` WS.

**Docs site outline:** Learn (Install & toolchain → Your first world →
Scripting model → Instances/Attributes/Tags → Prefabs → Input (IAS) → UI →
Audio → Physics & CharacterBody → Streaming large worlds → Shipping a build)
· Guides (Hot reload deep-dive, Assets & the glTF pipeline, Testing, i18n,
Building a backend with Lute, the Debug overlay) · Reference (generated from
the api-dump) · **"Coming from Roblox"** — the keystone doc
([`coming-from-roblox.md`](coming-from-roblox.md)).

---

## 9. Naming & style conventions

**The rule, in one sentence: everything LuauG defines is PascalCase; the Luau
language's own libraries and the `@std` convergence surface keep the spelling
their ecosystem gave them.** (ADR 0034.)

- **Everything LuauG defines — Instances, services, datatypes, and the
  `@luaug/*` libraries — is PascalCase.** Classes, properties, methods, events,
  constructors, static factories, and constants alike: `Instance.New`,
  `CFrame.FromEuler`, `Color3.FromRGB`, `CFrame.Identity`, `Vector3.Zero`,
  `require("@luaug/testing").Describe`. Methods are called with `:`.
  Services end in `Service` except `Workspace` and `Lighting`. Yielding methods
  end in `Async`. Events are past-tense facts (`Landed`, `Ended`, `ChildAdded`)
  or `Pre*/Post*` phases. Boolean properties have no `Is` prefix (`Anchored`,
  `Enabled`); boolean methods do (`IsA`, `IsPaused`). No abbreviations except
  UI. There are no camelCase aliases and never will be.
- **Two surfaces keep their ecosystem spelling, because they are not ours to
  rename.** The Luau language's own libraries — `task`, `vector`, `buffer`,
  `string`, `table`, `math`, `coroutine`, `utf8`, `os` — are the language, and
  `@std/*` exists precisely so that utility code runs unchanged on Lute and
  Roblox (ADR 0030); PascalCasing either would break the thing it is for.
  `task` sits in both camps and stays lowercase because `@std/task` *is* the
  global `task` (§7).
- **Consequently the native `vector` fields stay `x`/`y`/`z`** (divergence #9):
  they are fields of a VM primitive, like `string`. `Vector3` — LuauG's global —
  is fully PascalCase (`Vector3.New`, `Vector3.Magnitude`, `Vector3.Zero`), and
  the lowercase `vector` library remains available beside it, unchanged, for
  code that must also run under Lute.
- **Identifier casing inside Luau files**, engine runtime, tooling, examples,
  templates and specs alike:
  - **PascalCase** for anything declared **outside** a function scope — module
    locals, module-level functions, exported members, and types.
  - **camelCase** for locals and inner functions **inside** a function scope.
  - **Constants are PascalCase too, with no underscores.** There is no
    `SCREAMING_SNAKE_CASE` in this codebase; a constant is not a different kind
    of name, only a different kind of value.

  The rule reads off the page: indentation and casing agree, so a
  PascalCase identifier at a glance is file-scope and a camelCase one is local.
- **User code:** `--!strict` everywhere (also `.luaurc` languageMode strict),
  StyLua formatting, luau-lsp lints on.
- **Enforcement:** the generator's schema validator encodes every rule above
  as CI-failing lints on the IDL (regex + structural checks: `Async` iff
  yields, the event tense list, no `Get` prefix on properties, singular enum
  names); `luaug check` + `luaug fmt --check` gate user/example code; all
  engine examples and templates are analyzed in CI under the pinned luau-lsp.

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
   afterthought; wrap divergences behind our stubs.
4. **No ModuleScript breaks the "model with scripts inside" habit.**
   **Rec:** the asset importer strips embedded scripts with a loud, keyed
   warning; the migration guide gets a dedicated recipe (the spawner-module +
   prefab pattern).
5. **IAS is new even to Roblox devs (2026).** **Rec:** the obby template and
   five copy-paste IAS recipes in the migration guide are launch blockers.
6. **No general scene serialization format** (code-only worlds strain as
   content grows). **Rec:** deferred by design in v1 — glTF + `.prefab.luau`
   + spawner modules cover it (§2.6); prototype a broader scene format in
   1.x; never invent a format under deadline.
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
