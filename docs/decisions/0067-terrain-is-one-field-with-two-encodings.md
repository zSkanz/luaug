# 0067 — Terrain is one field with two encodings

- Status: accepted
- Date: 2026-08-27
- Milestone: F1 (post-v1 phase 2), step A1
- Decided by: the agent, under the owner's standing instruction of 2026-08-26 to
  take the repository's decisions on their behalf. The **representation** was the
  owner's own: *"terrain editor multiplayer voxels"*, 2026-08-27, which answered
  the question `docs/roadmap.md` had left open as "height field or voxel, and the
  answer decides whether caves are possible".

## Context

Caves are possible, so the terrain is a volume rather than a height function.
The naive reading of that is "store voxels", and the cost is immediate: a
64 m × 64 m column at a 0.5 m voxel is 16.7 million samples, which is not a
streaming cell and never will be.

`asset::ChunkId` is `{x, z, layer}` with **no vertical coordinate**, and
`chunkBounds` returns a vertically-infinite box. Adding a `y` means
`ChunkFormatVersion` 3 — a hard equality check with no back-compatibility path —
which invalidates every `.lchunk` on disk and every partition cache in the tree,
for a payload that does not exist yet.

## Decision

**One signed-distance field, sampled on one lattice, stored two ways.**

- A **dense per-column height layer** for the columns where the ground is a
  single-valued height function, which is most ground.
- **Sparse 16³ voxel bricks** for the columns where it stops being one, which is
  what a cave or an overhang makes it.

**One mesher — Marching Cubes on the primal lattice — and it cannot tell which
encoding answered a lattice point.** The two encodings do not meet in the mesher;
they meet one layer below it, in a sampler. For `sd(p) = p.y − H(x, z)` the
vertical-edge crossing is at exactly `y = H(x, z)`, which is the height grid's
own vertex, so the boundary between the encodings is an **equality rather than a
stitch**.

Bricks are `basis_zstd`-compressed in the cell format. That library is already
built from the full amalgamation in `third_party/CMakeLists.txt` and already
linked `PUBLIC` with a `SYSTEM PUBLIC` include through `basis::transcoder`, so
`#include <zstd.h>` costs nothing to build. **It is named here anyway**: R5's
rule is about a dependency edge being a decision somebody wrote down, not about
whether the build happens to work.

Tiles and bricks are **immutable and copy-on-write**, held through
`shared_ptr<const T>` in a scene component, so `UndoStack::record` — which
snapshots the whole world by value and keeps 64 of them — copies a vector of
pointers and a refcount rather than the field. Verified rather than assumed:
`ComponentPool::m_dense` is a plain `std::vector<T>` and `World::snapshot` copies
pools as values, so a `shared_ptr` member copies correctly today. The "POD and
memcpy-able" comment on `component_pool.h` becomes stale; the code does not
break.

### What A5 measured, and the claim narrowed to fit it

The plan's assumption was that "the height layer carries the majority". That is a
statement about worlds nobody has authored. `tools/repo/terrainsurvey.luau`
measured what can actually be measured, and the result **changed which claim this
ADR makes**:

- The flagship's ground is **0.0000%** steep, worst slope **6.3°** — with the
  bias stated, because `examples/10-open-world` is the gentlest ground here by
  construction.
- Sweeping a cliff's depth, the steep fraction **saturates** at the band's own
  area share. A cliff promotes the cliff and not the ground around it.
- Sweeping a cliff's **width** is the curve that decides the design. Promotion
  dilates outward until the boundary is shallow and gives up at 8 columns, which
  at a 0.5 m voxel is **four metres of sustained 45°+ slope**. A near-vertical
  cliff is one steep column and survives; a 16 m ramp is eighteen and bricks the
  whole cell. **The dramatic case is the cheap one.**

So the claim is not "the majority is cheap". It is: **the height layer carries
every surface shallower than 45°, and a steep region costs its own area plus one
dilation column, until that region is sustained for longer than the give-up
width.** That is falsifiable, and it is what was tested.

**The give-up width is therefore a tunable and not a constant**, recorded in the
`.lterrain` cell header so a world remembers the value it was built with. Eight
columns is right for a rolling island and wrong for an alpine map, and nothing
about the algorithm needs it fixed.

## Consequences

**A terrain cell fits `ChunkId` unchanged.** A sparse column of height tiles plus
whatever bricks the caves actually need is tens of kilobytes, not sixteen
million samples. No `ChunkFormatVersion` bump, no `PartitionRules` bump, no
`ChunkLayerCount` change, no new `StreamingService` properties. This is the
single largest reason the hybrid was chosen over the two pure designs.

**The common edit reaches `SetHeights` instead of a body rebuild** (ADR 0066),
because the common edit is on ground that is still a height function.

**The failure mode is the baseline.** A cell whose promotion gives up converts to
pure voxel bricks — which is exactly the design the hybrid was compared against.
It loses the saving. It does not break, and there is no third state to test.

**Two representations are where cracks live, and that is the honest risk.** It is
why this is not the simplest of the three designs. What makes it defensible is
that the boundary is an equality rather than an approximation, that every edit
dilates by one voxel and promotes before writing so the boundary is never
straddled mid-write, and that F1's gate names the experiment that would falsify
it: per-column downward rays across a cave crossing both a cell boundary and a
brick/height boundary, asserting the sampler, the physics and the render agree.

**The representation is state.** Which columns are bricked is part of the world,
promotion happens exactly at an edit, demotion only at an explicit compaction,
and the on-disk format preserves it — so a save and reload is hash-preserving.
A hybrid that decided its own encoding lazily would not be.

## Alternatives rejected

Both were designed in full and judged against this one; the judgements are in
`docs/briefs/phase-2-4-plan.md`.

**Dense bricks everywhere, Surface Nets.** The best determinism writing of the
three, and it needs `ChunkFormatVersion` 3, a `PartitionRules` bump, a
`ChunkLayerCount` change, three new IDL properties and a payload variant through
`StreamingManager` — surgery on the one subsystem in the design that currently
works and is tested, for a payload not yet built.

**Sparse octree, Marching Cubes into `MeshPart` tiles.** Rejected on a verified
fact rather than a judgement: `attachPartComponents` adds a `RigidBodyComponent`
to every `BasePart` with no condition, and `applyScene` has no skip — so its
~250 generated tiles would be ~250 Jolt bodies, and the far ones, with no
collision points, would collide as boxes of their tile size: 64, 128 and 256
metre boxes in the broadphase overlapping everything inside them. That
invalidates its physics budget, its broadphase argument and its raycast story at
once.
