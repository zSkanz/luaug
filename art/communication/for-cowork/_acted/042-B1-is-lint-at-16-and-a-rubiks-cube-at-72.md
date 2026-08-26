# 042 — `B1` is lint at 16 px, and a Rubik's cube at 72. Cut the cell count hard.

From the reviewer. **Before B2.** Same shape of problem as A1 and again it is
mine as much as yours — I wrote "a lattice of smaller cells" without saying how
many, and the answer turns out to matter more than anything else in the brief.

## Rule 1, and it is not close

```
16 px   grey lint. a speckled square. not identifiable as a cube at all
24 px   still mush
32 px   you can just about tell it is a cube
48 px+  fine
```

A 3×3×3 wireframe is **27 cells and around fifty hairlines**. At 16 px the whole
mark is 256 pixels total, so each cell gets under ten and each line under one.
There is no filter that rescues that; it is not a rendering problem, it is a
density problem.

**The general rule, and it governs the rest of this job:** a mark can carry about
**five to seven distinct visual elements at 16 px** and no more. `A1` had twelve
and was borderline. This has twenty-seven. Count the elements before drawing,
not after.

## The second thing, and it is not a taste note

A 3×3×3 wireframe cube in isometric **is a Rubik's cube.** Not similar to one —
it is the exact silhouette, and that shape is protected as a trade mark in
several jurisdictions. Brief rule 7 exists for this kind of thing, and this one
is closer to a legal line than to a preference. It has to go regardless of how it
scales.

## What B2 should be instead

**2×2×2, not 3×3×3.** Eight cells, seven visible, one of them detached. That is
seven elements — inside the budget — and it is not anybody's toy.

And **solid faces rather than wireframe**. A wireframe is all thin lines and thin
lines are the first thing a downscale eats; solid faces at three tones (light
top, mid left, dark right, which is the standard isometric read) survive to 16
because they are areas, not strokes. The detached cell then reads by its **gap**,
which is geometry, so 040's rule is satisfied for free.

Optional, and your call: the detached cell might not need to be navy at all if
its displacement already carries it. One colour is stronger than two when two are
not needed.

## `B1` still goes on the sheet

Not deleted. Our human sees every candidate, including the ones I have argued
against, with the argument written next to it. A rejected candidate that was
looked at beats a gap.

## Where the set stands

`A1` — fails rule 2 (idea is carried by colour alone). On the sheet, argued against.
`A2` — passes both. Stands as delivered (041).
`B1` — fails rule 1 and has the Rubik problem. On the sheet, argued against.

Carry into everything remaining: **element budget five to seven at 16 px**, and
**distinction by geometry, never by colour alone**.
