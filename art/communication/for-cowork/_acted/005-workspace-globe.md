# 005 — `Workspace` again, plus `ScriptService`, and what the 120 test showed

From the reviewer. Read after `004`.

## The Chrome diagnosis was better than mine

You were right that it was above the fetch rather than below it, and the fix —
a download name that cannot collide — closes the shape of both of today's
losses. Nothing further needed.

## But the 120 still did not happen, and this time nothing is broken

The new `Lighting` is genuinely a new generation; the hash moved and the icon
changed. Its rays measure **68 px** normalised to 1024. The previous one, drawn
when the brief said 85, measured **71**.

So the instruction moved the number by three, in the wrong direction. **The
image model does not count pixels.** Everywhere the numbers have been checked
this holds: `Part`'s internal cut is 35.1, `Model`'s are 23 to 46, keyline fills
land between 89% and 112%.

That is not a fault of yours or of the prompt. It is written into the brief now
as its own section: the constants set a *relationship* — a cut is much thinner
than a stroke, parts are separated by a visible gap — and the relationship
survives even when the absolute does not. **Write the numbers, do not expect
them, check afterwards.** The review loop is what holds the set together, not
the constants.

Practical consequence: **stop re-drawing icons to hit a pixel weight.**
`Lighting` and `RunService` are approved as they are. They read.

## `Workspace` — my idea was wrong, not your drawing

The parallelogram is exactly what I asked for and it means nothing. At any size
it is a white slab; a bare quadrilateral is not a symbol of anything. I chose it
to escape the hexagon collision and optimised the meaning away doing it.

Replaced. New subject line, keyline **Circle**:

> a solid filled circle with two narrow gaps cut across it — one straight and
> vertical, one bowed and horizontal — making a globe with a meridian and an
> equator. The cuts are the thin 35 px inner-cut weight, not a stroke. **No
> cube.**

A solid disc is a silhouette nothing else here has: `Lighting` is spiky with its
rays, `RunService` and `HotReloadService` are open rings. And a globe is
recognisably a world, which the slab never was.

The lesson is in the brief too, because it will recur: **an icon has to be
unlike every other outline in the set AND recognisably the thing it names.**
Passing only the first is not progress.

## `ScriptService` collides with `Script`

17.7% apart at 16 px. Two sheets stacked nearly square have the outline of one
sheet, and `Script` already owns that outline.

Rewritten, keyline changed Square → **Wide**:

> two solid document sheets **fanned apart at a clear angle**, the back one
> rotated so its corners project beyond the front one on both sides. The
> silhouette must be visibly WIDER than a single sheet.

## Approved and untouched

`Camera` · `Folder` · `Lighting` · `MeshPart` · `Model` · `Part` · `RunService` ·
`Script` · `HotReloadService`

`HotReloadService` in particular — the flame inside the loop reads at 16 px,
which I did not expect from a two-element icon.

## Next

`Workspace` and `ScriptService`, then carry on with the services. `DebugService`
and `PhysicsService` have landed and I am looking at them now.
