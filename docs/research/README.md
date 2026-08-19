# Research Reports

Three deep research reports were produced during the planning phase
(2026-08-19) by parallel research agents with live web access. They are the
factual foundation of the architecture and of the pinned stack.

A fourth report, [`luau-c-api-2026.md`](luau-c-api-2026.md), is different in
kind: it was produced by reading the **vendored** Luau sources at the pinned
commit, with no web access at all. Where it and a web-derived report disagree,
it wins — that is the rule below, applied once and written down.

## Rules

- **Reports are frozen snapshots.** Never edit their body. Corrections and
  updates go into a dated `## Addendum (YYYY-MM-DD)` section appended at the
  end of the affected report, or into the live registry below.
- **[`UNCONFIRMED.md`](UNCONFIRMED.md) is the live registry** of claims that
  were not verified against a primary source. Any web-derived claim the
  builder agent relies on in code must be added there until confirmed against
  vendored source or a passing test (`MASTER_PROMPT.md` §9).
- If a report contradicts vendored reality (headers in `third_party/`),
  **vendored reality wins** — add an addendum noting the correction.
- Each report ends with its own "UNCONFIRMED / COULD NOT VERIFY" section;
  the registry seeds from the highest-impact items and links back.

## Contents

| File | Scope | Captured |
|---|---|---|
| [`luau-2026.md`](luau-2026.md) | Luau language/runtime state: releases through 0.734, new type solver, native codegen, C++ embedding API, vector/buffer/integer/class, require-by-string, tooling | 2026-08-19 |
| [`lute-2026.md`](lute-2026.md) | Lute runtime deep dive: v1.0.0, architecture, `@lute/*` and `@std`, CLI, extensibility/embedding risk, Lute vs Lune | 2026-08-19 |
| [`ecosystem-2026.md`](ecosystem-2026.md) | Prior art (Luau engines), Roblox 2026 API conventions worth mirroring, and the native library survey (SDL3, GPU, physics, audio, assets, UI, nav, net, math) with licenses | 2026-08-19 |
| [`luau-c-api-2026.md`](luau-c-api-2026.md) | The embedding surface, verified line-by-line against vendored source: tagged userdata and atom/namecall dispatch; threads, resumption, sandboxing, errors; require-by-string, `.luaurc`, module registration; allocator, memory categories, GC pacing, compile options, vectors; FastFlags | 2026-08-19 |
