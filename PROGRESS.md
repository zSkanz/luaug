# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- Current milestone: **M0 — Bootstrap and First Light — in progress, gate not met**
- Last green commit: `56c7cf9` (ci: activate build, analysis, i18n and layering gates)
- Gate status: 3 of 5 gate items pass; 2 blocked (see below). Local build,
  ctest, StyLua, luau-analyze, i18n lint and layer check are all green on
  Tier-1 Windows. `docs/briefs/m0-kickoff.md` carries the full Gate Record.
- The engine builds and runs: `luaug-host examples/boot/boot.luau` prints a
  catalog-resolved greeting and exits 0.
- Repo created 2026-08-19 by the planning session (Claude Fable) at
  `D:\Projects\LuauG`.

## Now / Next

- **Next: get CI actually running.** Create the GitHub remote (human — needs an
  account), push `main`, and read the first `build-test` result. Expect Tier-2
  Linux compile failures on the first run: the engine has only ever been
  compiled with MSVC, and the Linux profile adds `-Wconversion
  -Wsign-conversion -Wold-style-cast -Wshadow -Wpedantic` under `-Werror`.
  Fixing those is ordinary work; finding them needs Linux.
- Then re-run the full M0 gate and, if green on both tiers, tag `milestone/m0`
  and stop for human review.
- Deferred, deliberately, to keep M0 scoped: `Luau.Analysis` is compiled but
  never linked (~1/3 of a clean build) — trim in a later milestone rather than
  carry a vendored patch.
- Do NOT start M1 in the session that closes M0 (§6).

## Blocked — needs human

1. **No git remote exists**, so CI has never run and the gate item "CI green on
   Tier-1/Tier-2" cannot be honestly asserted. Creating a GitHub repository and
   pushing requires an account, which is an escalation item (§10). All CI jobs
   are written and pass locally.
   *Ask:* create the remote (name/visibility your call) and confirm, or tell me
   to leave the project local — in which case the M0 gate needs rewording,
   because as written it is not satisfiable without CI.
2. **Tier-2 (Linux) has never been compiled.** Follows from (1): no Linux
   machine is available to this session. Not independently actionable — it
   resolves as soon as CI runs.

## Decisions pending ADR

- (none — ADR 0031 was written and approved during this session)

## Session Log

- **2026-08-19 (planning session, Claude Fable):** Created the full mission
  package: root files + Apache-2.0 licensing; docs/roadmap.md (M0–M8 with
  gates); ADRs 0001–0030; three frozen research reports + UNCONFIRMED
  registry; MASTER_PROMPT.md; CLAUDE.md; this ledger; architecture and
  api-design docs; repo skeleton, configs, and docs-lint CI. Learned: see
  research reports. Next: M0 per roadmap.

- **2026-08-19 (session 2, Claude Opus):** Executed most of M0. Wrote
  `docs/briefs/m0-kickoff.md`. Installed the toolchain (rokit → Lute 1.0.0,
  StyLua 2.5.2, luau-lsp 1.69.0) and fixed four real defects in
  `scripts/bootstrap.*` found by running it. Vendored Luau 0.734, SDL 3.4.14
  and doctest 2.5.3 at verified SHAs via the new `tools/repo/vendor.luau`
  (ADR 0021), which never writes the manifest — pinning stays a human
  decision. Activated CMake presets, wrote the cmake/ module layer, and built
  `luaug-host`: sandboxed Luau VM, i18n'd log, key-identified structured
  errors. Added `tools/repo/i18nlint.luau` and `checklayers.luau` and turned
  on the build/analysis/i18n/layering CI jobs.
  Learned: Luau ships no version constant at all (→ ADR 0031, gate amended
  with human approval); Lute 1.0.0's own typedefs fail luau-lsp 1.69, so CI
  must `--ignore` them; `fs.walk` cannot be driven by a generic `for`;
  `@std/json` decoded objects carry a `newproxy()` sentinel key; SDL's real
  pin is 3.4.14 (settles UNCONFIRMED U-07). Full list in the brief's Findings
  section.
  Next: create the GitHub remote, push, and fix whatever the first Tier-2
  Linux build reports — then re-run the M0 gate.

<!-- Format for future entries:
- **YYYY-MM-DD (session N):** did X; learned Y; Next: <literal first action>.
-->
