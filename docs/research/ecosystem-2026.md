---
title: "Luau Game Engine — Prior Art & Native Core Library Research"
captured: 2026-08-19
covers: "Prior art (Luau engines); Roblox 2026 API conventions worth mirroring (IAS, deferred events, streaming, server authority); native library survey (windowing, GPU, physics, audio, assets, UI, nav, net, math) with licenses and platform notes"
confidence: "Verified against upstream repos/docs where stated; version/date precision items and untestable claims listed in the UNCONFIRMED section and ../UNCONFIRMED.md. NOTE: this report's original math recommendation (LUA_VECTOR_SIZE=4) was superseded by the Luau report's analysis — the accepted decision is LUA_VECTOR_SIZE=3 f32 (ADR 0013)."
rule: "FROZEN SNAPSHOT — never edit the body; corrections go in a dated Addendum section at the end."
---

# Luau Game Engine — Prior Art & Native Core Library Research (as of August 2026)

---

# PART A — Prior art & the Roblox model

## A.1 — Existing open-source Luau engines/frameworks

**Bottom line: nothing mature exists. The niche is effectively open.** Every Roblox-like Luau engine I found is either a demo, archived, or unverifiable. That's both the opportunity and the warning.

### LunarEngine (formerly Librebox) — closest prior art, apparently stalled
- Org: https://github.com/lunarengine · HN: https://news.ycombinator.com/item?id=44995147 · Coverage: https://biggo.com/news/202508231913_Librebox_Engine_Breaks_Roblox_Platform_Lock
- C++ engine, **raylib** renderer, replicating Roblox's *public API only* (the authors explicitly state no Roblox source or assets used — worth copying that legal posture verbatim).
- Front-paged HN in Aug 2025. Demo had rendering, lighting, camera, and Luau scripting — **no physics, no networking, no input**.
- The `lunar-test-v1` repo ("(OLD) LunarEngine – Open-source Luau 3D engine (API-compatible)", C++) was **archived November 2025**. My direct fetch of the org page 404'd; status is unclear but does not look healthy.
- **Lesson:** the Roblox API veneer over Luau is the *cheap* part. Getting to a demo took one person weeks. Physics + replication + input + streaming is where projects die. Budget accordingly and build those first, not last.

### clawblox — Rust, different motivation, same shape
- https://github.com/nacloos/clawblox
- Rust core, Three.js rendering, **Rapier3D** physics, SQLite-backed `DataStoreService`, Roblox-compatible Luau API (WIP). ~586 commits, near-zero stars.
- Aimed at generating 3D multiplayer environments for **embodied AI agents**, not for shipping games. Interesting independent validation that "Roblox-like API on an open stack" is a shape people keep reaching for.

### vengine — toy
- https://github.com/valad47/vengine — C/C++, Luau + raylib. 35 commits, 1 star, Linux-only, self-described as lacking functionality. Reference value only.

### Lute — official Luau standalone runtime (genuinely useful)
- https://github.com/luau-lang/lute · https://lute.luau.org/ · HN: https://news.ycombinator.com/item?id=47870245
- Official `luau-lang` org project. "Node.js/Deno, but for Luau." Nightly releases through at least **v1.0.1-nightly.20260714** (July 2026), tracking Luau 0.729.
- Built-in APIs: filesystem, HTTP networking, crypto, process management, tty — **plus exposed APIs for manipulating Luau's syntax tree**, which is how you'd build codemods, custom linters and editor tooling.
- **This is the reference for the non-Roblox Luau stdlib question.** You will need to decide what `fs`/`net`/`process` look like outside a sandbox; Lute has already made those decisions with upstream blessing. Consider vendoring or matching its API rather than inventing.

### Lune — the older standalone Luau runtime
- https://github.com/lune-org — predates Lute, more mature ecosystem, community-driven. Worth reading for the same reasons.

### luau-lsp + Rojo/sourcemaps — do not skip this
- https://github.com/JohnnyMorganz/luau-lsp
- Resolves **DataModel instance trees via Rojo-style sourcemaps** to drive IntelliSense. If your engine emits (a) a sourcemap mapping files→instance paths and (b) an API-dump-equivalent type definition file, you get a working, well-tested LSP for near-free and inherit the entire Roblox-developer muscle memory.
- Similarly, study Rojo's filesystem↔DataModel projection format and Wally (package manager). Solving "text files in git ↔ a live instance tree" is one of Roblox's genuinely unsolved problems and a place you can beat them outright.
- *(LuauG note: with real filesystem modules, luau-lsp's custom-platform mode makes sourcemaps unnecessary — the final path is definition files + docs JSON + `platform.type`; see api-design.)*

### GDLuau — Luau in Godot
- https://github.com/Manonox/GDLuau — Luau C/C++ bindings for Godot. Useful as an embedding reference.

### ⚠️ "Luau Engine" / luauengine.org — CANNOT VERIFY, treat with suspicion
- https://luauengine.org/ claims to be "a free and open-source fork of **Roblox Studio**," "**Licensed and official by Roblox Corporation**," permissive license, with stability tiers: Stable (Roblox, Windows, macOS, Linux), Beta (WebAssembly, dedicated server), Experimental (Android, iOS).
- **Red flags I found:** the only repo I could locate, https://github.com/Luau-Engine/LuauEngine, is **the documentation website itself** — MIT, ~3 commits, ~7 stars, JavaScript/Markdown/CSS. No engine source. Roblox's own OSS announcement of April 9, 2026 ([devforum](https://devforum.roblox.com/t/evolving-luau-oss-community-contributions-more/4566806)) — which details the Signals library, rocale-cli, Wally publishing, and community contribution governance — **makes no mention of any standalone engine or Studio fork**.
- Do not plan around this until you can read actual engine source. See UNCONFIRMED section.

### Context: Luau outside Roblox is a proven bet
Luau (MIT, https://github.com/luau-lang/luau, ~0.729 as of mid-2026) ships in **Alan Wake 2, Farming Simulator 25, Second Life, and Warframe**. Runtime needs C++11; compiler/analysis need C++17. Native codegen is compiled into OSS binaries by default (`--codegen`). Embedding it in a C++ engine is well-trodden.

---

## A.2 — Roblox API conventions a clone-like engine must provide

### DataModel & Services
The root object is the `DataModel` (globally `game`), and all engine subsystems hang off it as singleton child instances retrieved via `game:GetService("Name")` — which lazily creates the service if absent, so it never returns nil. Services double as **containers with replication semantics baked into identity**: `ReplicatedStorage`/`ReplicatedFirst` replicate to clients, `ServerStorage`/`ServerScriptService` never do, `Workspace` is the 3D scene and streams, `StarterGui`/`StarterPack`/`StarterPlayer` are templates copied into players on spawn. Your engine must make "which service is it under" the *primary* declaration of replication and execution context — that single design choice is most of why Roblox feels coherent to beginners.
Ref: https://create.roblox.com/docs/scripting/services

### The Instance tree
Everything is an `Instance` with `Name`, `ClassName`, `Parent`, and `Archivable`; hierarchy is defined solely by setting `Parent`, and `Instance.new("Class")` creates detached objects that become live the moment they're parented. Core surface: `Clone`, `Destroy`, `FindFirstChild(name, recursive)`, `FindFirstChildOfClass`, `FindFirstAncestor`, `GetChildren`, `GetDescendants`, `IsA` (class-hierarchy-aware), `IsAncestorOf`/`IsDescendantOf`, `WaitForChild(name, timeout)`; events `ChildAdded`/`ChildRemoved`, `DescendantAdded`/`DescendantRemoving`, `AncestryChanged`, `AttributeChanged`, `Destroying`, and `Changed`/`GetPropertyChangedSignal(prop)`. **New since my training data:** `QueryDescendants` (selector-based search) and a styled-properties system (`GetStyled`, `GetStyledPropertyChangedSignal`, `StyledPropertiesChanged`) — a CSS-like cascade over instance properties.
Ref: https://create.roblox.com/docs/reference/engine/classes/Instance

### Attributes & Tags (CollectionService)
Attributes are named, typed, **replicated, serialized** key/value pairs on any instance (`SetAttribute`/`GetAttribute`/`GetAttributes`/`GetAttributeChangedSignal`) — user-defined properties without subclassing, and they show up in the Properties panel. Tags are string labels managed via `CollectionService` (or the `Instance:AddTag/RemoveTag/HasTag/GetTags` shortcuts) plus `GetTagged`, `GetAllTags`, and crucially `GetInstanceAddedSignal(tag)`/`GetInstanceRemovedSignal(tag)` — which give you retroactive-and-future iteration over a set, the standard "poor man's ECS" pattern. **New:** `CreateCollection` for query-based collections; the legacy `GetCollection`/`ItemAdded`/`ItemRemoved` are deprecated. Ship attributes + tags on day one; they're how real Roblox codebases avoid inheritance hell.
Ref: https://create.roblox.com/docs/reference/engine/classes/CollectionService

### Events (RBXScriptSignal)
Every event is an `RBXScriptSignal` supporting `:Connect(fn) -> RBXScriptConnection`, `:Once(fn)` (auto-disconnects after first fire), `:Wait()` (yields the calling coroutine, returns the event args), and `:ConnectParallel(fn)` (handler runs in the parallel phase; the script must be under an `Actor`). Connections expose `:Disconnect()` and `.Connected`. Handlers each run on their own coroutine so a yield or error in one never blocks others — this is non-negotiable for the "beginner writes `wait()` inside a `.Touched` handler" experience.
Ref: https://create.roblox.com/docs/reference/engine/datatypes/RBXScriptSignal

### Deferred event semantics — build this in from the start
Roblox has an engine-wide `Workspace.SignalBehavior` of `Immediate` vs `Deferred`. Under **Deferred**, firing a signal does not call handlers inline; handlers are queued and drained at the next **resumption point**, with a re-entrancy depth limit of **10**. The documented resumption points, in frame order, are: **input processing (once per input) → `RunService.PreRender` → legacy waiting-script resumption (`wait`/`spawn`/`delay`) → `RunService.PreAnimation` → `RunService.PreSimulation` → `RunService.PostSimulation` → waiting-script resumption (`task.wait`/`task.spawn`/`task.delay`) → `RunService.Heartbeat` → `DataModel.BindToClose`.** Roblox is migrating `Default` from Immediate to Deferred and new places already default to Deferred. **Recommendation: implement Deferred only, with no Immediate mode.** Roblox is stuck supporting both for compatibility; you are not, and Immediate-mode reentrancy is a permanent source of engine bugs.
Ref: https://create.roblox.com/docs/scripting/events/deferred

### Frame pipeline & RunService
The resumption-point list above *is* the authoritative frame ordering. The `RunService` events, with their modern names (legacy aliases in parens): `PreRender` (`RenderStepped`) — client-only, before rendering, only place safe to set `Camera.CFrame`; `PreAnimation` — before animation evaluation; `PreSimulation` (`Stepped`) — before physics, and documented as **the last Luau event fired before `Motor6D.Transform` is applied to part positions**; `PostSimulation` (`Heartbeat`) — after physics. Also `BindToRenderStep(name, priority, fn)`/`UnbindFromRenderStep(name)` for priority-ordered render callbacks (via `Enum.RenderPriority`), and the context predicates `IsClient`/`IsServer`/`IsStudio`/`IsRunning`/`IsEdit`. **New:** `BindToSimulation()`, `SetPredictionMode()`, `GetPredictionStatus()`, and `Rollback`/`Misprediction` events (see server authority, below).
Ref: https://create.roblox.com/docs/studio/microprofiler/task-scheduler · https://create.roblox.com/docs/reference/engine/classes/RunService

### task library
`task.spawn(fn, ...)` resumes immediately on a new coroutine; `task.defer(fn, ...)` queues to the **next resumption point** (this is the key difference — `spawn` is synchronous-now, `defer` is end-of-phase); `task.delay(t, fn, ...)` queues after `t` seconds; `task.wait(t)` yields the current coroutine and returns actual elapsed time; `task.cancel(thread)` kills a scheduled thread; `task.synchronize()`/`task.desynchronize()` move between serial and parallel phases. The legacy globals `wait`/`spawn`/`delay` resume at a *different, earlier* point and are throttled — **do not implement them at all** in a new engine; they exist in Roblox purely as technical debt.
Ref: https://create.roblox.com/docs/reference/engine/libraries/task

### Parallel Luau (Actors & SharedTable)
`Actor` instances are units of execution isolation; a script belongs to its closest ancestor `Actor`, scripts in the *same* actor always run serially, so parallelism requires *many* actors. Code enters the parallel phase via `task.desynchronize()` or `signal:ConnectParallel()` (the latter is more efficient), and must call `task.synchronize()` before mutating instances. The API surface is annotated with thread-safety levels — **Unsafe**, **Read Parallel**, **Local Safe**, **Safe** — and `require()` is illegal in a desynchronized phase. `SharedTable` is a table-like structure readable/writable atomically across actors with immediate visibility (no copying), the intended channel for cross-actor state. **Design note:** the annotate-every-property-with-a-safety-level model is what makes this tractable; bake thread-safety metadata into your API definition format from day one rather than retrofitting.
Ref: https://create.roblox.com/docs/scripting/multithreading

### Streaming (large worlds)
`Workspace.StreamingEnabled` (now default-on) dynamically loads/unloads `Workspace` descendants around one or more **replication foci** (`Player.ReplicationFocus`, `Player:AddReplicationFocus()`; defaults to the character's `PrimaryPart`); nothing outside `Workspace` ever streams. Tunables: `StreamingMinRadius` (default 64, never streams out) and `StreamingTargetRadius` (default 1024); `StreamOutBehavior` = `LowMemory` (default, only sheds under pressure) or `Opportunistic` (aggressive, big memory win); `StreamingIntegrityMode` = `PauseOutsideLoadedArea` (recommended — pauses gameplay rather than letting players fall through unloaded ground, surfaced as `Player.GameplayPaused`). Per-model granularity via `ModelStreamingMode`: `Nonatomic` (default), `Atomic` (all descendants arrive together), `Persistent` (never streams out, guaranteed before `Workspace.PersistentLoaded`), `PersistentPerPlayer` (`Model:AddPersistentPlayer()`). **Critical semantic:** streamed-out instances are **reparented to nil, not destroyed**, so Luau references stay valid and reconnect if the content returns — but client-local property changes are lost.
Ref: https://create.roblox.com/docs/workspace/streaming

### TweenService
`TweenService:Create(instance, TweenInfo.new(time, easingStyle, easingDirection, repeatCount, reverses, delayTime), {Prop = targetValue, ...})` returns a `Tween` with `Play`/`Pause`/`Cancel`, a `Completed` signal carrying a `PlaybackState`, and a `PlaybackState` property. `TweenService:GetValue(alpha, style, direction)` exposes the raw easing curves for manual interpolation. Tweens interpolate any numerically-interpolable property type (numbers, `Vector2/3`, `UDim/UDim2`, `CFrame`, `Color3`, `Rect`). It's a small feature that carries enormous weight in perceived polish — implement it early, and make the easing enum set identical so tutorials transfer.
Ref: https://create.roblox.com/docs/reference/engine/classes/TweenService

### Input — the new Input Action System supersedes ContextActionService
**This is the biggest Part-A finding and post-dates my training data.** Roblox's **Input Action System (IAS)** reached **full release** in 2026 and explicitly supersedes both `ContextActionService` and `UserInputService`. It is a three-instance, data-driven model: **`InputAction`** declares a semantic action with a `Type` of `Bool`, `Direction1D`, `Direction2D`, `Direction3D`, or `ViewportPosition`, firing `Pressed`/`Released` (Bool only) and `StateChanged` (all types, returning `bool`/scalar/`Vector2`/`Vector3` as appropriate); **`InputBinding`** children map hardware to that action via `KeyCode`, composite `Up`/`Down`/`Left`/`Right`/`Forward`/`Backward` for directional actions, `UIButton` for touch, plus `Scale`, `DisplayName`, `DisplayImage`; **`InputContext`** groups actions with `Enabled`, `Priority`, and `Sink` (consumes bound KeyCodes at its priority, blocking lower contexts). `InputAction.PreferredBinding` auto-resolves to the binding matching the player's *current* device, so on-screen hints update live when a player picks up a gamepad mid-session.
**Do not clone `ContextActionService`.** Clone IAS. It is a strictly better design, it's what new Roblox content targets, and for an engine that must span desktop + mobile + console it is exactly the right abstraction.
Ref: https://create.roblox.com/docs/input/input-action-system · https://devforum.roblox.com/t/full-release-input-action-system-ias-newly-converted-player-scripts/4678416

### Remotes & the replication model
Server↔client crossings are explicit instances: `RemoteEvent` (async one-way — `FireServer` → `OnServerEvent(player, ...)`; `FireClient(player,...)`/`FireAllClients(...)` → `OnClientEvent(...)`), `UnreliableRemoteEvent` (unordered, unreliable, for high-frequency state), and `RemoteFunction` (`InvokeServer`/`OnServerInvoke` yields for a return value; `InvokeClient` exists but is documented as carrying "serious risks" — errors propagate and a malicious/disconnecting client can yield you forever). Marshalling has hard rules worth copying exactly: **functions become nil, metatables are stripped, non-string table keys are coerced to strings, mixed array/dictionary tables fail, tables are deep-copied (identity is not preserved), and non-replicated instances arrive as nil.** Alongside remotes, the *implicit* replication of property/instance changes under replicating services is the real workhorse — the server mutates the DataModel and clients receive it automatically, with network-ownership assignment deciding who simulates a given assembly.
Ref: https://create.roblox.com/docs/scripting/events/remote

### ⭐ Server authority + client prediction/rollback — major 2026 addition
Roblox shipped a **server authority model** with full latency compensation. Opting in requires `Workspace.AuthorityMode = Server` and `NextGenerationReplication` enabled (six Workspace properties in total). The client runs the same core simulation as the server via **`RunService:BindToSimulation()`** (logic bound here runs identically on both sides so it can be deterministically re-run), predicts several frames ahead of the server, and on **`Misprediction`** snaps to the authoritative state and **`Rollback`**s + resimulates the buffered input frames. **`RunService:SetPredictionMode(instance, mode)`** with `Enum.PredictionMode.Automatic` (default) lets the engine decide per-instance whether to roll back and resimulate. Predicted gameplay state (health, ammo) is carried on **attributes**, and player input flows through the **Input Action System** rather than RemoteEvents.
**Design implication:** this is Roblox admitting that the classic network-ownership + RemoteEvent model can't produce competitive-feeling gameplay. If you're greenfielding a Roblox-like engine in 2026, architect for deterministic fixed-tick simulation + input buffering + rollback **from the first commit**. Retrofitting it is exactly what forced Roblox into an entire parallel replication stack.
Ref: https://create.roblox.com/docs/projects/server-authority

---

# PART B — Native library candidates

License key: ✅ permissive · ⚠️ caution · ❌ avoid for this project

## B.1 Windowing / input

| Lib | Latest | License | Mobile | Console | Verdict |
|---|---|---|---|---|---|
| **SDL3** | 3.4.x (3.4.0 = **Jan 1, 2026**; patches through ~3.4.14 mid-2026) | ✅ zlib | ✅ iOS + Android first-class | ✅ **NDA console forks of SDL** (all three platforms) | **Pick this** |
| GLFW | **3.5.1, July 31, 2026** (3.5 skipped — bad git tag) | ✅ zlib/libpng | ❌ **none** | ❌ none | Disqualified |

- SDL3 has been stable since Jan 2025 — over 18 months of production hardening. 3.4 highlights: improved interop between the GPU API and the 2D renderer, better Emscripten support, native PNG loading, improved pen handling, KMS/DRM atomic, new Steam Controller support.
- SDL gives you window + input + gamepad + haptics + audio + threads + filesystem + clipboard + IME + the **console porting path** in one dependency. GLFW gives you a window on desktop. For a mobile-required, console-ready engine there is no real contest.
- Sources: https://github.com/libsdl-org/SDL/releases · https://discourse.libsdl.org/t/announcing-sdl-3-4-0/65438 · https://www.glfw.org/version-3.5.1-released.html · https://www.glfw.org/version-3.5-skipped.html

## B.2 GPU / renderer abstraction

| Lib | Latest | License | Backends | Mobile | Console |
|---|---|---|---|---|---|
| **SDL3 GPU** | ships in SDL 3.4.x | ✅ zlib | **Vulkan, Metal, D3D12** only | Metal ✅ / Android "limited support" ⚠️ | ✅ NDA SDL console forks |
| **bgfx** | 1.146.x, commits ~daily | ✅ BSD-2 | D3D11/12, Vulkan, Metal, GL 2.1+, GLES 2.0+, WebGPU (2nd-take backend, in progress since Jan 2026) | ✅ ✅ proven iOS + Android | ✅ **NVN stub (Switch) + GNM (PS4/PS5)** for licensed devs via DevNet |
| Diligent Engine | (version unconfirmed) | ✅ Apache-2.0 | D3D11/12, Vulkan, Metal, GL/GLES, WebGPU | ✅ Android + iOS | ⚠️ not public |
| wgpu-native | **v27.0.4.1, Apr 10, 2026** | ✅ MIT/Apache-2.0 | Vulkan, Metal, D3D12, GL/GLES | ✅ | ❌ none |
| sokol_gfx | rolling | ✅ zlib/libpng | GL/GLES3, D3D11, Metal, WebGPU (+compute) | ✅ | ❌ none |
| NVRHI | rolling | ✅ MIT | D3D11, D3D12, Vulkan 1.2 | ❌ **no Metal, no mobile** | ❌ |

**SDL3 GPU — current state.** It has **compute** (`SDL_BeginGPUComputePass` with compute-writable buffers/textures). It **does not** have bindless, raytracing, or mesh shaders, and SDL's FAQ is explicit that these are "definitely not near future" items and directs you to raw Vulkan if you need them. Each backend needs a different shader format; the answer is **SDL_shadercross** (HLSL → SPIR-V / MSL / DXIL, runtime or build-time). It's a much smaller, lower-level API than bgfx — "an order of magnitude less code," exposing command buffers directly — and explicitly not designed to compete in AAA+. FNA has shipped console titles on it.
Sources: https://wiki.libsdl.org/SDL3/FAQDevelopment · https://wiki.libsdl.org/SDL3/CategoryGPU · https://moonside.games/posts/introducing-sdl-shadercross/ · https://github.com/libsdl-org/SDL/issues/11478

**bgfx** remains the strongest *console + legacy-Android* story among permissive options and is very actively maintained. Its GLES 2.0 backend is the realistic floor for the long tail of cheap Android devices — which matters a lot for a Roblox-like engine, since that's Roblox's actual audience.
Sources: https://github.com/bkaradzic/bgfx · https://bkaradzic.github.io/posts/webgpu/ · https://github.com/bkaradzic/bgfx/issues/1949 · https://github.com/bkaradzic/bgfx/issues/1486

**wgpu-native** caveat: it "does not yet implement the stable version" of `webgpu.h`, so the C ABI is still moving under you; plus a Rust toolchain in your build and no console path. **NVRHI** is disqualified outright (no Metal, no mobile). **Diligent** is genuinely attractive — Apache-2.0, every backend including WebGPU, iOS + Android, and HLSL everywhere — but see UNCONFIRMED regarding its Metal backend licensing.

## B.3 Physics

**Jolt Physics 5.6.0 — released July 11, 2026 — ✅ MIT — the clear 3D default.**
- Ships in **Horizon Forbidden West** and **Death Stranding 2**; is the default 3D physics engine in **Godot 4.4+**.
- 5.6 adds: **GPU strand-hair simulation** (Cosserat rods, experimental — hair-vs-hair + environment collision, skinning), **compute shader support for D3D12/Vulkan/Metal**, a new friction model **50% faster / 40% less memory**, and up to **40% perf / 70% memory** reductions in some scenes.
- Dropped in 5.6: UWP, ARM32, pre-VS2022 / pre-Clang-16 / pre-GCC-12.
- Multicore-friendly, deterministic, zero external deps, proven on mobile and console.
- https://github.com/jrouwe/JoltPhysics · https://gamefromscratch.com/jolt-physics-5-6-released/

**⭐ Box3D v0.1.0 — June 30, 2026 — ✅ MIT — watch closely, don't ship on it yet.**
- Erin Catto (author of Box2D). Portable **C17**, data-oriented, near-identical architecture to Box2D. ~6.1k stars almost immediately.
- Features: Soft Step rigid body solver, sub-stepping, **wide SIMD contact solver**, graph coloring for large islands, continuous collision, convex hulls/capsules/spheres/triangle meshes/heightfields, joints (revolute, prismatic, distance, motor, weld, wheel) with limits/motors/springs/friction, character mover, contact events, filtering, raycasts, **large world support with double-precision positioning**, **cross-platform determinism**, and **recording/replay**.
- Catto explicitly considered forking Jolt and chose not to. He calls it **alpha**, needing more testing and docs.
- **Why you care specifically:** large-world double-precision + cross-platform determinism + record/replay is *exactly* the feature triple a huge-open-world, rollback-netcode, Roblox-like engine needs. Put it behind your physics abstraction as a second backend now so you can switch when it hits 1.0.
- https://github.com/erincatto/box3d · https://box2d.org/posts/2026/06/announcing-box3d/

**Box2D v3.1.1 (June 4, 2025) — ✅ MIT — 2D default.** The C rewrite is done and settled: v3.0 (Aug 2024) was the ground-up C++→**C17** port with a breaking API change; v3.1 (Apr 2025) fixed rewrite fallout, improved distance/wheel joints, and added **SSE2/NEON SIMD**. Caveat: Catto's attention is now on Box3D, so expect slower 2D cadence. https://github.com/erincatto/box2d/releases · https://box2d.org/documentation/md_migration.html

**Rapier 0.20.0 (~Aug 2026) — ✅ Apache-2.0 — Rust.** Excellent engineering; 2026 roadmap is robotics accuracy + **cross-platform GPU rigid-body physics via rust-gpu**. But it's Rust in a C++ core: FFI boundary, cbindgen, a second toolchain in your console/mobile builds. Not worth it when Jolt exists. https://dimforge.com/blog/2026/01/09/the-year-2025-in-dimforge/

**Bullet3 — ✅ zlib — effectively legacy.** Stable release still **3.2.4, April 25, 2022**. Repo alive but game-side development has moved on. Don't start here. https://github.com/bulletphysics/bullet3

**PhysX 5 — ✅ BSD-3-Clause — genuinely fully open now.** NVIDIA open-sourced PhysX *and Flow* under BSD-3, **including the GPU/CUDA source** (previously proprietary). Documented stable is **5.5.0 (Dec 13, 2024)**. It's the physics engine of NVIDIA Omniverse and a reference implementation of OpenUSD Physics. Downsides: very large codebase, GPU acceleration is CUDA/NVIDIA-only (useless on mobile/console/AMD), and heavier integration cost than Jolt for no gameplay benefit. https://github.com/NVIDIA-Omniverse/PhysX · https://developer.nvidia.com/blog/open-source-simulation-expands-with-nvidia-physx-5-release

## B.4 Audio

- **miniaudio 0.11.25 (March 3, 2026)** — ✅ **public domain (Unlicense) OR MIT-0**, your choice. Single-file C, zero dependencies. Backends cover WASAPI/DirectSound/WinMM, Core Audio (macOS **and iOS**), ALSA/PulseAudio/JACK, **AAudio + OpenSL ES on Android**, Web Audio. Includes WAV/FLAC/MP3 decoding, resampling, a node graph, and 3D spatialization. **The default.** https://miniaud.io/ · https://github.com/mackron/miniaudio
- **SoLoud** — ✅ zlib/libpng. Nice API, good feature set (filters, speech synth, 3D audio), backend-agnostic. But upstream maintenance has been sporadic for years — flag as a project-risk if you make it the core. https://github.com/jarikomppa/soloud
- **OpenAL Soft** — ❌ **LGPL-2.1.** This is a real problem: LGPL requires dynamic linking or shipping relinkable object files, which conflicts with **iOS and console static-link-only** distribution. Avoid as the engine default.
- **FMOD** — ⚠️ commercial. Free "Indie" tier reported at **<$200k/yr revenue and <$500k budget**; ~**$2k one-time** for the Indie license tier. Closed source, per-title, attribution required, console builds need separately-licensed platform SDKs.
- **Wwise** — ⚠️ commercial. Free below a reported **$250k development budget**; ~**$7k** Pro. Closed source, per-title.
- **Both FMOD and Wwise are worth supporting as optional plugins later** (pro audio teams expect them) but neither can be the default for a permissively-licensed engine. Verify current pricing directly at fmod.com/licensing and audiokinetic.com — the figures come from secondary sources.

## B.5 Asset import / processing

- **cgltf** — ✅ MIT, single-header C, glTF 2.0 read + write. Tiny, no deps, extremely widely used. Great runtime loader.
- **fastgltf 0.9.x** — ✅ MIT, C++17, SIMD-accelerated, lazy-by-default ("you don't pay for what you don't use"). Benchmarks: **~7.4× faster than cgltf** on 2CylinderEngine, **~5× faster** on Bistro; ~24× faster than tinygltf+RapidJSON. Pick this over cgltf if you're already C++17 and care about load times. https://fastgltf.readthedocs.io/latest/overview.html
- **assimp v6.0.x** (6.0.2 June 2025; **6.0.5 reported April 30, 2026**) — ✅ BSD-3-Clause. 40+ formats. **Use it in your offline importer CLI only — never link it into the runtime.** It's a large dependency with a broad attack surface and a history of parser CVEs. https://github.com/assimp/assimp/releases
- **meshoptimizer v1.2** (v1.0 was the first stable, **Dec 9, 2025**) — ✅ MIT. Vertex cache / overdraw / vertex fetch optimization, simplification (LODs), **meshlet generation**, vertex+index compression codecs, and the `gltfpack` CLI. **Essential, not optional** — this is your LOD + streaming pipeline. https://github.com/zeux/meshoptimizer · https://zeux.io/2025/12/09/meshoptimizer-v1/
- **basis_universal v2.5x** — ✅ Apache-2.0. Transcodable LDR **and HDR** GPU textures in a **KTX2** container; one asset transcodes to BC1-7 / ETC1-2 / ASTC / PVRTC at load. **XUBC7 shipped in v2.50 on July 1, 2026.** ⚠️ Note the **XUASTC LDR supercompressed format was still not standardized as of late Feb 2026** — stay on standardized UASTC/ETC1S for anything you need portable. https://github.com/BinomialLLC/basis_universal
- **KTX-Software (Khronos)** — ✅ Apache-2.0. `libktx` for KTX2 container read/transcode; the pairing partner for basis. https://github.com/KhronosGroup/KTX-Software
- **stb** — ✅ public domain / MIT. `stb_image`, `stb_image_write`, `stb_truetype`, `stb_vorbis`, `stb_rect_pack`, `stb_ds`. Always useful. https://github.com/nothings/stb

## B.6 UI

- **Dear ImGui v1.92.x** — ✅ MIT. **Docking branch is still NOT merged into master** and this hasn't changed: ocornut is unhappy with the code and wants to rewrite it a third time; too many open issues lack tractable fixes. Since July 2023 docking gets parallel tags (`v1.92.6-docking`) and is kept synced with master, so it's *usable and maintained*, just permanently branch-resident. **v1.92 (June 2025) was a major font-system rewrite**: dynamic glyph loading + on-demand font sizing via `ImFontBaked`, requiring backends to support `ImGuiBackendFlags_HasTextures` — a big win for CJK/icon fonts (no more prebuilding every glyph). **Use for editor + debug UI only.** https://github.com/ocornut/imgui/releases · https://github.com/ocornut/imgui/wiki/Docking · https://github.com/ocornut/imgui/issues/4881
- **RmlUi** — ✅ MIT. HTML/CSS-subset UI with its own layout engine and few dependencies; a maintained evolution of libRocket. Has Lua bindings (Lua 5.x — you'd need to adapt them to Luau). Reasonable if you want CSS-style authoring, but it pulls you toward a *retained DOM* that is a different mental model from Roblox's instance tree. https://github.com/mikke89/RmlUi
- **Clay** — ✅ zlib. Single-header C, flexbox-style **layout only**, microsecond performance, **renderer-agnostic** (emits a command list you rasterize yourself). **This is the best fit:** keep `ScreenGui`/`Frame`/`TextLabel`/`UIListLayout`/`UIGridLayout` as real Instances with `UDim2` scale+offset (that's the Roblox DX), and use Clay as the layout solver underneath. You get the API you want without writing a constraint solver. https://github.com/nicbarker/clay · https://www.nicbarker.com/clay
- **Luau-friendly in-game UI:** nothing off-the-shelf. Roblox's model is a retained instance tree + declarative layout/constraint objects, and the community layers React-like frameworks (Roact/React-Lua, Fusion) on top. Expose the instance tree from C++ and let userland ship the reactive layer — do not build a React into the engine.

## B.7 Navigation / AI

**Recast & Detour** — ✅ **zlib**. The maintained canonical fork is the org repo **https://github.com/recastnavigation/recastnavigation** (moved from memononen's original), **actively maintained — updated Feb 27, 2026**. 1.6.x line, backwards-compatible with 1.x. Powers navmesh generation and pathfinding in **Unity, Unreal, Godot, O3DE**, and countless shipped titles. There is no serious permissive competitor. Note it releases infrequently by tag; most consumers track `main`.

## B.8 Networking

- **Valve GameNetworkingSockets** (~1.6.x) — ✅ BSD-3-Clause. Reliable + unreliable messages over UDP, robust fragmentation/reassembly, **AES-GCM-256 per-packet encryption with Curve25519** key exchange and cert signatures, congestion control and key derivation modeled on **Google QUIC**, reliability layer from DCCP's ack-vector model (RFC 4340 §11.4), P2P/NAT traversal. **Has shipped on consoles, mobile platforms, and non-Steam stores**, and is used for cross-platform connectivity. Dependencies: OpenSSL or libsodium, plus protobuf. **Best fit for a server-authoritative Roblox-like engine** — encryption and console/mobile shipping history are exactly what you need and exactly what you don't want to write. https://github.com/ValveSoftware/GameNetworkingSockets
- **ENet** — ✅ MIT. Tiny, robust, boring in the best way; reliable/unreliable channels over UDP. **No built-in encryption.** Widely reported as easier to integrate than GNS. `zpl-c/enet` is the modernized fork. Good as a LAN/dev-loopback transport and a fallback. http://enet.bespin.org/
- **yojimbo** — ✅ BSD-3. Glenn Fiedler's dedicated-server client/server library: connect tokens/auth, libsodium encryption, bitpacked serialization, designed around competitive FPS requirements. ⚠️ Upstream (`mas-bandwidth/yojimbo`) is low-activity; community forks exist (TeamHypersomnia). Its ideas are more valuable than its code at this point.
- **QUIC** — growing interest, not yet a game-networking default. The appeal is real: TLS 1.3, mature congestion control, connection migration (great for mobile network handoff), and **unreliable datagrams (RFC 9221)** for gameplay traffic — all specified and battle-tested rather than hand-rolled. Practical implementations: **msquic** (MIT, Microsoft, C, production scale) is the C option; **quinn** (Rust) is the most-cited in game contexts. **Recommendation: don't bet the engine on it in 2026, but keep your transport behind an interface so you can add a QUIC backend.** https://daposto.medium.com/quic-for-gamenetworking-46cf23936228

## B.9 Math — design around Luau's native `vector`

**The most important finding in this section is a design constraint, not a library.**

Luau's `vector` is a **primitive VM type**, not a userdata or table — it lives inline in the VM value, allocates nothing, and the native code generator has fast paths for it. It's 3 components (x,y,z) by default, and **4 components (x,y,z,w) if you build with `LUA_VECTOR_SIZE=4`**. There is now an official **`vector` standard library RFC** so runtimes stop each inventing incompatible vector libraries (Roblox and Lune previously diverged), and `buffer` read/write of whole vectors (`readvector`/`writevector`) is a designed operation.
Refs: https://rfcs.luau.org/vector-library.html · https://github.com/luau-lang/rfcs/blob/master/docs/vector-library.md · https://github.com/luau-lang/luau/discussions/1296 · https://create.roblox.com/docs/luau/native-code-gen

**Concrete recommendations:**
1. **Build Luau with `LUA_VECTOR_SIZE=4`** and make your engine `Vector3` *be* the Luau `vector` — not a userdata wrapper. *(SUPERSEDED: the Luau deep-dive report and ADR 0013 resolved this to `LUA_VECTOR_SIZE=3` f32 — 4-wide costs +33% memory per value and Roblox itself ships 3-wide. The "Vector3 IS the native vector" core of this recommendation stands.)*
2. **Implement the `vector` stdlib RFC** rather than inventing your own — free compatibility with Lute/Lune tooling and community code.
3. `CFrame`-equivalents, `Quaternion`, `Matrix4`, `UDim2`, `Color3` etc. still need to be userdata with metatables; keep them POD and trivially copyable.

**C++-side math library:**
- **glm** — ✅ MIT, header-only, ubiquitous. Downsides: heavy compile times, sprawling API, and its default layouts don't naturally line up with a 4-float Luau vector. Fine, not ideal.
- **HandmadeMath** — ✅ public domain/CC0, single-header C/C++, tiny, 2D/3D game-focused (vec/mat/quat). Excellent *reference implementation* to start from.
- **Eigen** — ⚠️ **MPL-2.0** (weak, file-level copyleft). Usually acceptable but it's not MIT/BSD, and it's a linear-algebra library, not a game math library. Skip.
- **DirectXMath** — ✅ MIT, excellent SIMD, but Windows/Xbox-centric.
- **Honest answer: hand-roll it.** ~1,000–1,500 lines of `Vec2/Vec3/Vec4/Quat/Mat4/Transform` that is *layout-identical* to Luau's `vector` and to your GPU constant buffers will save you more pain at the binding boundary than any third-party library saves you in authoring. This is one of the few places where NIH is correct, precisely because the binding layer is the hot path in a scripting-first engine.

---

# Recommended default stack

Optimized for: C++ core embedding Luau · mobile required · console-ready architecture · permissive licensing.

| Layer | Pick | License | Why |
|---|---|---|---|
| **Language/VM** | **Luau** (~0.729) + native codegen | MIT | The premise. Ships in AAA. C++11 runtime / C++17 compiler. |
| **Runtime stdlib model** | Follow **Lute** | MIT | Official, upstream-blessed answers for fs/net/process/crypto outside Roblox. |
| **Windowing/input/platform** | **SDL3** (3.4.x) | zlib | Only option with mobile + a real console porting path. |
| **RHI** | **Your own ~40-call interface.** Backend 1 = **SDL3 GPU**; Backend 2 = **bgfx** | zlib / BSD-2 | SDL3 GPU is zero marginal dependency (Vulkan/Metal/D3D12 + compute + NDA console forks + shadercross). bgfx is your escape hatch for **GLES 2.0 low-end Android** and the existing **NVN/GNM** console renderers. |
| **Shaders** | **SDL_shadercross** (HLSL → SPIR-V/MSL/DXIL) | zlib | One authoring language, all backends. |
| **Physics 3D** | **Jolt 5.6.0** | MIT | Only permissive engine with shipped AAA console + mobile record. Godot's default. |
| **Physics 3D (watch)** | **Box3D** | MIT | Large worlds w/ double precision + determinism + replay. Add as backend #2 now; promote at 1.0. |
| **Physics 2D** | **Box2D 3.1.1** | MIT | C17, SIMD, the standard. |
| **Audio** | **miniaudio 0.11.25** | Unlicense/MIT-0 | Single file, every platform incl. AAudio/OpenSL/CoreAudio-iOS, spatialization + node graph built in. |
| **Asset runtime** | **fastgltf** (or cgltf) + **meshoptimizer** + **basis_universal/KTX2** + **stb** | MIT / MIT / Apache-2.0 / PD | Fast load, LODs + meshlets, one transcodable texture per platform. |
| **Asset offline** | **assimp** in a CLI tool only | BSD-3 | Format long tail, kept out of the runtime binary. |
| **Editor/debug UI** | **Dear ImGui 1.92.x (docking tag)** | MIT | Branch state is stable-in-practice; new font system is a real win. |
| **In-game UI** | **Roblox-style retained Instance tree on top of Clay** | zlib | You get `Frame`/`TextLabel`/`UIListLayout`/`UDim2` DX without writing a layout solver. |
| **Navigation** | **Recast & Detour** (org fork) | zlib | No competitor. Maintained (Feb 2026). |
| **Networking** | **GameNetworkingSockets**, ENet as fallback | BSD-3 / MIT | Encryption + console/mobile shipping record; transport behind an interface so QUIC can slot in. |
| **Math** | **Hand-rolled, implementing the Luau vector RFC** | yours | Zero-alloc script vectors + SIMD/GPU-aligned C++ in one layout. *(Vector width: see ADR 0013 — 3-wide f32.)* |

**Everything above is MIT / BSD / zlib / Apache-2.0 / public domain. No GPL, no LGPL, no commercial dependency in the default path.**

### Six architectural notes that fall out of this research
1. **Design for rollback netcode from commit one.** Roblox's 2026 server-authority system is a retrofit that cost them an entire parallel replication stack. Deterministic fixed-tick sim + input buffering + resimulation is dramatically cheaper to build in than to add.
2. **Implement deferred signals only.** Skip Immediate mode and skip legacy `wait`/`spawn`/`delay`. Roblox carries both purely for compatibility you don't have.
3. **Clone the Input Action System, not ContextActionService.** It's the better design and it's what a desktop+mobile+console engine actually needs.
4. **iOS forbids JIT.** Luau's native codegen cannot ship on iOS App Store builds (and W^X is often restricted on consoles too). Your interpreter performance is your mobile performance — plan around it. *(Flagged as needing verification — see below.)*
5. **Annotate thread-safety in your API definition format from day one.** Parallel Luau's Unsafe/Read-Parallel/Local-Safe/Safe taxonomy is what makes multithreaded scripting tractable; retrofitting it across a large API surface is brutal.
6. **Emit Rojo-style sourcemaps + an API dump.** You get `luau-lsp` — a mature, well-tested language server — essentially for free, plus the entire Roblox developer ecosystem's tooling intuitions. *(LuauG note: superseded in detail — real filesystem modules + luau-lsp custom-platform mode need defs + docs JSON, not sourcemaps; the api-dump remains.)*

---

# UNCONFIRMED / COULD NOT VERIFY

**High importance:**
1. **luauengine.org's claim to be an "official, Roblox-licensed, open-source fork of Roblox Studio."** Could not substantiate. The only associated repo I found (`Luau-Engine/LuauEngine`) is the marketing/docs website itself — MIT, ~3 commits, ~7 stars, JS/Markdown/CSS, no engine source. Roblox's own April 9, 2026 Luau OSS announcement doesn't mention it. Note also that some of my search-summary snippets restating these claims appear to be echoing the site's own copy rather than independent reporting. **Verify by finding actual engine source before treating this as competition or as a base.**
2. **LunarEngine's current status.** Direct fetch of `github.com/lunarengine` returned 404; "archived November 2025" and the `lunar-test-v1` description come from search snippets only. Check manually.
3. **iOS/console JIT restrictions vs. Luau native codegen.** This is an inference from long-standing App Store policy, not something verified in 2026 sources. Confirm against Luau docs and Roblox's own iOS behavior — it materially affects the mobile performance budget.
4. **Diligent Engine's Metal backend licensing.** Historically the Metal backend required a separate commercial license from Diligent Graphics despite the Apache-2.0 core. Could not confirm whether this is still true in 2026. **Check before choosing Diligent** — it would negate its main advantage.

**Version/date precision:**
5. **SDL 3.4.x latest patch** — sources conflicted: "3.4.8, May 2, 2026" vs. "3.4.14, July 1, 2026" vs. "3.4.14 as of Aug 3, 2026." The 3.4.0 date (Jan 1, 2026) is solid.
6. **Jolt 5.6.0's exact date** — the releases page showed "July 11" without a year; 2026 inferred from GameFromScratch describing it as the first major update of 2026.
7. **Box3D's star count (~6.1k)** — from a page-summary, plausible given the announcement's reach but unverified.
8. **meshoptimizer v1.2's release date** — v1.0 (Dec 9, 2025) is confirmed; v1.2 as current is from npm/secondary sources.
9. **assimp 6.0.5 (Apr 30, 2026)** vs. documented stable 6.0.2 (Jun 8, 2025) — sources disagreed on which is current.
10. **Diligent Engine and Recast current version numbers** — neither surfaced a clear 2026 tag.
11. **PhysX** — 5.5.0 (Dec 13, 2024) is the documented stable; whether a 5.6 shipped in 2025–26 is unverified.
12. **bgfx 1.146.9292** is from the docs site header; bgfx doesn't do meaningful semver releases, so treat "current `master`" as the real answer.

**Claims that couldn't be tested:**
13. **Commercial console titles shipped on SDL3 GPU beyond FNA/MoonWorks-based ones** — no concrete examples found.
14. **FMOD/Wwise 2026 pricing tiers** — all figures are from secondary/comparison sites. Check fmod.com/licensing and audiokinetic.com directly.
15. **The exact Roblox frame-phase diagram** — the task-scheduler page's ordering lives in an SVG that couldn't be read, and the raw markdown 404'd. The frame ordering above is derived from the **deferred-events resumption point list**, which was retrieved verbatim and is authoritative for scheduling order. Note it reveals `Heartbeat` as a *separate, later* resumption point than `PostSimulation`, despite the two commonly being described as aliases — worth confirming before replicating.
16. **Whether SDL3 GPU's Android support has improved** since the "limited support on Android" note in the FAQ. Given mobile is a hard requirement, test this early on real low-end devices — it's the single biggest risk in the recommended stack.

---

## Sources

[luauengine.org](https://luauengine.org/) · [Luau-Engine/LuauEngine](https://github.com/Luau-Engine/LuauEngine) · [lunarengine org](https://github.com/lunarengine) · [LunarEngine HN](https://news.ycombinator.com/item?id=44995147) · [Librebox coverage](https://biggo.com/news/202508231913_Librebox_Engine_Breaks_Roblox_Platform_Lock) · [clawblox](https://github.com/nacloos/clawblox) · [vengine](https://github.com/valad47/vengine) · [luau-lang/luau](https://github.com/luau-lang/luau) · [luau-lang/lute](https://github.com/luau-lang/lute) · [lute.luau.org](https://lute.luau.org/) · [luau-lsp](https://github.com/JohnnyMorganz/luau-lsp) · [Evolving Luau OSS (Apr 2026)](https://devforum.roblox.com/t/evolving-luau-oss-community-contributions-more/4566806) · [Luau vector RFC](https://rfcs.luau.org/vector-library.html) · [Luau native codegen](https://create.roblox.com/docs/luau/native-code-gen) · [Roblox services](https://create.roblox.com/docs/scripting/services) · [Instance](https://create.roblox.com/docs/reference/engine/classes/Instance) · [CollectionService](https://create.roblox.com/docs/reference/engine/classes/CollectionService) · [RBXScriptSignal](https://create.roblox.com/docs/reference/engine/datatypes/RBXScriptSignal) · [Deferred events](https://create.roblox.com/docs/scripting/events/deferred) · [Task scheduler](https://create.roblox.com/docs/studio/microprofiler/task-scheduler) · [RunService](https://create.roblox.com/docs/reference/engine/classes/RunService) · [task library](https://create.roblox.com/docs/reference/engine/libraries/task) · [Multithreading](https://create.roblox.com/docs/scripting/multithreading) · [Streaming](https://create.roblox.com/docs/workspace/streaming) · [TweenService](https://create.roblox.com/docs/reference/engine/classes/TweenService) · [Input Action System](https://create.roblox.com/docs/input/input-action-system) · [IAS full release](https://devforum.roblox.com/t/full-release-input-action-system-ias-newly-converted-player-scripts/4678416) · [Remote events](https://create.roblox.com/docs/scripting/events/remote) · [Server authority](https://create.roblox.com/docs/projects/server-authority) · [SDL releases](https://github.com/libsdl-org/SDL/releases) · [SDL 3.4.0 announcement](https://discourse.libsdl.org/t/announcing-sdl-3-4-0/65438) · [SDL FAQ Development](https://wiki.libsdl.org/SDL3/FAQDevelopment) · [SDL3 GPU category](https://wiki.libsdl.org/SDL3/CategoryGPU) · [SDL GPU missing features](https://github.com/libsdl-org/SDL/issues/11478) · [SDL_shadercross](https://moonside.games/posts/introducing-sdl-shadercross/) · [GLFW 3.5.1](https://www.glfw.org/version-3.5.1-released.html) · [GLFW 3.5 skipped](https://www.glfw.org/version-3.5-skipped.html) · [bgfx](https://github.com/bkaradzic/bgfx) · [bgfx WebGPU](https://bkaradzic.github.io/posts/webgpu/) · [bgfx Switch](https://github.com/bkaradzic/bgfx/issues/1949) · [bgfx PlayStation](https://github.com/bkaradzic/bgfx/issues/1486) · [wgpu-native](https://github.com/gfx-rs/wgpu-native) · [webgpu-headers](https://github.com/webgpu-native/webgpu-headers) · [Diligent Engine](https://github.com/DiligentGraphics/DiligentEngine) · [NVRHI](https://github.com/NVIDIA-RTX/NVRHI) · [Jolt Physics](https://github.com/jrouwe/JoltPhysics) · [Jolt 5.6](https://gamefromscratch.com/jolt-physics-5-6-released/) · [Jolt release notes](https://jrouwe.github.io/JoltPhysics/md__docs_2_release_notes.html) · [Box3D](https://github.com/erincatto/box3d) · [Announcing Box3D](https://box2d.org/posts/2026/06/announcing-box3d/) · [Box2D releases](https://github.com/erincatto/box2d/releases) · [Box2D migration](https://box2d.org/documentation/md_migration.html) · [Rapier 2026 goals](https://dimforge.com/blog/2026/01/09/the-year-2025-in-dimforge/) · [bullet3](https://github.com/bulletphysics/bullet3) · [PhysX open source](https://developer.nvidia.com/blog/open-source-simulation-expands-with-nvidia-physx-5-release) · [PhysX docs](https://nvidia-omniverse.github.io/PhysX/physx/latest/) · [miniaudio](https://miniaud.io/) · [miniaudio releases](https://github.com/mackron/miniaudio/releases) · [fastgltf](https://fastgltf.readthedocs.io/latest/overview.html) · [assimp releases](https://github.com/assimp/assimp/releases) · [meshoptimizer](https://github.com/zeux/meshoptimizer) · [meshoptimizer 1.0](https://zeux.io/2025/12/09/meshoptimizer-v1/) · [basis_universal](https://binomialllc.github.io/basis_universal/) · [KTX-Software](https://github.com/KhronosGroup/KTX-Software/releases) · [stb](https://github.com/nothings/stb) · [Dear ImGui releases](https://github.com/ocornut/imgui/releases) · [ImGui Docking wiki](https://github.com/ocornut/imgui/wiki/Docking) · [ImGui docking release request](https://github.com/ocornut/imgui/issues/4881) · [RmlUi](https://github.com/mikke89/RmlUi) · [Clay](https://github.com/nicbarker/clay) · [Recast Navigation](https://github.com/recastnavigation/recastnavigation) · [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets) · [yojimbo](https://github.com/mas-bandwidth/yojimbo) · [QUIC for game networking](https://daposto.medium.com/quic-for-gamenetworking-46cf23936228)
