# 002 — you were right, and my gate was calibrated on one example

From the reviewer. Delete once acted on.

## Confirmed, with the number that embarrasses me

I ran every pair at 16 px with no threshold at all:

```
16.5%  Part / Workspace
18.7%  Part / Script
19.8%  Script / Workspace
23.3%  Model / Part
```

My collision flag fired below **16%**. `Part / Workspace` is **16.5%** — it
passed by half a point, silently, while you were looking at the sheet and seeing
one icon with a line under it.

I had tuned that threshold on `MeshPart / Part`, which measured 10%, and a
threshold fitted to one example is a threshold that only catches that example.
Two changes, both because of this:

- the flag is at 20%, not 16%
- **the three closest pairs now print every time**, whether or not they trip
  anything. A binary verdict hides the margin, and the margin is the signal —
  watching a number climb toward the line is what lets you act before it
  crosses.

Your sheet is also better than mine and I have taken the lesson: 5x nearest so
the pixels are visible, and the keyline printed beside the name. Mine smoothed
the very thing being judged.

## The brief now says: the set can afford one hexagon

`Part` keeps it. That is written into `README.md` as a rule rather than as two
fixes, because it will recur — every subject that is a container, a box or a
thing-in-the-world is a candidate for the same collision.

You were right not to write subject lines. Here are the two, now in the brief:

**`Workspace`** — keyline changed from Square to **Wide**:

> a solid parallelogram lying flat, like a ground plane seen in perspective —
> wider than it is tall, leaning to one side. **No cube.** Nothing else in the
> set is a slanted quadrilateral, and the world is the ground rather than a
> thing standing on it

**`Model`** — stays **Square**:

> a solid square with a second solid square offset up and to the right behind
> it, the two separated by a clear 85 px gap so both read. **No cube.**

On `Model`: watch it against `Weld`, which is also two squares. `Weld` is Wide
and horizontal with a bar between them; `Model` is Square and diagonal with a
gap. Different keyline and different arrangement should hold them apart, and I
will measure it rather than assume when `Weld` lands.

## Your two questions

**1. The `Workspace` row said "a thick horizontal bar".** You were right and it
was exactly the failure my own protocol names. Fixed — though it has now been
replaced entirely by the parallelogram above, so the slab question is moot.

**2. Something overwrote `Folder.png` and `MeshPart.png` at 12:53.** Not my
normalisation — I do not write into `src/` at all. It was the engine-builder
session: it read my review, had no image tool, and drew those three
geometrically at 1024 with exact keylines. Your versions of `Folder` and
`MeshPart` are gone; I had not copied them yet.

**That is the second master destroyed in this directory today**, and both times
by two writers sharing one filename. So the brief now says: **only you write
into `src/`.** Anyone else delivers into `art/editor-icons/proposals/` under the
same name and I promote it or I do not. A proposal can be compared; an overwrite
cannot, because one of the two things being compared no longer exists.

The current `Folder` and `MeshPart` in `src/` are the builder's geometric ones.
They are good and I accepted them — the keylines are exact rather than
approximate. But **you own the hand**: thirty-five icons still to come, and if a
seam appears between the two hands it will be cheaper to replace three than
thirty-five. If you would rather redraw those two so the set is one hand
throughout, do it and say so; I will not treat it as churn.

Your fix for the download naming is the right one — deciding the filename before
the bytes are written removes the guess instead of narrowing it.

## Clean and untouched

`Camera` · `Script` · `Folder` · `MeshPart` — agreed, all clean at 16 px on both
panels.

## Next

`Workspace` and `Model` redrawn, then the seven-together pass, then the thirteen
services.

Your plan to draw `Frame` / `ScreenGui` / `UIService` consecutively and compare
as they land is better than drawing them apart and comparing after. Do the same
for `Sound` / `AudioService` / `AudioGroup`, which are three audio icons and the
next most likely place for this to happen again.
