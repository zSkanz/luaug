# M0 Kickoff — Bootstrap and First Light

- Started: 2026-08-19
- Roadmap section: [`docs/roadmap.md`](../roadmap.md) § "M0 — Bootstrap and First Light (S)"

## Goal (restated)

Turn a docs-only repository into a repository that *builds and proves things*.
By the end of M0 there is a real, pinned, hermetic dependency tree under
`third_party/`; a CMake superbuild that produces `luaug-host` out-of-tree; and
that host creates a sandboxed Luau 0.734 VM, runs `boot.luau`, and prints a
greeting that was never written as a literal string in C++ — it came out of the
i18n catalog through a `TextKey`. Nothing renders, nothing simulates. What M0
buys is the *spine*: pinning, layering, sandboxing, the i18n seam, the error
pipeline, the test harness, and CI on two tiers. Every later milestone hangs
off these, so the bar here is correctness of the seams, not quantity of code.

The grounding proof matters as much as the greeting: `--version` must report
the Luau version by reading the **vendored header**, not a string we typed. That
single test is what makes "pinned" mean something.

## Scope checklist (from roadmap)

- [x] Vendor third-party dependencies into `third_party/` at manifest-target
      versions, recording **real commit SHAs** (Luau, SDL3, doctest first;
      later deps may be vendored lazily but manifest rows must exist)
- [x] Activate `CMakePresets.json` (build dirs under `$env{LUAUG_BUILD_ROOT}`,
      never in-tree)
- [x] `luaug-host` executable: create Luau VM, `luaL_sandbox`, register a
      minimal `print`/log bridge routed through the key+catalog i18n system
      (one English catalog proves the seam)
- [x] rokit-pinned toolchain (Lute 1.0.0, StyLua 2.5.2, luau-lsp 1.69.0)
- [x] Activate CI: Windows + Linux build, ctest, `luau-analyze` strict on all
      `.luau`, StyLua check, i18n lint stub, docs-lint — *written and green
      locally; never executed, because the repo has no remote (see Gate Record)*

**Deliverable:** `luaug-host boot.luau` prints a catalog-resolved greeting and
exits 0 on Windows and Linux.

## NOT in scope

Explicitly deferred — if any of these start to feel necessary, that is a signal
to stop and re-read the roadmap, not to widen M0:

- **No window, no SDL init, no rendering, no RHI.** SDL3 is *vendored and
  compiled* in M0 only if it is free to do so; `platform` and `rhi_*` modules
  are M1. If wiring SDL3 into the build costs more than trivial effort, the
  manifest row + vendored tree is the whole M0 obligation and the CMake target
  waits for M1.
- No ECS, no Instance, no `game`/`workspace`, no services, no signals, no
  `task` — that is all M2. `boot.luau` sees `print` and the Luau stdlib.
- No `require`/Luau.Require wiring, no `@std`, no `@luaug` modules (M2/M3).
- No `luaug` CLI, no hot reload, no dev server, no templates (M3).
- No `api/` IDL, no generated defs, no api-dump (M2/M3).
- No jobs system, no scheduler, no fixed tick, no determinism harness (M2).
- No native codegen (`Luau.CodeGen`) enablement work beyond linking what the
  build needs; NCG tuning is a later, benchmark-driven exercise.
- No `.luauc` bytecode cache, no shipping/bytecode-only profile.
- No memory categories beyond whatever the log/error path trivially needs;
  the 256-memcat plan (architecture §5) is M2 work.
- No new ADRs unless implementation genuinely contradicts an existing one.

## Subagent plan

**None. M0 is orchestrator-only, single-threaded.**

This is the correct call under MASTER_PROMPT §7, which explicitly excludes
fan-out for cross-cutting work, build-system work, and gate runs — and M0 is
*entirely* those three. There are no frozen module interfaces to hand out yet
(freezing them is what M0 does), so parallel implementation agents would be
inventing the seams they are supposed to be implementing against. The
adversarial-review pass at the end of the milestone is performed by the
orchestrator against the rule list (R1–R17) as part of the gate run.

## Gate checklist (verbatim from roadmap)

- [ ] CI green on Tier-1/Tier-2
- [ ] `ctest` includes at least: VM boots sandboxed (env mutation from script
      fails)
- [ ] `ctest` includes at least: script error surfaces as a structured engine
      error with an i18n key
- [ ] `--version` prints the engine version, the pinned Luau version **and
      commit SHA** (generated at configure time from
      `third_party/manifest.json`, never typed into source), and the Luau ABI
      constants **read from the vendored headers** (`LBC_VERSION_TARGET`,
      `LBC_TYPE_VERSION_TARGET`, `LUA_VECTOR_SIZE`, `LUA_VECTOR_DOUBLE`), with
      a test asserting the latter match ADR 0013 — the grounding proof

> Gate item 4 was amended by [ADR 0031](../decisions/0031-build-provenance-header.md)
> after the original wording proved unsatisfiable: Luau 0.734 ships no version
> constant in any header. Escalated and approved by the human per
> `MASTER_PROMPT.md` §10 before any code was written against it.

## Environment findings (dev machine, 2026-08-19)

Recorded because M0 is the milestone that makes the machine buildable, and the
starting state was not.

| Tool | State | Action |
|---|---|---|
| MSVC 14.50.35717 (VS 18 Community) | present | meets the C++20 baseline (architecture §8) |
| Windows SDK 10.0.26100.0 | present | ok |
| CMake | **not on PATH** | VS-bundled copy exists; must be resolved before configure |
| Ninja | **not on PATH** | VS-bundled copy exists; the presets require it |
| rokit | **absent** | required by `scripts/bootstrap.ps1`; installs the pinned trio |
| Lute / StyLua / luau-lsp | **absent** | installed by `rokit install` from `rokit.toml` |
| `pwsh` (PowerShell 7) | **absent** (5.1 present) | `CLAUDE.md` invokes `pwsh scripts/bootstrap.ps1` |
| Network | available | vendoring by `git clone` is viable |

## Pins captured (real SHAs, resolved 2026-08-19)

Resolved by `git ls-remote` against upstream, not from memory or the reports:

| Dependency | Tag | Commit |
|---|---|---|
| luau | `0.734` | `3fc82b1071ab387531175869afc4fb528464afa4` |
| sdl3 | `release-3.4.14` | `147a8ee32dbf9ac02f3794964490687b6bbda1bc` |
| doctest | `v2.5.3` | `2d0a9359a60c51affe2a9bebb1be1dca47868151` |

This resolves **U-07** (SDL 3.4.x exact patch version — the reports conflicted
between 3.4.8 and 3.4.14; upstream `releases/latest` says 3.4.14, published
2026-08-03). `docs/research/UNCONFIRMED.md` is updated in the same commit as
the manifest.

## Findings (things the docs assumed that reality corrected)

Recorded because each one cost real time and will cost it again otherwise.

1. **Luau 0.734 ships no version constant.** No `LUAU_VERSION`/`LUA_VERSION`
   macro, no `project(... VERSION ...)`, no `VERSION` file; the string `0.734`
   appears nowhere in the tree. Version exists only as a git tag. → ADR 0031.
2. **Lute 1.0.0's own generated typedefs do not typecheck** under the pinned
   luau-lsp 1.69.0 (`@std/path`, `@lute/time`, `@std/fs`, `@std/json` all emit
   errors). CI must pass `--ignore="**/.lute/**"` or it fails for upstream
   reasons. Also `--platform=standard`, or luau-lsp tries to load Roblox defs.
3. **`fs.walk` cannot be driven by a generic `for`** on Lute 1.0.0: its
   iterator makes a yielding libuv call, giving "attempt to yield across
   metamethod/C-call boundary". Use `fs.listDirectory` and walk explicitly.
   (`fs.watch` documents the same constraint — it is a general Lute pattern.)
4. **`@std/json` tags decoded objects with a `newproxy()` sentinel key**, so
   iterating a decoded object yields a userdata among the string keys.
5. **Lute passes the script path as `process.args[1]`**; user arguments start
   at index 2.
6. **`rokit install` aborts on an interactive trust prompt.** `--no-trust-check`
   is required for CI and for any non-interactive session.
7. **Visual Studio bundles CMake and Ninja**; the presets' real requirement is
   the Developer Shell (`strategy: external`), not a separate CMake install.
8. **CMake's regex engine has no bounded repetition** — `{40}` silently fails
   to match, so a SHA length check must be a separate `string(LENGTH ...)`.
9. **Luau builds `Luau.Analysis` unconditionally** even with CLI and tests off.
   We never link it; it is roughly a third of a clean build. Worth trimming in
   a later milestone, not worth a vendored patch now (ADR 0021).
10. **`luaL_openlibs` leaves `getfenv`, `setfenv` and `newproxy` in the global
    table, and points `_G` at the real environment** — all four contradict
    `api-design.md` §1.1. Found by running the built host and probing, not by
    reading code. This is not cosmetic: `getfenv`/`setfenv` disable the
    `safeenv` optimization, which is simultaneously the sandbox guarantee (R4)
    and the import fastpath, and native codegen gives up on any function that
    touches them (`docs/research/luau-2026.md` §3). Left in place it would have
    quietly invalidated every performance number measured afterwards. Fixed in
    `ScriptHost` before `luaL_sandbox`, with tests.

    *Scope note:* global curation nominally belongs to M2 (`architecture.md`
    §5 installs `game`/`workspace` in the same place). It was pulled forward
    deliberately because it is a sandbox-correctness issue against a spec that
    already exists, not new feature work — and because every measurement taken
    before the fix would have been misleading.
11. **Passing analyzer inputs as an expanded file list does not scale on
    Windows.** Measured: ~400 paths overflow the 32,767-character command-line
    limit and `luau-lsp` fails to launch. Harmless in CI (the `luau-check` job
    runs on Linux, where `ARG_MAX` is ~2 MB) but it will bite `luaug check` in
    M3, which must work on Windows. Passing a **directory** avoids it entirely,
    and `luau-lsp analyze` does recurse — verified with a planted error in file
    7 of 400.

## Measured: how analysis scales with .luau count

Taken on the dev machine so M3's `luaug check` has a baseline to start from.
Synthetic `--!strict` modules with generics, unions and refinements, analyzed
via the directory form, second run of two:

| files | wall time | ms per file |
|---|---|---|
| 25 | 73 ms | 2.92 |
| 100 | 105 ms | 1.05 |
| 400 | 236 ms | 0.59 |

Sublinear: roughly 60 ms fixed startup plus ~0.45 ms marginal per file.
Extrapolated, 10,000 files is still only a few seconds — analysis is nowhere
near being the bottleneck next to an ~8-minute cold C++ build.

Two caveats this measurement does **not** cover: the modules are independent,
with no `require` between them, so cross-module generic inference (the
expensive case) is untested; and the known new-solver failure mode is **memory
on large script counts**, not wall time (`docs/research/luau-2026.md` §2,
ADR 0018) — that is what to watch, and it needs a realistic dependency graph
to provoke.

## Attempted / abandoned

- **Patching Luau to add a version header** — considered to satisfy the
  original gate wording literally, rejected in ADR 0031: a permanent rebase
  burden in the fastest-moving dependency, against ADR 0021's near-empty patch
  set. The gate was amended instead, with human approval.
- **`json.serialize` to write back `manifest.json`** — rejected. `@std/json`
  emits keys in `pairs` order and renders an empty table as `[]`, so a
  round-trip would scramble a hand-authored, human-reviewed file. `vendor.luau`
  therefore never writes the manifest at all, which is also the right
  authority split (R5).

## Gate Record

Run 2026-08-19 on the dev machine (Windows 11, MSVC 19.50.35723 / VS 18
Community, CMake 4.1.1, Ninja 1.12.1, out-of-tree at
`%LOCALAPPDATA%\LuauG\build\win-msvc-dev`).

| Gate item | Result |
|---|---|
| CI green on Tier-1 | **Locally green, not executed in CI** — see blocker below |
| CI green on Tier-2 | **Unverified** — no Linux machine available to this session |
| `ctest`: VM boots sandboxed (env mutation from script fails) | **Pass** |
| `ctest`: script error → structured engine error with an i18n key | **Pass** |
| `--version` grounding proof (as amended by ADR 0031) | **Pass** |

```
$ cmake --preset win-msvc-dev && cmake --build --preset win-msvc-dev
231/231 targets, 0 warnings, 0 errors (/W4 /WX on engine code)

$ ctest --preset win-msvc-dev
100% tests passed, 0 tests failed out of 6
  core · app · host_version · host_version_abi · host_boot · host_usage_without_script

$ luaug-host --version
LuauG 0.0.1 (dev)
Luau 0.734 (3fc82b1071ab387531175869afc4fb528464afa4)
Luau ABI: bytecode 9, types 3, vector 3-wide f32

$ luaug-host examples/boot/boot.luau        # the M0 deliverable
LuauG 0.0.1 — engine initialized.           # resolved from i18n/en.json
boot.luau: hello from Luau
boot.luau: sandbox verified
exit 0

$ stylua --check tools examples                              # clean
$ luau-lsp analyze --platform=standard --ignore=**/.lute/**   # clean
$ lute tools/repo/i18nlint.luau        # 10 references, 22 keys, 0 missing
$ lute tools/repo/checklayers.luau     # 2 modules, 0 violations
```

**M0 is NOT closed.** Two gate items are unmet and neither is closeable by
this session:

- The repository has **no git remote**, so no CI run has ever happened. Every
  job is written and each one passes locally, but "CI green on Tier-1/Tier-2"
  is a statement about CI, and asserting it from a local run would be a lie.
  Creating/pushing to a remote needs an account — an escalation item under
  `MASTER_PROMPT.md` §10.
- **Tier-2 (Linux) has never been compiled.** The engine builds warning-free
  under MSVC, but the Linux profile adds `-Wconversion -Wsign-conversion
  -Wold-style-cast -Wshadow -Wpedantic` with `-Werror`. First-run failures
  there are likely and are ordinary work to fix — they simply cannot be found
  from this machine.
