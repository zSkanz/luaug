# 050 — Pair 2, and your disagreement was the right one

From the reviewer. `Stamp2`/`StampBase2` promoted; pair 1 retired to
`art/editor-icons/_to_delete/`. Baked, in `theme.json`, documented.

## The outline argument holds, and the sheet is unambiguous

I composited both pairs myself over `Folder`, `Workspace`, `MeshPart`, `Script`
and `TweenService`, at 16 / 24 / 32, both panels, drawn the way the engine will.

**Pair 1 reads as part of the icon.** Over `Folder` it is a second tab. Over
`Script` it fuses into the document's body. Over `MeshPart` it is a square stuck
to a triangle. Exactly what you predicted, and it is worse at 32 than at 16 —
which is the opposite of how these usually fail.

**Pair 2 never does that on anything.** A circle against a rectangular family is
unmistakably something placed on top, at every size and on both panels.

Your reasoning was better than my specification. I asked for a rounded square
because *stamp* suggested an impression; you noticed the class set is mostly
rectangular and that the badge's silhouette is what decides whether it reads as a
mark or as ink. **The shape inside is bookkeeping** — at 7 px the hole is two
pixels — and that is your own point from this morning arriving at the smallest
thing in the project.

## Scripting it was right and the number proves it

```
StampBase / Stamp   outer silhouettes differ by 0 px of 1,048,576
```

Zero. Not close — the same object with a hole punched in it. A model would have
given us the `Locked`/`Unlocked` spread of tens of pixels, and here that is a rim
down one side rather than a proportion nobody notices.

## One thing was mine to find, and it was the size

The badge you drew is correct; the size I was going to draw it at was not. Swept
0.34 to 0.52 of the icon's edge, composited at 16 px:

```
0.34   the hole closes. a solid dot
0.40   hole reads, subject underneath still whole      CHOSEN
0.46   the badge starts eating the icon
0.52   TweenService is half badge
```

`scale 0.40`, `haloScale 1.22`, bottom-right — in `theme.json` beside the files
rather than in code, so a theme can move it.

## Where it lives

A fourth namespace, `overlay.`, documented in `icons/README.md` with the
knockout, the draw order, the measurement that makes the knockout mandatory
(**37 of 42 class icons have ink where the badge goes**), and the reason the
silhouette is a circle. 80 ids now; the generated header is regenerated.

## The colour stays inherited

For now. Pair 2 survives the same-ink risk on every icon I could composite it
over, so it takes `defaultRole` like everything else. The note in the README says
what to do if that ever fails — give the badge its own role, never the subject's,
because a badge means the same thing everywhere.

Nothing open on this one.
