# 0082 — Terrain is one grid of voxels, each with a material and an occupancy

- Status: accepted
- Date: 2026-09-23
- Supersedes: [0067](0067-terrain-is-one-field-with-two-encodings.md) (one
  field, two encodings) and
  [0071](0071-terrain-ground-is-drawn-from-a-height-atlas.md) (the ground
  drawn from a height atlas)
- Amends: [0075](0075-terrain-and-block-worlds-stream-in-cells.md) (cells are
  cut from chunks now, not tiles and bricks) and
  [0081](0081-the-editor-holds-the-field-and-the-gpu-holds-what-is-near.md)
  (the GPU holds meshes of what is near, not atlas slots)
- Decided by: the owner ("vamos ter que fazer estilo o do roblox"), with the
  design by the agent under the standing instruction of 2026-08-26.

## Context

ADR 0067 kept most ground as a height layer and switched a column to voxel
bricks only where it stopped being a height function. That was cheap, and the
cost showed up at the join between the two encodings. The owner sculpted a
world and found the defects, in order:

- D161: a selected terrain outlined its buried cave geometry.
- D162: a height brush left a slot through a hill where a cave was.
- D163: a dig cut the hill beside it off flat, or deleted the plain under it.

Behind these, eight seam hypotheses were tried and reverted: snapping,
skirts, gradient normals, slope-scaled distance and others. Each fix was a
rule about how the two encodings meet. Two encodings meet at every column,
so the rules kept coming.

The owner also reported that digging caves lagged badly. Every dig promoted
columns to bricks, and every promoted column was:

- meshed on the CPU for drawing;
- meshed again for a collider;
- opened as a hole in the GPU atlas.

The owner asked for terrain that works like the reference platform's: "not a
plane, a square that fills itself in." The research, from the reference
platform's own engineering posts and API, found the following.

**Storage**
- One uniform grid of cells, 4 studs (about 1.1 m) across.
- Each cell stores a material and an occupancy between 0 and 1. Air is a
  material.
- The grid is kept in sparse 32-cubed chunks, with a mip pyramid for level
  of detail.
- Rows that are all one value take one byte each: 1 billion cells fit in
  488 MB instead of 2.9 GB.
- On disk, cells are run-length coded and then compressed.

**Meshing and physics**
- The surface is meshed per chunk from the occupancies. Coarser mips are
  meshed into larger render chunks for level of detail.
- Collision is built lazily from the same mesher, one 8-cubed chunk at a
  time, around the bodies that need it.

**Script API**
- `FillBall`, `FillBlock` and `FillCylinder`.
- `ReadVoxels` and `WriteVoxels`, which work in material and occupancy
  arrays.
- `WorldToCell` and `CellCenterToWorld`.

The reference platform is not a heightmap. Nobody ships a hybrid either.

## Decision

**A terrain is one sparse grid of voxels. Each voxel holds a material byte and
an occupancy byte, and there is nothing else.** No height layer, no bricks, no
promotion and no atlas. Every consumer asks the same grid.

### Storage (`asset::TerrainField`)

- **Voxel `i` covers `[i*v, (i+1)*v)`**, and its sample sits at its centre,
  `(i + 0.5) * v`. `v` is `VoxelSize`, which now defaults to 1 m (it was
  0.5). A terrain converted from the old format keeps its own size.
- **The surface is where occupancy crosses one half.** A flat ground at
  height `h` has occupancy `clamp(0.5 - (y - h) / (4v), 0, 1)` at a voxel whose
  centre is `y`. Mesher, raycast and `HeightAt` interpolate that crossing, so
  the surface lands where the brush put it, not on the lattice.
- **Occupancy ramps across four voxels, not one** (`RampVoxels`), and that
  was measured. Ground laid from heights knows only its vertical distance.
  With a one-voxel ramp, a slope past 45 degrees clamps the voxel beside the
  surface, and the crossing lands in the wrong place: every hillside was a
  staircase. Across four voxels the field stays linear to 63 degrees, and
  `writeHeights` divides by the slope for the rest.
- **Occupancy 0 is air, and air has material 0**, always. Every write path
  normalises this, because a digest over bytes would otherwise tell two
  equal worlds apart.
- **Chunks are 32 voxels on a side**, keyed `(x, y, z)` and sorted x, then z,
  then y. With that order a column of chunks is one contiguous run, which is
  what `HeightAt`, the column-top map and the LOD columns walk.
- **A chunk stores rows.** The row at `(y, z)` is 32 voxels along x. It is
  either one uniform value, or an index into the chunk's dense rows. A chunk
  whose rows all hold one value collapses to that value, and a chunk of air
  is not stored at all.
  - Flat ground at 1 m costs about 4 KiB per 32 m square, not 64 KiB.
  - The ground below it is a list of uniform chunks.
- **Copy-on-write through `shared_ptr`**, as before, so an undo snapshot
  copies pointers. A lazy digest per chunk keeps the world hash O(chunks).
- **A lazy mip pyramid per chunk**: 16³, 8³, 4³, 2³ and 1. Each level averages
  the occupancy of eight children and takes the material of the fullest.
  Level-of-detail meshing reads mips, never point samples, so a thin wall
  thins rather than aliasing away. Computed when first asked for, and
  dropped by any write.

### Editing

Every verb reads and writes occupancies over the brush's box, with the ramp
at the boundary so the surface lands exactly on it.

- **Fills** take the maximum of old and new occupancy. **Removals** take the
  minimum against one minus the new.
- **Raise** shifts each column up inside the brush's cylinder by the falloff,
  and keeps the larger of the old and shifted occupancy. **Lower** is the
  mirror. So a cave under the brush's reach is not moved.
- **Smooth** blends each voxel toward a separable box blur one to four voxels
  wide, growing with the brush. The mean of a linear ramp is the ramp, so a
  three-voxel blur only rounded a step's edges.
- **Flatten** blends toward the occupancy of a plane.
- **Paint** writes material where occupancy is not zero.
- **`fillFlat` and `writeHeights` move each column's top**, as volume:
  - they fill from the top up to the height, or clear from the height down to
    the top;
  - a column with no ground is laid as a slab (see "Edges and bottoms");
  - within the ramp round the new top the ramp is written exactly: taking the
    larger or the smaller of two ramps of different slopes put the top in
    neither place;
  - a cave under the top stays (D162's promise, kept);
  - they write chunk by chunk, so a whole square of uniform chunks costs one
    value each, not 32,768 writes.

Nothing examines a column any more. That was the code D163 lived in.

### Meshing (`asset::meshField`)

It is still a surface net, over occupancy. The density is `0.5 - occupancy`,
and vertex normals come from the density's gradient.

- **A region owns lattice points `[min, min + n)` on each axis.** It builds
  cells from `min - 1`, so every quad around an owned edge has its four
  cells.
- **Two regions side by side compute the shared cell ring from the same
  samples.** They place the same vertices, so the surface is watertight
  without stitching.
- **A region meshes at a level of detail `L`.** Its samples are that level's
  mips, and its lattice step is `2^L` voxels.
- **Skirts hang from the outer ring, along the negative normal.** They cover
  the crack where two levels meet. A collider is always meshed at level 0 and
  never gets a skirt.
- **Sky visibility is still baked per vertex.** It is read from a column-top
  map built once per region, not from `heightAt` per tap.

### Drawing

The GPU height atlas, the CDLOD grid, `luaug_terrain.hlsli` and the cave flag
are gone. The ground is meshes, exactly as caves were.
- The cave shader is now `terrain.hlsl`.
- `terrain_depth.hlsl` draws a mesh into the shadow maps with no culling,
  pushed away from the light.

- **The ground is split into a quadtree of columns of chunks.**
  - A leaf is one column of chunks, meshed at level 0.
  - A node at level `L` covers `2^L` by `2^L` leaves, meshed from their level
    `L` mips.
  - Each node spans the full height its chunks occupy, so level-of-detail
    seams are only ever on the sides, where skirts cover them.
- **A node is split when the camera is nearer than 2 times its width.**
- **A node is drawn until all its children are ready, and a set of children
  until their parent is.** So a change of level never shows a hole.
  - "Ready" means covered all the way down, not merely built.
  - Every node the selection walks through is kept.
  - Without either, an ancestor let go was rebuilt and counted as ready. It
    then covered the close-up, coarse, every few seconds: the flicker the owner
    saw in `17-cave`.
- **Meshes are built nearest first, a fixed count per frame** (R10: a count,
  never a clock). An edit rebuilds only the nodes whose chunks changed.
- **Shading is the old cave shader, renamed `terrain.hlsl`.** It uses:
  - the per-vertex material and sky visibility;
  - triplanar detail;
  - the slope rule.

### Colliders

**One triangle mesh per chunk, near things that move**, built by the same
mesher at level 0. It replaces a height field per tile plus a mesh per
bricked column.

- A chunk with no surface in it has no body.
- A chunk far from every mover has no body. This was already the rule for
  caves.

**`Workspace:Raycast` also asks the terrain directly** (`raycastField`), and
keeps the nearer hit. So a ray still meets ground that has no collider near
it, as it did when every tile had a height field.

### Format

`.lterrain` version 3:
- the header;
- then each chunk's key and its voxels, run-length coded as (occupancy,
  material) runs;
- then the body is run-length coded again, as version 2 did.

A version 2 file, from the hybrid, is decoded by the old reader. It is then
resampled into voxels by the old sampler, and never written again. So every
saved world opens.

### Edges and bottoms (amended the same day)

The owner extended generated ground sideways. The result showed two different
things side by side:
- a slab whose sides stopped short and were open underneath;
- a wall down to the world's floor, with a black line along its foot.

Three rules now make an edge look the same however it was made:

- **Ground laid where there was none is a slab `LaidDepth` (32 m) deep**, not
  a pillar to `MinHeight`. It lies under the lowest surface the verb lays and
  is rounded down to a chunk boundary. This covers `writeHeights`, `fillFlat`
  and a first raise on empty columns.
- **A solid chunk with anything but solid ground against any face can hold a
  surface.** That includes air past the terrain's edge beside it, or nothing
  under it. So walls and bottoms are meshed everywhere, and a node is meshed
  one run of such chunk layers at a time (`activeRuns`), never walking the
  rock between.
- **A vertex under all the ground in its column sees half the sky.** That is
  the terrain's underside, not a cave's roof, which has a floor under it.
  Counted as a roof, it was drawn black.

## Consequences

- **What each defect becomes.**
  - Every cave, overhang and tunnel is the same data as the ground beside
    it, so D161, D162 and D163 have no seam left to live in.
  - A dig writes a sphere of occupancies and remeshes the chunks it touched.
    Nothing is promoted or opened, and no second encoding is re-derived.
- **What it costs.**
  - Flat ground is now meshed where the atlas drew it from a texture. At
    1 m a leaf is about 2,000 triangles for a 32 m square.
  - Distant ground is coarse meshes built from mips, not a vertex shader
    morph.
  - Chunks far from movers have no collider. A dynamic body is a mover, so
    it builds its own.
- **`HeightAt`** now answers the top of the column, caves included. That is
  what it already meant in practice.
- **`Compact` has nothing to do.** Every write leaves the grid compact, and it
  returns zero. It stays so that scripts written against version 1.1 still
  run.
- **`CellCount`** counts chunks.
- **Additions to the script API:**
  - `ReadVoxels` and `WriteVoxels`;
  - `WorldToCell` and `CellCenterToWorld`;
  - `FillCylinder`, `SmoothBall`, `FlattenBall` and `ReplaceMaterial`.
- **A terrain still translates and does not turn.** That is no longer forced
  by the encoding. A voxel grid could be rotated, but nothing asked for it.
