# 0066 — The physics seam learns two static shapes

- Status: accepted
- Date: 2026-08-27
- Milestone: F1 (post-v1 phase 2), step A2
- Decided by: the agent, under the owner's standing instruction of 2026-08-26 to
  take the repository's decisions on their behalf. **This is one of the two the
  F1 plan reserved for a person** (`docs/briefs/phase-2-4-plan.md`), and it is
  recorded here as taken rather than skipped so that reversing it is one edit.

## Context

F1 sculpts terrain, and terrain has to be collidable. The shapes this engine can
build are `Box`, `Sphere`, `Capsule`, `Cylinder` and `ConvexHull`
(`engine/physics/include/luaug/physics/types.h`). **A voxel surface is none of
them.** It is concave by construction — that is what a cave *is* — so no convex
hull describes it, and it is not a primitive.

Both shape classes that would describe it are **already compiled and linked**.
`HeightFieldShape.cpp` and `MeshShape.cpp` are listed in
`third_party/jolt/Jolt/Jolt.cmake`, so this costs no CMake change and no new
dependency; the seam simply never exposed them.

## Decision

`ShapeType` gains **`HeightField`** and **`TriangleMesh`**. `ShapeDesc` gains the
payloads each needs, under the rule the struct already states: **no span may
outlive the call**, so `sameShape` and `withoutPoints` widen to strip every new
span exactly as they strip `points` today.

`IPhysics3D` gains **`updateHeightField(world, body, x, z, sizeX, sizeZ, heights)`**,
which is the whole reason a height field is worth having as its own kind rather
than meshing everything.

### Why `updateHeightField`, when `updateBody` exists

`updateBody` is `RemoveBody` + `DestroyBody` + `CreateAndAddBody`: a new Jolt
body, a broadphase removal and insertion, and every constraint on it rebuilt.
`HeightFieldShape::SetHeights(inX, inY, inSizeX, inSizeY, …)` rewrites a
sub-rectangle **in place on the shape that already exists** — verified at
`third_party/jolt/Jolt/Physics/Collision/Shape/HeightFieldShape.cpp:994`, with
its block-alignment assertion at `:1000`. Same shape object, same `JPH::BodyID`,
no broadphase churn.

**The block alignment is a real constraint on the caller and is stated here so
it is not discovered in a debugger**: `JPH_ASSERT(inX % mBlockSize == 0 && inY % mBlockSize == 0)`.
A brush's affected rectangle has to be grown outward to block boundaries before
it is handed over. It also takes a `TempAllocator&`, which the backend owns.

**What this ADR no longer needs, and why that is worth recording.** The F1 plan
asked for a "quiet rebuild" flag on the update path to suppress `forgetPairs`,
because reshaping a body re-announced every contact on it — a character standing
on ground being dragged got a `Touched` every tick of the drag. That was **D151**,
and it was fixed at the root instead: `updateBody` no longer forgets pairs at
all, because a reshaped body persists and its pairs are still about a body that
exists. So the correctness argument for `SetHeights` is gone and only the
performance one remains, which is the honest state and a smaller seam.

### The forced-Static rule

`instantiate` computes mass as
`max(shape->GetVolume() * max(density, 0.0001f), 0.001f)` and activates any body
whose motion is not `Static`. **Both new shapes report `GetVolume() == 0`**, so a
terrain body created as `Dynamic` would be a one-gram object with a
kilometre-wide collider, activated, and it would leave. Both also override
`MustBeStatic()` to return `true` (verified at `HeightFieldShape.h:132` and
`MeshShape.h:94`, against a base that returns `false`).

So `instantiate` **forces `Static` for any shape whose class reports
`MustBeStatic()`**, rather than trusting the description. Asking Jolt is better
than listing the two kinds here: a third static-only shape added later is handled
without this code knowing about it.

## Consequences

**`Enum.CollisionFidelity.Precise` becomes implementable, and F1 does not
implement it.** Its documentation says, in the IDL today: *"Accepted and not yet
implemented: this release collides against a hull and says so through this
property reading back `Precise` while behaving as `Hull`."* `TriangleMesh` is the
shape that sentence is waiting for. Wiring `MeshPart` to it is a separate piece
of work with its own cost — the collision points a mesh carries today are a hull
point cloud, not an index buffer — and **the enum's doc stays exactly as honest
as it is until something changes it**, which is the same rule that keeps a
declared class from existing without an implementation.

**Nothing above L2 can name these yet.** The two enumerators, their payloads and
their `buildShape` cases land with a test each and no caller, which is what makes
this step landable on its own.

**A concave mesh cannot be dynamic and that is Jolt's rule, not ours.** The
forced-Static rule makes the engine agree with it out loud instead of producing a
body that behaves absurdly. A caller asking for a dynamic terrain gets static
terrain rather than a refusal, because the alternative — refusing — leaves a
world with a hole in it where the ground should be.

## Alternatives rejected

**Mesh everything, no height field.** One shape kind instead of two, and no
block-alignment rule to respect. Rejected because it throws away `SetHeights`,
which is the only in-place shape update Jolt offers and the reason the majority
of a sculpt stroke can avoid a body rebuild entirely. ADR 0067's hybrid exists to
reach this seam.

**Expose Jolt's shape classes directly.** Refused by R17 before it was
considered: no backend type may appear in a description this engine hands around,
and `ShapeDesc` is a POD vocabulary precisely so a second backend stays possible.
