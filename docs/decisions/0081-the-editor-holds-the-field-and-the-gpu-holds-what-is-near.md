# 0081 — The editor holds the whole field, and the GPU holds what is near the camera

- Status: accepted
- Date: 2026-09-23
- Relates to: [0075](0075-terrain-and-block-worlds-stream-in-cells.md), which
  left "editing a world larger than memory" for later;
  [0071](0071-terrain-ground-is-drawn-from-a-height-atlas.md), the height atlas
- Decided by: the agent, under the owner's standing instruction of 2026-08-26.
- **Decision 3 built by [0087](0087-a-large-terrain-is-a-folder-of-cells-and-the-editor-pages-it.md)**
  (2026-09-23), with the trigger moved from memory to size: a terrain of 256
  cells or more is saved as a folder of cells and paged around the camera.

## Context

ADR 0075 streams a saved terrain in cells while a game plays, and says the
editor holds the whole field. Editing a world larger than memory would be a
scene that is a folder of cells, loaded around the editor's camera. That is
the shape of Unreal's World Partition, and it reaches every part of an editor:

- undo, which snapshots the world;
- selection and references to instances that are not loaded;
- the Explorer's search;
- saving only what changed.

Before building it, the question was where the editor's real ceiling is.

## Measured

**Memory is not the first wall.** A height tile is 32 by 32 columns, 5 KiB,
and covers 16 m at the default half-metre voxel:

| Terrain | Tiles | Field in memory |
|---|---|---|
| 1 km square, 0.5 m | 3,906 | 19 MiB |
| 4 km square, 0.5 m | 62,500 | 305 MiB |
| 8 km square, 0.5 m | 250,000 | 1.2 GiB |
| 8 km square, 2 m | 15,625 | 76 MiB |

**The GPU atlas was.** It holds 16,384 tiles, a 2 km square at half a metre.
Past that, the loader kept the first tiles in key order and dropped the rest,
so the editor drew a strip along one edge of a 3 km world, wherever the
camera was. A game never met this, because streaming keeps its resident set
small.

## Decision

1. **The atlas keeps the tiles nearest the camera.** When a terrain has more
   tiles than fit, the loader keeps the 16,384 nearest the viewer. It frees
   the slots of the ones left behind before it uploads the new ones, so the
   ground ahead of a moving camera arrives in the same frame. It uploads
   nearest first under the per-sync budget, so a large world appears from the
   camera outwards. This is rendering only: the field, the colliders and the
   simulation never read it.
2. **The editor keeps holding the whole field in memory.** That is the model
   the undo stack, the Explorer, selection and saving are built on. It covers a
   4 km square at full resolution in about 300 MiB.
3. **A scene that is a folder of cells is deferred**, with a trigger rather
   than a date. It is built when a project's field passes about 1 GiB in the
   editor, an 8 km square at half a metre. Before that, a larger voxel is the
   better answer: 2 m makes that same world 76 MiB.

## Evidence

`engine/render/tests/terrain_loader_tests.cpp`: a 2.2 km terrain of 19,000
tiles keeps exactly 16,384 resident. They are the ones around the camera, at
the east edge. When the camera moves to the west edge, the west edge becomes
resident and the east edge leaves.
