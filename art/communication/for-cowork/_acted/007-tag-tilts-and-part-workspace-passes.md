# 007 — `TagService` tilts, `Part` / `Workspace` passes, and my numbers stopped voting

From the reviewer. Read after `006`. Sixteen icons in `src/`.

## Landed and approved

**`ScriptService`** — the fan worked. It was 17.7% from `Script` stacked and is
out of the list entirely now. Two sheets read as two sheets.

**`TweenService`**, **`Workspace`** (the globe), **`StreamingService`**, and
**`PhysicsService`** as re-specified — all fine.

`Workspace` is the weakest icon in the set and I am accepting it anyway: at 16 px
a quartered circle reads more like a crosshair than a world, because what says
"sphere" is curvature and curvature is the first thing to die at that size. It
appears once, at the top of the tree, with its own name beside it, so the
ambiguity costs almost nothing there. If you ever redraw it, the cheap
improvement is an equator that is genuinely **bowed and sitting below centre** —
that reads as a sphere where a straight line through the middle reads as a
crosshair. Not a request.

## `TagService` — rewritten, keyline Wide → **Square**

17.4% from `Folder`, and fairly: lying flat, a tag is a wide slab with one
corner interrupted, and that is exactly what a folder is.

> a solid luggage tag **standing at a 45-degree angle**, its pointed end up and
> to the left, with a round hole cut out near the point.

The tilt is the whole fix. Every rectangle in this set is axis-aligned, so a
slanted one is unlike all of them at any size.

## `Part` / `Workspace` at 17.8% — passes, and I want you to know why

My checker put it in the top three and I am overruling it after looking. A
hexagon quartered by a Y and a circle quartered by a cross are distinguishable
on the sheet at 16 px. The number is close because both are a solid roundish
body with cuts; the eye separates them anyway.

## Which brings me to something I should have said earlier

**Both numbers in my checker have now been demoted from verdicts to hints, and
both for the same reason.**

The ink measure flagged `Lighting`, which reads perfectly — a sun is almost all
diagonal edge, and a diagonal antialiases exactly like a stroke that is too
thin. The distance measure was set at 16%, let `Part` / `Workspace` through at
16.5%, was raised to 20%, and then failed three pairs anybody can tell apart.

Both are good at *ordering* and bad at *deciding*. So the checker now prints the
three closest pairs and the three lightest icons every run, with no pass or
fail, and only calls something a collision below 13% — the one indefensible case
measured 10%.

Practically, for you: **when I quote a number, it is where to look, not a
verdict.** If you look and disagree, say so. You have been right twice today
against my measurements, and I would rather that keep happening than have you
redraw something because a number said so.

## Still open

`DebugService` — dominant oval body, short legs. Landed just now; I am looking
at it.
