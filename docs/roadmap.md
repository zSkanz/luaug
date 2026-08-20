# LuauG Roadmap — M0 → v1.0

This is the canonical build sequence for the autonomous agent. Milestone gates
are verbatim contracts: the agent may not weaken, skip, or reinterpret them.
Scope changes require human approval (see `MASTER_PROMPT.md` §10).

## Principles

- **Vertical slices, always runnable.** Every milestone ends in an
  `examples/NN-*` binary you can launch, plus an automated headless run of the
  same scene. "Cube falls onto ground, scripted in Luau" is the model.
- **The agent needs eyes before it needs features.** A headless
  `--headless --frames N --screenshot out.png --exit` harness ships in M1.
  An agent that cannot observe its own rendering output will ship
  green-but-broken visuals. Screenshot verification is mandatory for any change
  with visible output.
- **Kernel before renderer.** Instance-tree-over-ECS, deferred signals, `task`,
  and the fixed-tick scheduler land in M2 — everything else is a client of them.
  Hot reload lands immediately after (M3) so every later milestone is developed
  *with* the DX the engine sells.
- **Determinism is a test, not an aspiration.** The replay-hash harness exists
  from M2 and becomes a blocking CI gate at M5. The v1 guarantee (ADR 0025):
  same engine build + same platform + same initial state/seed + same tick
  configuration + same ordered inputs ⇒ same observable simulation result
  (WorldHash). Cross-platform hash equality is *recorded* in a non-blocking job
  but is NOT a v1 guarantee.
- **Render regression gate = command-stream capture.** The `rhi_capture`
  backend records a canonical, quantized command stream; its hash vs. golden is
  the deterministic, GPU-less blocking gate. Real golden images (lavapipe on
  Linux / WARP on Windows) run nightly, non-blocking. Screenshots remain the
  agent's self-verification tool from M1.
- **Performance gates are regression gates until M8.** Each milestone records
  baselines into `docs/perf-baselines.md` (fixed scenes, the recorded reference
  machine). Gate: no >10% regression vs. the previous milestone, plus
  scene-specific budgets. Absolute targets (60 fps @ 1080p on the reference
  machine) bind only at M8.
- **Platform tiers.** Tier-1: Windows x64 (the dev machine; all run /
  screenshot / perf gates execute here). Tier-2: Linux CI (must compile + pass
  headless logic tests every milestone; from M4, lavapipe golden images are
  attempted and promoted to blocking only after being stable for 2 consecutive
  milestones). Tier-3: macOS CI (must compile from M4; runtime verification is
  post-v1).
- **Early Android portability check (mobile itself stays post-v1).** From
  M1–M2, nightly CI gains a **non-blocking Android (NDK) cross-compile job** for
  `platform` + `rhi_api` + `rhi_sdlgpu` + the triangle sample — compile-only, it
  catches API/toolchain breaks early without a device. Validation on a real
  cheap Android device is a **human checkpoint** scheduled before the RHI
  interface freezes (end of M4): the agent escalates and asks the human to run
  the triangle APK; the agent does not hold phones.
- **Deliberate v1 de-scopes** (the agent cannot re-scope silently):
  Recast/Detour is vendored and the `nav` module seam exists, but navmesh
  *integration* and any NavigationService are post-v1 (ADR 0022). The 2D layer,
  mobile ports, the visual editor, and multiplayer/replication are post-v1
  phases. Low-level socket primitives are a small M7 scope bullet, not a
  milestone.

## Milestone summary

| ID | Name | Size | Runnable artifact |
|----|------|------|-------------------|
| M0 | Bootstrap and First Light | S (5%) | `luaug-host` runs `boot.luau`, prints via i18n catalog, exits 0 |
| M1 | Window, RHI, Frame Loop, Agent Eyes | M (10%) | `examples/00-clear`: pulsing clear + debug cubes driven by Luau; screenshot harness |
| M2 | Kernel: Instances/ECS, Scheduler, Signals, task | XL (18%) | `examples/01-instances`: 500 scripted spinning debug cubes, services, deferred signals |
| M3 | Tooling Loop: CLI, Hot Reload, Types, Tests | M (9%) | `luaug dev` — edit script, behavior changes <1 s; `luaug test` runs conformance suite |
| M4 | Seeing the World: Meshes, Materials, Lighting | L (13%) | `examples/02-meshes`: glTF scene, PBR, shadows, day/night slider |
| M5 | Feeling the World: Jolt + Character | L (12%) | `examples/03-physics-playground`: third-person capsule on ramps/stacks |
| M6 | Playing the World: Input Actions, UI, Tween, Audio, Minimal Animation | L (13%) | `examples/04-obby`: menus, HUD, checkpoints, tweens, sound, rebindable input, animated character |
| M7 | Scaling the World: Assets, Streaming, Floating Origin | L (13%) | `examples/05-streaming`: fly-cam over large chunked world, bounded memory |
| M8 | Flagship, Hardening, Docs, v1.0 | M (7%) | `examples/10-open-world` + tagged v1.0.0 release artifacts |

## Milestone detail

### M0 — Bootstrap and First Light (S)

- **Goal:** a green, pinned, CI-enforced repo where a C++ host embeds sandboxed
  Luau 0.734 and runs a script through the engine's logging/i18n seams.
- **Scope:** vendor all third-party dependencies into `third_party/` at the
  exact versions targeted in `third_party/manifest.json` (record real commit
  SHAs there; Luau, SDL3 and doctest first, others as needed by later
  milestones may be vendored lazily but the manifest rows must exist);
  activate `CMakePresets.json` (build dirs under `$env{LUAUG_BUILD_ROOT}`,
  never in-tree); `luaug-host` executable: create Luau VM, `luaL_sandbox`,
  register a minimal `print`/log bridge routed through the key+catalog i18n
  system (one English catalog proves the seam); rokit-pinned toolchain (Lute
  1.0.0, StyLua 2.5.2, luau-lsp 1.69.0); activate CI (Windows + Linux build,
  ctest, `luau-analyze` strict on all `.luau`, StyLua check, i18n lint stub,
  docs-lint).
- **Deliverable:** `luaug-host boot.luau` prints a catalog-resolved greeting and
  exits 0 on Windows and Linux.
- **Gate:** CI green on Tier-1/Tier-2; `ctest` includes at least: VM boots
  sandboxed (env mutation from script fails), script error surfaces as a
  structured engine error with an i18n key; `--version` prints the engine
  version, the pinned Luau version **and commit SHA** (generated at configure
  time from `third_party/manifest.json`, never typed into source), and the
  Luau ABI constants **read from the vendored headers**
  (`LBC_VERSION_TARGET`, `LBC_TYPE_VERSION_TARGET`, `LUA_VECTOR_SIZE`,
  `LUA_VECTOR_DOUBLE`), with a test asserting the latter match ADR 0013 —
  the grounding proof (amended by ADR 0031: Luau ships no version constant).

### M1 — Window, RHI, Frame Loop, Agent Eyes (M)

- **Goal:** pixels on screen through the RHI, and the self-observation harness
  every later gate depends on.
- **Scope:** SDL3 init/window/event pump; RHI v1 (`IRenderer`/`RenderWorld`
  contract per `docs/architecture.md`; ~40-call `rhi_api` with the SDL3 GPU
  backend); HLSL via SDL_shadercross with an on-disk shader cache;
  immediate-mode debug draw (lines, wire/solid cubes, text) — deliberate:
  M2/M3/M5 visualize through debug draw before the real renderer exists; ImGui
  docking overlay (F3); frame loop with fixed-tick accumulator (tick logic
  stubbed); **headless mode**: `--headless --frames N --screenshot path --exit`
  using an offscreen surface + readback, exercised in CI (WARP/software
  fallback on the Windows runner; if no GPU path works on CI the gate runs on
  the dev machine via a scripted local gate — recorded in the gate log either
  way); `rhi_capture` and `rhi_null` backends compile; nightly Android NDK
  cross-compile job added (non-blocking).
- **Deliverable:** `examples/00-clear` — clear color pulses and three debug
  cubes orbit, driven from a Luau script (via a temporary minimal binding that
  M2 replaces).
- **Gate:** Tier-1 runs windowed and headless; first golden capture-stream
  checked in (plus a reference screenshot with the tolerance comparator
  `tools/imgcmp`); GPU validation layer clean; Tier-2/Tier-3 compile.

### M2 — Kernel: Instances over ECS, Scheduler, Signals, task (XL — the most consequential milestone)

- **Goal:** the Roblox-shaped soul of the engine, implemented per
  `docs/architecture.md` and `docs/api-design.md`.
- **Scope:** ECS storage + component registry; Instance facade (`Instance.new`,
  properties, Parent/Children, `FindFirstChild` with duplicate-name support
  per ADR 0026, attributes, tags); DataModel + services skeleton
  (`game:GetService`); deferred-only signal implementation with documented
  ordering semantics; `task` library (`spawn/defer/delay/wait/cancel`) on the
  fixed-tick scheduler with documented resumption points; script host:
  lifecycle, `require` resolution per `.luaurc`, per-script sandboxing; seeded
  deterministic RNG service; world-state hash function + record/replay harness
  v1; frame-budget instrumentation; the 10k-parts/1k-listeners property-churn
  benchmark with a CI threshold.
- **Deliverable:** `examples/01-instances` — a Luau script builds 500
  instances, parents/reparents, connects deferred signals, animates via
  Heartbeat, all visualized with debug draw.
- **Gate:** conformance suite (~100+ Luau specs) covering signal deferral
  ordering, task semantics, tree mutation edge cases (destroy during
  iteration, reentrancy limit 10, duplicate-name FindFirstChild) — specs are
  written against `docs/api-design.md`, NOT against the implementation;
  determinism: same script+seed twice → identical hash after 10,000 ticks;
  500-instance scene ticks under budget (baseline recorded); zero
  `luau-analyze` errors under the new type solver across all example/spec code.

### M3 — Tooling Loop: CLI, Hot Reload, Types, Tests (M)

- **Goal:** the DX loop the engine sells — from here on, the agent itself
  develops every milestone using hot reload (dogfooding is the verification).
- **Scope:** `luaug` CLI implemented as Lute 1.0.0 scripts (unmodified Lute,
  pinned via rokit): `luaug dev` (launch engine + `fs.watch` + WebSocket
  control channel → **fast world restart** per ADR 0024: state bag +
  `PreserveOnReload`, engine-side content survives, <500 ms), `luaug test`
  (headless engine executing spec files, TAP/JUnit output), `luaug check`
  (luau-analyze default solver + StyLua + i18n lint), `luaug new` (template
  project); typedef generation: engine API emitted as `declare extern type`
  defs consumed by luau-lsp 1.69 custom-platform mode; VS Code workspace
  settings in the template.
- **Deliverable:** live edit of the M2 example: change spin speed in the
  `.luau` file, see it under 1 s without restart.
- **Gate:** automated E2E hot-reload test (dev server started headless, file
  mutated by the test, WebSocket confirms reload, behavior change asserted via
  world hash); `luaug test` green in CI on both tiers; defs file lints clean
  and is regenerated + diff-checked in CI (drift between API and defs fails
  the build); i18n lint now enforces zero hardcoded user-facing strings in C++
  and CLI Luau.

### M4 — Seeing the World: Meshes, Materials, Camera, Lighting (L)

- **Goal:** the real rendering vertical slice — glTF in, lit PBR out.
- **Scope:** fastgltf runtime import (the offline pipeline is M7; the runtime
  path stays as the dev-mode path forever); meshoptimizer on import; forward
  PBR (albedo/normal/metal-rough), directional + point lights, single-cascade
  shadow map, HDR + tonemap; Camera as an Instance; MeshPart-equivalent and
  material handling per api-design.md; frustum culling; render pass list kept
  behind the `IRenderer` contract. **End of M4 = RHI interface freeze**; the
  human Android-device checkpoint must have happened by now.
- **The `DebugShell` — explorer and properties.** Added to M4 by human decision
  on 2026-08-20. `ADR 0017` ships v1 without a visual editor on the explicit
  grounds that an in-game ImGui shell "stands in for inspection needs", and
  `architecture.md` §app specifies it; no milestone had ever imported it, so the
  compensating control the no-editor decision rests on did not exist. What lands
  here is the tree explorer and a properties panel that reads **and writes**
  through the generated descriptors, honouring `readOnly` and the same setters a
  script goes through — never a second write path.
  - **Why M4 rather than later.** The same argument the roadmap already made for
    debug draw in M1: a tool built early is used by every milestone after it.
    An inspector that arrives at M8 debugged nothing.
  - **Why it is small.** `ClassRegistry` and the generated descriptor tables have
    carried per-property type, `readOnly` and default since M2, so the panel is
    one generic sweep rather than code per class. This is the "reflection layer
    editor-ready by construction" ADR 0017 promises, spent for the first time.
  - **Not here:** the log/REPL half — `eval` is deferred by M3's protocol
    decision because running arbitrary source in a live world touches R4 and
    needs its own design; the streaming map (M7) and the physics wireframe (M5)
    arrive with the systems they show.
- **Carried debt, scheduled here by human decision on 2026-08-20.** Five of these
  have been reappearing in `PROGRESS.md` since M0 or M1. Three are paid in this
  milestone; the other three get a named destination instead, because a debt
  scheduled where it does harm is not scheduled, it is moved.
  - [ ] **Trim `Luau.Analysis`** (carried from M0). Vendored Luau builds four
        libraries; the engine links the VM and the compiler and throws the type
        checker away, which is roughly a third of a cold build. It needs a patch
        under `third_party/patches/` (R13) — impossible until M4, when
        `applyPatches` was found to have never run and every tree was found to
        be CRLF-mangled. Both are fixed, so this is now half an hour.
  - [ ] **`api-dump.json`** (carried from M3). `api-design.md` §5 specifies it as
        diff-checked in CI "to force changelog entries and catch accidental API
        breaks" — it is the gate that notices the public surface changing, and
        M4 is the milestone that grows that surface the most (Camera, MeshPart,
        materials, lights). It is the one carried item that loses value by
        waiting: shipped now it guards M5–M8, shipped at M8 it guarded nothing.
  - [ ] **`luaug --version`.** Advertised by `luaug --help` and answered with
        "Unknown command". One dispatch entry.
  - **Not here, and where instead:** the **shipping profile** does not configure
    and also needs a bytecode-loading path that does not exist — it belongs with
    `luaug build` (M8). **DXIL signing on Linux** has no consumer until a Linux
    job produces a Windows shader pack — also M8. The **clang-format gate**
    requires reformatting the whole C++ tree and pinning a toolchain version;
    doing that while the renderer is being written buys a milestone of diff
    noise, so it lands at the **start of M5**, on a quiet tree.
  - The remaining M3 artifacts — the `@std`/`@luaug` stubs and
    `docs/reference/**` — stay carried: both are DX surface with no gate behind
    them, and neither degrades by waiting the way the api-dump does.
- **Design constraints (not scope).** Three seams must stay open. None is
  built here — M4 is glTF in, lit PBR out — but both are nearly free while the
  render module is being designed and cost a refactor to reopen afterwards.
  ADR 0014 is the precedent: `CFrame` carries f64 from its first commit because
  widening a type after four milestones of code has consumed it is a different
  project from starting wide.
  - **Engine-generated geometry must be able to reach the renderer.** The mesh
    path cannot assume "a mesh is a handle to an imported asset". Known callers:
    procedural and voxel meshing, which produce vertices in memory and never
    touch a file. Geometry that changes every frame also needs an upload path
    that does not allocate every frame — a persistent or ring buffer, decided
    here rather than retrofitted.
  - **A material must be able to name a shader that is not the default PBR
    one.** The HLSL toolchain from M1 (`cmake/luaug_shaders.cmake`, ADR 0006)
    already compiles one source to three backends; what does not exist is any
    way for a material to point at a shader other than the built-in. Known
    caller: vertex-displaced water, where the ocean is a static grid displaced
    in the vertex shader rather than a mesh edited per frame.
  - **Draw order and batching belong to `extract`, not to a backend.**
    `RenderWorld` is a POD snapshot, so grouping by pipeline and material there
    is inherited by every backend; doing it inside `rhi_sdlgpu` is work bgfx
    would have to repeat.
- **Performance recording.** The frame-time baseline this gate records is a
  *what*, not a *why*: record submitted draw calls and triangles beside it.
  The capture stream has counted commands deterministically and without a GPU
  since M1, so this costs a column rather than a harness.
- **Deliverable:** `examples/02-meshes` — a small glTF scene
  (permissively-licensed sample assets, licenses recorded in
  `THIRD_PARTY_NOTICES.md`), orbit camera, ImGui sun-angle slider previewing
  day/night.
- **Gate:** capture-stream goldens for 3 camera angles × 2 lighting states
  (blocking); Tier-2 lavapipe image goldens attempted (non-blocking); frame
  time baseline at 1080p recorded; GPU validation clean; Tier-3 compile gate
  becomes blocking.

### M5 — Feeling the World: Jolt Physics + Character (L)

- **Goal:** the physical vertical slice: "cube falls onto ground, scripted in
  Luau," then a character you can steer.
- **Scope:** Jolt 5.6 on the fixed tick (single-threaded first; Jolt's job
  system wired to the engine job system when M7 lands it — note the seam);
  rigidbody/collider state as BasePart properties per api-design.md; collision
  events as deferred signals; raycast/shapecast API; Jolt debug-draw bridge;
  CharacterBody (Jolt CharacterVirtual: capsule, slopes, steps, jump);
  third-person follow camera; minimal direct keyboard polling — deliberately
  replaced in M6 by the Input Action System (that migration is an API-quality
  test).
- **Performance and determinism notes.** The tick budget this gate records
  should be broken down — broadphase, narrowphase, solver — because one number
  says a budget was missed and three say which stage missed it. And the
  determinism gate becomes *blocking* here while Jolt is single-threaded, but
  M7 wires it to the job system: whether recorded hashes survive that is a
  question for the grounding pass that vendors Jolt (§9, `UNCONFIRMED.md`),
  answered before the gate hardens rather than after it breaks.
- **Deliverable:** `examples/03-physics-playground` — stacks, ramps, seesaw,
  third-person character walking/jumping through it.
- **Gate:** **determinism becomes blocking**: recorded 60 s input replay →
  identical final world hash across 3 runs in CI; physics tick budget for
  1,000 active bodies recorded and regression-gated; a scripted bot replay
  drives the character up ramps and steps (functional gate); collision-event
  conformance specs green.

### M6 — Playing the World: Input Actions, UI, Tween, Audio, Minimal Animation (L)

- **Goal:** the game-feel layer; the obby example is born as the living
  conformance test for all five systems.
- **Scope:** Input Action System clone (actions, bindings, contexts,
  gamepad + KB/M, runtime rebinding) per api-design.md; UI Instances
  (ScreenGui/Frame/TextLabel/TextButton/UIListLayout equivalents) over Clay
  layout, stb_truetype text, UDim2-style coordinates; TweenService-equivalent
  (property tweens incl. UI; easing families conformant to reference easing
  tables checked in as test fixtures); miniaudio: 2D sounds + basic 3D
  spatialization as Sound Instances; **minimal skeletal animation**: glTF clip
  playback + linear blending (AnimationPlayer/AnimationTrack per
  api-design.md) — no state machines, no IK; enough for idle/walk/jump.
  *(Roadmap-gap fix: the API defines AnimationPlayer; this is where it ships.)*
- **Performance notes.** Tweens are property churn and must write through the
  same quiet-write path the 10k-parts benchmark measures — a second write route
  would silently forfeit the equality filter that is worth roughly a third of
  that measurement (M2 Decision 6). UI cost is relayout rather than draw, so
  measure the two separately and keep a static-UI case whose relayout cost is
  expected to be ~zero.
- **Deliverable:** `examples/04-obby` — main menu (tweened), HUD, checkpoints,
  moving platforms (tweens on physics-kinematic parts — a deliberate
  integration stressor), sounds, an animated character, fully playable
  start→finish.
- **Gate:** UI capture goldens at two resolutions (proves layout scaling);
  tween output vs. easing fixture tables; input replay of a full obby run
  completes to the finish flag in CI headless (the E2E gate for the whole
  stack so far); audio smoke test (device opens, buffer underrun counter zero
  in a 60 s soak); animation clip sampling determinism covered by the replay
  hash; the M5 example migrated to the Action System with no regression.

### M7 — Scaling the World: Asset Pipeline, Async IO, Streaming, Floating Origin (L)

- **Goal:** the open-world substrate.
- **Scope:** offline pipeline via Lute (`luaug build-assets`): glTF → engine
  mesh format, basis_universal/KTX2 textures, meshopt LODs, content-addressed
  pack + manifest, deterministic outputs (same input → same hashes; enables CI
  caching); assimp as the offline-CLI-only importer for exotic formats; engine
  job system + async IO; 64-bit world coordinates + floating origin
  (render-relative translation; origin/rebase is per-World state, never a
  global — ADR 0014) — enforced by tests; chunked world format +
  StreamingService semantics (load/unload radius around foci, priority queue,
  budgeted per-frame materialization); basic LOD switching; low-level net
  primitives: GameNetworkingSockets + ENet behind the transport interface,
  exposed as the minimal socket/HTTP surface via `@std/net` (loopback echo
  example only — replication is post-v1); Recast/Detour: vendored, seam
  defined, **no integration** (explicit non-goal, ADR 0022).
- **Performance note.** The per-frame materialization budget is denominated in
  *time*, not in a count of chunks: chunk cost varies with content, and the gate
  is stated as "zero hitches >33 ms attributable to streaming" — budget and gate
  should measure the same thing.
- **Deliverable:** `examples/05-streaming` — a procedurally generated large
  world (no giant binary assets in the repo), fly-cam, ImGui chunk-state
  overlay, memory graph.
- **Gate:** scripted 5-minute fly-through: peak memory under the declared
  ceiling, zero frame hitches >33 ms attributable to streaming (frame-time
  histogram asserted); float-precision test: object behavior at coordinate
  1e7 identical to origin (hash comparison); asset build determinism check in
  CI; loopback socket echo test; pak round-trip fuzz test (truncated/corrupt
  pak → structured error, no crash).

### M8 — Flagship, Hardening, Docs, v1.0 (M)

- **Goal:** assemble, polish, prove, ship.
- **Scope:** `examples/10-open-world` — third-person character exploring a
  large open world: streamed chunks (terrain + props via the M7 pipeline),
  Jolt physics, day/night cycle (sun animation + tuned tonemap), HUD, ambient
  audio, all hot-reloadable; performance pass to absolute targets;
  `luaug build` packaging (distributable player + content); docs completion:
  `docs/coming-from-roblox.md` written for real, API reference generated from
  the defs pipeline, README with screenshots/GIF; license/NOTICE audit of
  every vendored dep; CHANGELOG; tag `v1.0.0`, GitHub release with Windows
  binaries + source instructions.
- **Application identity.** Not "set the window icon" — the thing an engine owes
  a game it ships. `branding/` carries the LuauG mark, and that mark is the
  *fallback for the dev host only*: a game built with `luaug build` takes its
  icon from `[project] icon` in `luaug.toml`, because an engine whose games all
  wear the engine's face is a template, not an engine.
  - **Embedded in the artifact, never a file beside it.** A PNG next to the
    binary survives until the first install, move or pack. Windows takes a
    multi-size `.ico` through an `.rc` resource; macOS an `.icns` named by the
    bundle's `Info.plist`; Linux a `.desktop` entry plus the hicolor theme
    directories. `SDL_SetWindowIcon` reads the same embedded bytes at window
    creation, decoded with the already-vendored `stb_image`.
  - **All sizes in one resource** (16 through 256): the OS picks per context,
    and a single 256 downscaled by the shell is what makes an icon look cheap
    in a title bar.
  - **Windows taskbar identity.** `SetCurrentProcessExplicitAppUserModelID`, or
    a pinned shortcut loses the icon and two games group under one button.
  - **Verifiable, not eyeballed.** The clean-machine job reads the resource back
    out of the built artifact — the PE resource table, the bundle's
    `Info.plist` — and fails if it is absent or is still the engine's default.
    An icon nobody can assert is an icon that silently regresses.
- **Gate (definition of done):** 10-minute scripted soak (walk + fly path)
  with zero crashes and bounded memory delta; 60 fps at 1080p on the recorded
  reference machine; every example launches and its automated run passes;
  clean-machine CI job: fresh clone → bootstrap → build → `luaug new` template
  project runs; determinism replay green; `luaug check` clean repo-wide;
  docs-lint clean; **a human plays the demo and signs off** — the one gate
  that is deliberately not automatable.

## Post-v1 phases (ordered intent, not scheduled)

1. **2D layer** — sprites, tilemaps, Box2D 3.1, dedicated 2D workflow (first
   item, per user decision #7), together with **navmesh integration**
   (NavigationService over the existing Recast/Detour seam, ADR 0022).
2. **Mobile** — Android first (bgfx RHI backend for the GLES2 long tail;
   NCG on Android), then iOS (interpreter-only; no JIT).
3. **Visual editor** — built on the engine (Studio-like, phase 2 of the
   original vision).
4. **Multiplayer/replication** — official server authority + prediction over
   the deterministic fixed-tick foundations; `ITransport` becomes the
   replication channel.
5. **Ecosystem** — FMOD/Wwise audio module alternatives, per-module hot
   reload (only if the world-restart budget proves insufficient), Box3D as a
   second 3D physics backend when it reaches 1.0.
