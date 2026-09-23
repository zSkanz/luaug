# 0075 — Terrain and block worlds stream from disk in cells of their own

- Status: accepted
- Date: 2026-09-23
- Milestone: F1 Part E (terrain streaming), and V1's "chunks streamed from disk"
- Decided by: the agent, under the owner's instruction of 2026-09-23 to build
  terrain and block-world streaming first, and the standing instruction of
  2026-08-26 to take the repository's decisions on their behalf.
- Builds on: ADR 0053 (a scene meets the streaming grid at a partition),
  ADR 0067 (one field, two encodings), ADR 0071 (the ground is drawn from a
  height atlas)

## Context

A `Terrain` and the block world were whole-resident. The scene carried each
inline, the renderer uploaded every tile, and the physics mirror built a
collider for every tile. Parts had streamed since M7, through a partition
that cuts a scene into 256 m cells on play. The ground did not, and the
plan's Part E said what it would take. Reading the partitioner for it found
two defects that were already shipping:

- **D157:** a partitioned scene lost its block world. The residual was rebuilt
  from three keys, and `voxels` is a fourth.
- **D159:** a scene carries its field as one cell holding everything, and the
  reader held that to a *streamed* cell's ceiling of 4,096 tiles. A terrain
  past about a square kilometre saved, and then reopened empty.

## Decision

1. **The partition cuts both fields into cells of about 64 m**, which is the
   size `Terrain.CellSize` has always named. The size is rounded to whole
   32-column tiles and whole 16-block chunks. Each cell is one file,
   `.lterrain` or `.lvoxel`, indexed on a grid of its own (`fields.json`)
   rather than the parts' grid, so a `ChunkId` there names its own square.
   The residual scene keeps what describes each field: the voxel size and
   height range, the block size and the types.
2. **Only a field of sixteen cells or more is cut.** That is a 256 m square.
   Smaller ground sits inside any load radius, and streaming it would buy a
   wait on first load and nothing else. When a field streams and the parts
   would not (fewer than four cells), the parts stay authored, so a small
   project with a large terrain keeps its models.
3. **A second `StreamingManager`, the same policy.** It scores by distance,
   evicts with hysteresis, stays inside the budget, and fails a cell
   terminally. The manager gains a raw-bytes mode, so a payload that is not a
   chunk of instances decodes itself. The ground goes first, and the two
   managers share the frame's two milliseconds rather than taking two each.
4. **What arrives joins the field, and what the field holds wins.** A cell is
   shared into the live field without replacing anything already there,
   because what is there is newer.
5. **An edited cell is never evicted, and nobody raises a flag for it.** The
   streamer keeps each cell it loaded, so every object in the field is shared
   twice. The field is copy-on-write, so the first edit clones the object. At
   eviction, a cell is dropped only if its square holds exactly the objects it
   brought. Otherwise the work stays in the world.
6. **The simulation waits for the ground once.** Until the ring inside
   `TerrainMinRadius` has arrived around the foci, ticks are held. Every
   streamed world has this initial load; without it, a crate on streamed
   ground fell through on the first tick. After that,
   `PauseOutsideLoadedArea` decides, as it does for parts.
7. **Terrain colliders are built nearest a moving body first.** The mirror's
   few builds a tick used to go in key order, and on a kilometre of ground the
   tile under a crate came round twenty seconds after the crate had fallen
   through. The distance order depends only on the world (R10).

## What it does not do

- **The editor holds the whole field**, as it holds the whole scene: the
  partition does not run there. Editing a world larger than memory is a
  scene that is a folder of cells, and that is further off than this.
  [ADR 0081](0081-the-editor-holds-the-field-and-the-gpu-holds-what-is-near.md)
  measured where the editor's ceiling really is and set the trigger.
- **A script's own terrain is resident.** Only a field saved with a scene
  streams. Ground a script generates at runtime lives in memory, like the
  instances it makes.
- **Terrain still does not replicate** (ADR 0069, decision 7).

## Measured

The case is 1.6 km of half-metre ground (10,404 tiles) with a strip of blocks,
saved and run headless:

- **The scene** went from 2.4 MB to 638 bytes: 676 terrain cells and 44
  block cells.
- **The ground arrived in 43 to 61 ms** around the camera: 526 to 700 cells.
- **A crate dropped on it** rests at 0.98 m after three seconds.

Two first versions did not reach those numbers:

- **Merging a cell tile by tile** into a sorted vector of ten thousand made
  the first load take seconds. Both the merge and the eviction are now one
  linear pass.
- **Eight reads in flight** made a ring of a few hundred cells wait on the
  queue. Ground now keeps 32 reads in flight.
