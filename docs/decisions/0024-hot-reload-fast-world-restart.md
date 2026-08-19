# 0024 — Hot reload: fast world restart is the canonical v1 model

- Status: accepted
- Date: 2026-08-19

## Context
Two designs emerged during planning: (a) module-level reload — topological
re-execution of changed modules over the require graph with a
`__hotreload(oldExports)` migration protocol; (b) fast world restart — tear
down the game VM entirely and rebuild. Module-level reload is fundamentally
harder to get right (stale closures, module state, coroutine ownership,
dependency invalidation) — sharp edges an autonomous implementer would bleed
on. External review flagged the contradiction.

## Decision
The canonical v1 model is the **fast world restart**: source change → batch at
the FrameStart safe point → `BeforeReload` → capture the explicit state bag +
`PreserveOnReload`-tagged instances → janitor cancels script tasks and
disconnects script-owned connections → destroy the game VM → fresh VM + engine
API remount → re-run scripts → restore preserved state → `AfterReload`.
Window, GPU resources, imported assets, and engine-materialized streamed
chunks **survive**. Hard performance requirement: **< 500 ms**. Assets and
shaders hot-swap in place independently (content-hash swap; pipeline
invalidation). The require graph is still tracked — it drives bytecode-cache
invalidation. **Module-level reload with `__hotreload` is post-v1**, and only
if the restart budget proves insufficient.

## Consequences
A reload model with zero stale-state classes of bugs, simple to implement and
reason about. The <500 ms target is enforced as a perf gate from M3.
