# 027 — the badge is scripted, and my disagreement is the outline, not the mark

From the drawing agent. Four files in `art/editor-icons/overlay/`. Sheet:
`027-stamp-sheet.png` — both pairs composited onto `Folder`, `Workspace` and
`TweenService` at 16 / 24 / 32, both panels, drawn the way the engine will draw
them: base knocked out in the panel background, oversized, then the mark on top.

## Scripted, and this one is not a preference

"They must share an outline exactly, or the halo shows as a rim on one side."

That is a **precision** requirement, and a model cannot meet it. Two generations
of the same silhouette differ by tens of pixels — we proved that today on
`Locked` / `Unlocked`, where the body was specified as identical, drawn twice,
and came back at 745 × 1001 against 765 × 975. A rim of one pixel is a defect
here; twenty is not a near miss.

Scripted, the mark is the base **with a hole punched in it**, so the outlines are
not *matched*, they are the same object:

```
StampBase1 / Stamp1   bbox identical · 1 hole · 0 px outside the base
StampBase2 / Stamp2   bbox identical · 1 hole · 0 px outside the base
ratio 1.00 · midtone 0.00% · PNG
```

Your own rule from `030` decides it: for pure geometry there is no hand, so a
script costs nothing and buys exactness. This is that case with a tolerance
attached.

## My disagreement is not the mark inside. It is the outline.

You specified a **rounded square** body. I drew it — pair 1 — and then drew pair 2
as a **circle**, because I think the square is the one thing that can sink this.

Your own risk note says a badge in the same ink as the icon can read as *part of*
the icon rather than as a mark *about* it. **The badge's silhouette is what
decides that**, and most of the class set is square: `Frame`, `TextButton`,
`ScreenGui`, `Stop`, `Grid`, `Part`, `Model`, `UICorner`. A rounded square in the
corner of a rounded rectangle is the same family of shape at the same radius.

On the sheet, at 16 px over `Folder`, pair 1 reads as **a notch bitten out of the
folder's corner**. Over `Workspace` it lands among the quadrants and joins them.
Pair 2 never does that on any of the three, at any size, on either panel — a
circle against a rectangle is unmistakably something placed on top.

It is also how every badge anybody has ever seen works: notification dots, status
pips, sync markers. The roundness is the *is-a-badge* signal, before anything
inside it is read.

So the inner marks are inverted between the pairs — round body / square hole
against square body / round hole — but that is bookkeeping. **At 7 px the hole is
two pixels and its shape does not survive; only the outline does.** Which is your
own rule from this morning arriving at the smallest thing in the project.

## What the sheet does not settle

Whether a badge in the panel's own foreground reads as a mark or as ink. Pair 2
survives it because the outline is foreign; pair 1 partly does not. If you still
want to measure the same-ink risk before committing to inheriting the foreground,
**pair 1 over `Folder` at 16 px is your worst case** and it is on the sheet.

## Not drawn, on purpose

No rubber stamp, no postage stamp, no handle, no perforation — as instructed, and
the sheet shows why the instruction was right: at 7 px there are about 49 pixels
and roughly two of them are the hole.

## `049`

The parametric sweep landing on 8.5% from the other direction, a quarter of a
pixel from my 7.5%, is the best outcome that exercise could have had. And finding
the pale bake was luminance-derived alpha — azure differing from white by half
what black does — is the kind of thing that would have shipped silently.
