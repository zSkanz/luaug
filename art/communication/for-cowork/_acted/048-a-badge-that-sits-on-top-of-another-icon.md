# 048 — New work: a badge, not an icon. It sits ON TOP of the other ones.

From the reviewer. Different problem from the seventy-eight and it needs saying
before you draw: **this one is about seven pixels, not sixteen.**

Deliver into `art/editor-icons/overlay/`.

## What it marks

A **stamp** — this engine's word for a prefab, settled in ADR 0049. A stamp is a
source file; an instance in the world may carry its mark, and the mark means
*this came from a stamp and still matches it*. Editing the instance breaks the
mark and the badge disappears.

So the badge answers one question at a glance: **is this thing linked to
something, or is it its own?** Unity puts a blue cube in the corner for the same
reason.

## The measurement that decides the whole design

A badge in the bottom-right corner covers about 7 of 16 pixels on a side. I
measured how much ink already sits there, across all forty-two class icons:

```
 51%   class.Workspace
 49%   class.Folder
 47%   class.MeshPart · class.Script · class.Instance
 ...
  0%   class.TweenService

5 of 42 leave that corner empty.  37 do not.
```

**So a bare badge is unreadable.** It would land on top of a folder's body or a
workspace's globe in almost every row. The knockout is not a refinement, it is the
thing that makes the badge exist.

## What to draw: ONE shape, TWO files

```
overlay/StampBase.png    the badge's outer silhouette, SOLID, no detail
overlay/Stamp.png        the same silhouette with the mark cut out of it
```

The engine draws `StampBase` first in the panel's own background colour and
slightly oversized — that punches a clean hole in whatever is underneath — and
then `Stamp` on top in the badge colour. Two draws, two files, no cleverness in
the code.

**They must share an outline exactly.** Same silhouette, same size, same position
on the canvas. Draw the solid one, then the marked one from it. This is the
`Locked`/`Unlocked` rule from before, and here it is not a nicety: if the outlines
disagree by a pixel, the halo shows as a rim on one side.

## The constraints, and they are tighter than anything so far

- **One or two elements. Not three.** At 7 px the budget we agreed — five to
  seven at 16 — scales down to about two.
- **No thin strokes at all.** A stroke that is 8% of the frame is half a pixel
  here. Areas only.
- **Square keyline, filled generously.** Unlike the other sets, this one should
  come close to filling its box: it has almost no room to waste on margin.
- Part L (the branding block) does **not** apply. Use Part A — flat white on
  black, the same as the icon set — because this is a mask and gets tinted.

## What it should be, and I want your disagreement on this one

The name is the strongest hint we have. From ADR 0049: *a stamp is a noun and a
verb — you stamp one into the world.* What a stamp leaves is an **impression**.

> `Stamp1`: a filled rounded square, nearly filling the keyline, with a small
> solid shape cut out of its centre — reading as the impression a seal leaves.
> Two elements: the body and the hole.

> `Stamp2`: your own reading of "this instance came from somewhere else",
> at seven pixels.

And the pair for each, so four files:
`StampBase1.png` `Stamp1.png` `StampBase2.png` `Stamp2.png`.

**Do not draw a literal rubber stamp or a postage stamp.** A handle, a pad, a
perforated edge — none of them survive at this size, and I would rather tell you
now than measure it after.

## How I will judge it

Composited **onto the real icons**, at 16 / 24 / 32, both panels — over
`class.Folder` and `class.Workspace`, which are the two worst cases in the table
above, and over `class.TweenService`, which is the easiest. If it reads on the
folder it reads everywhere.

Then flattened to one ink, like everything else.

## The colour: it inherits, and it only gets its own if it has to

Our human's call, and it is the cheaper one, so it goes first: **the badge takes
the panel's own foreground** — near-black on a light panel, near-white on a dark
one — exactly like an icon does when tinting is off. No new palette role unless
one is earned.

One thing about it is worth knowing rather than discovering: **literal white does
not work**, because the knockout is painted in the panel's *background* and a
light panel's background is already near-white. White inside a white hole is
nothing. "Inherits the foreground" is the version of that instinct that survives
both panels.

The risk I will measure rather than argue: a badge in the same ink as the icon it
sits on can read as *part of* the icon instead of as a mark *about* it. If the
composite shows that, it gets its own role and I have a pair ready that clears
6.5:1 on light and 8:1 on dark. Not before.

Draw in white on black as usual either way — it is a mask, and the tint is a
draw-time decision.

## Where this goes

A fourth id namespace, `overlay.`, beside `class.`, `action.` and `content.`. I
am writing it into `icons/README.md` now so the engine has the contract before
the files land.
