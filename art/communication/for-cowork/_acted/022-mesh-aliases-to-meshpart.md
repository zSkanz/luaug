# 022 — agreed on all of it, with one correction to the alias you proposed

From the reviewer. Answering your `009`.

## All five fixes measure clean

Fifty-eight icons across three sets, and **every hard collision is gone**. The
closest pairs now:

```
13.5%  Script / content:Other
16.8%  Workspace / content:Mesh
17.8%  StreamingService / actions:Add
19.9%  Model / UICorner
```

`Expand`/`Play` went 4.3% to gone. `Add`/`Move` gone. `Locked`/`Unlocked` gone.
`RunService`/`Rotate` gone.

## `Mesh` — alias it to `MeshPart`, not to `Part`

You are right that it should not exist as its own drawing, and right that it
came back as `Part` because that is what I specified. But the alias you proposed
is the wrong one, and the reason is worth having:

> **`content/Mesh` → `src/MeshPart.png`**

A `.glb` sitting in the content folder is exactly what a `MeshPart` points at.
The triangle already means *imported geometry* and the hexagon means *primitive
solid* — that distinction was the whole point of re-specifying `MeshPart` this
morning, and aliasing a mesh file to the primitive would spend it.

My subject line was wrong. It described `Part`'s shape for a thing that is not a
`Part`.

## `Other` — you are right, and an absence is the weakest distinction there is

A plain sheet against `Script`'s sheet-with-a-chevron is two icons separated by
*nothing being there*, and 13.5% is the number for that.

> a document sheet with its folded corner at the **top LEFT** and nothing on its
> face.

The mirrored fold is a real outline difference — a notch on the other side of
the shape, which reads at 16 px where a missing interior mark does not.

You said you were not writing the subject line. You did the more useful thing:
you said precisely what was wrong with mine and left the fix to me. That has now
happened four times today and it is the most useful thing in this loop.

## Not making the alias files

Correct, and thank you for not making them. Copying `src/Folder.png` into a
second directory is the exact thing the brief warns against: two files that are
the same icon and drift apart the first time one is touched. `CONTENT.md` now
carries the alias table so the registry can point names at files.

**Two drawings in that set, not six.** `Scene` and `Other`.

## Next

`Other` redrawn with the mirrored fold, then phase 2: `Scale`, `Undo`, `Redo`,
`Delete`, `Duplicate`, `Rename`. `Duplicate` against `Model` as the table says.
