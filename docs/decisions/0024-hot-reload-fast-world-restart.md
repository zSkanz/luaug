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

## Addendum — 2026-08-20 (M3): the member names the lints chose

The Decision above names the two signals `BeforeReload` and `AfterReload`, and
api-design.md gave the service an `IsReload: boolean` property. All three
spellings fail the API definition's own §9 lints: an event must be a past-tense
fact or a `Pre*`/`Post*` phase, and a boolean property may not carry an `Is`
prefix while a boolean *method* is expected to.

The rules are right and the spellings were not, so the surface is
`PreReload` / `PostReload` and `IsReload()` — a method, which also drops the
service's only property and the component behind it. Nothing had shipped; the
alternative was to grow the exception list for names that had never been
argued for, which is exactly what that list's own comment warns against.

## Addendum — 2026-08-20 (M3): build before destroy

The Decision above reads as a straight line, and one step of that order is
wrong. Taken literally — destroy the VM, then build the fresh one — a project
that fails to mount leaves the process alive with **no world at all**, and the
failure has nothing to say about what the developer is now looking at. One
syntax error in one entry script is the common case in a loop whose whole point
is that you save often.

The order is therefore: capture the state bag and the `PreserveOnReload`
instances from the old world, **boot the new `WorldHost` alongside it**, and only
on a successful boot restore into it, swap the frame loop's pointer, and destroy
the old. A failed boot destroys the half-built new one and leaves the previous
world running untouched.

Nothing about *what survives* changes — window, GPU resources, imported assets
and streamed chunks are exactly as above, and so is the <500 ms budget, which now
covers the build and the destroy together. The cost is that two VMs and two
worlds coexist for the length of one FrameStart safe point.

