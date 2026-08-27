# CLAUDE.md — Standing Guidance for Agent Sessions

**If you are the autonomous builder: your mission is [`MASTER_PROMPT.md`](MASTER_PROMPT.md).
Read it first and follow its Boot Sequence (§2). This file is only the quick
digest for any agent touching this repo.**

## What this repo is

LuauG — an open-source (Apache-2.0) standalone game engine: C++ core embedding
Luau 0.734 directly, Roblox-familiar Instance/Service API over a data-oriented
ECS, deterministic fixed-tick simulation, SDL3 + custom RHI (SDL3 GPU default),
Jolt physics, code-first DX with hot reload. **v1.0.0 is released** — M0 through
M8 signed off, every milestone tagged, and the release published on 2026-08-22:
the host boots a sandboxed VM, runs a deterministic tick over an Instance tree,
hot-reloads in under two milliseconds, renders a world through cascaded shadows,
clustered lights, image-based lighting and a post chain, simulates it with Jolt,
streams it in chunks around a floating origin, and plays it — input actions, UI,
tweens, audio and skeletal animation — with `examples/10-open-world` walkable
from a built folder. **The visual editor is what comes next** (post-v1 phase 1,
human decision 2026-08-22), and it is the first thing this repository builds that
the roadmap does not already specify. The engine is built milestone by milestone
per `docs/roadmap.md`; `PROGRESS.md` says where things stand, and it is the file
to trust when this paragraph and it disagree.

## Commands

**Run the gates locally. Do not use CI as your test runner.**

```
scripts/localgate.ps1                # EVERYTHING that can run here: docs + Luau gates
                                     #   + Windows build/tests + the Linux tier in Docker
                                     #   + the shipping and player profiles nothing else builds
scripts/localgate.ps1 -Only windows  # one stage: docs | luau | format | windows | linux | shipping
```

**Run the Linux stage.** It is ~12 s warm, and it is not redundant with the
Windows one: Clang diagnoses things MSVC does not, warnings are errors, and this
has already caught a real defect that would otherwise have gone to CI. Use
`-Only` when iterating on one thing; the full gate before you push.

`-SkipLinux` exists for one situation — Docker is genuinely unavailable — and
using it means accepting that a Clang-only diagnostic reaches `main` instead of
you.

Roughly 90 seconds warm for the full six stages, 27 tests on Windows and 26 on
Linux. The same run costs ~68 charged minutes on GitHub, and this repository is
**private**, so Actions minutes carry platform multipliers (Linux 1×, Windows
2×, macOS 10×) against a quota that has been close to exhausted — about 30 of
those minutes are macOS alone. CI is there to prove `main` is green after a push
and to build macOS — it is not where you find out whether your change compiles.

The gate logic lives in `scripts/gates/*.sh`, and `ci.yml` runs the same files.
If you add a check, add it there, not in the workflow.

**Only macOS cannot run locally.** It is **blocking on every push that touches
code** (M4's gate item), and skipped on a documentation-only push and on a
`milestone/*` tag — the tag points at a commit the job already built when it
landed.

Underneath, when you need a single step:

```
scripts/bootstrap.ps1                # Windows: toolchain check, LUAUG_BUILD_ROOT, rokit install, lute setup
./scripts/bootstrap.sh               # Linux/macOS: same
lute tools/repo/vendor.luau status   # what is vendored vs what the manifest pins
cmake --preset win-msvc-dev          # configure (build dir OUTSIDE the repo tree)
cmake --build --preset win-msvc-dev  # build
ctest --preset win-msvc-dev          # C++ unit + integration tests
scripts/gates/clang-format.sh        # C++ formatting, at the pinned clang-format 18
                                     #   (Windows: scripts/localgate.ps1 -Only format [-Fix],
                                     #    which runs it in the Tier-2 container -- VS ships 20,
                                     #    and a different major reformats the whole tree)
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
- **Build with `chcp 65001` on Windows, always.** CMake writes ninja's
  `msvc_deps_prefix` as UTF-8; a LOCALISED MSVC emits that string in the console
  codepage, the two never match, and ninja then records **no header dependencies
  at all** — every incremental build silently reuses objects compiled against an
  older header, and the symptom is an access violation in code that has nothing
  wrong with it. `scripts/localgate.ps1` sets it; a hand-run `cmake --build`
  must too. Four debugging sessions were spent on the symptom before anybody
  looked at the cause (D040).
- **`pwsh` (PowerShell 7) may not be installed**; Windows PowerShell 5.1 runs
  the bootstrap fine. Invoke the script directly rather than assuming a shell.
- **rokit needs `--no-trust-check`** in any non-interactive session, or
  `rokit install` blocks on a trust prompt. `bootstrap` already passes it.
- **`lute setup` must have run** or luau-lsp cannot resolve a single `@std`
  require under `tools/` (see `tools/.luaurc`). `bootstrap` does it.
- **CI uses `cancel-in-progress: true`.** Pushing cancels any run still in
  flight — do not push while waiting on a result you care about.
- **Docker Desktop must be running** for `scripts/localgate.ps1`'s Linux stage.
  Without it, pass `-SkipLinux` — but then a Clang-only diagnostic reaches CI
  instead of you, and `-Werror` means that is a red `main`.
- **Windows PowerShell reports native commands' success backwards.** `$?` is set
  from whether the command wrote to stderr, not from its exit code, and
  `$ErrorActionPreference='Stop'` turns any stderr line into an exception —
  Docker's buildkit writes ordinary progress there. Check `$LASTEXITCODE`, and
  never pipe a build through `tail`/`head`: the pager's exit code replaces the
  build's, which has already once reported a broken build as green.
- **`--dirty` on a `git describe` in this repo is slow**, and catastrophically so
  through a Docker mount (0.7 s native, 37 s in the container) because it stats
  every vendored file. `third_party/CMakeLists.txt` sets a git ceiling so
  vendored projects cannot reach our repository at all.

## Invariants (digest — full text in MASTER_PROMPT.md §3)

R1 English everywhere · R2 all Luau `--!strict` new solver; no `class`/`integer`
syntax · R3 no hardcoded user-facing strings (i18n keys) · R4 sandbox always ·
R5 pinned versions only · R6 permissive licenses only · R7 clean-room re Roblox
· R8 deferred-only signals; no legacy globals · R9 vector = 3-wide f32; f64
world coords engine-side · R10 sim determinism (no wall-clock/unseeded
RNG/unordered iteration; stable parallel commit) · R11 main always green ·
R12 conventional commits · R13 never edit `third_party/` in place ·
R14 out-of-tree builds only · R15 v1 scope closed, and post-v1 phases open one
at a time by human decision (1 editor, 2 effects/world and 4 multiplayer are
OPEN; 2D, navmesh and mobile are not) · R16 interpreter-first perf (iOS has no
JIT) · R17 no backend
types in the public API · R18 rendering is judged against a stated reference,
and visual fidelity is a v1 target (ADR 0038).

## Style

- C++20 per `.clang-format` (4-space, 120 cols); warnings-as-errors on
  `engine/`; namespaces `luaug::<module>`; files `snake_case`.
- Luau per `stylua.toml` + `.luaurc` (strict). **Naming (ADR 0034): index it
  off an *object* and it is PascalCase** (`part.Name`, `part:Destroy()`,
  `v.Magnitude`); **index it off a *module or namespace* and it is camelCase**,
  constructors included (`Instance.new`, `CFrame.fromEuler`, `Color3.fromRGB`,
  `CFrame.identity`, `task.spawn`, and whatever `@std/*` and `@luaug/*`
  export). Type and class names are PascalCase.
- Inside our own Luau files: **module-level variables and constants are
  PascalCase with no underscores** (no `SCREAMING_SNAKE_CASE` anywhere),
  locals and inner functions are camelCase, and module-level functions stay
  camelCase because a module is not an object.
- Comments explain *why* and contracts, never narrate code.

## Do-not list

- Do not edit anything under `third_party/` (use `third_party/patches/` + manifest).
- Do not commit build output, generated-file edits, or secrets.
- Do not disable sandbox, strict mode, or lints to make CI pass.
- Do not add/upgrade dependencies without a human-approved ADR.
- Do not write to `main` while red; do not skip milestone gates.
- Do not implement anything on the R15 post-v1 list **whose phase is not open**.
  Phases 1 (editor), 2 (effects and world content) and 4 (multiplayer) ARE open;
  the 2D layer, navmesh and mobile are not. Opening one is the owner's decision —
  taking it for them is what R15 actually forbids.

## Map

`docs/architecture.md` (native core) · `docs/api-design.md` (Luau API/DX) ·
`docs/roadmap.md` (M0 through M8, plus M4.5 and M7.5, with gates) ·
`docs/decisions/` (ADRs 0001–0061) ·
`docs/research/` (frozen reports + UNCONFIRMED registry) · `PROGRESS.md`
(ledger) · `docs/briefs/` (per-milestone kickoffs).

Each closed milestone's brief carries a **Findings** section — the things the
docs assumed that reality corrected. Read the current and previous milestone's
before starting work; they exist so the same discovery is not paid for twice.
