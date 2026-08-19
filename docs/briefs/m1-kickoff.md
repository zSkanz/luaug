# M1 Kickoff — Window, RHI, Frame Loop, Agent Eyes

- Started: 2026-08-19
- Roadmap section: [docs/roadmap.md](../roadmap.md) § "M1 — Window, RHI, Frame
  Loop, Agent Eyes (M)"
- Previous milestone: [m0-kickoff.md](m0-kickoff.md) — read its **Findings**
  before touching the build.

## Goal (restated)

M0 proved the engine can run a script. M1 must make it *show* something and,
more importantly, make it able to **look at what it showed**. Two deliverables
sit behind one milestone: a real frame — SDL3 window, a swappable RHI with the
SDL3 GPU backend, HLSL shaders compiled at build time, immediate-mode debug
draw, a fixed-tick accumulator loop — and the self-observation harness
(`--headless --frames N --screenshot out.png --exit` plus the `rhi_capture`
command-stream backend) that every later gate in this project depends on.

The ordering matters and is not negotiable: the roadmap's own principle is
*"the agent needs eyes before it needs features"*. An autonomous builder that
cannot see its own output ships green-but-broken visuals, and every milestone
from M4 onward gates on screenshots and capture hashes. So the harness is not a
nice-to-have at the end of M1 — it is the milestone's real product. The pulsing
clear colour and three orbiting cubes exist mainly to give the harness
something to be right or wrong about.

Architecturally, M1 is where the **backend seam** stops being a document and
becomes code: `rhi_api` is header-only and must not mention a single SDL type
(R17, ADR 0005); the proof is that `rhi_capture` and `rhi_null` compile against
the same headers and that `app`'s hand-written factory (ADR 0023) is the only
place that knows which one exists.

## Scope checklist (from roadmap)

- [ ] SDL3 init / window / event pump — the `platform` module (L1), the only
      module besides `rhi_sdlgpu` and `app` glue that may touch SDL (ADR 0004)
- [ ] RHI v1: the `IRenderer`/`RenderWorld` contract per architecture.md
      (ADR 0027) and the ~40-call `rhi_api` (L2, header-only)
- [ ] `rhi_sdlgpu` backend (v1 default, ADR 0005)
- [ ] `rhi_capture` and `rhi_null` backends compile
- [ ] HLSL shaders via SDL_shadercross with an on-disk shader cache (ADR 0006,
      `cmake/luaug_shaders.cmake`)
- [ ] Immediate-mode debug draw: lines, wire/solid cubes, text — deliberately
      early, because M2/M3/M5 visualize through it before a real renderer exists
- [ ] ImGui docking overlay bound to F3 (ADR 0011, dev builds only)
- [ ] Frame loop with the fixed-tick accumulator (architecture.md §3); tick
      logic itself stays stubbed — the kernel is M2
- [ ] Headless mode: `--headless --frames N --screenshot path --exit` via an
      offscreen surface + readback, exercised in CI (WARP/software fallback on
      the Windows runner; if no CI GPU path works, a scripted local gate on the
      dev machine — recorded in the gate log either way)
- [ ] Nightly Android NDK cross-compile job (non-blocking) covering
      `platform` + `rhi_api` + `rhi_sdlgpu` + the triangle sample
- [ ] `examples/00-clear`: clear colour pulses, three debug cubes orbit, driven
      from a Luau script through a **temporary minimal binding that M2 replaces**
- [ ] `tools/imgcmp` — the screenshot tolerance comparator (named by the gate)
- [ ] First golden capture-stream checked in, plus a reference screenshot

## NOT in scope

Written down because each of these is one small step away from something M1
does do, and the cost of drifting into them is a milestone that never closes.

- **The kernel.** No ECS, no Instance facade, no `game`/`workspace`, no
  services, no deferred signals, no `task` library. The accumulator loop exists;
  what it ticks is a stub. All of it is M2, and M2 is the largest milestone in
  the project precisely because it is not smuggled in early.
- **The real renderer.** `renderer_default` (CSM → depth → clustered forward →
  sky/fog → tonemap) is M4. M1 ships debug draw and a clear colour. The
  `IRenderer` *interface* is defined here; the only implementation is the debug
  one.
- **Meshes, materials, textures, lighting, cameras as Instances.** M4.
- **Asset pipeline, `asset` module, glTF, KTX2.** M4/M7. Shaders are compiled by
  CMake in M1, not loaded through `asset`.
- **The in-game UI tree** (`ScreenGui`/`Frame`/Clay). M6. ImGui in M1 is the
  dev-only debug overlay, which is a different thing (ADR 0011).
- **Input as the Action System.** M1 pumps SDL events and may read a key for
  F3; `InputAction`/`InputBinding`/`InputContext` (ADR 0029) are M6.
- **The `jobs` module.** Nothing in M1 needs a work-stealing pool, and adding
  one without a consumer is speculative. It lands when a real parallel workload
  arrives.
- **Hot reload / `luaug` CLI.** M3.
- **A render thread.** The extract/render split exists as a data contract
  (`RenderWorld` is a POD snapshot) precisely so that this stays a later,
  cheap change. M1 runs single-threaded.
- **Real-image golden comparison as a blocking gate.** The blocking render gate
  is the capture-stream hash. Image goldens are nightly and non-blocking until
  they have been stable for two milestones (roadmap, platform tiers).
- **Actual mobile support.** The Android job is compile-only and non-blocking.
  The device checkpoint is a human escalation before the RHI freeze at end of
  M4, not now.

## Planned order of work

Layer order, each step ending somewhere the repo is green and committable:

1. **SDL3 into the build.** Vendored and pinned at 3.4.14 since M0 but
   deliberately not compiled. Turn on only the subsystems M1 needs and measure
   the cold-build cost before and after — see Risk 1.
2. **`platform` (L1).** `Window`, `pumpEvents`, `nowNs`, `paths()`,
   `setThreadName`. No SDL type in the public header. Absorb the M0 carried-
   forward item: console output on Windows should stop depending on a codepage
   side effect (`WriteConsoleW` when stdout is a console).
3. **`rhi_api` (L2, header-only) + `rhi_null`.** Freeze the interface *first*
   and prove it compiles with a backend that does nothing. This is the point
   where the interface can be handed to parallel implementers (§7).
4. **`rhi_capture`.** Canonical, quantized command-stream recording. Cheap to
   write, and it is the blocking render gate — so it exists before the backend
   that could make it lie.
5. **`rhi_sdlgpu`.** The real backend. First light: a cleared swapchain.
6. **Shaders.** `luaug_add_shaders()` → SDL_shadercross → SPIR-V/DXIL/MSL +
   manifest, with the on-disk cache. See Risk 2 — this is the step most likely
   to escalate.
7. **Frame loop + debug draw.** `FrameScheduler` with the accumulator; debug
   draw as the first real user of the RHI.
8. **Headless + screenshot + `tools/imgcmp`.** The eyes.
9. **ImGui overlay (F3).**
10. **`examples/00-clear`** and its temporary Luau binding.
11. **Nightly Android NDK job**, golden capture, gate record.

Steps 1–5 are the milestone's spine. If M1 has to be split across many sessions
(it will), the split points are after 3, after 5, and after 8.

## Subagent plan

Per MASTER_PROMPT §7 — fan out only where an interface is already frozen and
compiling, and never for seams.

- **Orchestrator-only** (single-threaded, me): SDL3 build integration,
  `platform`, the `rhi_api` header design, the frame scheduler, the shader
  pipeline, all CMake, all gate runs. Every one of these is either a seam or a
  cross-cutting concern, which §7 explicitly excludes from fan-out.
- **Candidate fan-out, only after step 3 lands and `rhi_api` compiles:**
  `rhi_capture` and `rhi_null` are two independent implementations of a frozen
  header — the exact shape §7 sanctions. `tools/imgcmp` is fully independent of
  everything else and can run in parallel with anything.
- **Adversarial reviewer** on the `rhi_api` header and on the `rhi_sdlgpu`
  diff, briefed to attack: backend types leaking into public headers (R17),
  hardcoded user-facing strings (R3), wall-clock reads inside anything the
  simulation will later observe (R10), and layer violations (architecture.md
  §2) — citing rule numbers.
- **Research verifier** for SDL3 GPU API questions, answering only by quoting
  `third_party/sdl3/include/SDL3/*.h` with file and line (§9). No API signature
  gets written from memory.

## Gate checklist (verbatim from roadmap)

- [ ] Tier-1 runs windowed and headless
- [ ] first golden capture-stream checked in (plus a reference screenshot with
      the tolerance comparator `tools/imgcmp`)
- [ ] GPU validation layer clean
- [ ] Tier-2/Tier-3 compile

Note on Tier-3: macOS is not in `ci.yml` today — M0 shipped Windows + Linux.
The gate says Tier-3 must **compile**, and the roadmap's tier table says macOS
"must compile from M4". These are in tension; the M1 gate wording is the
narrower contract and wins, so a macOS CI job is part of this milestone. It is
compile-only — no runtime verification, which stays post-v1.

## Risks entering this milestone

1. ~~**Build time.**~~ **Retired for SDL3, still open for Luau** — see Finding 1.
   Measured rather than assumed: SDL3 costs **5.6 s**, not minutes. M0's ~8-min
   cold build is essentially all Luau, so the lever that matters is the
   carried-forward `Luau.Analysis` trim, not anything M1 adds. Re-measure when
   SDL_shadercross and ImGui land.
2. **SDL_shadercross's own dependencies.** ~~Expected~~ **Confirmed, escalated,
   and decided** — see Finding 7. The guess in this row was wrong in the way
   that mattered: DXC is not needed "for DXIL", it is the only HLSL front-end
   for every output format, so there is no DXC-free configuration that keeps
   ADR 0006. The human chose the prebuilt-DXC path on 2026-08-19.
3. **Headless GPU on CI runners.** The roadmap already anticipates this and
   permits a scripted local gate on the dev machine. `rhi_capture` is
   deliberately GPU-less so the *blocking* gate never depends on the answer.
4. **`--version`-style provenance for a growing manifest.** Three pins today,
   likely six by the end of M1. The generated provenance header currently
   special-cases Luau; it should generalize rather than grow a branch per
   dependency.

## Findings (things the docs assumed that reality corrected)

1. **SDL3 is cheap to build; the ~8-minute cold build is entirely Luau.**
   Measured on the dev machine, `win-msvc-dev`, after `ninja -t clean
   SDL3-static`:

   | what | compilation units | wall time |
   |---|---|---|
   | SDL3 static, M1 subsystem set | 228 | **5.6 s** |
   | full cold build at M0 close | — | 8 min 26 s (7 min 59 s compiling) |

   The "~2200 files" figure that made build cost risk 1 counts SDL's entire
   source tree across every platform and subsystem; a single-platform build
   with audio, render, joystick, haptic, hidapi and sensor off compiles 228
   units. So the only build-time lever worth pulling is the carried-forward
   one: `Luau.Analysis` is compiled and never linked (M0 Finding 9). Do not
   spend M1 time on `sccache` on the strength of a guess about SDL.

2. **Disabling an SDL subsystem does not remove it from the build**, it swaps
   the real drivers for dummy ones — `SDL_dummyaudio.c`, `SDL_dummysensor.c`,
   the dummy joystick/haptic/camera/tray/dialog backends all still compile.
   The API symbols stay present and fail at runtime instead of vanishing at
   link time. Useful to know before assuming a subsystem toggle is what makes
   a call unavailable: it is not, and `platform` must not rely on it for that.

3. **`SDL_GetTicksNS` reads 0 before `SDL_Init`.** Its epoch is SDL library
   initialization, not the process. A frame loop or a profiler built on it
   silently measures startup as zero and then reports one impossibly long first
   frame. Caught by a test that passed standalone and failed under CTest
   depending on which case ran first — the kind of order dependence that is
   painful to find later. `platform::nowNs()` uses `std::chrono::steady_clock`
   instead: monotonic by standard guarantee, no bring-up, same resolution on
   every target.

4. **Changing an `option()`'s default does not reach an existing build
   directory.** CMake's `option` only writes the cache when the entry is
   absent, so flipping a default in `luaug_options.cmake` leaves every
   already-configured tree on the old value — and the failure surfaces
   somewhere else entirely (a test target naming a module that was not built).
   Fix locally with `-DLUAUG_X=ON` on the next configure; the durable fix is
   what the RHI CMakeLists does now, which is to build its target lists from
   what is actually enabled rather than naming things unconditionally.

5. **SDL's Linux dependency check is all-or-nothing per feature.** Assembling
   the package list from whatever the last CI failure named costs one round
   trip per missing package (X11 found → XTEST missing → …). The list is in
   `third_party/sdl3/docs/README-linux.md`; take it from there.

6. **A clear is a complete frame, and that is enough to prove the harness.**
   The screenshot path — create a target, open a render pass, submit, read the
   pixels back — needs no shaders, no pipeline and no window. Getting it
   working before the shader pipeline (step 6) means every later rendering
   change is verifiable from the moment it exists, rather than at the end of
   the milestone. Recorded because the obvious order (shaders → triangle →
   screenshot) would have deferred the milestone's actual product to last.

   The test clears to `(1.0, 0.5, 0.0, 1.0)` on purpose: 0.5 is neither 0 nor
   255, so a dropped, swizzled or constant-written channel shows up instead of
   matching by luck. Unorm8 rounding of 0.5 lands on 127 or 128 depending on
   backend convention, so green is asserted as a range and the other three
   exactly.

7. **DXC is SDL_shadercross's only HLSL front-end, not its DXIL back-end.**
   This brief's risk 2 assumed DXC could be dropped at the cost of DXIL. It
   cannot. `SDL_ShaderCross_CompileSPIRVFromHLSL` delegates straight to
   `SDL_ShaderCross_INTERNAL_CompileUsingDXC`
   (`src/SDL_shadercross.c:629-642`), and with `SDL_SHADERCROSS_DXC` off that
   function is a stub that sets an error (`:568-571`). MSL is derived from
   SPIR-V, so no DXC means no SPIR-V *and* no MSL. SPIRV-Cross is equally
   unavoidable: `find_package(spirv_cross_c_shared REQUIRED)` with no guarding
   option (`CMakeLists.txt:145-158`).

   Three more facts that changed the shape of the decision:

   - **SDL_shadercross has never cut a release.** `"version": "latest-tag"` in
     the manifest is unsatisfiable; there are no tags at all. It must be pinned
     to a `main` commit with a dated version string, the way `stb` is.
   - **DXC from source is a fork of LLVM 3.7** — `project(LLVM)` in
     `dxc/CMakeLists.txt:26` — at 120 MB and 17,648 files, needing Python 3 and
     TableGen. Against a cold build already near nine minutes per tier, that is
     not a dependency, it is a second project.
   - **Upstream pins official Microsoft prebuilt DXC by SHA256**
     (`build-scripts/download-prebuilt-DirectXShaderCompiler.cmake:1-4`), which
     is the path chosen. It has no macOS binary — irrelevant here, because
     architecture.md §8 already builds shadercross as a host tool used when
     cross-compiling, and macOS Tier-3 is compile-only.

   Licence notes for the record: SPIRV-Cross is Apache-2.0; DXC is NCSA, which
   is BSD-equivalent but **not literally on R6's list**, so it was a human call.
   `vkd3d` (LGPL) is reachable only under `SDLSHADERCROSS_INSTALL_RUNTIME`,
   which defaults off (`CMakeLists.txt:52`) and stays off.

## Attempted / abandoned

- **Vendoring DXC from source (option B).** Rejected with the human: four
  manifest rows, ~170 MB, 17k files, a Python 3 toolchain requirement, and an
  LLVM build on every cold CI tier. It is the only option that gives a macOS
  *host* path, which is why it was considered at all — but macOS is a
  compile-only tier and shaders cross-compile from a Tier-1/2 host.
- **Dropping DXC (option C).** Rejected: it violates ADR 0006 and yields DXBC
  on Windows only. D3D12 does accept DXBC alone
  (`third_party/sdl3/src/gpu/d3d12/SDL_gpu_d3d12.c:8688-8689` warns rather than
  fails), so it would have worked — until M4, at the cost of Vulkan, lavapipe
  goldens, macOS and Android.

(append during the milestone; §12)

## Gate Record

(filled at milestone end, before human review)
