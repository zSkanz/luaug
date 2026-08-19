# 0014 — f64 world coordinates + per-region floating origin

- Status: accepted
- Date: 2026-08-19

## Context
Huge open worlds break f32 precision (jitter far from origin). Script-side
double vectors are off the table (ADR 0013). Physics and rendering want f32.
A future multiplayer server may host players hundreds of kilometers apart, so
a single hard-coded global origin would be an architectural dead end.

## Decision
Authoritative transforms store **f64 positions** (`CFrameD`) in the ECS.
Physics and rendering operate in **f32 space rebased to a floating origin**;
the origin shifts when the primary focus moves >4 km from it, in one budgeted
pass at the FrameStart safe point (physics teleports preserve velocity).
Scripts always see true world coordinates: `CFrame` carries the f64
translation; rebasing is invisible to Luau. **Origin/rebase state belongs to
the World (simulation region), never to a global** — `IPhysics3D` is already
multi-world, so N regions with independent origins remain possible without
rework (none of that is implemented in v1).

## Consequences
Precision error does not accumulate (f64 truth); f32 fast paths everywhere
hot. Tests must prove behavior at coordinate 1e7 matches origin behavior
(M7 gate).
