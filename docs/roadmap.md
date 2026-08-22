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

- **Scope — the defects, each with the evidence that found it.**
  - [ ] **`Lighting` is unreachable from the renderer.** `WorldHost::start`
        caches `findFirstChildOfClass(dataModel, Lighting)` before any script
        runs, under a comment claiming the service exists by then. `services.cpp`
        states the rule one line from where it is broken: `Workspace` and
        `ScriptService` exist from boot, every other service is created by its
        first `GetService`. The fix is boot order — `Lighting` joins those two,
        for the identical reason: `extract` reads it every frame whether or not
        a script ever asks for it. Resolving lazily on each miss is smaller and
        leaves the same trap one refactor away.
  - [ ] **A host-level test for it.** `render_world_tests.cpp` builds a
        `Lighting` instance itself and hands its id straight to `extract`, so
        every environment assertion passes against an id the host never
        produces. The untested step is the one that was broken. The test belongs
        at the host, and it must fail if the id is resolved before the service
        exists.
  - [ ] **Re-record every M4 gate artifact afterwards.** The goldens, the
        lavapipe attempt and the 1080p baseline all describe a scene lit by the
        wrong sun. A number recorded against a defect is not a baseline, and
        M5's "no >10% regression" clause would be measured against it.
  - [ ] **`BasePart.Transparency` must actually fade, by human instruction on
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
  - [ ] **The shadow grid crawls.** The ortho box is centred on the camera (the
        snapshot is camera-relative and `sunViewProjection` looks at the
        origin), so an orbiting camera slides the texel grid 0.42 of a texel per
        frame at the example's speed. Snap the box centre to texel increments in
        light space; extent and resolution are both compile-time constants, so
        the increment is one too. The rotational half — a moving sun turning its
        own grid — is a separate problem needing normal-offset bias; it is
        visible only while `ClockTime` moves and is not required here.
  - [ ] **`PointLight.Shadows` and `SpotLight.Shadows` accept a write and change
        nothing.** Stored, extracted, never read: one cascade from the sun is
        all this release has. The M4 brief names it as deliberate, in a C++
        comment and a NOT-in-scope list — neither of which is where the person
        clicking the inspector reads. Decide it the way Transparency was
        decided: honour it, or remove the property until the milestone that
        renders it.
  - [ ] **The pivot is a `Model` concept, and in the reference API it is not.**
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
  - [ ] **The crash handler and the log file sink.** `architecture.md` §app
        promises "crash handler (minidump + log)" and neither exists. A human
        running the engine by hand is this project's verification model, and has
        now reported five defects from memory. The handler is the half that
        matters — a captured crash held two lines, because `core::log` already
        flushes per line and the process died without reaching any C++ path.
        `core` is L0 and `platform::paths()` is L1, so `app` injects the path at
        boot.

  - [ ] **A crash while editing `Size` and `CFrame` in the inspector, still
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
  - [ ] **The inspector marks a property with no consumer.** All three of the
        unbacked properties above were found by a human changing a value and
        watching nothing happen. The descriptor table already knows each
        property's backing; what it cannot say today is whether anything reads
        it. Whatever the mechanism, the requirement is that the panel
        distinguishes "written and acted on" from "written and stored" — that
        distinction is the only defence `instances.api.luau`'s own rule has.
  - [ ] **Read `architecture.md` §app against reality, once, as a list.** Four
        items so far had no milestone owner and each was discovered separately:
        the `DebugShell`, the api-dump, the triangle sample, and now the crash
        handler. The point is to find the fifth before a human does.
  - [ ] **A milestone-close rewrite must not drop open defects.** Three
        human-reported items were removed from `PROGRESS.md` — not archived —
        while it was being rewritten to close M4, on the day the human is asked
        to sign it off. Whatever enforces it, the ledger's open items have to
        survive a close.

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
  answered before the gate hardens rather than after it breaks.
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

**The sequence, and only E1 is specified.** E2 through E5 are ordered intent in
the same sense as the phase list above: each gets its detail and its gate at its
own kickoff, from what the milestone before it learned. Writing five gates today
would be writing four of them from a position that has not seen an editor run.

**Re-cut 2026-08-22, at E1's review, by human decision.** The first sequence
below put manipulators in one milestone, saving in another and play in a third —
which optimised for whoever implements it and not for whoever uses it. The
review said so in one sentence: *an engine without stop is not an engine — how
are we going to edit, test and save?* **Edit, test and save are one loop**, and
an editor that delivers a third of a loop three times is not usable in between.
So E2 is now the loop, whole, and it is large on purpose. ADR 0047 is the
decision underneath it: the authored world becomes data and scripts become
behaviour, the way Unity, Unreal and Roblox all arrange it.

| ID | Name | Size | Runnable artifact |
|----|------|------|-------------------|
| E1 | The Editor Opens | M | `luaug edit examples/10-open-world`: docked panels, the world in a viewport, click a tower and its properties are there and editable. **Built, awaiting review** |
| E2 | The Loop — Edit, Test, Save | XL | Open a scene, move something, press play, watch it run, press stop and get your edit back, press save, close the editor, open it again and it is there |
| E3 | The Editor Changes Things | L | Manipulators, create/delete/rename/reparent, undo/redo, multi-select, an editor camera — the authoring the loop makes worth having |
| E4 | Assets and Prefabs | M | An asset browser, prefabs as scenes, and a scene that references what it uses |
| E5 | The Editor Ships | M | The distribution question ADR 0046 deliberately declined, and the editor's own performance gate |

**Why E3 comes after E2 rather than before it, which is the opposite of the
first cut.** A manipulator without a save is a way to lose work, and an undo
stack designed before there is a scene to undo *into* is an undo stack designed
twice. The loop first makes every one of E3's tools land against something that
keeps them.

### E1 — The Editor Opens (M)

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
