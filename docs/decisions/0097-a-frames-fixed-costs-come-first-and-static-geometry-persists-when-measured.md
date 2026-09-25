# 0097 — A frame's fixed costs come first, and static geometry persists when a measurement asks for it

- Status: accepted
- Date: 2026-09-25
- Decided by: the agent, under the owner's instruction of 2026-09-25 to close
  the gap with Godot on a like-for-like scene (*"você vai ficar responsável por
  fazer isso mete marcha"*), which named the design this record tests: stop
  rebuilding, every frame, the objects that do not move.
- Relates to: [0037](0037-rhi-interface-frozen-at-m4.md) (the frozen RHI),
  [0043](0043-per-instance-vertex-stepping.md) (instancing),
  [0090](0090-a-material-is-an-asset-a-part-wears-one-and-a-script-clones-one.md)
  (per-part material overrides), D183, D184, D193

## Context

The owner's CityBench is one scene built identically in LuauG and Godot 4.7.2 by
the same seeded generator. It has 400 buildings and 4000 static props under 8
shared materials, 64 point lights, a four-cascade sun, 500 Jolt crates, 400
script-driven kinematic parts, an orbiting camera and a UI label. It lives
outside the repository, in the owner's `luaug-playground/benchmark`.

On 2026-09-25, with D183 fixed and both engines at the same priority (D193),
LuauG's median frame was about 3.3 ms and Godot's 2.0-2.2 ms. Of the 4879
visible objects, 4400 never move, and the extract rebuilt all of them every
frame. That was the obvious suspect, and the owner named it.

**It was measured before it was built**, with temporary per-phase timers in a
separate worktree. They were never committed. The median frame of 3.05 ms
broke down as follows:

| Phase | ms |
|---|---|
| submit | 0.54 |
| the light-table texture uploads | 0.37 |
| batching and uploading the instance stream | 0.35 |
| the tick (physics 0.47) | 0.60 |
| the extract's part loop | 0.46 |
| the draw sort | 0.20 |
| everything between the extract and the render | 0.20 |
| recording every pass | about 0.15 in all |

**The largest single cost was not the static objects.** It was that every
`upload`, `uploadTexture` and `uploadTextureRegion` in `rhi_sdlgpu` created an
SDL transfer buffer and released it: a driver resource allocated and freed per
call, a few dozen times a frame, with much of the bill paid again at submit.
The code's own comment had said *"a per-frame pool is an optimization for
whichever workload first needs one"*. This was that workload.

## Decision

### Fixed per-frame costs are removed first, and without an interface change

- **One staging buffer per frame** (`20b43f0c`). Each upload is written at the
  next aligned offset: 512 bytes for a texture copy, which D3D12 requires, and
  16 for a buffer. The first write of a frame cycles the buffer, which is SDL's
  guarantee that bytes a frame still in flight is reading are never overwritten.
  The buffer grows between frames up to 16 MiB. An upload over 4 MiB, or one
  that does not fit mid-frame, keeps a buffer of its own. The RHI interface is
  unchanged (ADR 0037).
- **The extract resolves the five primitive meshes once per frame** (`e122225b`)
  instead of by name for every part.
- **The part matrix is computed fused** (`toRenderMatrixScaled`, landed with
  this record), bit for bit
  the product it replaces: sixty of the general product's sixty-four
  multiplications were by a known zero. The extract also remembers the last
  material a part matched, and sorts (key, index) pairs rather than moving
  hundred-byte draw items.

Each of these changes an implementation and not a contract. Each is proven
invisible:

- screenshots are byte-identical before and after in every deterministic scene
  tried;
- the capture goldens and the lavapipe pixel goldens pass unchanged;
- the fused matrix is checked against the long form on 900 cases, negative
  zeros included.

**Result**: CityBench's median frame went from 3.05 to about 1.6 ms, against
Godot's 2.0-2.2 ms on the same machine.

### Static geometry persists when a measurement asks for it, and this is the shape it takes

After those changes, the work that rebuilds unmoving objects every frame is
about 0.35 ms of a 1.6 ms frame: the part loop, the sort, and the physics
mirror's apply. That is real, and it grows with the number of static objects.
It is also the one optimisation here that changes an architecture rather than
an implementation. So it is recorded now and built when one of these is true:

- a reference scene's extract loop and sort together exceed a quarter of its
  median frame, or
- a scene of 50,000 or more static objects is a stated target, which is where
  a per-frame rebuild stops being a constant and becomes the frame.

When it is built, it takes this shape:

1. **The pools say what changed, conservatively.** `ComponentPool` stamps a slot
   on every *mutable* access (`find` and `forEach` through a non-const pool)
   with a monotonic counter. A consumer that remembers the counter at its last
   visit knows a slot is untouched if its stamp is older. Over-marking is
   allowed and costs only speed; under-marking is impossible, because nothing
   can write a component without a mutable access. **The price is an audit**:
   every hot read path that holds a non-const `World` must read through a
   `const` one, or it marks everything and the scheme saves nothing. That audit
   is the work, and it is why this is not a quick change.
2. **The extract keeps a per-instance cache** of what it derives from a part:
   the rotation-scale block, the local bounds, the material slot. It is
   refreshed only for stamped slots. The translation is still recomputed every
   frame, because the render origin is the camera (ADR 0014); that is a
   subtraction, and it is what `toRenderMatrixScaled` already isolates.
3. **The instance stream stays resident on the GPU**, and a frame uploads only
   the ranges of the instances that changed.
4. **The sort becomes incremental per material family**: an unchanged family
   keeps its order.
5. **The physics mirror's apply skips unstamped bodies** (0.18 ms a tick at
   CityBench's 5429 bodies).

### Rejected

- **Building the persistent scene first.** It was the owner's hypothesis and a
  reasonable one, and it would have removed about a third of the gap. It would
  also have left the uploads, the larger cost, where they were. Measuring first
  is what found them.
- **An input-comparing cache without stamps.** Comparing a part's inputs
  against a cached copy costs about as much as the recomputation it saves:
  roughly 80 bytes a part, read out of order. The stamps are what make a cache
  cheaper than the work.
- **Making the extract loop's writeback check a binary search.** It was
  quadratic on paper, and measured at no difference, because kinematic bodies
  never enter that branch. It was reverted.

## Consequences

- **The two engines now meet on this scene with LuauG ahead**, and every change
  that did it is invisible to what is drawn.
- **Frame tails** (p95 and p99) improved with the median, because the removed
  work included driver allocations, whose cost is variable.
- **The benchmark and its tools stay outside the repository**, in the owner's
  playground, with its `LEIA-ME.md`. The numbers above are the owner's
  machine's: i5-14600K, RTX 4070 Ti SUPER, a 240 Hz monitor.
