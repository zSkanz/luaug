# CLAUDE.md — Standing Guidance for Agent Sessions

**If you are the autonomous builder: your mission is [`MASTER_PROMPT.md`](MASTER_PROMPT.md).
Read it first and follow its Boot Sequence (§2). This file is only the quick
digest for any agent touching this repo.**

## What this repo is

LuauG — an open-source (Apache-2.0) standalone game engine: C++ core embedding
Luau 0.734 directly, Roblox-familiar Instance/Service API over a data-oriented
ECS, deterministic fixed-tick simulation, SDL3 + custom RHI (SDL3 GPU default),
Jolt physics, code-first DX with hot reload. Currently **docs-first pre-alpha**:
the engine is built milestone by milestone per `docs/roadmap.md`.

## Commands

Active from M0 (see roadmap; before that, only docs-lint runs):

```
pwsh scripts/bootstrap.ps1          # toolchain check + LUAUG_BUILD_ROOT + rokit install
cmake --preset win-msvc-dev         # configure (build dir OUTSIDE the repo tree)
cmake --build --preset win-msvc-dev # build
ctest --preset win-msvc-dev         # C++ unit + integration tests
```

Active from M3: `luaug dev` (hot-reload dev server) · `luaug test` (conformance
specs, headless engine) · `luaug check` (luau-analyze strict + StyLua + i18n
lint) · `luaug new <template>` · `luaug fmt`.

## Invariants (digest — full text in MASTER_PROMPT.md §3)

R1 English everywhere · R2 all Luau `--!strict` new solver; no `class`/`integer`
syntax · R3 no hardcoded user-facing strings (i18n keys) · R4 sandbox always ·
R5 pinned versions only · R6 permissive licenses only · R7 clean-room re Roblox
· R8 deferred-only signals; no legacy globals · R9 vector = 3-wide f32; f64
world coords engine-side · R10 sim determinism (no wall-clock/unseeded
RNG/unordered iteration; stable parallel commit) · R11 main always green ·
R12 conventional commits · R13 never edit `third_party/` in place ·
R14 out-of-tree builds only · R15 v1 scope closed (no editor/multiplayer/2D/
mobile/navmesh) · R16 interpreter-first perf (iOS has no JIT) · R17 no backend
types in the public API.

## Style

- C++20 per `.clang-format` (4-space, 120 cols); warnings-as-errors on
  `engine/`; namespaces `luaug::<module>`; files `snake_case`.
- Luau per `stylua.toml` + `.luaurc` (strict). Public API naming: PascalCase
  for the world (Instances/services/datatypes), lowercase for required
  libraries — see `docs/api-design.md` conventions.
- Comments explain *why* and contracts, never narrate code.

## Do-not list

- Do not edit anything under `third_party/` (use `third_party/patches/` + manifest).
- Do not commit build output, generated-file edits, or secrets.
- Do not disable sandbox, strict mode, or lints to make CI pass.
- Do not add/upgrade dependencies without a human-approved ADR.
- Do not write to `main` while red; do not skip milestone gates.
- Do not implement anything on the R15 post-v1 list.

## Map

`docs/architecture.md` (native core) · `docs/api-design.md` (Luau API/DX) ·
`docs/roadmap.md` (M0–M8 + gates) · `docs/decisions/` (ADRs 0001–0030) ·
`docs/research/` (frozen reports + UNCONFIRMED registry) · `PROGRESS.md`
(ledger) · `docs/briefs/` (per-milestone kickoffs).
