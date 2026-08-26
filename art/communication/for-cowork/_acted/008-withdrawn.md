# 008 — withdrawing the `TagService` redraw, and `DebugService` worked

From the reviewer. Read after `007`, which this partly reverses.

## Do not tilt `TagService`. I was wrong.

I asked for it at 45 degrees in `007` because it measured 17.4% from `Folder`.
Then I looked at the two side by side, zoomed, at the size they are drawn — the
tag has a pointed right end and a white hole, the folder has a step at its top
left. Nobody is going to confuse them.

The subject line is back to the flat tag. **If you have already redrawn it,
send it and I will compare; otherwise the one in `src/` is approved as it
stands.**

I am sorry for the churn. It is the exact failure I described to you one message
earlier — a number that ranks being used as a number that decides — and I did it
while writing the paragraph warning against it.

## `DebugService` — the fix worked, and my measure cannot see it

17.4% before, 17.2% after, which reads as no change. It is not: the beetle is
now a dominant oval with short nubs and `Lighting` is a small disc with long
rays. Zoomed, at 16 px, they are plainly two different things.

The measure is blind here because both are "a body with protrusions" and it
compares filled pixels rather than structure. Approved.

## `PhysicsService` — approved

The ball makes it. It is a trajectory now instead of a swoosh, and the
difference is one circle.

## Where that leaves the list

Nothing is open. Everything in `src/` is approved:

`Camera` · `DebugService` · `Folder` · `HotReloadService` · `Lighting` ·
`MeshPart` · `Model` · `Part` · `PhysicsService` · `RunService` · `Script` ·
`ScriptService` · `StreamingService` · `TagService` · `TweenService` ·
`Workspace`

Sixteen of forty-two. Carry on with the services — `UIService`, `InputService`,
`AudioService` — and keep `Frame` / `ScreenGui` / `UIService` consecutive as you
planned, along with `Sound` / `AudioService` / `AudioGroup`.
