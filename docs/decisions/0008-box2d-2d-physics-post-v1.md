# 0008 — Box2D 3.1 for the post-v1 2D layer

- Status: accepted
- Date: 2026-08-19

## Context
LuauG targets complete 2D games as well as 3D (user requirement), with 3D
first (user decision #7). The dedicated 2D layer (sprites, tilemaps, 2D
physics — not just an orthographic camera) is the first post-v1 phase.

## Decision
**Box2D 3.1.1** (MIT, C17, SIMD — the industry standard) is the committed 2D
physics backend, behind its own `IPhysics2D` interface (separate from
`IPhysics3D`). It is vendored and manifest-pinned from M0 but not integrated
until the 2D phase.

## Consequences
The 2D layer starts from a settled physics decision. Note Box2D's cadence may
slow (upstream author's focus moved to Box3D) — acceptable for a stable,
finished v3 line.
