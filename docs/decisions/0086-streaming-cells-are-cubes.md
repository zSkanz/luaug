# 0086 — Streaming cells are cubes

- Status: accepted
- Date: 2026-09-23
- Decided by: the agent, under the owner's mandate of 2026-09-23: "Streaming:
  if it can be vertical, apply it to our game too, because I think that is
  going to be very important -- but look at how other engines do it."
- Amends: architecture.md §10 ("uniform world grid", which was 2D), ADR 0053
  (the partitioner's cells)

## Context

A streaming cell was a column: an `x` and a `z` on a grid of `chunkSize`
squares, with no `y`. Scoring was already three-dimensional. The streaming
manager measures a focus against each cell's real box, and the partitioner
gives that box the vertical extent of what the cell holds. But a column holds
everything above and below its square. A cave four hundred metres under a
village was one cell with the village, and its box ran from the cave to the
rooftops. Whoever stood in the cave loaded the village, and whoever stood in
the village loaded the cave.

## How mature engines cut the vertical axis

- **Unreal's World Partition** uses a two-dimensional runtime grid by default.
  Its loading range around each streaming source is a sphere, so height does
  count against a cell whose bounds have it. Epic has also shipped a fully
  three-dimensional streaming grid: the Loose Hierarchical Grid, whose cells
  vary their bounds with the actors in them. It is for worlds whose content is
  stacked vertically.
- **Unreal's replication graph** gathers candidates on a 2D spatial grid,
  `GridSpatialization2D`, and culls each actor by a three-dimensional
  `NetCullDistanceSquared`.
- **Minecraft** divides a chunk column into 16-block sections and tracks them
  section by section. The protocol still sends a whole column, with a bitmask
  saying which sections are present.
- **The common shape** is a coarse grid for finding candidates and a real 3D
  extent for deciding. A cube grid is chosen where content is stacked.

## Decision

1. **`ChunkId` gains `y`, a vertical band.**
   - Cells are cubes of `chunkSize`, the same edge as the footprint.
   - `y` is the last field, so an id written `{x, z, layer}` means what it
     meant.
2. **Bands are centred on sea level.** Band zero is `[-size/2, size/2)`.
   - A boundary at y = 0 would cut every world at its ground, filing a floor
     and the props on it into two cells that always load together.
   - Centred, a world within half a cell of sea level is band zero throughout.
     That is every world before this, and each partitions exactly as the
     column grid did: same cells, same file names, same index.
3. **A cell's bounds are still what it holds**, footprint and contents; the
   band decides only which cell a record is filed under. Scoring is unchanged:
   a focus against each cell's real box, now a box only as tall as one band's
   content.
4. **The formats carry the band, and read what came before.**
   - `.lchunk` version 3 writes `y` after the layer, and version 2 reads as
     band zero.
   - The index writes `"y"` only off band zero.
   - A partition cache from the old rules is not believed (partition rules 3).
5. **Terrain and block-world cells stay columns.** The ground over a cave is
   the cave's roof. It is what its colliders, its raycasts and its openness
   term read, so a cave without it would be a cave open to the sky. A
   terrain's air and solid rock cost a few bytes a chunk (ADR 0082), so a
   column's cost is its surface. Every engine surveyed streams a height or
   voxel terrain the same way.
6. **Replication's interest was already a sphere**, measured per part. A test
   now holds its vertical axis (the mandate's S3).

## Consequences

- A world with content far above or below its ground, such as a mine, a
  sky island or a dungeon under a town, streams each level by itself.
- `examples/10-open-world` has one: **the Deep**, a hall a kilometre under the
  island, reached by a lift beside the start.
  - Headless, on the surface: the island's four towers loaded, none of the
    Deep's forty crystals.
  - In the hall: the crystals and no tower. Back up: the towers again.
- The streaming grid overlay draws the band the camera is in: bands are
  taller than any hill, and several at one height would be several grids
  pretending to be one.
- A band is `chunkSize` tall. A world that wants thinner bands, say for a
  tower of floors, would need the band height to be a setting of its own. That
  waits for a world that needs it.
