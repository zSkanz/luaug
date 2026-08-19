# CLAUDE.md — Standing Guidance for Agent Sessions

**If you are the autonomous builder: your mission is [`MASTER_PROMPT.md`](MASTER_PROMPT.md).
Read it first and follow its Boot Sequence (§2). This file is only the quick
digest for any agent touching this repo.**

## What this repo is

LuauG — an open-source (Apache-2.0) standalone game engine: C++ core embedding
Luau 0.734 directly, Roblox-familiar Instance/Service API over a data-oriented
ECS, deterministic fixed-tick simulation, SDL3 + custom RHI (SDL3 GPU default),
Jolt physics, code-first DX with hot reload. **Pre-alpha, M0 complete**: the
host boots a sandboxed VM and runs Luau; nothing renders yet. The engine is
built milestone by milestone per `docs/roadmap.md`; `PROGRESS.md` says where
things stand.

## Commands

```
scripts/bootstrap.ps1                # Windows: toolchain check, LUAUG_BUILD_ROOT, rokit install, lute setup
./scripts/bootstrap.sh               # Linux/macOS: same
lute tools/repo/vendor.luau status   # what is vendored vs what the manifest pins
cmake --preset win-msvc-dev          # configure (build dir OUTSIDE the repo tree)
cmake --build --preset win-msvc-dev  # build
ctest --preset win-msvc-dev          # C++ unit + integration tests
```

Luau-side gates, all runnable locally and all mirrored in `ci.yml`:

```
stylua --check .                     # formatting (third_party via .styluaignore)
luau-lsp analyze --platform=standard --ignore="**/.lute/**" <files>
lute tools/repo/i18nlint.luau        # every LUAUG_TR key exists in i18n/en.json
lute tools/repo/checklayers.luau     # module layering from real #include edges
```

Active from M3: `luaug dev` (hot-reload dev server) · `luaug test` (conformance
specs, headless engine) · `luaug check` (luau-analyze strict + StyLua + i18n
lint) · `luaug new <template>` · `luaug fmt`.

### Things that will waste an hour if you do not know them

- **Windows: the presets need a Developer Shell.** They use the Ninja generator
  with `strategy: external`, so `cl`, `cmake` and `ninja` must already be on
  PATH. Visual Studio bundles CMake and Ninja — the fix is almost never "install
  CMake", it is to run `VC\Auxiliary\Build\vcvars64.bat` first. `bootstrap.ps1`
  detects this and prints the exact command.
- **`pwsh` (PowerShell 7) may not be installed**; Windows PowerShell 5.1 runs
  the bootstrap fine. Invoke the script directly rather than assuming a shell.
- **rokit needs `--no-trust-check`** in any non-interactive session, or
  `rokit install` blocks on a trust prompt. `bootstrap` already passes it.
- **`lute setup` must have run** or luau-lsp cannot resolve a single `@std`
  require under `tools/` (see `tools/.luaurc`). `bootstrap` does it.
- **CI uses `cancel-in-progress: true`.** Pushing cancels any run still in
  flight — do not push while waiting on a result you care about.
- **A cold CI build is ~8 minutes per tier**, nearly all of it compiling Luau.
  There is no cache yet; `sccache` is planned (architecture.md §9).

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
`docs/roadmap.md` (M0–M8 + gates) · `docs/decisions/` (ADRs 0001–0031) ·
`docs/research/` (frozen reports + UNCONFIRMED registry) · `PROGRESS.md`
(ledger) · `docs/briefs/` (per-milestone kickoffs).

Each closed milestone's brief carries a **Findings** section — the things the
docs assumed that reality corrected. Read the current and previous milestone's
before starting work; they exist so the same discovery is not paid for twice.
