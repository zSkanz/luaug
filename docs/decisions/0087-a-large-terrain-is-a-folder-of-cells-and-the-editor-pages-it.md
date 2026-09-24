# 0087 — A large terrain is a folder of cells, and the editor pages it

- Status: accepted
- Date: 2026-09-23
- Decided by: the agent, under the owner's mandate of 2026-09-23 ("the terrain
  gaps: if they can be solved, solve them").
- Builds the deferred third decision of
  [0081](0081-the-editor-holds-the-field-and-the-gpu-holds-what-is-near.md),
  on the cells of [0075](0075-terrain-and-block-worlds-stream-in-cells.md) and
  the voxel grid of [0082](0082-terrain-is-a-grid-of-voxels.md).

## Context

A scene carried its terrain inline: the whole field, encoded as one `.lterrain`
cell and written into the scene's text as base64. The editor held all of it
in memory, and every save wrote all of it.

ADR 0081 measured that memory was not the first wall. It deferred a scene
that is a folder of cells until a project's field passed about 1 GiB. Two
things since moved that line:

- A sculpted field inline in a text file is unreviewable and slow to write
  long before it is large in memory. A 5 km world saved in one piece is
  hundreds of megabytes of base64 on every save.
- The owner asked for the terrain's gaps to be closed if they could be, and
  editing a world larger than memory was the one named.

The pieces already existed: cells of about 64 m that a game streams (ADR
0075); copy-on-write chunks, so a loaded cell that nobody touched is still the
object its file produced; and an undo that snapshots a field by sharing
chunks.

## Decision

1. **A terrain split into 256 cells or more is saved as a folder of cells**
   beside its scene: `content/terrain/<scene>/cell_x_z.lterrain`, plus an
   `index.json` in the field-index format a partition already writes.
   - 256 cells is a kilometre square at a metre.
   - The scene names the index and the field's settings (`terrainCells`)
     instead of carrying the ground.
   - A smaller terrain stays inline, and every existing scene is unchanged.
2. **The editor streams that terrain around its own camera**, with the same
   streamer, radii and cells a game uses. What is in memory is the ring around
   the camera, plus every cell edited since it was last saved: an edited cell
   is never let go before its edit is written.
3. **A save writes the cells that changed and no others.**
   - A loaded cell nobody touched costs a comparison, and one nobody loaded
     costs nothing.
   - An edited cell is written and becomes an ordinary cell again, which the
     camera moving away may let go.
   - Ground written into a cell that is not loaded is merged with the cell's
     file before it is written, so a large Generate Flat Ground cannot wipe
     what it did not see.
   - A cell dug to nothing loses its file.
   - Save As copies the cells into the new scene's folder, so two scenes never
     share ground.
4. **Undo stays a snapshot of the world, and loading is reconciled with it.**
   - A snapshot holds the field as it was, which lacks every cell loaded
     since. After an undo, a redo or a stop, each loaded cell is shared back
     into the field wherever the field holds nothing newer. So the ground
     around the camera is there, and an edit the undo took back stays taken
     back.
   - A cell counts as untouched when its chunks are the same objects its file
     produced, or chunks with the same bytes. That is what a snapshot hands
     back.
5. **A game plays that terrain the same way.** A run adopts the index the
   scene names and streams it around its foci. A packaged game carries the
   folder, because it is content.

## Consequences

- Memory while editing is bounded by the view and by what is unsaved, not by
  the world.
- A scene's text no longer grows with its terrain past the threshold, and a
  change to the ground shows up in version control as the cells it touched.
- **Evidence** (`engine/app/tests/terrain_cells_tests.cpp`, over real files and
  the real reader, through the object the engine and the editor both call):
  - a 1.1 km terrain becomes 324 cells;
  - reopened, it loads around the camera and not 500 m away;
  - a dig saves exactly one cell;
  - the saved cell is let go when the camera leaves, and comes back with the
    dig;
  - an undo to before a load leaves no hole.
- Scripts that change a streamed terrain in a game were already kept by the
  game's streamer. Saving them is still the editor's alone: a running game
  writes nothing to its content.
- ADR 0081's first two decisions stand, and its third is built.
