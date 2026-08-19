# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- Current milestone: **M1 — Window, RHI, Frame Loop, Agent Eyes — in progress**
  (brief: `docs/briefs/m1-kickoff.md`).
- **M0 signed off by the human on 2026-08-19**, tagged `milestone/m0`. All five
  gate items passed on Tier-1 (Windows) and Tier-2 (Linux); evidence in
  `docs/briefs/m0-kickoff.md` § Gate Record. Re-verified green at M1 kickoff:
  `cmake --build` up to date, `ctest` 6/6, `--version` reports Luau 0.734
  (3fc82b1) and bytecode 9 / types 3 / vector 3-wide f32.
- Remote: `github.com/zSkanz/luaug` (private). CI runs on every push.
- The engine builds and runs on both tiers: `luaug-host examples/boot/boot.luau`
  prints a catalog-resolved greeting and exits 0. Nothing renders yet.

## Now / Next

- **Steps 1–5 of the brief are done** — the milestone's spine. SDL3 is wired
  into the build, `platform` (L1) exists, and the RHI seam (L2) has three
  backends: `rhi_null`, `rhi_capture` and `rhi_sdlgpu`. A real GPU device
  clears a texture and reads it back as exactly the colour it was cleared to,
  with no window and no shaders — the screenshot harness the later gates depend
  on already works.
- **Next: the shader pipeline (step 6 of the brief).** The literal first action
  is to vendor `sdl_shadercross` with `lute tools/repo/vendor.luau resolve
  sdl_shadercross <tag>` and **inspect what it pulls in before wiring
  anything**: it is expected to need SPIRV-Cross and DirectXShaderCompiler, and
  neither has a manifest row. If it does, that is R5/R6 and stops for the human
  (§10) — do not add rows unilaterally. The fallback that keeps M1 moving
  meanwhile is in the brief under risk 2.
- Remaining after that: frame loop + debug draw (7), headless/screenshot +
  `tools/imgcmp` (8), ImGui overlay (9), `examples/00-clear` (10), the nightly
  Android job, the macOS compile job and the first golden capture (11).
- **The macOS job is M1 work**, not a discrepancy: the M1 gate says
  "Tier-2/Tier-3 compile" while the roadmap's tier table says macOS compiles
  from M4. The gate is the narrower contract and wins. Compile-only.
- Carried forward from M0 (none blocked the M0 gate):
  - **CI has no cache.** Every run pays a full cold build: ~8 min per tier,
    almost all of it compiling Luau. `sccache` + a `third_party` cache keyed on
    `hash(manifest.json) + toolchain + preset` is the plan in architecture.md §9.
  - **`Luau.Analysis` is compiled but never linked** — roughly a third of a
    clean build. Trimming it means touching the vendored CMake, so it wants a
    patch under `third_party/patches/` and an entry in the manifest (ADR 0021).
  - **`actions/checkout@v4` warns that Node.js 20 is deprecated** on the
    runners. Cosmetic today.
  - **Console text on Windows is fixed by codepage, not by wide writes.** A
    fully robust fix (`WriteConsoleW` when stdout is a console) belongs to the
    `platform` module in M1 — folded into step 2 of the M1 brief.
  - **`luaug check` must not pass file lists on Windows** — a few hundred paths
    overflow the 32,767-character command line. Pass directories (M3).

## Blocked — needs human

- (none — M0 sign-off was given on 2026-08-19)

## Decisions pending ADR

- (none — ADR 0031 was written and human-approved during M0)

## Session Log

- **2026-08-19 (planning session, Claude Fable):** Created the full mission
  package: root files + Apache-2.0 licensing; docs/roadmap.md (M0–M8 with
  gates); ADRs 0001–0030; three frozen research reports + UNCONFIRMED
  registry; MASTER_PROMPT.md; CLAUDE.md; this ledger; architecture and
  api-design docs; repo skeleton, configs, and docs-lint CI. Learned: see
  research reports. Next: M0 per roadmap.

- **2026-08-19 (session 2, Claude Opus):** **Completed M0.** Wrote the kickoff
  brief; installed and pinned the toolchain, fixing four defects in
  `scripts/bootstrap.*` found by running it; vendored Luau 0.734, SDL 3.4.14
  and doctest 2.5.3 at verified SHAs through the new `tools/repo/vendor.luau`,
  which never writes the manifest so pinning stays a human decision; activated
  the CMake presets and module layer; built `luaug-host` (sandboxed VM, i18n'd
  log, key-identified structured errors); added `i18nlint.luau` and
  `checklayers.luau`; activated CI. The human created the remote mid-session,
  which closed both recorded blockers: CI ran, and Tier-2 Linux — never
  compiled before and expected to fail under `-Werror` with the full warning
  set — compiled clean on the first attempt.
  Learned: Luau ships no version constant (→ ADR 0031, gate amended with human
  approval); Lute 1.0.0's own typedefs fail luau-lsp 1.69, so CI must
  `--ignore` them; `fs.walk` cannot be driven by a generic `for`; `@std/json`
  decoded objects carry a `newproxy()` sentinel key; SDL's real pin is 3.4.14
  (settles U-07). Two defects were found only by *running* the thing: the VM
  still exposed `getfenv`/`setfenv`/`newproxy` and a live `_G`, and the Windows
  console mangled UTF-8 catalog text — both invisible to CI, the second
  reported by the human. Full list in the brief's Findings section.
  Next: **stop for M0 human review** (§6). Do not start M1 this session.

- **2026-08-19 (session 3, Claude Opus):** M0 signed off by the human; **opened
  M1**. Ran the §2 boot sequence against a cleared context — the ledger and the
  repo agreed, and the M0 gate re-ran green locally (ctest 6/6). Wrote
  `docs/briefs/m1-kickoff.md`: goal, scope, an explicit NOT-in-scope list, the
  step order with its multi-session split points, the subagent plan, and four
  entering risks.
  Learned: the M1 gate says "Tier-2/Tier-3 compile" but `ci.yml` has no macOS
  job and the roadmap's tier table says macOS compiles "from M4" — the gate is
  the narrower contract, so a compile-only macOS job is M1 work (recorded in
  the brief). Also: `vcvars64.bat` lives under Visual Studio **18** on this
  machine, not 2022 — always locate it with vswhere, as `bootstrap.ps1` does.
  Next: wire `third_party/sdl3` into the build with only the M1 subsystems
  enabled, measuring cold-build time before and after.

- **2026-08-19 (session 3 continued):** Built the M1 spine — steps 1–5 of the
  brief. SDL3 wired in with subsystems chosen by architecture rather than by
  size; `platform` (L1) with window, event pump, clock, paths and the console
  fix carried over from M0; the `rhi_api` seam (header-only, no SDL type) with
  `rhi_null`, `rhi_capture` and `rhi_sdlgpu`. **First light: a real GPU device
  clears a texture and reads it back exactly, with no window and no shaders.**
  Learned, all recorded in the brief's Findings: SDL3 costs 5.6 s, not minutes,
  so the build-time risk was retired by measurement and the real lever stays
  the `Luau.Analysis` trim; `SDL_GetTicksNS` reads 0 before `SDL_Init`, which
  is why `nowNs` uses `steady_clock`; changing an `option()` default does not
  reach an existing build directory; SDL's Linux dependency check is
  all-or-nothing per feature, so take the package list from its own docs.
  Two CI rounds went red on Linux and were fixed rather than worked around —
  SDL has a flag to skip the X11/Wayland check, and taking it would have bought
  a green run at the price of a Linux port that cannot open a window.
  Next: vendor `sdl_shadercross` and inspect its dependencies before wiring
  anything (§10 escalation is likely — see Now / Next).

<!-- Format for future entries:
- **YYYY-MM-DD (session N):** did X; learned Y; Next: <literal first action>.
-->
