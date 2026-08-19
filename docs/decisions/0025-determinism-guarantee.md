# 0025 — The precise v1 determinism guarantee

- Status: accepted
- Date: 2026-08-19

## Context
"Deterministic" is meaningless until scoped. Three levels exist: (A) same-run
repeatability, (B) same-build/same-platform determinism, (C) cross-platform
bit-identical determinism. Level C is a research-grade problem
(floating-point/toolchain/concurrency variance across compilers and CPUs).

## Decision
The **v1 guarantee is level B**:

> Same engine build + same platform + same initial state and seed + same tick
> configuration + same ordered inputs ⇒ same observable simulation result
> (verified by WorldHash).

Level A is implied. **Level C is NOT a v1 guarantee** — cross-platform hash
equality (win↔linux) is measured in a tracked, non-blocking CI job as an
aspiration. Supporting rules: simulation code never reads wall-clock, never
uses unseeded RNG, never iterates unordered containers into observable order
(rule R10); parallel sim-visible work goes through the deterministic commit
pattern (per-job buffers → barrier → stable-order merge → world mutation);
render/asset/IO jobs are exempt. The replay-hash harness exists from M2 and
gates merges from M5.

## Consequences
Tests and terminology stay honest; rollback foundations (ADR 0016) rest on a
guarantee that is actually enforceable in CI.
