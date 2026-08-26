# 003 — your observation about the ground bar, measured

From the reviewer. Delete once acted on. Read after `002`.

## You saw this before I measured it

Your `002` ended with a throwaway line:

> the lightest area on the sheet is `Workspace`'s ground bar on the light panel;
> it is the one element in the seven that is visibly thinner than everything
> else around it even though it is drawn at the same 85 px.

That is a real property of the set and it now has a number. `Lighting` and
`RunService` arrived and made it unmissable, because both of them are *entirely*
that element.

## The measurement

Share of each icon's ink still at full intensity once shrunk to 16 px:

```
Lighting     72%      <- a sun is rays
RunService   75%      <- a circular arrow is a ring
Workspace    78%
Model        76%
Part         82%
Script       88%
MeshPart     89%
Camera       91%
Folder       96%      <- a folder is a body
```

A stroke thinner than one pixel antialiases to grey along its *whole length*. A
solid body does not. So a stroke and a body drawn at the same 85 do not weigh
the same, which is exactly what your eye reported.

## The rule, now in the brief

**An icon that is a LINE rather than a body is drawn at 120, not 85.**

A circular arrow, an ease curve, the rays of a sun. A stroke that is one part of
an otherwise solid icon stays at 85 — this is only for icons that are stroke all
the way through.

That makes three weights, all in `PROMPT.md`:

| | |
|---|---|
| **120** | an icon that is a line rather than a body |
| **85** | strokes, bars, corner radii, and the gap between separate parts |
| **35** | a cut scored inside one solid shape |

`Lighting` and `RunService` are worth redrawing at 120 when you next have a
window. They are not broken — both read — and this is a family-weight fix rather
than a defect.

Coming up, so you can draw them at 120 the first time: `TweenService` (an ease
curve), `HotReloadService` (a circular arrow with a flame), `AnimationPlayer` (a
line with keyframes on it), and `PointLight` and `SpotLight`, whose rays are the
same thing as `Lighting`'s.

## A note on how I got there, because it cost me a wrong metric first

My first attempt measured how much of the bounding box was lit. It was wrong in
both directions: `MeshPart` uses 62% of its box and is one of the crispest icons
in the set, while `Script` uses 94% and is not. Coverage says nothing. What
decides legibility is how much ink survives at full intensity.

Both the wrong metric and the right one are written into `README.md` with the
numbers, so nobody re-derives the wrong one.

## Also from your 002, applied

The collision flag was at 16% and `Part / Workspace` measured 16.5%. It now
flags at 20%, and **the three closest pairs print on every run** whether or not
they trip it — a binary verdict hides the margin, and the margin is the signal.

Your sheet format is better than mine and I have adopted the reasoning: 5x
nearest so the pixels are visible, and the keyline printed beside the name. Mine
was smoothing the exact thing being judged.
