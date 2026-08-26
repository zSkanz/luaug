# 052 — `ModuleScript` is missing, and it is a pair with `Script`. One chat.

From the reviewer. The engine grew `ModuleScript`, so the set is one icon short.
I checked the whole class list rather than taking the request at face value:

```
classes declared  47      icons  42

missing:  BasePart      Abstract   -- no instance ever has this className
          BaseScript    Abstract
          PVInstance    Abstract, NotCreatable
          UIObject      Abstract, NotCreatable
          DataModel     the root, hidden in the Explorer
          ModuleScript  CONCRETE  <- the only one that needs a drawing
```

An abstract class is never a row's `className`, so a lookup for it never happens.
One icon, not six.

## And it is a pair, so `028`'s rule applies

`Script` and `ModuleScript` are not two subjects. They are **the same document
with a different mark** — that is what they are in every editor that has both,
and it is what makes them tellable apart at 16 px: the page is shared, the mark
is not.

Which means this is the case you generalised this morning:

> *When two icons must share anything exact — a body, a silhouette, a stroke
> weight — they come from one chat, the second as a follow-up to the first.*

So: **both in one chat, `Script` redrawn first, `ModuleScript` as the follow-up.**

**Yes, that replaces an approved `Script`**, and yes that is deliberate — it is
the corollary you wrote when you replaced `Locked`. Keeping the shipping `Script`
and drawing `ModuleScript` beside it rebuilds exactly the defect the lock pair
just cost us two rounds to remove. The current `Script` is not wrong; it is
merely not *from the same sitting*, which is the only thing that matters here.

## The two turns

**Turn 1** — Part A unedited, then:

> a **solid page**: a tall rounded rectangle filling the Tall keyline, with its
> top-right corner folded — the fold cut out of the solid so the black shows
> through as a triangle. Across the page's middle, three horizontal bars cut out
> of it, left-aligned, the bottom one shorter than the other two.

**Turn 2** — same chat, no Part A, no re-description:

> Now the same page as a **module**. **The page is identical** to the image you
> just drew — same width, same height, same corner radius, same folded corner,
> same position on the canvas. Remove the three bars and cut a **single bold
> shape** from the middle of the page instead: two facing angle brackets with a
> gap between them. Nothing else moves.

## What I will measure

Same three bars as the lock pair, which you cleared at 0.0 / 0.0 / 2%:

```
page width      within 2%
page centre x   within 1% of the frame
corner fold     the same size
```

And then the thing the numbers cannot answer: **at 16 px, are they two documents
or one document twice?** They have to be the second, differing only in the mark.

## Not a redraw request for anything else

`Weld`/`WeldConstraint` and `ImageLabel`/`ImageButton` stay as they are. You
named them and I agreed — neither is broken, and neither is being reopened. This
one is only reopened because half of it does not exist yet.
