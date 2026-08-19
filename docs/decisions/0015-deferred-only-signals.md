# 0015 — Deferred-only signals; no legacy scheduling globals

- Status: accepted
- Date: 2026-08-19

## Context
Roblox carries two signal modes (Immediate/Deferred) plus legacy
`wait`/`spawn`/`delay` globals purely for backwards compatibility, and is
migrating everyone to Deferred. Immediate-mode reentrancy is a permanent
source of engine bugs. LuauG has no legacy to honor.

## Decision
Signals are **deferred-only**: firing enqueues; handler queues drain at
documented resumption points; each handler runs on its own coroutine; nested
re-entrancy depth is capped at 10 (overflow logs a keyed error and drops).
There is **no Immediate mode** and the legacy globals
(`wait`/`spawn`/`delay`/`tick`) **do not exist** — only the `task` library.

## Consequences
One predictable semantics, parallel-phase-ready, and a clean story for
determinism. Divergence from Roblox is documented loudly in
`docs/coming-from-roblox.md`.
