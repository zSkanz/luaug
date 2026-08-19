# 0016 — Fixed-tick deterministic simulation with rollback-oriented foundations

- Status: accepted
- Date: 2026-08-19

## Context
Roblox shipped server authority + client prediction/rollback in 2026 — an
admission that the classic replication model cannot produce competitive-feel
gameplay, retrofitted at the cost of a parallel replication stack. LuauG v1 is
single-player (ADR 0012) but must not repeat that retrofit.

## Decision
The simulation runs on a **fixed tick** (default 1/60, configurable) with
variable-rate interpolated rendering. The architecture ships the
**rollback-oriented foundations**: deterministic scheduler ordering, seeded
RNG streams, tick-stamped input buffering, stable `InstanceId`s, snapshottable
POD ECS pools, and the physics `saveState`/`restoreState` seam. **Terminology
is deliberate: these are foundations, not rollback.** Full rollback — which
would also require restoring Luau gameplay state, coroutines, and the task
scheduler — is an explicit **non-goal of v1**.

## Consequences
Future official multiplayer builds on ground that was born correct. The
determinism gates (ADR 0025) keep the foundations honest. No v1 effort is
spent on Luau-state restoration.
