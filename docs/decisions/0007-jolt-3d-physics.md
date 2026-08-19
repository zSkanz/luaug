# 0007 — Jolt 5.6 as the default 3D physics backend

- Status: accepted
- Date: 2026-08-19

## Context
Physics must be swappable with a professional default. Candidates: Jolt 5.6
(MIT; ships in Horizon Forbidden West and Death Stranding 2; Godot 4.4+
default; deterministic, multicore, zero external deps, mobile+console proven),
PhysX 5 (BSD-3 but huge, GPU path CUDA-only), Bullet (effectively legacy),
Rapier (Rust FFI in a C++ core), Box3D (Erin Catto's new 3D engine — MIT,
double precision + cross-platform determinism + replay, but alpha as of
June 2026).

## Decision
**Jolt 5.6** behind `physics_api` (`IPhysics3D`): world create/step, bodies,
queries, contact drain → deferred `Touched` signals, `CharacterVirtual` for
CharacterBody, collision groups, and `saveState`/`restoreState` as the
rollback-oriented seam. Single-threaded stepping first; Jolt's job system
bridges onto the engine `jobs` pool when it lands (M7). **Box3D is tracked as
the future second backend** — its double-precision large-world support and
cross-platform determinism are exactly LuauG's long-term needs; promote when it
reaches 1.0 (new ADR then).

## Consequences
AAA-proven default with a real interface seam. Determinism gates (ADR 0025)
constrain how Jolt is configured (fixed tick, no wall-clock, stable iteration).
