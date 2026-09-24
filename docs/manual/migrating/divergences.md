# Every deliberate divergence

The complete list of places where a familiar spelling is different here, with the
reason for each. **This list is frozen**: no further renames without a new row,
and no runtime aliases, ever.

An alias would be a second spelling to learn, a second thing to document, and a
permanent invitation to write the old one. The cost of not having them is paid
once, at migration.

## Scheduling and events

| Was | Is | Why |
|---|---|---|
| Immediate / Deferred signal modes | **Deferred only** | One semantics, predictable, and ready for parallel work later. |
| `wait`, `spawn`, `delay`, `tick` | **`task.*` and `os.clock`** | The footguns removed at birth rather than deprecated. |
| `RBXScriptSignal` / `RBXScriptConnection` | **`Signal<T...>` / `Connection`** | Generic-typed, and not somebody else's trademark. |
| `BindableEvent` / `BindableFunction` | **`Signal.new()` and plain functions** | Instances were the wrong shape for this. |
| `.Changed` catch-all | **`GetPropertyChangedSignal` / `AttributeChanged`** | The catch-all cannot be typed. |

## The tree

| Was | Is | Why |
|---|---|---|
| `Instance.new(class, parent)` | **Single argument** | The parent-then-mutate performance wart, removed. |
| `ModuleScript` + `require(instance)` | **Plain files, required by string** | Real modules the analyzer can follow; no wait-then-require. |
| A destroyed instance stays readable | **Handles stop resolving** after the drain in which `Destroying` fired | The slot is reclaimed; use-after-destroy is a keyed error rather than a silent read. |
| Deprecated camelCase aliases (`:connect`) | **Never existed** | One spelling. |
| Optional typing | **Fully strict, fully typed** | Non-negotiable quality bar. |

## Datatypes

| Was | Is | Why |
|---|---|---|
| `Vector3.X` / `.Y` / `.Z` | **`x` / `y` / `z`** | It *is* the language's native vector primitive. |
| `CFrame.Angles`, `fromOrientation`, `fromEulerAnglesXYZ` | **`CFrame.fromEuler(…, order?)`** | Three confusing spellings became one explicit one. |
| `BackgroundColor3`, `TextColor3` | **`BackgroundColor`, `TextColor`** | The `3` suffix was a historical artefact. |
| `BrickColor`, `Region3`, `BorderSizePixel` | **Absent** | Legacy warts. |
| Studs | **Metres, kilograms, seconds** | Native to glTF and native to physics. |

## The world

| Was | Is | Why |
|---|---|---|
| `Humanoid` + `HumanoidRootPart` | **`CharacterBody`**, a `BasePart` | One instance, a direct character controller. |
| `PhysicalProperties` and material-derived physics | **`Friction`, `Restitution`, `Density`** | Direct and typed, with no bundle object. |
| `CameraType` state machine | **A fully scriptable `Camera`**, plus rigs in `@luaug/camera` | No hidden controllers, and the near and far planes exposed. |
| `workspace.StreamingEnabled` | **`StreamingService`** | Streaming is a system, not scene-root state. |
| `BasePart.Color`, `BasePart.Transparency`, and `Material` as an enum of surface kinds | **`BasePart.Material`, a [material asset](manual:world/materials) the part wears**, and `part:SetMaterialParameter("Color", c)` | A surface is governed by its material, as in the engines this design follows (ADR 0090). The default material a plain part wears lets it change `Color` and `Transparency`, so a tint is one method call; `luaug migrate materials` converts a project's files. **This is the one row that removes a member scripts write constantly.** |

## Interface

| Was | Is | Why |
|---|---|---|
| `MouseEnter` / `MouseLeave`, `InputBegan` on a GUI | **`PointerEntered` / `PointerExited` / `Activated`** | Device-neutral: a click, a tap and a bound gamepad button are one event. |
| `TextXAlignment` / `TextYAlignment` | **`HorizontalAlignment` / `VerticalAlignment`** | One alignment vocabulary, shared with layout. |
| `UserInputService`, `ContextActionService`, `Mouse` | **The Input Action System** | One model, rebindable and promptable by default. |

## Audio and assets

| Was | Is | Why |
|---|---|---|
| `SoundService` / `SoundGroup` | **`AudioService` / `AudioGroup`** | One prefix across a family beats two. |
| `rbxassetid://` | **`asset://` project paths** | An open, local-first pipeline. |

## Networking

| Was | Is | Why |
|---|---|---|
| `RemoteEvent.OnServerEvent` / `OnClientEvent` | **`RemoteEvent.ServerReceived` / `ClientReceived`** | An event is named as a fact that happened, everywhere in this API. `FireServer`, `FireClient` and `FireAllClients` keep their names. |
| `RemoteFunction:InvokeServer` / `InvokeClient` | **`InvokeServerAsync`**, and no `InvokeClient` | A call that parks its caller says so in its name, everywhere in this API. The server never waits on a client, because a client that never answered would hold the server's script forever. |
| `ReplicatedStorage` / `ServerStorage` | **The same two**, saved with the scene | What they hold reaches every replica, or stays on the authority. Scripts are files rather than instances, so neither holds modules. |

## Naming

| Was | Is | Why |
|---|---|---|
| `AnimationTrack.IsPlaying` | **`AnimationTrack.Playing`** | A boolean *property* carries no `Is` prefix and a boolean *method* does. `Sound.Playing` was already spelled this way, and one engine cannot have both. |

## What the list is for

Two things.

**It is a checklist for porting.** Everything on it will produce an error rather
than silently doing something else, because none of the old spellings exist.

**It is a commitment.** A frozen list means the surface you learn is the surface
you keep. A rename after this point needs a new row here, and that is a
deliberately high bar.

## Where to look next

- [The migration guide](manual:migrating/migration) — the same ground, with code
- [What is not here](manual:migrating/not-here)
