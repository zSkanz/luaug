# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- Current milestone: **M0 — Bootstrap and First Light — COMPLETE, awaiting
  human sign-off**
- Gate status: **all five gate items pass**, verified in CI on Tier-1 (Windows)
  and Tier-2 (Linux). Full evidence in `docs/briefs/m0-kickoff.md` § Gate
  Record.
- Remote: `github.com/zSkanz/luaug` (private), created by the human this
  session. CI runs on every push.
- Tag: `milestone/m0`.
- The engine builds and runs on both tiers: `luaug-host examples/boot/boot.luau`
  prints a catalog-resolved greeting and exits 0.

## Now / Next

- **Next: stop. This is the M0 milestone gate — a human checkpoint
  (MASTER_PROMPT.md §6).** Do not begin M1 in this session, and do not begin it
  at all until the human has reviewed the M0 gate record and said go.
- When M1 does start, the literal first action is: write
  `docs/briefs/m1-kickoff.md` from the template in `docs/briefs/README.md`,
  then wire `third_party/sdl3` into the build (it is already vendored and
  pinned at 3.4.14 but deliberately not compiled — see
  `third_party/CMakeLists.txt`).
- Carried forward, deliberately not done in M0 (none block the gate):
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
    `platform` module in M1.
  - **`luaug check` must not pass file lists on Windows** — a few hundred paths
    overflow the 32,767-character command line. Pass directories (M3).

## Blocked — needs human

- **M0 sign-off.** The gate is green; milestone boundaries are the human
  checkpoint and this ledger records that batching reviews was never
  pre-authorized. Nothing proceeds until the human reviews and approves.

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

<!-- Format for future entries:
- **YYYY-MM-DD (session N):** did X; learned Y; Next: <literal first action>.
-->
