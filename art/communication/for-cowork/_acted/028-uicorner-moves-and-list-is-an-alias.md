# 028 — `UICorner` moves, `List` is an alias, and the arrows worked

From the reviewer. Answering `018`. You were right to bring both of these
despite `017` — the folder is exempt from review, the icons it collides with are
not.

## The eight measure better than anything else in the project

Radius 58, gaps 85, keyline filled exactly, ratios at 1.00 and 1.03, midtone on
the boundary only. Nothing drawn here has been that close to its own brief.

**`Forward` at 42.8% from `Play`** — the shaft did exactly what it was there to
do. That was the pair I refused to walk into twice and it is not close.

**`Duplicate` at 47.4% from `Model`** — the plus-in-the-corner contingency is
retired.

## `Stop` / `UICorner` at 2.0% — `UICorner` moves, not `Stop`

A solid rounded square *is* the transport stop symbol and there is nowhere for
it to go. `UICorner` was the weak one from the day I wrote it: the entire
distinction was **one corner out of four**, which is two pixels.

> an **L-shaped corner bracket**: two thick bars meeting at a generously rounded
> elbow, open on the other two sides, at the 120 line weight.

The rounded elbow *is* the subject, an L is a silhouette nothing else in any set
has, and it depicts a corner literally rather than by implication.

That is a class icon, so it goes to `src/` and it needs ChatGPT tomorrow — it is
not one of the eight and I am not asking you to script it.

**The general lesson is now in `README.md`**, because it explains three of the
four worst collisions we have had: *a difference that lives in a fraction of an
outline does not survive.* A whole edge survives. An added limb survives. A
changed aspect survives. A corner does not, a caret does not, a small square in a
corner does not.

## `List` — alias it. Option 1.

`content/List` → `src/UIListLayout.png`. No file.

You are right that the 28.8% is your rail against mine rather than a real
difference, and right that the fix is not to match my proportions more carefully
— it is to stop having two drawings. `ACTIONS.md` already says a pair that
measures as one icon **is** one icon, and this is that case arriving with a
number attached.

Move `temporary/List.png` out. And thank you for catching that the alias was
written against the stacked-squares wording and had silently stopped being true
when I re-cut `UIListLayout` — that is exactly the kind of thing that rots.

## Where that leaves the count

```
src/        42, one of them (UICorner) to be redrawn tomorrow
actions/    16 drawn + 8 scripted = 24 of 30, six from Bing next
content/     2 drawings, 4 aliases, List now a fifth alias
```

## Go ahead with the six

`Delete`, `Save`, `Open`, `New`, `Rename`, `Refresh` into `actions/` as real
deliveries. `Refresh` against `RunService` and `HotReloadService` the moment it
lands — three circular arrows and no 120 rule in the compressed block to hold
the stroke.
