# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- Current milestone: **M2 — Kernel: Instances over ECS, Scheduler, Signals,
  task — IN PROGRESS** (brief: `docs/briefs/m2-kickoff.md`). The largest
  milestone in the project (18%), expected to span several sessions; the split
  points are recorded in the brief's Entering Risks §5.
- **M1 — Window, RHI, Frame Loop, Agent Eyes — COMPLETE and SIGNED OFF by the
  human on 2026-08-19**, tagged `milestone/m1` (brief:
  `docs/briefs/m1-kickoff.md`). Its gate was re-run green on both tiers at M2
  kickoff (`scripts/localgate.ps1`, 25.8 s, 15/15 tests).
- **M0 was signed off on 2026-08-19**, tagged `milestone/m0`.
- The engine renders: three wire cubes orbiting a world triad over a pulsing
  clear colour, driven from `examples/00-clear/init.luau` through a temporary
  Luau binding that M2 replaces. F3 toggles an ImGui overlay in dev builds.
- 15 CTest entries, green on Windows and Linux.

## Now / Next

- **Next: write `api/defs/*.api.luau` for the M2 surface**, against
  `api/schema.luau` (which exists and type-checks). The rulings that fill in
  every previously-unanswered semantic are now in `docs/api-design.md`, so the
  def files have something to be faithful to. Then `api/generator/gen_dts.luau`
  — it unblocks the conformance suite, which cannot pass `luau-analyze` until
  engine declarations exist.
- 932 conformance cases in 47 files are **staged outside the repo** at
  `…/scratchpad/conformance/` and are NOT yet integrated: they cannot pass the
  analyzer until `gen_dts` emits `runtime/types/engine.d.luau`. Several need
  correcting first against rulings that overrode their author's guess — the
  equality-filtered property write, the re-entrancy cap covering `task.defer`,
  and the removed-globals files, which move to C++ entirely.
- Kernel work (ECS, Instance facade, signal queue, `task`, bindings) stays
  single-threaded per MASTER_PROMPT §7. `core` primitives, spec authoring, the
  IDL and the doc passes are what fanned out.
- **Read `docs/research/luau-c-api-2026.md` before writing any binding code.**
  It is the frozen, file-and-line-verified account of the Luau C API at the pin,
  and rows U-17…U-51 in `docs/research/UNCONFIRMED.md` record where it
  contradicts the architecture. Brief Decision 13 settles the biggest one.
- **Run `scripts/localgate.ps1` before every push. Do not use CI as a test
  runner.** Both tiers, ~20 seconds warm. The repository is private, so Actions
  minutes carry platform multipliers and the quota has been close to exhausted.
  CI proves `main` is green and builds macOS, which nothing local can.
- Carried forward from M1 (none blocked the gate):
  - **The shipping profile does not configure.** `script_host.cpp` includes
    `<Luau/Compiler.h>` unconditionally while `LUAUG_LUAU_COMPILER` is forced
    off in shipping (ADR 0002). The guard is one line, but a shipping host also
    needs the bytecode-loading path, which is M3 at the earliest.
  - **`architecture.md` §9 lists a clang-format gate that does not exist.**
    Turning it on means reformatting the tree and pinning a version; version
    pinning for the C++ toolchain is M3's `luaug check` work.
  - **`Luau.Analysis` is still compiled and never linked** — carried from M0.
    sccache now makes this cheap on a warm CI cache, so it is less urgent.
  - **The example's `luaug` global and its hand-written `.d.luau`** are M1
    scaffolding with a demolition date; M2 replaces both.
  - **DXIL produced on Linux is never verified as signed.** It is also never
    loaded there — D3D12 is Windows-only — so this only matters if a Linux job
    ever produces a shipping shader pack for Windows (M8).

## Blocked — needs human

- (none — M0 sign-off was given on 2026-08-19)

## Decisions pending ADR

- (none — ADR 0032 and ADR 0033 were written during M1)

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

- **2026-08-19 (session 3, final stretch):** **Completed M1.** Shaders (one HLSL
  source to SPIR-V, DXIL and MSL, with DirectXShaderCompiler fetched and
  hash-pinned rather than vendored — ADR 0032, a §10 escalation the human
  resolved); `core::json` replacing the restricted catalog reader so there is
  one parser (ADR 0033); `render::DebugDraw` and the debug pass; the screenshot
  and capture gates; `examples/00-clear` driven from Luau; the ImGui F3 overlay.
  Also, off the critical path but not off the books: `scripts/localgate.ps1`
  runs both tiers locally in ~20 s, because the repository is private and the
  human's Actions quota was nearly spent. CI dropped from ~68 to ~23 charged
  minutes per push.
  Learned, all in the brief's Findings: the capture backend was blind to the
  debug pass because the gate asked "will pixels come out" instead of "can this
  device take shaders"; the engine's idle scene looked like the example, so a
  broken script binding would have fallen back to a frame matching the golden;
  `luaL_sandbox` freezes globals and M0's own comment said so, which is why
  `ScriptHost` now takes the installer in its signature rather than documenting
  the ordering; a vendored tree reads THIS repo's git metadata unless stopped,
  which was both wrong information and 37 seconds of every container configure;
  and C++20 module scanning was running on every translation unit to discover
  that nothing uses modules.
  Next: **stop for M1 human review** (§6). Do not start M2 this session.

- **2026-08-19 (session 4, Claude Opus):** M1 signed off; **opened M2**. Ran the
  §2 boot sequence — ledger and repo agreed, and the M1 gate re-ran green on
  both tiers locally (15/15, 25.8 s). Wrote `docs/briefs/m2-kickoff.md`: the
  scope checklist, an explicit NOT-in-scope list, the subagent split, the gate
  verbatim, five entering risks, and twelve numbered decisions each naming the
  alternative it rejects.
  Learned at planning time, before any code: the gate's phrase "*documented*
  ordering semantics" is not satisfied by anything currently written down, so
  the spec must precede the implementation; `api-design.md` §2.1 omits
  `ScriptService` even though §3 requires it; the roadmap's "RNG service" and
  api-design's `Random` datatype are the same thing under two names, and
  api-design wins per §4; and M2's own gate ("zero luau-analyze errors across
  all spec code") cannot be met without the `.d.luau` generator the roadmap
  scheduled for M3 — so `gen_dts` is pulled forward, forced by the gate rather
  than by preference.
  Next: write the ordering semantics into `docs/api-design.md`.

- **2026-08-19 (session 4 continued):** Wrote api-design §3.1/§3.2 (the deferred
  ordering contract), settled the casing rule with the human (ADR 0034), and
  fanned out: five vendored-source verifiers, five conformance authors, a
  restyle pass, a research writer, an i18n pass, a doc-rulings pass and the
  `core` primitives. Landed `@luaug/testing`, `api/schema.luau` with the §9
  naming lints, `core`'s `SlotMap`/`AtomTable`/`Pcg32`/`Phase` (71 cases, six
  mutation tests, all caught), 14 error keys, and 125 rulings written into the
  documents.
  Learned: the conformance authors found two contradictions between our own
  documents and two places where independent authors guessed opposite answers —
  and the R8 removals turn out to be untestable from inside Luau, so they move
  to C++. Reading the vendored VM contradicted the architecture in eight
  load-bearing ways (`docs/research/luau-c-api-2026.md`, U-17…U-51): one
  metatable per tag makes the Instance binding shape as written impossible,
  cyclic require does not exist at this pin, `luaL_sandbox` removes nothing, and
  the 1 s runaway kill raises an error the script can catch. Two headers I had
  frozen had real defects, both found by the implementer: `AtomTable`'s
  `string_view` keys dangled on vector growth (SSO), and `SlotMap::erase` was
  `noexcept` while allocating.
  Next: write `api/defs/*.api.luau`, then `gen_dts`.

<!-- Format for future entries:
- **YYYY-MM-DD (session N):** did X; learned Y; Next: <literal first action>.
-->
