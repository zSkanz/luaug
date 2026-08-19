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

- [x] SDL3 init / window / event pump — the `platform` module (L1), the only
      module besides `rhi_sdlgpu` and `app` glue that may touch SDL (ADR 0004)
- [~] RHI v1: the ~40-call `rhi_api` (L2, header-only) is **done**, with three
      backends. The `IRenderer`/`RenderWorld` contract (ADR 0027) is **not** —
      `engine/render` currently holds only `DebugDraw`. `RenderWorld` is defined
      as the POD snapshot extracted from `scene`, and `scene` is M2, so the
      honest shape here is a debug-draw renderer behind the interface rather
      than an empty `RenderWorld` invented ahead of the thing it snapshots.
      Decide at the gate whether that satisfies the item or defers it.
- [x] `rhi_sdlgpu` backend (v1 default, ADR 0005)
- [x] `rhi_capture` and `rhi_null` backends compile
- [x] HLSL shaders via SDL_shadercross with an on-disk shader cache (ADR 0006,
      `cmake/luaug_shaders.cmake`)
- [x] Immediate-mode debug draw: lines, wire/solid cubes, text — deliberately
      early, because M2/M3/M5 visualize through it before a real renderer exists
- [x] ImGui docking overlay bound to F3 (ADR 0011, dev builds only)
- [x] Frame loop with the fixed-tick accumulator (architecture.md §3); tick
      logic itself stays stubbed — the kernel is M2
- [x] Headless mode: `--headless --frames N --screenshot path --exit` via an
      offscreen surface + readback, exercised in CI (WARP/software fallback on
      the Windows runner; if no CI GPU path works, a scripted local gate on the
      dev machine — recorded in the gate log either way)
- [x] Nightly Android NDK cross-compile job (non-blocking) covering
      `platform` + `rhi_api` + `rhi_sdlgpu` + the triangle sample
- [x] `examples/00-clear`: clear colour pulses, three debug cubes orbit, driven
      from a Luau script through a **temporary minimal binding that M2 replaces**
- [x] `tools/imgcmp` — the screenshot tolerance comparator (named by the gate)
- [x] First golden capture-stream checked in, plus a reference screenshot

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

- [x] Tier-1 runs windowed and headless
- [x] first golden capture-stream checked in (plus a reference screenshot with
      the tolerance comparator `tools/imgcmp`)
- [x] GPU validation layer clean
- [x] Tier-2/Tier-3 compile

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

8. **The headless clock's synthetic step must round up, not truncate.** One
   sixtieth of a second is 16,666,666.67 ns; truncating leaves every frame a
   fraction short of the accumulator's threshold, so ticks fire on some frames
   and not others — deterministically, but not the "exactly one step per frame"
   the code claimed. Three frames produced two identical colours and one
   different. `std::ceil` costs 0.3 ns of drift per frame and makes the claim
   true.

9. **The screenshot gate did not catch finding 8; the capture gate did — by
   design, and worth understanding before trusting either.** Fixing the tick
   step changed frame 30's colour by a max channel delta of exactly 2, which
   the tolerance-2 comparison passes. The tolerance is right: GPUs round the
   last bit of a unorm conversion differently and a gate that fires on that gets
   switched off. But on a smoothly varying flat colour, a one-tick shift lives
   inside that tolerance.

   The command stream showed it instantly (`"r":0.5000` against `"r":0.5083`),
   which is exactly why architecture.md §9 makes the capture stream the
   *blocking* gate and images the secondary one. Two consequences to keep in
   mind: the screenshot gate only grows teeth once there is geometry with edges
   in the frame, and a golden image is never sufficient evidence on its own.

10. **Linux compiles DXIL it will never load, and that is the mitigation.** The
    shader toolchain was flagged as risky on Linux because `libdxcompiler.so`
    loads `libdxil.so` dynamically to sign its output, and D3D12 rejects
    unsigned DXIL outside Developer Mode — but the Linux loader does not search
    beside the executable the way Windows does. CI shows the DXIL steps running
    and succeeding there; whether the blobs are *signed* is still unverified.

    It does not currently matter, and the reason is worth writing down rather
    than rediscovering: DXIL is consumed only by D3D12, which is Windows-only,
    so a Linux build's DXIL output is never loaded by anything. The risk
    materialises the day a Linux job produces a shipping shader pack for
    Windows — i.e. at packaging (`luaug build`, M8), not before. Verify it
    then, or drop DXIL from non-Windows hosts.

11. **A property test that has never failed is decoration.** The debug-draw
    tests were mutation-tested: the implementation was broken four ways to check
    each assertion actually fires. Three caught it; one did not. Comparing
    `vertices().data()` only after a full refill does not detect a
    `shrink_to_fit()` in `clear()`, because the allocator hands the same address
    back. Worth doing to every test suite whose value is in what it rejects —
    which is most of the interesting ones.

12. **`std::from_chars` for `double` is not portable enough to use.** Apple's
    libc++ lacks the floating-point overloads on the Xcode versions
    `macos-latest` ships, and `build-macos` compiles the test executables — so
    using it would have been a macOS-only compile failure discoverable *only*
    in CI, on the one tier nobody here can reproduce. `core::json` uses
    `strtod`, which is correctly rounded everywhere but reads the **locale's**
    decimal separator; the parser substitutes it from `std::localeconv()`.
    Nothing in the engine calls `setlocale`, but a dependency that does must not
    be able to turn `1.5` into `1`.

13. **`architecture.md` §9 lists a clang-format check among the static gates,
    and there is none.** `ci.yml` has no C++ formatting job. Discovered when
    clang-format 20 reported violations in `error.cpp`, `math.cpp` and
    `log.cpp` — files written before this milestone, so it is version drift
    from whatever produced `.clang-format`, not a deviation introduced here.

    Not fixed in M1, deliberately: turning the gate on means reformatting the
    tree and pinning a clang-format version, and version pinning for the
    C++ toolchain is M3's `luaug check` work. Recorded so the gap is a decision
    rather than an oversight.

14. **A golden gate whose fallback looks like its subject tests nothing.** The
    engine draws its own scene when no script registers a frame callback, and
    the first version of that scene was the same three orbiting cubes
    `examples/00-clear` draws. So a broken Luau binding would silently fall back
    to a frame that matched the golden, and both gates would pass on a frame the
    script never touched.

    The engine's idle scene is now a bare triad — visibly *not* the example.
    Verified by breaking the binding on purpose: 1465 differing pixels, gate
    red. Before the change, green.

    The general shape is worth carrying: a gate is only as strong as the
    difference between "working" and "fallen back".

15. **`luaL_sandbox` freezes the globals table, and M0's own comment said so.**
    Installing the preview API after constructing `ScriptHost` failed inside the
    VM with no error anyone could see — the process printed its boot line and
    vanished. The M0 constructor carries the warning verbatim ("Engine globals
    must be installed BEFORE luaL_sandbox"), and it was still walked into from
    the outside, because nothing in the *interface* made the ordering visible.

    `ScriptHost` now takes a `GlobalInstaller` invoked in exactly that window.
    The fix worth generalising is not the callback, it is that a constraint
    documented only in a comment gets violated by the next caller; put it in the
    signature.

16. **A pixel golden is tied to the GPU that recorded it, and geometry is where
    that stops being survivable.** The screenshot gate passed on Windows CI
    while the frame was a flat colour — tolerance 2 covers a unorm rounding
    difference between devices. It went red the moment the frame contained
    edges: line rasterization rules differ between this machine's GPU and the
    runner's software rasterizer.

    The tempting fix is to widen the tolerance until both pass, which leaves a
    gate that cannot see a real change either. architecture.md §9 already had
    the answer — the command stream is the blocking gate precisely because it
    needs no GPU, and it passed on that same Windows run. The screenshot gate is
    now labelled `gpu-golden` and excluded from CI with `ctest -LE gpu-golden`,
    running instead on every local gate, which is the arrangement the roadmap
    explicitly sanctions.

17. **Extracting shared logic and not switching the caller leaves two copies,
    which is worse than one.** `scripts/gates/*.sh` were written so the local
    gate and CI would run the same files — and then `ci.yml` kept its inline
    steps. Within an hour they had diverged: the scripts learned to exclude
    `.d.luau` from analysis and to allow `scripts/gates/` in the R7 sweep, CI
    did not, and a change that was green locally went red on CI for reasons
    unrelated to it. The extraction was only finished when the workflow called
    the scripts.

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

Recorded 2026-08-19. Every command below was run on the dev machine (Windows
x64, Tier-1) or in the Tier-2 container, through `scripts/localgate.ps1`.

### The four gate items

**Tier-1 runs windowed and headless.** Both, with the same code path.

```
luaug-host examples/00-clear/init.luau --frames=600 --exit
  [info] Ran 600 frames, 357 simulation ticks.      exit 0
```

That pair of numbers is the two-clock design working rather than a coincidence:
600 render frames against 357 fixed simulation steps, decoupled by the
accumulator exactly as architecture.md §3 specifies.

Headless is exercised by every gate run and by four CTest entries.

**First golden capture-stream, plus a reference screenshot with `tools/imgcmp`.**
`tests/rendercapture/clear-3frames.jsonl` and
`tests/screenshots/pulse-frame30.png`, both recorded from
`examples/00-clear/init.luau` and both compared by CTest entries that run on
every tier. The capture gate is byte-for-byte with no tolerance and needs no
GPU, so it also runs in CI; the screenshot gate uses tolerance 2 with zero
differing pixels allowed and skips where there is no device.

Both were confirmed to *fail* when they should — a golden swapped for a
neighbouring frame, and a deliberately broken script binding (1465 differing
pixels). A gate that has never failed has never been tested.

**GPU validation layer clean.** The device is created with `debug = true` on
every run, including the 600-frame windowed one above and the real-device rhi
tests. No validation output was produced.

**Tier-2/Tier-3 compile.** Tier-2 builds *and tests* — 15/15 in the container,
with `screenshot_gate` correctly skipped for want of a driver — and again on
CI. Tier-3 macOS compiles on CI, with `LUAUG_SHADER_TOOLCHAIN=OFF` because
Microsoft publishes no macOS DirectXShaderCompiler (ADR 0032).

### Tests

15 CTest entries, green on both tiers:

```
core  platform  rhi  render  app
host_version  host_version_abi  host_boot  host_headless_frames
host_headless_needs_frames  host_unknown_backend  host_usage_without_script
capture_gate  screenshot_gate  imgcmp
```

### Deliberately not claimed

The `IRenderer`/`RenderWorld` contract (ADR 0027) is **not** built; see the
scope checklist. `RenderWorld` is defined as the POD snapshot extracted from
`scene`, and `scene` is M2. What exists is a debug-draw renderer with the same
shape; inventing an empty `RenderWorld` ahead of the thing it snapshots would
have been a worse answer than saying so.

**The shipping profile does not configure.** `engine/app/src/script_host.cpp`
includes `<Luau/Compiler.h>` unconditionally, and `LUAUG_LUAU_COMPILER` is
forced off in shipping (ADR 0002), so the include path lacks it. Found while
proving ImGui is excluded from shipping builds, which it is. Not fixed here on
purpose: the guard is one line, but a shipping host with no compiler also needs
the bytecode-loading path that does not exist until M3, and a profile that
configures while being unable to run anything is a worse lie than one that
refuses. Carried forward.

### Result

**All four gate items pass.** M1 is complete, pending human sign-off — the
milestone boundary is the human checkpoint (MASTER_PROMPT.md §6).

