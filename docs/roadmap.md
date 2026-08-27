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
  `platform` + `rhi_api` + `rhi_sdlgpu` — compile-only, it catches API/toolchain
  breaks early without a device. It builds those two libraries and stops: it does
  not link an application and it produces no APK, which is why the sample the
  checkpoint below needs is M4 scope rather than something already lying around. Validation on a real
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
| M0 | Bootstrap and First Light | S (4%) | `luaug-host` runs `boot.luau`, prints via i18n catalog, exits 0 |
| M1 | Window, RHI, Frame Loop, Agent Eyes | M (9%) | `examples/00-clear`: pulsing clear + debug cubes driven by Luau; screenshot harness |
| M2 | Kernel: Instances/ECS, Scheduler, Signals, task | XL (16%) | `examples/01-instances`: 500 scripted spinning debug cubes, services, deferred signals |
| M3 | Tooling Loop: CLI, Hot Reload, Types, Tests | M (8%) | `luaug dev` — edit script, behavior changes <1 s; `luaug test` runs conformance suite |
| M4 | Seeing the World: Meshes, Materials, Lighting | L (10%) | `examples/02-meshes`: glTF scene, PBR, shadows, day/night slider |
| M4.5 | Correcting the World: the environment the renderer never read | S (3%) | `examples/02-meshes` rendering the scene its own script describes: the sun crosses the sky, shadows lengthen, the day/night slider works |
| M5 | Feeling the World: Jolt + Character | L (11%) | `examples/03-physics-playground`: third-person capsule on ramps/stacks |
| M6 | Playing the World: Input Actions, UI, Tween, Audio, Minimal Animation | L (11%) | `examples/04-obby`: menus, HUD, checkpoints, tweens, sound, rebindable input, animated character |
| M7 | Scaling the World: Assets, Streaming, Floating Origin | L (12%) | `examples/05-streaming`: fly-cam over large chunked world, bounded memory |
| M7.5 | Looking Like an Engine: Shadows, Lights, Reflections | L (10%) | `examples/02-meshes` and the streaming world through CSM + clustered lights + IBL + post, each beside its M4.5 render at the same camera and clock |
| M8 | Flagship, Hardening, Docs, v1.0 | M (6%) | `examples/10-open-world` + tagged v1.0.0 release artifacts |

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
- **The triangle sample and its Android package.** Added to M4 by human decision
  on 2026-08-20, because the checkpoint this milestone must pass has no artifact
  to pass it with. The nightly job compiles two libraries for arm64 and stops;
  there is no sample, no Android project, and no APK anywhere in the tree.
  - **A standalone triangle**, not `luaug-host`. The host links the Luau VM, and
    cross-compiling that answers a much larger question than "does SDL3 GPU draw
    on this device" — the nightly job avoids it for exactly that reason. What is
    wanted is a window, a clear, and one triangle through `rhi_sdlgpu`.
  - **An Android project around it.** SDL3 vendors its own template under
    `third_party/sdl3/android-project/`; the shaders ship as SPIR-V in the APK,
    since Android is Vulkan.
  - **Why it cannot slip past the freeze.** ADR 0005 records SDL3 GPU's Android
    support as officially "limited" and keeps bgfx as the hedge. Which way that
    goes is a question only a device answers, and answering it after the RHI
    interface is frozen means changing backends against a frozen interface.
- **Carried debt, scheduled here by human decision on 2026-08-20.** Five of these
  have been reappearing in `PROGRESS.md` since M0 or M1. Three are paid in this
  milestone; the other three get a named destination instead, because a debt
  scheduled where it does harm is not scheduled, it is moved.
  - [x] **Trim `Luau.Analysis`** (carried from M0). Vendored Luau builds four
        libraries; the engine links the VM and the compiler and throws the type
        checker away, which is roughly a third of a cold build. It needs a patch
        under `third_party/patches/` (R13) — impossible until M4, when
        `applyPatches` was found to have never run and every tree was found to
        be CRLF-mangled. Both are fixed, so this is now half an hour.
        **Closed, and not with a patch after all**: upstream exposes no option to
        switch Analysis off, so `cmake/luaug_luau.cmake` adds Luau
        `EXCLUDE_FROM_ALL` and names the four targets the engine actually links —
        `Luau.VM`, `Luau.CodeGen`, `Luau.Compiler` and `Luau.Ast`. Nothing else
        is reachable from a build target, so nothing else is built. The file
        records what it was worth: 407 s of every cold build, compiled and thrown
        away.
  - [x] **`api-dump.json`** (carried from M3). `api-design.md` §5 specifies it as
        diff-checked in CI "to force changelog entries and catch accidental API
        breaks" — it is the gate that notices the public surface changing, and
        M4 is the milestone that grows that surface the most (Camera, MeshPart,
        materials, lights). It is the one carried item that loses value by
        waiting: shipped now it guards M5–M8, shipped at M8 it guarded nothing.
        **Closed**: `api/api-dump.json` is generated from the IDL, and
        `scripts/gates/luau-check.sh` regenerates it and diffs the result, so a
        change to the public surface that does not carry its dump is a red gate
        rather than a silent break.
  - [x] **`luaug --version`.** Advertised by `luaug --help` and answered with
        "Unknown command". One dispatch entry. **Closed**: `tools/cli/main.luau`
        dispatches both `--version` and the bare flag to `commands/version`, and
        E4 made it answer from inside an installation as well as from inside this
        repository, with the stamp naming where the number came from.
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
  becomes blocking. **The last item was met at M4 and narrowed back on
  2026-08-20 by human decision**, on the condition its own commit named: GitHub
  reported 80% of the month's Actions quota consumed, macOS is charged at 10x on
  a private repository and is about two thirds of the cost of every push. It runs
  on a milestone tag or a manual dispatch again. Windows and Linux stay blocking
  on every push, and the Linux tier is the one that must — it catches what a
  local run cannot, a transitive header graph being a property of the runner
  rather than of the code (`758b322c`). The cost is stated rather than glossed: a
  macOS-only break is found at the tag rather than at the commit that caused it.

### M4.5 — Correcting the World: the Environment the Renderer Never Read (S)

- **Why this milestone exists.** M4 shipped a renderer that never reads
  `Lighting`. Proven by observation, not by argument: `examples/02-meshes` with
  `Lighting.Ambient` set to pure red renders byte-identical to the ambient the
  example ships. Every M4 image — the capture goldens, the lavapipe screenshots,
  the 1080p frame-time baseline — was recorded against a sun pinned straight up,
  a brightness of 2.0 and fog switched off, none of which is what the example
  asks for. The milestone's own deliverable is a day/night slider that has never
  done anything.

  It is numbered 4.5 rather than folded into M5 because M5 opens on the
  assumption that what M4 draws is what M4's scene describes, and every physics
  demo after it is looked at through that renderer.

- **Weight.** Three points, none of them new: two come from M4, which is
  reassessed at 11% delivered, and one comes from M6, which no longer builds the
  transparent pass. The roadmap still totals a hundred. Nothing here is scope
  added — it is M4's own scope finished, plus one piece of M6's moved to where
  its first caller already needs it.

- **Sign-off, by human decision on 2026-08-20.** **This milestone may be marked
  complete only by explicit human approval.** Not by a green gate, and not by
  the agent's own reading of the checklist. No `milestone/m4.5` tag and no
  "COMPLETE" in `PROGRESS.md` before the human says so in words. The same is
  true of `milestone/m4`, which is tagged today over the defect above: whether
  that tag stands is the human's call.

  **They said so on 2026-08-20** (`33c6ee83`), and `milestone/m4.5` is tagged on
  that commit. Every box below is ticked with the row or the commit that closed
  it except one — `PointLight.Shadows` and `SpotLight.Shadows` — which is left
  open and visible rather than tidied away, because what closed it at the time
  was a marker rather than a decision.

- **Scope — the defects, each with the evidence that found it.**
  - [x] **`Lighting` is unreachable from the renderer.** `WorldHost::start`
        caches `findFirstChildOfClass(dataModel, Lighting)` before any script
        runs, under a comment claiming the service exists by then. `services.cpp`
        states the rule one line from where it is broken: `Workspace` and
        `ScriptService` exist from boot, every other service is created by its
        first `GetService`. The fix is boot order — `Lighting` joins those two,
        for the identical reason: `extract` reads it every frame whether or not
        a script ever asks for it. Resolving lazily on each miss is smaller and
        leaves the same trap one refactor away.
        **Closed by D002** (`4e4e3fc2`): `Lighting` is a boot service beside
        `Workspace` and `ScriptService`, guarded on the class existing because
        `registerServices` lives in `script` and must not acquire an opinion
        about which modules are compiled in, and `api-design.md` §1.2's list
        of boot services changed in the same commit.
  - [x] **A host-level test for it.** `render_world_tests.cpp` builds a
        `Lighting` instance itself and hands its id straight to `extract`, so
        every environment assertion passes against an id the host never
        produces. The untested step is the one that was broken. The test belongs
        at the host, and it must fail if the id is resolved before the service
        exists.
        **Closed**: `world_host_tests.cpp`, "the host resolves `Lighting` on a
        world no script ever touched" — and the proof beside it is a
        differential rather than a golden, because a golden can only say "the
        same as last time", which is exactly what it said while this was
        broken.
  - [x] **Re-record every M4 gate artifact afterwards.** The goldens, the
        lavapipe attempt and the 1080p baseline all describe a scene lit by the
        wrong sun. A number recorded against a defect is not a baseline, and
        M5's "no >10% regression" clause would be measured against it.
        **Closed at the milestone's own gate** (`33c6ee83`): the six capture
        goldens re-recorded and green, the lavapipe image re-recorded and
        rendering the blended pass, and `docs/perf-baselines.md` given an M4.5
        row — 0.46 ms median, 1.79 ms worst, three runs inside 4% — with M4's
        rows kept and struck through rather than overwritten.
  - [x] **`BasePart.Transparency` must actually fade, by human instruction on
        2026-08-20.** The sorted, blended transparent pass, which was scheduled
        at M6 earlier the same day and moved here when the human was told what
        cutout does not do: at a 0.5 cutoff, 0.4 renders fully opaque and 0.6
        vanishes, and nothing in between fades. The property is named for the
        thing cutout cannot do.

        Three pieces, and the first two are shared with the cutout path so
        nothing is wasted if this is ever split again:

        - **The value has to reach the renderer at all.** `DrawItem` has no
          field for it, and the material block cannot hold it: materials are
          deduplicated per frame precisely so the sort key can group draws that
          share a bind set, and a per-instance alpha written there splits one
          material into as many as there are distinct values.
          `GpuObjectUniforms` is already per draw and adds no RHI call, which
          is what keeps this possible after the ADR 0037 freeze.
        - **The pass.** After the opaque one, depth-tested and depth-write off,
          source-alpha blending. The shadow pass keeps drawing everything: a
          half-transparent part still occludes, and deciding otherwise is a
          separate question this milestone should not open.
        - **The sort, and it belongs in `extract`.** Back-to-front by view
          depth, in the snapshot rather than in a backend — that is M4's third
          design constraint, and doing it inside `rhi_sdlgpu` is work bgfx
          would have to repeat. The existing `sortKey` already carries a pass
          field and a quantized depth; what changes is that the transparent
          pass reads that depth descending.

        **What this still does not buy**, and the deliverable should not imply
        otherwise: no order-independent transparency, so two transparent
        surfaces intersecting each other sort per draw and not per pixel. That
        is correct for the ninety-nine cases a part with `Transparency` set is
        actually used for, and visibly wrong for the hundredth. Order-independent
        transparency is not on the v1 list and this does not put it there.

        **M6 inherits the pass rather than building it.** UI needs exactly this
        one — a `Frame` over a world is the same blended, sorted draw — so the
        milestone that ships UI now gets it already written and tested against
        world geometry, which is the harder of its two callers.

        **Closed by D001** (`e86f9b0b`), and the finding underneath it is the
        part worth carrying: the value could not reach the renderer *at all*
        — `DrawItem` had no field for it — so the pass was never the first
        problem.
  - [x] **The shadow grid crawls.** The ortho box is centred on the camera (the
        snapshot is camera-relative and `sunViewProjection` looks at the
        origin), so an orbiting camera slides the texel grid 0.42 of a texel per
        frame at the example's speed. Snap the box centre to texel increments in
        light space; extent and resolution are both compile-time constants, so
        the increment is one too. The rotational half — a moving sun turning its
        own grid — is a separate problem needing normal-offset bias; it is
        visible only while `ClockTime` moves and is not required here.
        **Closed by D003** (`27c47549`) for the half this milestone scoped. The
        rotational half was reported three more times afterwards — D044, D050
        and D054 — and what those closed is a mitigation rather than a
        removal: a cascade's lattice lives in the light's rotating frame, so a
        shadow edge advances one texel at a time and cannot slide. D054 hides
        the step behind a six-texel penumbra floor and a seven-by-seven kernel
        rotated per pixel, which took the worst single step from 3.58 pixels
        to 0.75.
  - [x] **`PointLight.Shadows` and `SpotLight.Shadows` accept a write and change
        nothing.** Stored, extracted, never read: one cascade from the sun was
        all v1 had. The M4 brief named it as deliberate, in a C++ comment and a
        NOT-in-scope list — neither of which is where the person clicking the
        inspector reads. Decide it the way Transparency was decided: honour it,
        or remove the property until the milestone that renders it.

        **Honoured, and the box was stale for a milestone after it was.** M4.5
        answered with a third option rather than either of the two — the
        properties stayed and were marked `Inert`, which made the surface honest
        and left it inert, a different thing from deciding it. The campaign's
        decision 5 took the decision the box actually asks for: a shadow atlas,
        a 2D map per spot light and a cube map per point light, a per-frame
        caster budget ordered by apparent size with creation order breaking
        ties, sampled from the clustered pass. `renderer_default.cpp` reads
        `light.shadows` and allocates tiles from it.

        **And it is gated.** `local_shadow_differential` renders the same scene
        with the flag on and off and requires the images to DIFFER — not a
        `gpu-golden`, because there is no checked-in image and no reference GPU,
        which is what makes it a gate on every machine rather than only where a
        golden was recorded. That is the instrument whose absence let the
        property sit inert for three milestones while being "accepted, read back
        and plumbed to `RenderLight::shadows`" (S6.11 verified this box against
        the code on 2026-08-27 and found it stale).
  - [x] **The pivot is a `Model` concept, and in the reference API it is not.**
        Replaces an item that said `Model.PrimaryPart` has no consumer, which
        was wrong: `modelPivot` reads it, and the sweep that "found" it searched
        `engine/render` alone for a value consumed in `engine/script`. A
        property audit scoped to one module is not an audit, and the retraction
        is left visible rather than deleted.

        What the check should have found is next to it. `PVInstance` is the
        reference hierarchy's abstract base for **`BasePart`, `Model` and
        `Camera`**, carrying `GetPivot()`, `PivotTo()` and `PivotOffset`; here
        `GetPivot`/`PivotTo` exist on `Model` only and `PivotOffset` does not
        exist at all. Three consequences, in order of sharpness:

        - **`PivotOffset` is what gives `PivotTo` its meaning.** Without it,
          `Model:PivotTo(cf)` is `PrimaryPart.CFrame = cf` — which is the
          deprecated call the pivot API was introduced to replace. The new name
          is implemented with the old semantics: it passes its tests and cannot
          hinge a door.
        - **The no-primary-part fallback differs in kind, not in formula.** The
          reference stores an explicit `WorldPivot`; `modelPivot` computes a
          centroid. And its own comment says "the centre of the extents box"
          while the code averages part positions, which is a different point
          whenever parts differ in size — `GetExtentsSize` in the same file
          already computes the box.
        - **Generic code has to branch on class**, where the reference lets
          anything positional take `obj:PivotTo(cf)`. That cost grows with every
          milestone that writes such code, which is the argument for taking it
          before M5 rather than after.

        **Decided by the human, 2026-08-20: everything that has a pivot in the
        reference gets one here.** That is a short and closed list — `BasePart`,
        `Model` and `Camera`, all three of which already exist — and `PVInstance`
        becomes a real abstract class between them and `Instance` rather than
        two copies of the same methods. It has to be a class because `IsA` and
        `FindFirstChildWhichIsA` are documented to accept abstract base names, so
        `IsA("PVInstance")` is the question generic code actually wants to ask,
        and it can only be asked of a class that exists. `Attachment` stays out,
        as it does in the reference: it carries `CFrame`/`WorldCFrame` and is not
        positional in this sense. `CharacterBody` inherits it through `BasePart`
        at M5 without doing anything.

        **One thing to name before M5 writes it: `PivotOffset` is not a centre of
        mass.** It is a scripting-space transform, and Jolt has its own notion of
        a body's centre. Wiring one into the other would make a hinge move a
        body's dynamics, which is a bug that would take a milestone to notice.

        **Closed by D012 and D013** (`721f6194`): `PVInstance` is a real
        abstract class between `Instance` and `BasePart`/`Model`/`Camera`,
        `PivotOffset` exists, and `modelPivot` computes the extents box its own
        comment had always claimed.
  - [x] **The crash handler and the log file sink.** `architecture.md` §app
        promises "crash handler (minidump + log)" and neither exists. A human
        running the engine by hand is this project's verification model, and has
        now reported five defects from memory. The handler is the half that
        matters — a captured crash held two lines, because `core::log` already
        flushes per line and the process died without reaching any C++ path.
        `core` is L0 and `platform::paths()` is L1, so `app` injects the path at
        boot.
        **Closed** (`7cc1c77c`): a minidump and a log file beside it, and D015
        with them — `fopen_s` opened the log for exclusive access, so nothing
        could read the engine's log while it ran, which is the half of a log
        sink that makes it useful.

  - [x] **A crash while editing `Size` and `CFrame` in the inspector, still
        undiagnosed.** Reported by the human on 2026-08-20, separately from the
        `Parent` crash and not explained by it: that one was an uncaught
        `std::bad_variant_access` from a property editor trusting a declared type
        over an absent value, and it is fixed (`a0e41ac1`). This one took the
        host down while values were being dragged, and the human's own note is
        that it may have coincided with the window losing focus. The captured
        `crash.log` held two lines — the two an ordinary run prints — and
        `core::log` flushes after every line, so nothing was lost to buffering:
        the process died without reaching any C++ error path. That is why the
        handler above is listed as a prerequisite rather than beside this. It is
        the one reported defect with no reproduction, so the first work on it is
        earning one: the write path goes through the scheduler's FrameStart safe
        point (Decision 15), a defocus tears down and reclaims swapchain
        resources, and either is a place a stale pointer would survive testing.
        **Closed by D004, and not by a fix.** It was never reproduced: the
        write path was driven through zero, negative, 1e30 and infinity for 32
        frames with a render extraction each time, and the window was minimized
        and restored 25 times over 900 windowed frames, and neither faulted.
        The reporter said on 2026-08-23 that it no longer happens, and the
        register carries what most likely closed it — the panel was rebuilt
        around a gesture, a selection set and one row height between M4.5 and
        E2. **If it returns it returns with evidence**, which is what the
        handler above is for.

- **Everything the human reported, and where each one is.** The list exists
  because eight of these arrived over one afternoon of a person using the
  engine, and a defect that is fixed silently gets re-reported.

  | Reported | State |
  |---|---|
  | `Transparency` changes nothing | **M4.5**, the sorted blended pass — moved here from M6 by the human so it fades rather than switches |
  | The sun never moves; shadows never lengthen | **M4.5**, the `Lighting` defect above |
  | The sun's shadow flickers | **M4.5**, texel snapping |
  | A crash while editing `Size`/`CFrame`, no log | **M4.5**, above — and the handler with it |
  | The F3 panel is unreadable while running | Fixed, `e7aa645f` — sampled at 4 Hz and held, worst frame beside the mean |
  | "go" on `RunService.Parent` crashes the host | Fixed, `a0e41ac1` — an editor trusted the declared type over an absent value |
  | F5 in the editor launched an unrelated extension | Fixed — `.vscode/launch.json`, four configurations against the real binaries |
  | Type errors on `engine.d.luau` in the editor | Not ours: `selene` from an extension outside this project's toolchain. `.vscode/settings.json` did also point `luau-lsp` at a deleted file, and that is fixed |
  | The Android triangle is stretched in portrait | Not a defect: the sample's vertices are in NDC, so it fills whatever aspect the device has. Recorded at the checkpoint |

- **Scope — so this class of defect stops being found by clicking.**
  - [x] **The inspector marks a property with no consumer.** All three of the
        unbacked properties above were found by a human changing a value and
        watching nothing happen. The descriptor table already knows each
        property's backing; what it cannot say today is whether anything reads
        it. Whatever the mechanism, the requirement is that the panel
        distinguishes "written and acted on" from "written and stored" — that
        distinction is the only defence `instances.api.luau`'s own rule has.
        **Closed**: `Inert` is a property-level declaration in the IDL meaning
        "backed, stored, read back faithfully, and nothing acts on it yet", and
        it flows to `PropertyDesc`, to the inspector as a visible tag, and to
        the api-dump. `tools/repo/inertcheck.luau` then makes it a gate rather
        than a convention, and D055 closed that tool's own blind spot: it swept
        component pools only, so every property belonging to a service with one
        instance per world — half the API surface — was never checked at all.
  - [x] **Read `architecture.md` §app against reality, once, as a list.** Four
        items so far had no milestone owner and each was discovered separately:
        the `DebugShell`, the api-dump, the triangle sample, and now the crash
        handler. The point is to find the fifth before a human does.
        **Closed** (`13e01d3f`): the list is in `briefs/m4.5-kickoff.md`, one
        row per promise. It found two rather than one, and each was given a
        milestone rather than a note — D016, `BindToClose` with no capped grace
        period, at M5, and D017, the `DebugShell`'s missing memory-category
        table and log/REPL pane, at M6. Both are fixed now. The reusable part
        is what the list found the two gaps to have in common: **a promise
        whose absence is invisible from inside the code**, because nothing
        fails to compile because `BindToClose` does not wait.
  - [x] **A milestone-close rewrite must not drop open defects.** Three
        human-reported items were removed from `PROGRESS.md` — not archived —
        while it was being rewritten to close M4, on the day the human is asked
        to sign it off. Whatever enforces it, the ledger's open items have to
        survive a close.
        **Closed** (`13e01d3f`): `docs/defects.md`, append-only, with
        `scripts/gates/docs-lint.sh` checking that the ids run without gaps or
        duplicates and that every `D###` cited anywhere else exists there. A
        deleted row is a hole the gate names rather than a bullet nobody
        misses, and that is the whole mechanism: not a database, a shape a
        check can hold.

- **Deliverable:** `examples/02-meshes`, unchanged as source, rendering what its
  script actually describes — the sun crossing the sky over the ninety-second
  day, shadows that lengthen towards evening, the fog and brightness the example
  sets, and a `Transparency` that fades rather than switches. Shown as a strip of frames
  across one full day from a fixed camera, so the sun's motion is the only
  variable in the strip.
- **Gate:** the M4 gate re-run in full and re-recorded, plus three additions
  that would have caught this — an assertion that the environment reaching the
  renderer is the one the world holds; a golden pair at two different
  `ClockTime` values that must differ; and `Lighting` resolution tested at the
  host rather than at the extractor. **None of it counts as complete without the
  human's explicit approval.**

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
  says a budget was missed and three say which stage missed it.
  **Amended at M5 (§5: docs follow reality).** Those three are not available:
  the split exists only inside Jolt's own profiler, which is a compile-time
  feature that dumps to a file rather than answering a query, and whose
  alternative replaces the measurement class in every scope of the library in
  every configuration (`UNCONFIRMED.md` U-56). The breakdown the gate records is
  therefore the one that is separable at this seam and measures stages that
  really are distinct — **apply / step / writeback** — which answers the same
  question the note was asking: which stage missed it. And the
  determinism gate becomes *blocking* here while Jolt is single-threaded, but
  M7 wires it to the job system: whether recorded hashes survive that is a
  question for the grounding pass that vendors Jolt (§9, `UNCONFIRMED.md`),
  answered before the gate hardens rather than after it breaks. **Answered
  2026-08-27 by running it** (S6.10, ADR 0064): the hashes survive unchanged --
  `churn` reproduced its committed hash on both tiers with the solver on four
  threads -- and the step is roughly two and a half times faster, with
  `churn10k`'s worst tick falling from 174 ms to 40 ms. It is Jolt's own pool at
  a FIXED count rather than the engine job system, because that pool sizes itself
  from the machine and Jolt's determinism is per thread count.
- **`Weld` and `WeldConstraint`, added to M5 by human decision on 2026-08-20.**
  They arrive here rather than at M6 because the milestone is running ahead and
  because nothing else in v1 can keep one part on another: `CharacterBody` is the
  capsule that collides, a skinned `MeshPart` is what a player sees, and
  `AnimationPlayer` is documented as living "under a Model/MeshPart with a
  skinned mesh" — three pieces with no specified way to occupy the same place.
  Without a weld the answer is a `Heartbeat` handler writing
  `mesh.CFrame = body.CFrame * offset` in every character of every project,
  which is a per-frame Luau cost standing in for a relationship the engine could
  simply know.

  - **It is a transform weld, not a Jolt constraint, and that is forced rather
    than chosen.** `CharacterVirtual` is not a `Body` in the physics system
    (Decision 8 of the M5 brief), and a Jolt constraint connects bodies — so a
    constraint could not attach to a character at all. The motivating case
    settles the design: a weld derives the welded part's `CFrame` from its
    anchor's, and the solver is not involved.
  - **A welded part is driven, not simulated.** While welded it stops being an
    independently simulated body and follows its anchor; the anchor may be a
    dynamic body, a `CharacterBody` or an anchored part. Welding two dynamic
    bodies so the SOLVER treats them as one rigid assembly is a different
    feature — Jolt's `FixedConstraint` — and is **not** this one. Name which
    behaviour a script gets in the docs, because the two are indistinguishable
    until something pushes.
  - **`Weld` carries explicit `C0`/`C1`; `WeldConstraint` captures the relative
    transform when it becomes active** and holds it. That difference is the
    whole reason both names exist, and shipping only one of them is worse than
    shipping neither, because the wrong one is silently wrong.
  - **R10 binds here.** Welds form a graph, and a chain (A welded to B, B welded
    to C) resolves in topological order or produces a frame of lag that depends
    on container order. Resolve in a stable topological order at a defined point
    in the tick, and **reject cycles** rather than iterating until something
    converges.
  - **Still not here:** `HingeConstraint`, `SpringConstraint`, `Motor6D`, and any
    solver-level joint. The seesaw in the deliverable is still a body resting on
    a fulcrum. `PivotOffset` is still not a constraint anchor and still must not
    reach Jolt's centre of mass.
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
- **The transparent pass is inherited, not built.** It was scheduled here on
  2026-08-20 and moved to M4.5 the same day, when the human asked for a
  `Transparency` that fades rather than one that switches. UI needs the same
  blended, back-to-front pass a transparent part does, so this milestone gets it
  already written and already tested against world geometry — the harder of its
  two callers. What remains here is UI's own use of it, not the pass.
- **Performance notes.** Tweens are property churn and must write through the
  same quiet-write path the 10k-parts benchmark measures — a second write route
  would silently forfeit the equality filter that is worth roughly a third of
  that measurement (M2 Decision 6). UI cost is relayout rather than draw, so
  measure the two separately and keep a static-UI case whose relayout cost is
  expected to be ~zero.
- **Solid `Part` rendering, added to M6 by human decision on 2026-08-20.** A
  `Part` has no solid path at all today: `renderer_default` walks `draws`, which
  only `MeshPart` fills, and a `Part` reaches the frame solely as a debug wire
  box. So `Instance.new("Part")` — the primary building block of the API this
  engine is familiar to — produces something invisible, and M5's own
  `examples/03-physics-playground` builds its stacks, ramps and seesaw out of
  `Part`, which means the physics milestone is looked at in wireframe.

  - **`Part.Shape` is extracted and ignored, which is the same shape of defect
    `Transparency` was.** It is declared in the IDL, stored, and copied into
    `RenderPart.shape` — and `submitWorld` calls `wireBox` whatever it says, so
    a `Ball` draws as a box. Either this item fixes it or it is marked `Inert`;
    it may not stay as it is, because that is precisely the state the M4.5
    marker exists to make impossible.
  - **This is the M4 design constraint being spent for the first time.** The
    roadmap required that "engine-generated geometry must be able to reach the
    renderer" and that "the mesh path cannot assume a mesh is a handle to an
    imported asset". A unit mesh per shape, built once at boot and registered
    with `MeshCache` like any other, is exactly that caller — and if the seam
    was left open correctly, **the renderer changes not at all**. If it turns
    out the renderer has to change, that is the finding, and it is worth more
    than the feature.
  - **One mesh per shape, scaled by `Size`.** Not a mesh per part: the transform
    already carries non-uniform scale and the cofactor normal matrix that
    survives it exists since M4. Five meshes total, for the five members of
    `Enum.PartShape`.
  - **Name the tessellation rather than picking it silently.** A ball, a cylinder
    and a capsule need a segment count, and it is a permanent decision — the
    number is baked into every golden recorded after it, and changing it later
    re-records all of them.
  - **Colour, `Material`, `Transparency` and the blended pass come free**, since
    a `Part` draw becomes an ordinary `DrawItem` with an ordinary material. That
    is the point of routing this through `draws` rather than growing the debug
    path a second time.
  - **The debug wire path stays.** `render_world.cpp` says why in its own
    comment: it is how anything is seen when the real path is not working, so a
    bug in the culler must not be able to hide it. What changes is that it stops
    being the only way a `Part` is visible.
- **`InputService` gains the raw event surface a Roblox developer reaches for**
  (human decision, 2026-08-21; **ADR 0041**, which amends ADR 0029's "only input
  model" clause). `InputBegan`, `InputChanged`, `InputEnded` — each carrying an
  `InputObject` (`UserInputType`, `KeyCode`, `Position`, `Delta`) and a second
  argument saying whether the UI already consumed it — plus `IsKeyDown`. The
  first line of this repository's README promises the developer experience a
  Roblox developer already has, and a person arriving from that platform reaches
  for `UserInputService.InputBegan`; no amount of sugar over a different model
  answers that.

  - **Fed from the IAS's dispatch, never from the OS.** Same source, same frame,
    after the UI has consumed what it consumed — which is the `gameProcessedEvent`
    Roblox has, and which `938522b6` already built the flag for. In a replay they
    come from the recorded stream, so M6's own gate can still see every input a
    game reads.
  - **On the `Simulation` clock**, so a handler that writes to the world is
    replayable by construction. Render-rate input for a camera is an
    `InputContext` with `Rate = Render`, which `examples/03-physics-playground`
    already demonstrates. Firing raw events at render rate was rejected: it makes
    the easy path the non-deterministic one.
  - **Rebinding is the cost that remains, and it is stated in the events' own
    doc text.** A key handled here does not appear in a remapping screen; an
    action does. Roblox has the identical split — `UserInputService` is not
    rebindable there either — so it is a familiar cost rather than a new one.
  - **The IAS stays and stays the recommended path for a shipped game**: it is
    what the examples use, what a rebinding screen can enumerate, and what binds
    a keyboard key and a gamepad button to one action.
  - **New work:** the `InputObject` datatype and `Enum.UserInputType`.
    `Enum.KeyCode` already spans keyboard, mouse and gamepad.
  - **`@luaug/input` is dropped.** It existed to make the simple case cheap and
    `IsKeyDown` makes it cheap; shipping both is two answers to one question.

- **Design constraint (not scope): an action must be drivable by something that
  is not a physical device** (human decision, 2026-08-20). `InputBinding` is
  keyed by `KeyCode`, so today only hardware can feed an `InputAction`. A
  touch-screen button and a virtual thumbstick are the reason it was raised, and
  mobile stays post-v1 (R15) — but the seam is nearly free while the IAS is
  being written and costs a refactor to reopen, which is the same argument M4
  made for its three renderer seams.

  - **The proving caller ships in this milestone.** M6 is the milestone with UI,
    so a `TextButton` in the obby's HUD driving a real action is buildable now,
    testable on a desktop, and useful on its own — HUD buttons, menu shortcuts,
    an accessibility path for a player who cannot hold a key. A seam with a
    caller is a seam; a seam without one is speculation.
  - **It goes through the same dispatch or it is the thing that was just
    declined.** Same clock, same `Sink`, same fallthrough order, and **recorded
    in the replay stream like any other input** — otherwise a HUD button is a
    second input model wearing a different hat, and M6's own gate (an obby run
    replayed headless to the finish flag) cannot see it.
  - **Carry a VALUE, not a press.** The easy mistake is designing a virtual
    *button* — and then a thumbstick, which is the other half of any touch
    control scheme, does not fit and needs a second mechanism. Whatever the seam
    is, it must be able to express `Direction2D` on day one.
  - **`Enum.InputDeviceType.Touch` already exists**, declared with "nothing
    produces it in v1" written into the enum. When something eventually does, it
    should produce it through this seam rather than growing a new one.
- **Design constraint (not scope): the glyph store is a cache, not a bake**
  (human decision, 2026-08-20). `TextLabel.Font` is typed `Content`, so a game
  will supply its own TrueType face by URI the way a `MeshPart` supplies a mesh,
  and M7's asset pipeline is what hands it over. A glyph atlas baked once at boot
  works only while there is one face at one size — which stops being true the
  moment that lands, and a bake then becomes a rewrite rather than a widening.
  So whatever M6 builds must be keyed by **face, size and codepoint** and filled
  on demand, even while there is exactly one face to fill it with. Unicode is the
  same decision from the other side: `stb_easy_font` is ASCII, and a game in
  Portuguese already needs á ç ã õ, so the cache is sized and the
  missing-glyph behaviour is chosen here rather than discovered by a player.
- **D031 — D027's fix is never reached, because the platform is a static body**
  (human, 2026-08-21, confirmed by playing a build newer than the fix).
  `physics_sync.cpp:122` picks `Kinematic` only for a part an active `Weld`
  drives, so an `Anchored` part a *tween* moves is `Static` — and Jolt neither
  moves a static body nor derives velocity from one, so `MoveKinematic` is handed
  a target and nothing happens. The example already states the intent the
  classification does not implement.

  **The human chose the narrow fix**: an anchored part becomes `Kinematic` when
  its `CFrame` is written and returns to `Static` after N ticks without a write.
  The wide one — `Anchored` meaning kinematic always — was rejected on cost:
  `jolt_physics.cpp:89` splits the broadphase into `NonMoving` and `Moving`, and
  the static layer exists precisely so that it is never re-fitted. Making every
  floor and wall kinematic would put a world of never-moving bodies into the
  layer that is updated every tick. Moving only what is written moves exactly the
  count that has to be moving anyway.

  Two things to settle rather than discover: **hysteresis**, since a part written
  every other tick would otherwise oscillate between broadphase layers; and the
  **transition cost**, which is measured rather than estimated — a row in
  `perf-baselines.md` with N platforms transitioning, beside the physics numbers.

- **D027 — a character does not ride a moving platform** (human, 2026-08-21,
  found by playing `examples/04-obby`). Scheduled here because this is where it
  was found and where the deliverable that shows it ships; the seam it belongs to
  is M5's. Two halves, and the second reaches past platforms: `GetGroundVelocity`
  is called nowhere in the engine, so a character never inherits its ground's
  motion — and it would read zero anyway, because kinematic bodies are moved with
  `SetPositionAndRotation`, which teleports. Jolt derives no velocity from a
  teleport; `MoveKinematic` is the call that takes a target and a delta and
  computes one. **Until that changes, every script-moved kinematic body in the
  engine has zero velocity**: a closing door does not push, a piston does not
  launch, a conveyor does not carry. The roadmap called the obby's moving
  platforms "a deliberate integration stressor" — this is the stress showing, and
  it worked.
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
- **The default typeface ships here** (human decision, 2026-08-20): **Inter,
  OFL 1.1**, vendored with its licence recorded in `THIRD_PARTY_NOTICES.md` (R6).
  One face, not several — a good default plus "bring your own" covers the ground,
  and three vendored faces are three things to licence, update and explain.
  Roboto (Apache-2.0) is the alternative if matching the repository's own licence
  family is worth more than the typeface. `TextLabel.Font` stops being `Inert`
  when it lands, and the marker goes with it.
- **`CharacterBody:Jump()` stops refusing in mid-air** (human decision,
  2026-08-21). It currently applies `JumpSpeed` only when `Grounded`, which makes
  a double jump — and a wall jump, and a triple jump — impossible to write, since
  `LinearVelocity` is read-only and Luau has no other way to push a character
  upward. The human's argument is the right one and it is short: **`Grounded` is
  already exposed**, so `if character.Grounded then character:Jump() end`
  reproduces today's behaviour in one line, in the game, where a jump policy
  belongs.

  - **The recorded reasoning never weighed this.** The property doc argues
    *ignored* against *queued* — "a jump that fires the moment you land is a jump
    you did not ask for" — and that argument is about queuing, which nothing here
    proposes. Letting the caller decide was not among the alternatives it ruled
    out.
  - **What stays engine-side is the tick**, and it must: the jump is applied at
    the next simulation tick and never immediately, or a replay diverges (R10).
  - **What it unlocks costs nothing more**: double and triple jump, wall jump,
    coyote time and jump buffering all become counters in Luau.
  - **Say in the doc that calling it every frame is flying.** That is the game's
    bug and it is the same in every engine that offers a mechanism rather than a
    policy — but somebody arriving from a platform where the engine held that
    guard will expect it, and the sentence is cheaper than the surprise.
  - **Migration is three call sites**: `examples/03-physics-playground`, the
    conformance character spec and the determinism character scene each gain
    `if character.Grounded then`. `examples/04-obby` already writes it, because
    it needed to know whether the jump actually happened in order to play a
    sound.
  - **Not taken, and named so it is not re-derived**: making velocity writable
    (`SetVelocity` or similar) would unlock dash, knockback and grapple as well.
    It is the more powerful change and the larger surface, and writing velocity
    on a `CharacterVirtual` has rules of its own since it is not a solver body.
    Left until a caller asks for it.
- **Deliverable:** `examples/05-streaming` — a procedurally generated large
  world (no giant binary assets in the repo), fly-cam, ImGui chunk-state
  overlay, memory graph.
- **Gate:** scripted 5-minute fly-through: peak memory under the declared
  ceiling, zero frame hitches >33 ms attributable to streaming (frame-time
  histogram asserted); float-precision test: object behavior at coordinate
  1e7 identical to origin (hash comparison); asset build determinism check in
  CI; loopback socket echo test; pak round-trip fuzz test (truncated/corrupt
  pak → structured error, no crash).

### M7.5 — Looking Like an Engine: Shadows, Lights, Reflections (L)

- **Why it exists.** Human decision, 2026-08-20, after watching a full day pass
  in `examples/02-meshes`: the engine looks far from Unity and Unreal, and
  closing that is a requirement rather than a preference. Recorded as **ADR
  0038**, which states the gap M4 actually shipped rather than a feeling about
  it — one cascade at 5.9 cm per texel, eight unculled lights per draw, no
  image-based lighting at all, and no post chain beyond the tonemap resolve.
- **Why here and not at M8.** M8 is the flagship plus hardening plus docs plus
  the release, and a renderer's second half is none of those. Built here, the
  flagship is built *on* this; built at M8, the flagship ships against the
  renderer this milestone exists to replace. Cascades also want a streamed world
  to be designed against, which M7 is what produces.
- **Goal:** the same scenes, rendered so that a person who has used another
  engine does not immediately notice which one they are looking at.
- **Scope.** Named techniques with published parameters, because a milestone
  whose scope is "better" cannot be finished:
  - **Cascaded shadow maps.** Four cascades. Splits by the practical scheme
    (GPU Gems 3 ch. 10), a blend of uniform and logarithmic partitioning with
    `λ` between them. **Normal-offset bias** replacing depth-only — displacing
    the sample along the surface normal is what survives a grazing receiver, and
    it is the half of the M4.5 flicker that snapping deliberately did not fix.
    A **hardware comparison sampler** so a tap degrades instead of switching.
    PCF between 2×2 and 7×7; the published cost of the widest at 1080p is about
    0.4 ms, which prices the choice rather than arguing it.

    **Three of the four pieces already exist**, which makes this smaller than it
    reads: `sampleSunShadow` already does 3×3 PCF and already carries a
    slope-scaled bias (`lerp(0.005, 0.0005, N·L)`) — the pair a comparable engine
    exposes as `shadowBias` and `shadowBiasAngleScale`. What is missing is
    literally the cascades.

    **And the two things that make a CSM implementation look amateur are not the
    cascades**, so they are named here rather than discovered:
    - **The PCF radius must be constant in WORLD space across cascades.** Each
      cascade has a different world-units-per-texel, so a kernel fixed in texels
      makes shadow softness change as an object crosses a split — read by a
      viewer as a seam, and the most common tell of a first CSM.
    - **Cascades must blend over a band, not switch at a plane.** A hard
      handover is visible for the same reason, and the fix is a lerp or a dither
      across the last fraction of each cascade.
  - **Clustered forward shading**, so eight lights per draw stops being the
    limit. Olsson and Assarsson's clustering with a 16×9×24 grid and exponential
    depth slicing, `slice = max(log2(linearDepth) · scale + bias, 0)` — one grid
    serving a near plane at 0.1 and a far plane in the thousands is the whole
    reason for the log.
  - **Image-based lighting**, split-sum (Karis 2013): a prefiltered environment
    cubemap whose mip chain is indexed by roughness, plus a 2D BRDF LUT indexed
    by (N·V, roughness) supplying the Fresnel scale and bias. **This is the
    single largest visual difference** between what M4 draws and a mainstream
    engine — `pbr.hlsl`'s own comment already calls its flat ambient "the
    degenerate case of the split-sum approximation where the environment is one
    colour". A metal currently reflects nothing because there is nothing to
    reflect.
  - **The post chain that makes the rest visible**: exposure, bloom, an ambient
    occlusion term, anti-aliasing. A correctly lit frame through a naive resolve
    still does not look like the reference.
  - **Instanced draws for repeated meshes.** Human question, 2026-08-20, asked
    as "could I build a survivors-like on this" and answered by measuring one:
    the ceiling is one draw call and one uniform upload per visible object,
    which `renderer_default.cpp:407` emits in a loop, and which nothing batches.
    Two thousand enemies sharing one mesh cost 11.1 ms a frame, of which 6.9 ms
    is submitting 2,092 forward draws — and the same scene costs the *same*
    10.2 ms at 320×180 and at 4K, which is what says the GPU is idle and the
    cost is CPU-side submission. Twelve thousand triangles, in total.

    `roadmap.md`'s own M4 note already places this — "draw order and batching
    belong to `extract`, not to a backend" — so what is missing is the batching,
    not the seam. `extract` already sorts by material and mesh (M4 Decision 7),
    which is the sort an instanced path needs; the work is grouping the sorted
    run into one call with a per-instance transform buffer, and the RHI's
    `draw(vertexCount, instanceCount, ...)` already takes the count.

    **The measurement is recorded in `docs/perf-baselines.md`** so this is
    priced rather than argued, and so the milestone can show the same scene
    before and after. Per R16 the win is largest exactly where it matters most:
    a CPU-bound frame is what a phone has.
- **Build order note.** IBL first is the likely order — it is the largest visual
  win for the least work, and it is independent of the other three. That is an
  ordering, not a stopping point: ADR 0038 explicitly rejects shipping it alone.
- **Not here:** anything on R15's closed list; ray tracing; virtualized geometry
  or shadow maps; global illumination beyond what a prefiltered environment
  gives; temporal upscaling. Reflection probes placed by hand are a judgement
  call for the brief, since v1 has no editor to place them with (ADR 0017).
- **Performance notes.** Every feature here is a frame-time cost and the table
  records each separately, not a lump: a milestone that makes the frame twice as
  expensive without saying which half is which cannot be optimized afterwards.
  The reduced-CPU row `perf-baselines.md` asks for stops being optional here —
  R16's logic is about the low end, and this is the milestone most able to
  forget it.
- **Design constraint (not scope): the scene's depth must be samplable by a
  later pass** (human decision, 2026-08-21). It has two known callers and
  neither is built here, which is what makes it a constraint rather than work:
  - **Screen-space ambient occlusion**, which is in this milestone's own post
    chain and cannot be written without reading depth.
  - **Intersection foam on water** — the white line where an ocean meets sand or
    a hull. It is not geometry: it is the water fragment's depth compared against
    the depth the opaque pass already wrote, foam drawn where the two are close.
    The human named it, and it is the same caller M4's shader-per-material
    constraint already names.

  The pieces mostly exist. Depth is a `D32Float` attachment, sampling a
  `D32Float` is already done every frame by the shadow map, and the sorted
  blended pass water would ride in landed at M4.5. **What does not exist is
  binding the scene's depth as a texture while it is also the depth attachment**
  — a read-only depth state, or a copy. Deciding which while the pass list is
  being rebuilt for IBL and post costs a decision; deciding it afterwards costs
  the pass list.

  **This is the freeze's first real test and that is fine.** If the binding needs
  a call ADR 0037 does not have, that is an ADR — which is the freeze working
  rather than failing. Record which way it went either way, because "we already
  have depth" is exactly the kind of half-truth that reads as done.
- **Design constraint (not scope): motion vectors and a jitterable projection are
  renderer OUTPUTS, not a detail inside the anti-aliasing** (human decision,
  2026-08-21, asked as "will we support DLSS/FSR, and frame generation"). None of
  those ship in v1. What ships here is the anti-aliasing this milestone's post
  chain already owes, and if that anti-aliasing is temporal it needs **exactly
  the same two things** a temporal upscaler does: a per-pixel motion vector, and
  a projection that can be jittered sub-pixel. Declaring them as outputs of the
  renderer rather than as private state of a TAA pass is what makes the later
  work days instead of a milestone.

  - **The velocity buffer is the expensive half and it is renderer-wide.** Every
    draw has to say where its pixel was last frame, which means each object
    carries its previous transform and the pass writes a second target. That is
    not a bolt-on, which is precisely why it is decided while the pass list is
    being rebuilt.
  - **Licences differ and the difference is not cosmetic.** FSR is MIT and XeSS
    has an open variant, so both fit R6. **DLSS is a proprietary SDK and does
    not** — adopting it would be a human-approved ADR and an optional path
    outside the default build, which is how other engines carry it anyway. The
    door is worth leaving open rather than closing by omission.
  - **Frame generation needs two things beyond that**, and one is already true
    here. It needs optical flow — hardware-assisted on some vendors, software on
    others — and it needs **the UI composited AFTER the generated frame**, or a
    HUD smears across every synthesized one. M6 built the ui2d pass as a separate
    pass over the world, so that ordering already exists: **do not collapse it
    into the world pass while rebuilding the pipeline here.** That sentence is
    the whole of what this milestone owes frame generation.
  - **None of this threatens R10.** Upscaling and frame generation are
    presentation; the simulation hash never sees them. What they do cost is
    input-to-photon latency, which is a real trade a player feels and which the
    milestone that ships them has to measure rather than assume.
- **Deliverable:** `examples/02-meshes` and the M7 streaming example, both
  rendered through the new path, each shown beside its M4.5 render at the same
  camera and clock — so the difference is the only variable.
- **Gate:** the same scene compared against a reference render and against the
  previous milestone's; per-feature frame cost recorded at 1080p including a
  reduced-CPU row; GPU validation clean; goldens re-recorded and the clock
  differential still differing. **"Looks better" is not a gate result** — the
  comparison is against a stated reference, or it is not a comparison. **Plus
  one number that is not about looking**: the horde scene in
  `docs/perf-baselines.md`, re-measured, with the count of draw *calls* stated
  beside the count of visible objects — if those two numbers are still equal,
  the instanced path is not doing anything.

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
- **Graphics settings, as a family rather than a number.** Human decision,
  2026-08-20, from asking whether shadow resolution could be changed and finding
  that no configuration concept exists anywhere in the engine: shadow resolution,
  cascade count and distance, render scale, light budget and post toggles are all
  `constexpr` today. They are **engine** settings and not `Lighting` properties —
  a scene must not decide the player's GPU budget — and they land here because
  until M7.5 exists there is nothing worth exposing, and a quality slider is a
  hardening concern. See ADR 0038.
- **Prove the editor seam is still open** (human decision, 2026-08-20). ADR 0017
  declines a visual editor for v1 on the explicit condition that **nothing in v1
  hard-codes an assumption that blocks one**, and four milestones in, nobody has
  checked whether that is still true. The concrete test is the one the phase-2
  editor needs first, and it is what prefab-isolation mode is made of: **two
  `WorldHost`s alive at once**, each with its own `ScriptRuntime` — that is two
  Luau VMs — rendered into two targets.
  - **Half of it already works and should be recorded as such.** `IRenderer`
    takes an arbitrary `RenderTarget` and an arbitrary `RenderWorld`, and
    headless renders a world into an offscreen texture in the gate every day.
    Rendering a second world into a second texture is already expressible in the
    interface ADR 0037 froze, which is the half that would have been expensive
    to discover missing.
  - **The untested half is the second world.** `engine.cpp` creates exactly one
    host. A static, a singleton or a global index anywhere between here and v1.0
    would make the phase-2 editor pay a refactor, and this check is how that is
    found while it is still cheap.
  - **Networking is the second caller, which is why this matters more than it
    did when it was only the editor's.** The post-v1 multiplayer design (post-v1 phase 4)
    puts an authoritative world and a replica in one process over a loopback
    transport — the fastest multiplayer development environment there is, and
    impossible if anything here assumes one world. A seam with two callers is
    much harder to quietly drop than a seam with one.
  - It is owed here because hardening is where architectural promises are
    proven, not because this is the first milestone that could run it — the test
    is small enough for any milestone to take early, and its value decays with
    every milestone that adds code before it.
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

**Reordered and extended 2026-08-21 by human decision**, from 1-2-3-4-5 to the sequence
below: the visual editor first because it multiplies everything after it, then
effects and world content, then the 2D layer, then multiplayer, then mobile. This supersedes "first item, per
user decision #7", which had put the 2D layer first — that item is unchanged and
only its position moved.

1. **Visual editor** — built on the engine (Studio-like, phase 2 of the
   original vision). **Opened 2026-08-22 by human decision** and specified below
   under "Phase 1 detail"; it is the first work this repository does that v1's
   roadmap did not already contain.
2. **Effects and world content** — added 2026-08-21 by human decision, after an
   audit of what this engine will not have that others do turned up a group with
   no owner anywhere: not in v1, not in any post-v1 phase, and no recorded reason.
   Being honest about v1 in `api-design.md` is not the same as scheduling, and
   this is the scheduling. It sits after the editor because most of it is
   *authored* — a particle system and a terrain are both miserable to tune
   without one.

   - **Particles** (`ParticleEmitter`, named in `api-design.md` as not-in-v1).
     **The most visible gap of the group**: without it a game has no fire, smoke,
     sparks, dust, impact or magic, and there is no way to fake it. Terrain can
     be a mesh and global illumination can be a well-chosen ambient; a missing
     particle system has no workaround. Most of what it needs already exists —
     the sorted blended pass (M4.5), instanced draws (M7.5), and the texture
     path (M7). **Soft particles** — fading a quad as it approaches the geometry
     behind it, instead of showing a hard intersection line — need to sample the
     scene's depth, which is exactly the seam M7.5 was required to leave open for
     water foam and SSAO. Third caller for that one.
   - **Decals — projected, never per-face** (human decision, 2026-08-21). A
     bullet hole, a puddle, graffiti, a scorch mark. The reference API parents a
     decal to a part and picks a `Face`, which covers that whole face and only
     exists on an axis-aligned side of a primitive — so a bullet hole is not
     expressible, and every shooter on that platform fabricates a thin part or a
     surface GUI at the impact instead. Here a decal is **a thing in the world
     with a position and an orientation**, projected onto whatever geometry is
     already there: curved, sculpted, skinned, anything.

     **The canonical input already exists and has since M5.** `Workspace:Raycast`
     returns a `RaycastResult` carrying `Position` and `Normal`, which is exactly
     a decal's placement — `CFrame.lookAt(hit.Position, hit.Position + hit.Normal)`
     and a size, with no parent part and no face to choose. Whatever else the
     class grows, that call has to remain the one-liner.

     Projection reads the scene's depth, making decals the **fourth caller** of
     the seam M7.5 was required to leave open — after water foam, SSAO and soft
     particles. Mesh decals (clipping the hit triangles into a small hugging
     mesh) are the alternative: more exact on complicated geometry, no depth
     read, but CPU work per decal and access to the mesh. Choose with the reason
     written down.
   - **Terrain** — a sculpted, collidable landscape rather than a floor made of
     parts. Jolt has a height-field shape, so the physics half is a shape type
     rather than a system; the render half is a chunked LOD surface, which is
     what M7's streaming and LOD chain already do for meshes. The open question
     is authoring: height field or voxel, and the answer decides whether caves
     are possible.
   - **`SurfaceGui` and billboards** — the UI tree rendered in world space: a
     screen on a wall, a name over a head, a health bar that follows a body. The
     tree, the layout and the `ui2d` pass all exist since M6; what does not is
     putting that output somewhere other than the screen. **Note for whoever
     builds it:** this is world-space UI and therefore part of the world image —
     it is *not* the screen-space UI pass that the frame-generation constraint
     says must be composited last. Two different things with the same word in
     them.
   - **Rich text** — colour, weight and size varying inside one label. The glyph
     cache is already keyed by **face, size and codepoint** (M6, from the human's
     own font decision), so a label carrying three sizes and two weights already
     fits the cache that exists. That decision was made for user-supplied fonts
     and pays here a second time.
3. **2D layer** — sprites, tilemaps, Box2D 3.1, dedicated 2D workflow (user
   decision #7 made this the first item; the human moved it to second on
   2026-08-21), together with **navmesh integration**
   (NavigationService over the existing Recast/Detour seam, ADR 0022).
4. **Multiplayer/replication** — official server authority + prediction over the
   deterministic fixed-tick foundations; `ITransport` becomes the replication
   channel. **Designed and approved by the human on 2026-08-21**, and ready to
   start as soon as v1 ships. The shape below is a commitment, not a sketch: what
   it costs v1 is nothing, because every seam it needs is already open.

   - **Authority is world state, not build flavour.** A `World` is authoritative
     or a replica, and that single fact produces every topology. **One binary,
     four postures, decided at runtime**: run it and you are solo; run it and
     host and you are client and server at once; run it `--headless` with a
     server script and you are a dedicated server; join one and you are a
     replica. There is no "server build" — `--headless` has existed since M1 and
     is how every gate already runs the engine.
   - **Three pieces, each owning one thing.** The **client** shows and sends
     intent and decides nothing. The **simulation server** — the engine, headless
     — owns the match or the region: positions, collisions, whether the ray hit.
     The **backend** is the game author's, in any language, and owns the account:
     inventory, progression, persistence. It is reached with `@std/net.request`,
     which already works.
   - **Two rules.** The client says what it did, never what happened — a client
     that could assert a result is a client that always hits. And the backend is
     called at the start and the end, never inside the tick: a tick is 60 Hz and a
     database is not.
   - **Absent by default, and that is stronger than disabled.** A solo game
     compiles without `engine/net` and pays nothing — no transport, no
     serialisation, no code path. That is the pattern this engine already has for
     a whole module being gone: "null is a real state and not an error", as the
     animation host and the physics mirror both say. With the module present but
     playing alone, the service exists and tells the truth — authority true,
     topology solo, one player — so **one script runs solo and networked without
     a configuration branch**. A game does not ask "am I networked", it asks "do
     I decide this".
   - **`NetworkService` is the optional, familiar surface**, and it earns its
     place by owning what `@std/net` cannot: players, authority, topology and
     replication. A socket has no notion of a player. **`@std/net` is parallel,
     not underneath** — one talks to the outside world (the author's backend, an
     API), the other talks to the players in the match. They do not overlap. The
     service goes through `ITransport` like everything else and **may never open a
     socket of its own**, which is the same rule that made `InputService`'s raw
     events feed from the IAS dispatch rather than from the OS (ADR 0041).
   - **State replication is the model.** Deltas of world state, with interest
     management reusing what streaming already built — foci are plural in
     `StreamingManager` today, and a match of six has six. Rollback stays
     *possible* rather than planned: it needs cross-platform determinism as a
     guarantee, and ADR 0025 leaves that recorded rather than enforced.
   - **The protocol is declared, not derived.** If replication is "the engine
     serialises its own structs" then only the engine can ever speak it, and the
     option of an independently written server closes by accident. Declaring the
     wire format keeps that open. **Committing to it as a public contract is NOT
     part of this** — that is a versioned promise, and it is a separate decision
     taken when somebody actually wants it.

   **Deliberately not committed here**, so the milestone that builds this is not
   boxed in by a paragraph: replication semantics (what replicates, how often,
   delta encoding) which is the genuinely hard part and needs a milestone;
   rollback; a public protocol; and the `NetworkService` class in the IDL, since a
   declared class nothing implements is exactly what `instances.api.luau`
   forbids. The names are already reserved — `Enum.RunContext`, `src/client` and
   `src/server` — and reserving is all v1 owes.
5. **Mobile** — Android first (bgfx RHI backend for the GLES2 long tail;
   NCG on Android), then iOS (interpreter-only; no JIT).
6. **Ecosystem** — FMOD/Wwise audio module alternatives, per-module hot
   reload (only if the world-restart budget proves insufficient), Box3D as a
   second 3D physics backend when it reaches 1.0.


## Phase 1 detail — the visual editor

Opened 2026-08-22 by human decision, the first sequence this repository builds
that the v1 roadmap did not contain. The shape is settled by **ADR 0046**: the
editor is a mode of the `luaug-host` binary drawn in Dear ImGui, launched by
`luaug edit`, driving the `Inspector` model that already exists. Read the ADR
before this section — it records the two shapes that were rejected and why, and
one of them was rejected because the sandbox would have had to be opened to
allow it.

**The starting position is better than the phase's size suggests**, and five
reconnaissance passes measured it rather than assumed it. Already built and
tested: a property grid that walks the generated descriptor tables with no
switch on any class name (`engine/app/src/inspector.cpp`, nine doctest cases
against classes it has never seen); writes queued and drained through
`World::setProperty` at the frame's safe point; five distinguishable refusal
outcomes; a preorder tree flattener; a Luau REPL against the live VM; ImGui
vendored on the docking branch with tables, multi-select, drag-and-drop and a
list clipper; and `--two-worlds`, which proves two worlds and two Luau VMs alive
in one process. **What is absent is smaller and sharper**: no picking at all — a
repo-wide search for `ScreenPointToRay`, `unproject` or `WorldToScreen` finds
only prose in `docs/api-design.md` — no selection highlight, no manipulator, no
undo, no dockspace, no scene file format, and no way for the engine to write a
file (`engine/platform/include/luaug/platform/file.h` reads and never writes).

**The sequence, and only E1 and E2 were specified on the day the phase opened.**
Everything after them was ordered intent in the same sense as the phase list
above: each got its detail and its gate at its own kickoff, from what the
milestone before it learned. Writing five gates on that first day would have been
writing four of them from a position that had not seen an editor run — and E2's,
written after E1 shipped, is a different document for it: it names three defects
that only somebody who had built the first one could have found. Every section
below now exists, and **E3's was written after its milestone closed** rather than
before it opened, which is stated at the top of that section rather than left for
a reader to notice.

**Re-cut 2026-08-22, at E1's review, by human decision.** The first sequence
below put manipulators in one milestone, saving in another and play in a third —
which optimised for whoever implements it and not for whoever uses it. The
review said so in one sentence: *an engine without stop is not an engine — how
are we going to edit, test and save?* **Edit, test and save are one loop**, and
an editor that delivers a third of a loop three times is not usable in between.
So E2 is now the loop, whole, and it is large on purpose. ADR 0047 is the
decision underneath it: the authored world becomes data and scripts become
behaviour, the way Unity, Unreal and Roblox all arrange it.

**Corrected again, same day, by the same reviewer.** The re-cut below first put
the loop in E2 and left E1 as "the editor opens". That was still the wrong
reading: what was asked for was *edit, test and save*, and those were named as
E1's own scope and as its priority. **So the loop is E1**, and E1 is not
finished until a person can open the editor, change something, press play, press
stop and get their change back, and save it. What was already built — the shell,
picking, the transport, the fly camera — is the first part of that milestone
rather than the whole of it.

**And re-cut once more, at the end of E1 rather than at its start**, because
that is when it became clear what E1 had actually become. It was opened as "the
editor opens" and closed as an editor: the loop, the content browser, the
application menu, undo and redo, context menus, and a scene format. Every one of
those was asked for at review, each was right, and **the honest record is that
this milestone absorbed most of what the first cut called E2** — which is worth
saying plainly so the next milestone is not planned as though a milestone this
size were normal.

| ID | Name | Size | Runnable artifact |
|----|------|------|-------------------|
| E1 | The Editor | XXL | `luaug edit`: an application with a menu bar and dockable panels; a viewport you click, fly and select in; **play, pause, step, stop and save**; a **content browser** with folders and context menus, from which **opening a scene loads it**; **undo and redo**; and a scene format that makes a project's world data. **COMPLETE**, signed off 2026-08-22, tagged `milestone/e1` |
| E2 | Moving Things | L | Translate, rotate and scale manipulators; creating instances; reparenting by drag; multi-select — the direct manipulation the loop and the undo stack make safe. **COMPLETE**, signed off 2026-08-23, tagged `milestone/e2` |
| E3 | Content and Prefabs | M | Prefabs as scenes, an asset importer path from the browser, and a scene that references what it uses. **The model is settled: ADR 0048** -- content holds SOURCES, and an instance in the world may be a LINK to one. Written at E2 from the human's own description rather than invented while wiring a browser. ADR 0048's third clause -- that editing a linked instance breaks the link -- was reversed while the milestone was building it: an edit is an override and the link survives (ADR 0051). **COMPLETE**, signed off 2026-08-23, tagged `milestone/e3` |
| E4 | The Editor Ships | M | The distribution question ADR 0046 deliberately declined, and the editor's own performance gate: a folder somebody downloads, and an Explorer that costs what is open rather than what exists. **Settled: ADR 0054.** **COMPLETE**, signed off 2026-08-24, tagged `milestone/e4` |
| E5 | The World You Build | L | A world authored in `Workspace` becomes a streamed world on its own — no generator script, nothing sorted by hand. `Model.StreamingMode` makes the model the unit rather than the part; cells are chosen by size as well as position on the `layer` that has existed unused since M7. **Settled: ADR 0053.** Placed here rather than in phase 2 because it is an authoring capability and E3's stamps are its neighbour. **BUILT, awaiting review** since 2026-08-24: every gate row is green except the chunk-state overlay on `examples/06-scene`, which is a picture |
| E6 | The Launcher | M | `luaug-host` with no project opens a project browser instead of printing a usage error: recent projects, a new one from a template, a folder picker, and the editor started on what you chose. **Settled: ADR 0055.** **COMPLETE**, signed off 2026-08-24, tagged `milestone/e6` |
| E7 | The Look | M | The shell stops looking like the debug overlay it grew out of: one theme as data rather than `StyleColorsDark` plus nine literals, Inter instead of a 13 px bitmap face, square everywhere, a palette measured against WCAG rather than argued about, and a launcher laid out for somebody arriving rather than for whoever wired it. **Settled: ADR 0056.** **BUILT, awaiting review** since 2026-08-24: what it waits on is a person looking at the four pictures in `docs/images/e7/` |
| E8 | The Script Editor | XL | You can write Luau inside the engine: any number of scripts as tabs beside the Viewport, Luau colour from the engine's own lexer, find and replace, errors underlined where the parser puts them, autocomplete from the reflection tables, and a **working debugger** -- breakpoints, stepping, the call stack and the locals, with the script parked and the frame loop still drawing. **Settled: ADR 0057.** **BUILT, awaiting review** since 2026-08-24 |
| E9 | Compiled Assets and a Skeleton You Can Touch | XL | Dragging a model in makes a `Model` you can open, with named parts you can select and materials you can edit; `Model.Scale` grows it with one number; a `Bone` is something a part can be welded to; and a character can be made a ragdoll. The import compiles rather than the runtime parsing, so a loose `.gltf` stops being what ships. **The material reversal is settled: ADR 0060.** **Done** -- all fifteen steps in `main`, one of them (step 3, the material) reversed on the way and settled by ADR 0060; the cut-over is ADR 0065 |

**What moved into E1 and why it was right.** Undo was E2's, and E1 grew delete
and duplicate — the two actions that make its absence dangerous. The content
browser was E3's, and it moved when a scene became an asset (ADR 0047), because
there is no way to open a scene without somewhere to open it from. Rename and
delete were E2's, and they arrived with the browser for the same reason.

**What is left in E2 is what E1 could not have done first**: a manipulator needs
something that keeps the change (E1's save) and something that takes it back
(E1's undo), and designing either before those existed would have been designing
them twice.

**Why the manipulators come after the loop, which is the opposite of the first
cut.** A manipulator without a save is a way to lose work, and an undo stack
designed before there is a scene to undo *into* is an undo stack designed twice.
The loop first makes every one of those tools land against something that keeps
them — and by the end of E1 both of those foundations existed, which is why the
manipulators are all that E2 still owes.

### E1 — The Editor, the Loop, and the Content Browser (XL)

**Scope added 2026-08-22, at review, on the human's word, and recorded here
before any of it was built** — which is this project's mechanism for scope
surviving the session that agreed to it.

**A scene is an asset, and the content directory holds all of them.** That
settles a question ADR 0047 left open and that the first implementation guessed
wrong: a scene was written to `main.scene.json` at the project's root, one per
project, treated as source beside `src/`. It is not. A scene lives under
`content/` with the meshes and the textures, a project has as many as it likes,
and opening one loads it.

**The word is SCENE, everywhere and without exception** (human decision,
2026-08-22). The comparison that produced this design was to a Roblox place, and
that is a comparison and not a name: nothing in this engine — no class, no file
extension, no directory, no doc, no identifier — is called a place. A `.unity`
scene and a `.umap` level are the same object under two other names, and this
engine has picked its own and keeps it.

- **Scenes move into the content tree.** `content/scenes/<name>.scene.json`,
  addressed by URN like everything else there, resolved by `ContentMounts` — so
  a loose scene overrides a packed one and the dev loop keeps working the way it
  already does for a mesh.
- **A content browser panel**, over the project's whole asset tree: **folders**,
  navigation, and the kinds the pipeline already knows (`Mesh`, `Texture`,
  `Chunk`, `Raw`) plus scenes. Not a file dialog — a panel, docked, the way
  Unity's Project window and Unreal's Content Browser are, because an asset
  browser you have to open is one nobody uses.
- **Opening a scene loads it**, and the editor knows which scene is open, says
  so, and saves back to that one.
- **Creating a folder** from the panel, because a browser that can only read a
  tree somebody else made is half a browser.
- **Quality bar, stated because it was asked for in these terms**: Unity and
  Unreal level, which for a browser means it stays responsive over a tree with
  thousands of entries. That is a measurement and not an adjective — the panel
  is virtualised (ImGui ships `ImGuiListClipper` and the DebugShell does not use
  it yet) and the tree is walked once and cached, never per frame.

### The rest of E1, as originally cut

- **Goal:** an editor that opens a real project, shows it, lets you choose a
  thing in it with the mouse, and lets you change that thing — nothing more. The
  milestone is deliberately short of manipulators and of saving, because both are
  large and neither can be designed honestly before somebody has used the
  selection they depend on.
- **Scope:**
  - `luaug edit [path]` in the CLI, following `tools/cli/main.luau`'s existing
    shape: an `if` branch, `rejectUnknownFlags` with the exact allowed set, a
    `cli.edit.*` catalog key for every user-facing line (R3), and a test that
    drives the real command line in a spawned process rather than calling `run`.
  - An **editor mode** in `engine/app`, gated the way `--replay`, `--bench` and
    `--two-worlds` already are, that boots a windowed host and draws an editor
    instead of a game's overlay.
  - **A dockspace and real panels.** `drawShell`'s single `Begin("LuauG")` with
    everything stacked inside becomes a dockspace host with Explorer, Properties,
    Viewport, Console and Stats as dockable windows, and `io.IniFilename` is
    turned back on with a decided location so a layout survives a restart.
  - **The world in a viewport panel.** The renderer already draws into an
    arbitrary `RenderTarget` and does it headlessly every day; the editor renders
    into a texture and shows it in a panel, which is what makes the panels around
    it worth having.
  - **Picking, which is the milestone's one genuinely new mechanism.** A screen
    point becomes a ray in world space and the ray chooses an instance. It is
    written as a testable function over a camera and a viewport rectangle, not
    as a lambda inside a UI callback, because a picking bug that can only be
    reproduced by clicking is a bug nobody fixes twice.
  - **A selection you can see.** The selected instance is outlined or boxed in
    the viewport through the debug-draw path that already exists. A selection
    that only appears in a tree view is not a selection in a 3D editor.
  - **The two reflection gaps that a real property grid cannot work around**,
    both additive to a pipeline that already exists: `PropertyDesc` gains the
    identity of the enum a property accepts — today the overlay recovers it from
    the current value, so a combo cannot be filled without a live instance — and
    `docKey` stops being emitted empty, so a property can carry the tooltip the
    IDL already wrote for it.
  - **D056 and D057**, because ADR 0046's editor lives behind `LUAUG_DEBUG_UI`
    and that flag is profile-gated, so this milestone is where the profile gating
    stops being fiction. At minimum: the `shipping` profile compiles, something
    builds it every gate, and `luaug build` either selects it or the release says
    plainly what it selects instead.
- **NOT in scope, and stated so it is not drifted into:** manipulators of any
  kind, undo, creating or deleting instances, reparenting, multi-select, saving
  anything at all, a scene file format, an asset browser, prefabs, play/stop, a
  second window, and any distribution of the editor as a downloadable product.
- **Gate (definition of done):**
  - `luaug edit examples/10-open-world` opens, docks, and renders the world in
    its viewport; a screenshot is attached to the gate record.
  - Clicking a part in the viewport selects it: the Explorer highlights the same
    instance, the Properties panel shows its class, and the viewport draws the
    selection. Proven by a headless test that drives a synthetic click through
    the picking function, not by eye alone.
  - Picking has unit tests over a camera and a viewport rectangle covering: the
    centre of the screen, each corner, a click on empty space returning nothing,
    and a non-square viewport — the last because an aspect-ratio bug is invisible
    at the centre and wrong everywhere else.
  - Editing a property in the editor changes the world: the existing safe-point
    drain is used unchanged and a test asserts the write lands.
  - An enum-valued property offers its full set of items with no live instance
    needed to discover them, and a property with a doc string shows it.
  - The `shipping` profile compiles, and a gate stage builds it.
  - `scripts/localgate.ps1` green on every stage; `luaug check` clean;
    docs-lint clean.
  - **A human opens the editor on the flagship and says whether it is an editor**
    — the gate item that is deliberately not automatable, and the one every
    milestone since M4 has proven is where the real defects come from.

### E2 — Moving Things (L)

Specified 2026-08-22, at kickoff, from four read-only reconnaissance passes over
the repository — the same method ADR 0046 used to size E1, and for the same
reason the phase list gives: E2 through E5 get their detail at their own kickoff,
written from what the milestone before them learned rather than from a position
that has not yet seen an editor run.

**The starting position, measured rather than assumed.** Every foundation a
manipulator stands on exists and is tested. `picking.h` is arithmetic rather than
a UI callback, and its own opening paragraph says why: an aspect-ratio error is
exactly right at the centre of the screen and wrong at every edge, so a picking
bug that reproduces by clicking is one nobody fixes twice. `DebugDraw` submits in
world space, is rebased once a frame, and draws in a pass with no depth
attachment — which is to say it already draws over everything, which is what a
gizmo wants and what E1's selection outline already uses. Undo is snapshots with
a coalescing key, and `World::setParent` already returns a typed error and
already refuses a cycle. The vendored ImGui is 1.92.9b on the docking branch and
carries `BeginDragDropSource`/`AcceptDragDropPayload` and `BeginMultiSelect`
already compiled.

**What is absent is smaller and sharper than the milestone's name suggests.**
There is no world-to-screen projection anywhere in the tree — a repo-wide search
for `worldToScreen`, `worldToViewport` or `projectPoint` finds nothing. The
selection is a single `InstanceId` and nothing anywhere holds two. `setParent`
has exactly one caller in the editor layer and no drag-and-drop exists in the
repository at all. There is no `Editor::createInstance`. And the undo coalescing
key is computed inline in `engine.cpp` out of *how many writes are pending this
frame*, which no test covers and which a manipulator breaks on its first dragged
frame.

#### The six decisions this milestone is built on

**1. The selection becomes a set, and it stays where the single one lived.** The
`Inspector` owns it — `editor.h` already says in its own words that a second copy
would be two answers to one question. `selection()` keeps meaning *the primary*,
so every existing reader stays correct, and a new `selectionSet()` serves the
ones that need all of them. A single `Inspector::pruneDead(world)` replaces the
four hand-written `if (!alive(selection())) select({})` sites — and closes a
latent defect nobody has reported: select a child, delete its parent, and the
selection today points at a dead id, because the check compares against the
deleted id and not against its subtree.

**2. An edit is a GESTURE, and that is what coalesces.** Today the key is
`(target << 32) | property`, and it is computed only when the frame has *exactly
one* pending write; two or more make it zero, and zero never coalesces. A gizmo
dragging three parts, or writing `CFrame` and `Size` together, therefore records
a full world snapshot every frame — a hundred and twenty undo steps and a hundred
and twenty world copies in two seconds, which is precisely the failure
`editor_tests.cpp`'s coalescing case exists to prevent and precisely the failure
that case cannot see, because it calls `record` directly and the calculation in
`engine.cpp` has no test at all. E2 replaces it with a gesture opened by whoever
starts the drag and closed when it ends. One drag is one step however many writes
it made, and two consecutive drags on the same property are two steps — which the
current scheme wrongly merges into one.

**3. A transform over N instances is a DELTA applied to each, never one value
broadcast.** Dragging three parts with an absolute write stacks them on top of
each other. The editor computes the delta and enqueues N absolute writes, which
means `PendingWrite` does not change and the safe-point drain does not change:
the arithmetic moves into the editor, where a test can drive it, rather than into
the inspector, where it would be a second meaning for a queue that has one.

**4. The gizmo is DRAWN in the world and HIT-TESTED by arithmetic.** Drawn
through `DebugDraw`, because that path already draws over everything, already
goes through the same view-projection that drew the frame — so it cannot disagree
with the image the way a separately-computed screen-space overlay eventually
would — already clips to the viewport panel, and is a buffer of vertices a
headless test can read. Hit-tested in `picking.h`, as free functions over a ray
and a gizmo frame, because E1's rule is unchanged: a bug that reproduces only by
dragging is a bug nobody fixes twice. `picking.h` gains `worldToViewport` as the
exact inverse of `rayThroughPixel` — reading the two tangents back off the same
projection matrix, for the same reason — which is what keeps a handle a constant
size in pixels.

**5. The gizmo is submitted CAMERA-RELATIVE, and so is the selection outline.**
`DebugDraw::rebaseTo` subtracts in f32 and its own header says so, so a
submission in world coordinates quantises the absolute metre value *before* the
camera is subtracted from it. That is about half a millimetre at four kilometres
and worse further out, on a handle somebody is trying to drag precisely.
`submitSelection` has this defect today and it has never been reported; E2 fixes
both, by subtracting in f64 at the submission, which is what `toRenderMatrix`
exists for.

**6. Authorable is not the same question as engine-owned, and reparenting is
where the difference bites.** `isEngineOwned` covers the DataModel root and
classes flagged `Service`; it says nothing about `generated`, which is the flag
streaming puts on a chunk's folder and which the scene format reads three times.
So nothing today stops a drag from dropping an authored part inside
`Chunk_12_-4`: the save would skip it, because the serializer skips a generated
subtree whole, and the next eviction would destroy it without a word. E2 answers
that with one predicate — authorable — that tests engine-owned, `generated`, and
the ancestors of both, and every new verb goes through it.

#### Scope

- **Three manipulators in the viewport** — translate, rotate, scale — driven from
  the primary selection with the rest following by delta. Axis handles and plane
  handles for translate; per-axis rings for rotate; per-axis and uniform for
  scale. World space and local space, because a rotated part is unusable in one
  of them and which one depends on the part.
- **Snapping**, with a modifier to suspend it. It costs almost nothing on top of
  the delta arithmetic, and its absence is what makes a manipulator feel like a
  toy.
- **Multi-select**: ctrl-click to add and remove, shift-click for a range in the
  Explorer, ctrl-click in the viewport. The Properties panel shows the properties
  the selection holds in COMMON and marks a differing value as mixed rather than
  showing the first one's, which is a lie somebody edits by accident.
- **Creating an instance** from the Explorer's context menu and the menu bar,
  over the 27 classes that are neither `Abstract`, `Service` nor `NotCreatable` —
  the same filter `Instance.new` applies, read from the same flags rather than
  from a list this milestone would have to maintain. Parented where the menu was
  opened, selected on creation, undoable, and placed in front of the editor
  camera rather than at the origin: an instance created four kilometres from the
  view is one nobody finds.
- **Reparenting by drag** in the Explorer, refusing a cycle, refusing a target
  that is not authorable, and with a drop-between-rows target so ordering is
  expressible.
- **Batch delete and duplicate** over the whole selection, as ONE undo step each,
  because somebody who deleted four things did one thing.
- **The gesture-based undo key**, extracted out of `engine.cpp` into something a
  test can reach.

#### NOT in scope, stated so it is not drifted into

Prefabs and any notion of a nested scene (E3). An asset importer (E3). Any
distribution of the editor (E4). Shape-exact picking — every part is still picked
as its box, which `picking.h` already writes down as an approximation. Solid
gizmo geometry: `DebugDraw` is a line list and stays one, so the handles are made
of lines. A second viewport, an orthographic view, or view bookmarks. Aligning or
distributing a selection. Copy and paste. A transform panel with numeric entry
beyond the Properties grid that already exists. And the VM that a stop does not
put back, which is still E1's honest limit and still waits on what ADR 0047
changes over time.

#### Gate (definition of done)

- `luaug edit examples/06-scene` opens, a part is selected, and each of the three
  manipulators moves it in the viewport. A screenshot per mode is attached to the
  gate record.
- **The manipulator arithmetic has unit tests over a camera and a viewport
  rectangle**, covering: an axis handle hit at the centre of the screen and at a
  corner; an axis nearly parallel to the view direction; a drag that begins off
  the axis; a non-square viewport; and a round trip — `worldToViewport` of a
  point, back through `rayThroughPixel`, aiming at that point — checked at the
  four corners, because that is the aspect-ratio error the file already exists to
  catch.
- **One drag is one undo step**, proven headlessly: sixty frames of writes inside
  one gesture produce one history entry, and two gestures over the same property
  produce two. The extracted key has its own test; the inline calculation it
  replaces had none.
- **A transform over a multi-selection moves each instance by the same delta**,
  proven on a selection whose members start at different transforms: three parts
  a metre apart are still a metre apart after the drag.
- **The gizmo does not shake four kilometres from the origin.** The vertices
  `DebugDraw` holds for a gizmo submitted at 4 km agree with the exact
  camera-relative value to within a tenth of a millimetre. The same check covers
  the selection outline, which is the defect it finds.
- Multi-select in the Explorer and in the viewport; the Properties panel shows
  the common properties of a mixed-class selection and marks a differing value as
  mixed. Proven by tests over the free functions that compute both, since the
  panel itself cannot be driven headlessly.
- Creating an instance from the UI lands under the parent the menu was opened on,
  is selected, and is taken back by one undo. Reparenting by drag moves a
  subtree, refuses a cycle, and refuses a target inside a streamed chunk — all of
  it driven through `Editor` directly by a test rather than by a mouse.
- Deleting a selection of four is one undo step, and undoing it brings all four
  back with the same instance ids, which is the property E1's delete test already
  asserts for one.
- `scripts/localgate.ps1` green on every stage; `luaug check` clean; docs-lint
  clean.
- **A human opens the editor on the flagship, moves something, and says whether
  it moves the way a manipulator should** — deliberately not automatable, and the
  gate item every milestone since M4 has proven is where the real defects come
  from.


### E3 — Content and Prefabs (M)

**Settled by three decision records rather than one**, and that is the
milestone's shape rather than an accident.
[ADR 0049](decisions/0049-a-stamp-is-a-source-and-an-instance-carries-its-mark.md)
names the thing and says how an instance carries its mark;
[ADR 0050](decisions/0050-a-script-is-an-ordinary-instance-and-its-source-is-a-property.md)
makes a script an ordinary instance; and
[ADR 0051](decisions/0051-a-prefab-is-inherited-and-an-edit-is-an-override.md)
reverses 0049's own break-on-edit rule. All three implement
[ADR 0048](decisions/0048-content-is-the-source-and-an-instance-is-a-link-to-it.md),
which E2 wrote down at its close in the human's own words: content holds sources,
the world holds a world, and an instance in the world may be a link to a source.

**This section was written after the milestone closed, which is worth saying
first.** E3 had no kickoff. It was specified in a conversation, one message at a
time, by a person who was using the editor while it was being built — which is
why four of its defects were reported by that person on the day they were
introduced, and why three of its decisions were reversed by whoever asked for
them. `docs/briefs/e3-kickoff.md` is the account assembled at the close; this is
the scope and the gate it was actually held to. **COMPLETE, signed off
2026-08-23**, tagged `milestone/e3`.

#### Scope

- **A prefab is a STAMP**, named by the human over prefab, blueprint and model.
  A stamp file is a scene of one subtree — the same writer, the same reader, a
  different root — so there is one format rather than two definitions of
  "everything about a subtree".
- **Convert any instance to one from the row it is on**, and the instance becomes
  an instance of the file it just made. A source plus a copy of it that nothing
  connects is two things that drift apart by tomorrow.
- **Place one linked or as a copy**, from the browser and from code:
  `Instance.stamp(name)` and `Instance.stamp(name, false)`. Both are real things
  to want — a lamp post you will place forty of wants the link, a starting point
  you are about to rebuild does not.
- **Open one onto a stage**: a `scene::World` of its own with a Workspace, a
  Lighting and nothing else. The game's world is never touched, so there is
  nothing to restore and nothing that can go wrong on the way back.
- **An edit is an OVERRIDE and the link survives it.** Changing the source moves
  every instance that has not overridden that property. A structural change is
  written in full and unlinked rather than refused, because **a save that refuses
  is a save that loses work**.
- **A script is an ordinary instance carrying its own `Source`**: `Script` that
  runs, `ModuleScript` that is required, and `require` taking an instance. There
  is no New Script dialog, because there is no file to write.
- **The Explorer badges a stamped root**, two draws with a knockout, with the
  scale, the halo and the corner read from the icon theme rather than from three
  literals in a draw call. The knockout is what makes it exist: measured across
  the class set at 16 px, 37 of 42 icons already have ink where the badge goes.
- **And what a person expects to be there already**: dragging an instance into
  the browser makes a stamp of it and dragging one out places it; `Del`, `F2`,
  `Ctrl+D` and `Ctrl+S`; and a clipboard on `Ctrl+C`/`X`/`V` and `Ctrl+Shift+V`
  that holds TEXT rather than ids — which is what lets a copy survive the
  delete, the scene load or the stamp session that happens between it and the
  paste.

#### NOT in scope

A stamp whose source is another stamp — a variant. Making one is refused
outright rather than half-answered, because which level an override belongs to
has no answer yet (ADR 0049, ADR 0051). A second content tree: `content/` holds
files and the world holds a world, and ADR 0052 is the record of the other shape
being built, tested, shipped and taken out the same afternoon, on one question
from the human that Unity and Unreal do not have two contents either. Anything
in `content/` reachable from a script: there is no global for it, deliberately,
until somebody needs one — a name on the global list is the hardest thing in
this API to take back. And retiring `src/scripts` as the mount, which every
example in this repository is built on and which is ADR 0050's open question.

#### Gate (definition of done)

- **A stamp round-trips, and a scene holds a mark rather than a copy.**
  `scene_file_tests.cpp`, four cases: the round trip, the collapse to a mark,
  changing the stamp changing every instance, and a scene naming a stamp nobody
  can supply still opening. Break-verified — with the collapse removed, three of
  the four fail.
- **An edit is an override and the link survives it**: the override written and
  read back, the source moving everything except where an instance said
  otherwise, and a structural change written in full and unlinked.
- **Make, place, break, and open onto a stage.** `editor_tests.cpp`: the file
  written and the subject converted, a stamp of a stamp refused, one undo for a
  whole placement, and the stage built with the game's world untouched — the
  test holds ids from before and checks them after, including one it retires on
  the way in.
- **A script is an instance, and a module is required.**
  `script_environment.spec.luau` asserts the new model where it asserted the old
  one, `instance_construction.spec.luau` makes both classes, and a module's
  failure is cached rather than retried.
- **The clipboard survives what happens between a copy and a paste**, asserted
  by deleting the original and pasting it back.
- **The badge's geometry lives in the theme**, asserted on a real device: the two
  ids are in the staged atlas, a theme with no overlay block gets the documented
  defaults, and one that overrides all three gets all three.
- **A badge over `class.Folder` reads at 16 px on both panels. PENDING — a
  person at a window.** The knockout is what the claim rests on and it is drawn;
  whether it *reads* is a picture, and the geometry test that passes on a real
  device is not that picture. It stays open rather than being answered by the
  test next to it, for the reason every visual row in this phase stays open: the
  ImGui shell cannot render headlessly and SDL does not accept injected input,
  so there is no automated path to a picture of this editor.
- **A human uses it and says whether it works. Pass by construction** — E3 was
  specified while being used, and four of its defects came from that.
- **`scripts/localgate.ps1` green on every stage.**


### E4 — The Editor Ships (M)

**Settled by
[ADR 0054](decisions/0054-the-editor-ships-as-a-folder-and-the-cli-finds-its-own-install.md).**
Two halves, and they are one sentence: **a person downloads the editor, and the
editor stays fast on the world they build with it.** The first is the question
[ADR 0046](decisions/0046-the-editor-is-a-mode-of-the-engine-binary.md) declined
on purpose and told this phase not to answer by accident; the second is the gate
that milestone said the editor would owe once it had one.

#### What the reconnaissance found

- **The repository already believes it has an installed shape, and it has never
  had one.** `project.luau`'s last engine candidate is `process.cwd()` under a
  comment calling it "beside this CLI"; `version.luau` reads a `CMakeLists.txt`
  an installation does not have; `new.luau` walks up looking for the layout and
  is the only one of the three that is right. Nothing noticed, because nothing
  in this repository has ever run outside it.
- **The Explorer walks the whole world every frame.** `collectTree` is a full
  preorder over every instance, called at the top of `drawExplorer`, and the
  visibility pass over its output is a second one. The *drawing* is already
  clipped — `ImGuiListClipper` over the visible rows, at an exact row pitch — so
  the panel is fast for the reason a profile would not show first: the cost is
  all in the walk, and the walk is charged for a world nobody can see.
- **The content browser is already virtualised, and the comment beside it says
  the Explorer is not.** That comment was written when neither was, and half of
  it stopped being true when the row clipper landed. It is the half that is
  cheap to fix that is still open.

#### Scope

- **An `editor` build profile**: Release, the debug UI on, the Luau compiler on,
  the C++ suite off. Built by `scripts/gates/shipping-build.sh` beside `shipping`
  and `player`, for the reason those two are there — a profile nothing builds is
  a profile nobody knows is broken.
- **`tools/repo/package.luau` and `scripts/package.ps1`**: the folder and the
  archive. The host, its content, the CLI and the pinned Lute, the template, the
  generated definitions, `assetc`, `iconpatch`, the licences and a version stamp.
- **The attributions the repository already generates**, carried into the
  archive rather than written a second time for it.
- **`installRoot()` as the one answer to where the CLI is installed**, asked by
  the engine search, the tool search, the template copy and `--version` — with
  the candidate roots passed in, so the order is a pure function a test drives.
- **The Explorer descends only into what is open.** One walk instead of two, and
  it does not enter a collapsed subtree or a generated one at all.
- **The editor's own performance gate, asserted on counted work rather than on a
  clock** — the instrument E5's partition peak argued for, applied to a panel.
- **The manual says how to install it**, and building from source stops being
  the first sentence a reader meets.

#### NOT in scope

An installer, code signing, and an update channel — each is a dependency and a
decision, and all three are about an archive that has to exist first. A Linux or
macOS package: `luaug build` is Windows-only and says so, and a packaging path no
tier here can execute is one that ships broken. A project browser or start
screen: `luaug new` then `luaug edit` is the path, and inventing a start screen
at the packaging milestone would be inventing it without having watched anybody
need it. Bundling `luau-lsp` and `stylua`, which is a redistribution question
bought for two commands that already fail politely. And making the repository
public, which is the human's and is in the ledger.

#### Gate (definition of done)

- **A person who has never built this can use it.** The archive is unpacked on a
  machine with no repository, no `LUAUG_BUILD_ROOT` and no rokit, and
  `luaug --version`, `luaug new`, `luaug edit` and `luaug build` all work from
  it. The transcript goes in the gate record.
- **The installed resolution is asserted, not described.** A test drives the
  order — an explicit environment variable, then the build tree, then the
  installation — and a second one proves the installation is found when the
  working directory is somewhere else entirely, which is the case that has never
  worked.
- **`luaug --version` answers inside an installation**, with the number the
  binary in it was built from, and the stamp names where that number came from.
- **The `editor` profile is compiled and linked by the gate**, on the tier the
  shipping stage already runs.
- **The archive carries its attributions**, generated from the manifest, and the
  licence audit is still green.
- **The Explorer costs what is open, not what exists.** A world an order of
  magnitude larger than the flagship's, with the same subtrees expanded, visits
  the **same** number of instances — equal, not merely fewer. A test asserts the
  equality, for the reason E5's peak measurement asserted one: a bound that is
  merely small passes while the defect is still there.
- **The cost is recorded as work, not as a clock.** `docs/perf-baselines.md`
  gains an editor row giving the instances the Explorer visits per frame before
  and after, on worlds whose sizes are stated — a number that is the same on any
  machine, which is what the baselines methodology asks for and what a threshold
  on a busy machine cannot be. A wall-clock impression from a person with the
  editor open belongs in the gate record beside it, where a number that depends
  on the machine belongs.
- **`scripts/localgate.ps1` is green on every stage.**

### E5 — The World You Build (L)

**Settled by [ADR 0053](decisions/0053-the-grid-decides-when-and-the-model-decides-what.md).**
Read it before planning: it carries the survey of Unreal's World Partition,
Godot's Open World Database and Roblox's `ModelStreamingMode`, and the four
rejected alternatives. This section is the scope and the gate.

**Built in one pass, not in phases.** The pieces below are one system and the
seams between them are not natural places to stop: a partitioner with no
`StreamingMode` writes the wrong cells, and `StreamingMode` with no partitioner
has no caller.

#### What the survey found already built

Two things were assumed missing and are not, and both change the size of this
milestone downward:

- **`ChunkId::layer` has existed since M7 and nothing uses it.** It was
  documented at the time as "how interiors, or a lower level of detail of the
  same ground, get addressed without a second coordinate system". The size
  classes below live there, so the on-disk `ChunkId` does not change.
- **`ChunkIndexEntry` already carries a world-space `DAABB`**, so a building that
  overhangs its cell is already described and already scored correctly. The
  straddling problem was solved before anybody asked it.

And four of the five properties Roblox exposes already exist here — `Enabled`,
`MinRadius`, `LoadRadius`, `PauseOutsideLoadedArea` — with none needing a change.
`Enabled` is the better of the pair: it **freezes** the resident set rather than
draining it.

#### Scope

- **`Model.StreamingMode`**, three values, `Nonatomic` the default:
  parts descend individually; `Atomic` materialises and evicts the model whole
  across a cell boundary; `Persistent` never enters the grid and stays in the
  scene. `PersistentPerPlayer` is **out** — it needs a per-connection player,
  which is phase 4's.
- **The partitioner.** Walks `Workspace`, computes `floor(position / chunkSize)`,
  groups by cell honouring each model's mode, writes cell sources and the index.
  **It buckets records as it reads and never materialises the world**, which is
  the whole reason it may run at play.
- **It runs on play, cached by a hash of the scene.** A shipping build pre-warms
  the same cache and is not a second code path. Rejected in the ADR: build-only,
  because the daily path would be the one that goes stale.
- **A cell holds groups.** The one file-format change: an atomic model and its
  descendants materialise together. `layer` and the index bounds are untouched.
- **Size classes on `layer`** — 0 detail, 1 structures, 2 terrain features — with
  a `minRadius`/`loadRadius` pair per layer on `StreamingFocus`. **This is the
  only change to the existing streaming runtime.**
- **Tag addressing documented as the primary path.** `TagService` already has
  `GetTagged` and the added/removed signals, and those signals are what fire as
  cells arrive and leave. The docs say so; nothing is built.
- **The replica's pause.** `PauseOutsideLoadedArea` stays the authoritative
  world's; a replica holds its camera instead. Decided now so phase 4 does not
  rediscover it.

#### NOT in scope

Baked distant geometry (HLOD). Outside `LoadRadius` there is nothing, not a cheap
version of something — and the layers push the horizon out for large objects
without being a LOD hierarchy. Named as the next wall, with Decima's twelve
person-years as the estimate.

Arbitrary hierarchy in a cell. A scene that is a *folder* of per-cell files, with
streaming while editing (Unreal's One File Per Actor) — that wall appears far
later than the one this removes. Making a materialised cell savable: the
serializer's `generated` skip stays and stays correct.

#### Gate (definition of done)

- **A world built by hand in the editor streams.** `examples/06-scene` grows a
  few hundred parts spread over more than one cell, is saved, and `luaug run`
  streams it — with no generator script anywhere in the project. A screenshot of
  the chunk-state overlay is attached to the gate record.
- **The default changes nothing.** `examples/10-open-world` runs byte-identically
  through the partitioner: the cells it produces from the flagship's scene plus
  its generated world match what `generate_world.luau` and `assetc` produce today,
  or the difference is explained in the record. This is the regression that
  matters, because `Nonatomic` is supposed to be what already happens.
- **The partitioner never holds the world.** Proven by measurement, not by
  inspection: peak resident instance count during a partition of a world with
  N instances is bounded by a constant, not by N. A test partitions a synthetic
  world an order of magnitude larger than the flagship's and asserts it.
- **An atomic model crosses a boundary and arrives whole.** A model whose parts
  straddle two cells materialises in one frame with every descendant present, and
  evicts with none left behind. Driven headlessly.
- **A persistent model is never in a cell.** It is in the saved scene, it exists
  before the first tick, and no eviction touches it — including at four
  kilometres from the origin, where its cell would long since have gone.
- **The cache is a cache.** Editing the scene and pressing play repartitions;
  pressing play again does not. Asserted on the hash, not on wall-clock time.
- **Layers separate.** With three layers configured, a large object stays
  resident at a distance that has already evicted a small one, and the overlay
  shows both states. A unit test over the scoring covers it without a window.
- **`luaug check` and the full local gate are green**, and the docs say what a
  script may assume about a streamed world — the tag path written down, with the
  `nil` a path reference may return stated rather than discovered.

### E6 — The Launcher (M)

**Settled by
[ADR 0055](decisions/0055-the-launcher-is-the-engine-with-no-project-open.md).**
Opened 2026-08-24, on the human's word, an hour after E4 was signed off and for
the reason E4's own scope list predicted: the archive was unzipped and the first
question was *what do I open*. E4 named the absence deliberately — a project
browser invented at a packaging milestone would have been invented without
having watched anybody need one. Somebody has now needed one.

**The quality bar was given in those words: Unity and Unreal.** What that means
here is stated rather than left to taste, because the three comparison points do
not agree with each other and one of them is answering a question this engine
does not have. Unity's Hub is a separate application whose real job is choosing
*which editor version* a project opens with; there is one engine in a LuauG
installation, and a folder holding two of them is two folders. Unreal and Godot
both put the browser in the editor binary. So does this.

#### Scope

- **A launcher shell.** `luaug-host` with no project shows a project browser
  instead of printing a usage error: `Shell::Launcher` beside the F3 overlay and
  the editor's dockspace, drawn by the same overlay, with no world, no Luau VM
  and no physics behind it.
- **Recent projects, per user.** `SDL_GetPrefPath` supplies the user directory
  `platform.h` has been describing since M1 as arriving with its first consumer.
  A project that has moved or been deleted is shown as missing and removable
  rather than silently dropped.
- **Creating a project**, from the template `luaug new` already scaffolds, with a
  name and a location — and the two must produce identical trees.
- **Opening a project by browsing for it**, through the native folder picker, and
  by typing a path where there is no picker.
- **Choosing a project starts the editor as a new process** and the launcher
  quits. What a project decides — content mounts, the Luau VM, the `.luaurc`, the
  partition cache, the editor layout — is everything the host resolves at boot.
- **`luaug edit` with nowhere to go opens it too**, rather than reporting that
  the working directory is not a project.

#### NOT in scope

Managing several engine versions, which is the job Unity's Hub exists for and
which this engine does not have. A project list that syncs between machines. A
template gallery: the launcher lists the template directory, so a second template
is a directory rather than a feature. Cloning a project from a repository.
Anything that opens a project without starting the editor.

#### Gate (definition of done)

- **Double-clicking the engine opens the launcher.** From the packaged folder,
  with no arguments, on a machine with no build tree. A screenshot goes in the
  gate record.
- **The model is asserted without a window.** The recents list round-trips
  through its file, deduplicates by path, orders by most recent, keeps a missing
  project visible and removable, and refuses a directory that is not a project —
  all in `launcher_tests.cpp`, with no ImGui in the header it tests.
- **The launcher and `luaug new` scaffold the same project.** A test creates one
  each way and compares the trees file by file, because two implementations of
  one thing that nothing compares are two implementations that have already
  drifted.
- **A created project opens.** Made in the launcher, started by it, and the
  editor comes up on it — the loop a person actually performs, driven as far as a
  window allows and recorded for the part that needs a person.
- **No native dialog is required.** With `SDL_DIALOG` unavailable or refused, the
  path field still opens a project, and the launcher says why the picker did not
  appear rather than doing nothing when the button is pressed.
- **The engine still refuses what it refused.** A host given a path that is not
  there reports it exactly as before; the launcher is what happens when there is
  no path at all, not a fallback that swallows a mistyped one.
- **`scripts/localgate.ps1` is green on every stage.**


### E7 — The Look (M)

**Settled by
[ADR 0056](decisions/0056-the-shell-has-one-theme-and-it-is-square.md).**
Opened 2026-08-24, on the human's word, an hour after E6 was built and for a
reason six milestones had been carrying: nothing in E1 through E6 ever decided
what the editor should look like, so the default decided for them. The brief was
given as three words and one shape -- **clean, simple, professional**, and
**square** -- and it covers the editor and the launcher alike, because they are
two shells of one binary and a person meets the launcher first.

**What the default had been deciding.** `ImGui::StyleColorsDark()` called once at
construction and never touched again; no font loaded at all, so every shell drew
in ImGui's built-in 13-pixel bitmap face while `content/fonts/Inter.ttf` sat
staged beside the binary for the game's own text; nine colours written out as
literals at their call sites, three of them the same orange in three places; and
nothing scaling with the display, though `platform::windowDisplayScale` has
existed since M6.

#### Scope

- **A theme is data.** `engine/app/ui_theme.h`: eleven palette tokens and one
  table of metrics, from which `applyTheme` derives ImGui's sixty-odd colour
  slots. The only file in the repository that decides a colour or a corner
  radius.
- **Square, everywhere there is a corner.** One number in `ThemeMetrics`, zero,
  written into all eleven of ImGui's rounding members -- and a border colour that
  is load-bearing rather than decorative, because with no radius the line is what
  separates two panels.
- **A palette that is measured rather than argued about.** Every foreground token
  clears 4.5:1 (WCAG 2.1 AA) against every ground it is drawn on: the window, a
  field, and a hovered row. Computed by `contrastRatio` and asserted, not
  commented.
- **Two themes**, dark and light, because one palette behind an abstraction is an
  abstraction with no second case.
- **Inter as the shell's typeface**, from the content already staged beside the
  binary, with the built-in face as a stated fallback rather than a silent one.
- **The shell scales with the display**, and a person can override it. Per user
  in `<userDir>/appearance.json` beside the launcher's recents, because it is a
  fact about their eyes and not about a project -- which is also what lets the
  launcher have the setting at all.
- **The launcher, laid out for somebody arriving**: a header band carrying the
  wordmark, the project list as the screen's subject, making a project before
  opening one, and every message in one place at the bottom rather than under
  whichever column produced it.
- **The editor's panels named the way an application names them** -- Title Case,
  which costs one layout file name.

#### NOT in scope

Themes loaded from a file, chosen by a plugin, or authored outside the
repository: a theme is a struct, and a fourth one is somebody writing a struct.
Per-theme metrics. A syntax-highlighting palette for the console or for a script
editor, which is a different problem with a different set of tokens. A second
typeface -- the engine has one face by human decision (M7). Icon themes, which
`icons/README.md` already owns and which are chosen separately. Anything that
changes what a panel DOES.

#### Gate (definition of done)

- **The palette is legible, and the test says so rather than the author.** Every
  theme, every foreground token, against the window background, a field and a
  raised surface, at 4.5:1 -- in `ui_theme_tests.cpp`, with no ImGui in the
  header it tests. Two of the first values committed failed this and were
  changed because of it.
- **The shell is square, asserted.** `themeMetrics().rounding == 0`, and a border
  that is not also zero.
- **The appearance survives a process, and a broken file does not break the
  shell.** Round-trip through `appearance.json`; a missing file, an unparseable
  one, a theme name this build does not carry and a scale that is not a number
  all open on the default.
- **Both themes are looked at, in both shells.** Screenshots in the gate record.
  The same limit E1 recorded applies -- the ImGui shell cannot render headlessly
  and SDL does not accept injected input -- so this one is a person, and the
  pictures are taken off the running window.
- **The typeface actually reaches the window.** A screenshot in Inter rather than
  in ProggyClean, and the fallback said out loud in the log when the content is
  not staged.
- **`scripts/localgate.ps1` is green on every stage.**


### E8 — The Script Editor (XL)

**Settled by
[ADR 0057](decisions/0057-a-script-is-an-instance-and-the-editor-edits-one-thing.md).**
Opened 2026-08-24, on the human's word, immediately after E7. Six editor
milestones had built an Explorer, a Viewport, a Properties grid, a Content
browser and a Console, and none of them had built a place to put code -- the
Properties panel drew `Script.Source` through a 256-byte buffer and refused to
edit anything longer, so the one property carrying a game's behaviour was the one
the editor could not touch.

**The quality bar was given as Roblox Studio, and the debugger was named
explicitly.** So was the shape: several scripts open at once, as tabs where the
Viewport is, and draggable out to sit beside the world.

**And the model underneath was half-built.** ADR 0050 decided a script's source
is a property of the instance; the mount never wrote it and `startScripts` never
read it. Files that ran without a `Source`, and instances with a `Source` that
never ran. The human settled it in one sentence -- *the only way to run a script
in the game is by instance* -- and that is what E8 implements first, because a
tab has to edit one thing.

#### Scope

- **An instance is the only thing that runs.** `startScripts` walks the world in
  document order and starts every enabled `Script` from its own `Source`; the
  mount reads the files under `src/scripts` and puts their text into the
  instances it creates.
- **A code pane drawn by hand**, because `InputTextMultiline` renders in one
  colour with no per-token hook, has no length guard on its multiline path, and
  keeps a single `ImGuiInputTextState` for the whole context.
- **Tabs in the central dock node**, siblings of the Viewport, draggable out.
- **Luau colour from Luau's own lexer**, incremental: an edit re-lexes the lines
  whose incoming state changed and not one more.
- **Find, replace and go to line**, and syntax errors underlined where
  `Luau::Parser` puts them -- the first structured diagnostic this engine has
  had.
- **Autocomplete from the world as well as the API.** A dotted path walks the
  real tree -- `Workspace.MainCamera`, `game.Workspace.Beacon`, `script.Parent`
  -- and offers that instance's children beside its class's members; a name
  inside `WaitForChild("` or `FindFirstChild("` is completed from the same walk,
  and `GetService("` from the service flag. A step may be a call that names
  something (`game:GetService("AudioService").`) or a local somebody assigned
  earlier. One row per name, and a string that is nobody's argument offers
  nothing.
- **Luau's own surface, from the pin.** `typeof`, `pcall`, `assert`,
  `math.floor`, `string.format`, `table.create`, `buffer.readf32`, and the
  library names themselves. Written down in `engine/script` because it is a fact
  about the VM, and checked against a real sandboxed VM in both directions -- so
  a Luau bump fails a test rather than leaving the editor a version behind. It
  is THIS engine's surface: `os` has three names, and `getfenv` is not offered
  because it is not there.
- **The gestures a code pane is expected to have.** Ctrl and the wheel zoom the
  text, with the percentage and `Ctrl+0` shown in the corner for as long as it
  has just changed -- a zoom with no readout is a state somebody can get into
  and not out of. A double-click takes the whole word and dragging from it
  extends a word at a time. Alt+Up and Alt+Down move the selected lines, in one
  undo.
- **Autocomplete from `ClassRegistry`**, with the IDL's own prose as the hint.
  Not from `Luau.Analysis`, which is deliberately not built.
- **A debugger**: breakpoints, continue, step over, into and out, the call stack
  and the locals. The script parks on `LUA_BREAK` and the frame loop keeps
  drawing; `allowedTicks` answers zero so no tick begins while it is stopped.
- **Ctrl+S writes where the instance came from** -- its file, or the scene --
  and does nothing else. It does not reload: play is what compiles `Source`
  (ADR 0058), so in the editor there is no running chunk to refresh.
- **Eight syntax tokens in the theme**, measured against the code pane's ground.

#### NOT in scope

Type inference: a local holding an instance is not resolved, and that is
asserted as an absence rather than left to be reported as a defect. Editing a
variable from the debug panel (`lua_setlocal`), which is a divergence generator.
A protocol for an external debugger. Multi-cursor, folding and a minimap: the
pane is built so they are additions rather than rewrites, and none is promised.
Native codegen, which would silently take breakpoints and locals away -- the
engine links `Luau.CodeGen` and never calls it, and that is now a debugger
invariant written where whoever enables it will read it.

#### Gate (definition of done)

- **A script that is an instance runs.** A `Script` the scene brought runs, a
  disabled one does not, a `ModuleScript` still only runs when required, and a
  mounted script carries its file in its own `Source` -- four cases in
  `world_host_tests.cpp`, with the determinism traces re-recorded on both tiers.
- **An edit costs the lines it reached, asserted as an equality.** Typing one
  character in a 200-line file and in a 20,000-line one both re-lex exactly one
  line; opening `--[[` at the top of a 500-line file re-lexes 500, once, and the
  next keystroke inside it re-lexes one.
- **A cell is a codepoint and a column is bytes**, asserted over a two-byte and
  a four-byte codepoint, both directions. This is the defect a person typing
  Portuguese found and every ASCII test passed through.
- **The debugger stops, reads and resumes, with no window.** A breakpoint set
  before the world runs is bound when the chunk loads; one on a comment lands on
  the next line with code and says which; the locals read back by name;
  continue finishes the script; a step stops on the next line; detach forgets a
  parked thread; and **the engine keeps running while a script is parked**.
- **Completion answers from the reflection tables**, including inherited members
  and the IDL's prose, and answers nothing for a local it cannot resolve.
- **Eight syntax tokens clear 4.5:1** against the code pane's ground in both
  themes, by the same `contrastRatio` the interface tokens use.
- **A person writes a script in it.** Open two scripts as tabs, type in both,
  break a line and see the mark, set a breakpoint, press play, watch it stop
  with the locals showing, step, continue, save, and see the world reload.
  Screenshots in the gate record, in both themes.
- **`scripts/localgate.ps1` is green on every stage.**

### E9 — Compiled Assets and a Skeleton You Can Touch (XL)

**Complete, and it is the first milestone in this phase that was built before
anything wrote down what it was.** Its plan lived outside this repository and its
decisions lived in commit messages;
[ADR 0060](decisions/0060-a-material-is-an-instance-and-a-stamp-is-how-one-is-shared.md)
and [ADR 0065](decisions/0065-a-loose-gltf-is-not-a-runtime-format.md) are the
records step 15 owed. The
reconnaissance and the account of what is actually in `main` are in
`docs/briefs/e9-kickoff.md`; this section is the scope statement and the gate,
because a milestone that is being deferred against by number needs something to
close against.

**Opened 2026-08-25 by importing a downloaded model**, which exposed the whole
seam in one afternoon: the file was refused outright, then loaded lying on its
back, then turned out to carry 677 joints against a 64-matrix palette. Three
fixes landed and none of them was the problem. The shape underneath is wrong in
four connected ways. **The runtime parses vendor formats** — a loose `.gltf` is
re-parsed on every launch, with no LODs, no meshlets and textures uploaded as
raw RGBA8 with no mips, while `assetc`, which does all of that, exists and the
editor does not call it. **A model arrives as one opaque `MeshPart`**, so five
materials become five submeshes of one part with nothing to select and nothing
to give a material to. **There is no material** — `MaterialDef` is embedded in
the mesh, so two parts cannot point at one. And **the skeleton is invisible and
untouchable**: joints are a flat array in a render-side library keyed by URN, so
nothing in `scene` or `physics` can read a joint transform, which means there is
no socket to weld to and no path for physics to drive a pose.

#### Outcome

**You drag a model in and it becomes a `Model` you can open, with named parts you
can select and materials you can edit; you scale it with one number; you weld a
sword to a hand; and you turn a character into a ragdoll.** Compiled,
incremental, and fast enough that none of it is a wait.

Where it stands against the plan's fifteen steps, read off the tree rather than
off the plan:

- **In `main`:** steps 1 through 11 and step 13, twelve of the fifteen —
  `core::cframeFromMatrix` and the pivot helpers lifted into
  `engine/scene/src/pivot.cpp`; the object mount on `ContentMounts`;
  `Model.Scale` and `MeshPart.MeshSize`; the constraint seam on `IPhysics3D`
  and its Jolt backend; `scene::SkeletonHost` in both directions;
  `splitByPrimitive`; the sRGB fix, which is `encodeTexture` taking what KIND of
  data the pixels are rather than assuming colour; `Attachment` and `Bone`;
  `BallSocketConstraint`, `HingeConstraint` and `FixedConstraint`;
  incrementality in `assetc`; and `Ragdoll`, driven every tick as a joint
  override, with `Bone.Transform` beside it.
- **Reversed, and it is one of those twelve:** step 3. A material was to be a
  `content/*.material.json` file written by the import. It is an **instance**,
  and what a project keeps in `content/` is a **stamp** of one — which is E3's
  mechanism doing this job rather than a second answer to all of it.
  `BasePart.Material` points at either, and `BasePart.Color` multiplies it, so
  white on both sides is the identity and no existing scene changes. **Settled
  by ADR 0060**, written after the reversal had already shipped, which is the
  order this milestone has done everything in and the reason step 15 exists.
- **Also in `main`, and each was the unbuilt half of a step the count said had
  landed** — which is the part a step count hides: `assetc::importOne`, so the
  editor's import and `assetc` are one call rather than two code paths; the
  skeleton overlay and the joint picker, so `JointName` stopped being free text
  typed from memory; parallel texture encode with the `--jobs=1` leg that proves
  it changed nothing; and `Ragdoll:Build(profile)` with `Ragdoll.Blend`.
- **Step 12:** the editor compiles on import — the object-store writer,
  `luaug_assetc_lib` linked into the app under `LUAUG_DEBUG_UI`, the store
  mounted between the source tree and the pack, and `splitByPrimitive` reaching
  both a pack and a world as a `Model` of named parts.
- **Step 14, the cut-over, last because it was the only irreversible one:** a
  loose `.gltf` no longer feeds the runtime
  ([ADR 0065](decisions/0065-a-loose-gltf-is-not-a-runtime-format.md)). There
  turned out to be **three** loose feeds rather than the one the plan named —
  `MeshLoader::sync`, `syncSkeletons` on the sim thread, and `syncTextures`,
  which had no compiled branch at all and so read the raw PNG sitting beside a
  `.ktx2` the compiler was already producing and nobody was reading. Opening a
  project now compiles what has no compiled form in EVERY host mode, so a clone
  from git still works with no command. One golden moved:
  `tests/screenshots/lavapipe/specular.png`, by a single pixel.
- **Step 15:** `tests/bench/{ragdoll10, sockets200}`, the E9 block in
  `docs/perf-baselines.md`, both decision records, and both soak ceilings
  re-measured — `streaming_soak` 192 MiB to 96, `openworld_soak` 384 to 192.

#### Gate (definition of done)

- **The editor's import and `assetc` produce the same bytes**, per URN and per
  blob, because they are the same call — `import_matches_build` asserts it
  rather than the two paths being compared by eye.
- **A re-import of an unchanged tree does no work**, asserted as an equality:
  `texturesEncoded == 0 && meshesCompiled == 0`. A threshold would pass while
  the defect was still there.
- **Parallelism does not change a byte.** `asset_determinism` gains a `--jobs=1`
  leg that must be byte-identical to the parallel one.
- **The constraint seam does not break R10.** Two worlds built by the same call
  sequence are bit-identical after 300 steps; `tests/determinism/ragdoll` runs
  four ragdolls for 3000 ticks, checkpointed; and `tests/determinism/churn` and
  `character` **must not move**, verified before landing rather than after.
- **The four traps the Jolt backend has to survive are each asserted**, because
  every one of them is silent when missed: a constrained body that is rebuilt
  does not dangle its constraints, a destroyed body drops them, constraints are
  retired before bodies, and `collideConnected = false` really excludes the pair.
- **A `Bone` that names a joint the rig does not have resolves to `-1` and falls
  back to its parent**, not to the origin.
- **The existing benches are unchanged within noise** —
  `tests/bench/{physics1k, churn10k, instances500, crowd50, platforms200}` —
  because everything in Part B is behind an empty container. New:
  `tests/bench/{ragdoll10, sockets200}`.
- **`streaming_soak`'s memory ceiling is re-measured**, not carried: BC7 and
  mips reach editor content for the first time, and a ceiling that stays
  generous after the thing it bounds got four times cheaper no longer bounds
  anything.
- **A person imports the model this milestone was opened for**: drags it in,
  gets a `Model` with named parts and materials in the browser, sets
  `Model.Scale` and takes it back with one undo, welds a part to a `Bone` and
  sees it follow, builds a ragdoll and watches the skinned mesh deform with it,
  and reopens the project with nothing recompiled.
- **`scripts/localgate.ps1` is green on every stage**, before each step rather
  than at the end — `-Only linux` included, because this milestone touches six
  modules and Clang diagnoses what MSVC does not.
