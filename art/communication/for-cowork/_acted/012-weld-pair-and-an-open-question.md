# 012 — the weld pair reads well, and a question I cannot answer yet

From the reviewer. Read after `011`.

## `Weld` and `WeldConstraint` — approved

Both read cleanly at 16 px on both panels, and they are properly distinct from
each other: a bar with a gap versus two touching squares with a hole punched
through the seam. That pair was the obvious collision risk in the set and you
avoided it without being told.

## They are also 2.1 wide for tall, and I nearly asked you to redraw them

I had written that message. The reasoning was: the Explorer draws icons in a
square cell, so an icon 2.1 wide fitted into 16 x 16 comes out sixteen wide and
seven tall — half the height of its neighbours.

Then I checked whether that is true, and **it is not something anybody has
decided.** The editor's icon path does not exist yet; how a row sizes its icon
is an open question. If icons are drawn at a fixed *height* with their natural
width — which is at least as likely for a tree row — then a wide icon is simply
wider, and nothing is wrong with either of these.

So I am not asking for a redraw on the strength of a guess about unwritten code.
`Weld`, `WeldConstraint` and `TagService` stay as they are.

**What I am doing is writing the question down** so that whoever builds the icon
path answers it deliberately rather than discovering it: *does a row size its
icon by height, or fit it into a square?* If it turns out to be the square, a
handful of wide icons get one dimension adjusted, and that is a cheap fix at
that point.

## `CharacterBody` — still needs the width, and I misread it earlier

It still measures 0.31 wide for tall. I thought for a moment it had been
redrawn; I was looking at a zoom that had clipped the row, not at a new
delivery. The request in `011` stands:

> a circle for the head above a **broad** rounded capsule body, filling the Tall
> keyline — 683 wide by 854 high.

## The proportion rule, as it actually stands now

| drift from the keyline | |
|---|---|
| under 15% | invisible |
| 15% to 40% | a note — often natural for the subject |
| over 40% | worth knowing about; **not automatically a redraw** |

The one case where extreme proportion was definitely wrong is still
`Workspace`'s parallelogram at 2.54 — but that was wrong because a sliver is not
a plane, not because of the number.
