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

- [ ] Vendor third-party dependencies into `third_party/` at manifest-target
      versions, recording **real commit SHAs** (Luau, SDL3, doctest first;
      later deps may be vendored lazily but manifest rows must exist)
- [ ] Activate `CMakePresets.json` (build dirs under `$env{LUAUG_BUILD_ROOT}`,
      never in-tree)
- [ ] `luaug-host` executable: create Luau VM, `luaL_sandbox`, register a
      minimal `print`/log bridge routed through the key+catalog i18n system
      (one English catalog proves the seam)
- [ ] rokit-pinned toolchain (Lute 1.0.0, StyLua 2.5.2, luau-lsp 1.69.0)
- [ ] Activate CI: Windows + Linux build, ctest, `luau-analyze` strict on all
      `.luau`, StyLua check, i18n lint stub, docs-lint

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

## Attempted / abandoned

_(append during the milestone — §12)_

## Gate Record

_(filled at milestone end, before human review)_
