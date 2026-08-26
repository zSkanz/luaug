# 036 — `Refresh` is right. `RunService` moves.

From the reviewer. `Rename` approved — the pencil reads and it is near nothing.

## The collision I predicted, and this time the number is right

```
Refresh / RunService          9.6%   COLLISION
Refresh / HotReloadService   24.0%   fine
RunService / HotReloadService 21.3%  fine
```

I said this would be the one where the measure is correct because none of the
three has an absence doing the work, and it is. `Refresh` and `RunService` are
the same drawing under two names.

## `Refresh` keeps the arrow. `RunService` moves.

`Refresh` is the most literal user of that glyph — a browser refresh is where
people expect a circular arrow, and taking it away to protect a class icon would
be protecting the wrong one.

**`RunService`** becomes:

> a **solid stopwatch**: a filled circle with a small stem and crown on top
> breaking the outline, and one narrow hand cut out of the face.

A tick is a clock. And a **solid face against an open ring** is the same
distinction that already separates `UIService` from `Frame` and works there at
16 px.

**Measure it against `Workspace` when it lands** — that is also a solid circle,
with a cross cut instead of a hand. The stem should carry it, the way
`ScreenGui`'s foot carried it against `Frame`, but I would rather check than
assume. If they land close, `RunService` gets the stem enlarged rather than a
new concept.

## Sixth time, and I finally understand why it keeps happening

This is the sixth pair specified in separate sittings that turned out to share a
shape. I wrote the rule after the third — *decide a domain together* — and it
kept happening anyway.

The reason is that **a domain is not a section of the table.** These three sit
in three different groups: a service, a service, and an action. Reading each
group as a whole, which is what I have been doing, would never have put them
side by side. Only reading for *shape* across all three sets does.

So the rule needs its other half, and I am putting it in `README.md`: before
writing any subject line, ask **what else in any set is this shape** — not what
else is in this category.

## One left

The seven into `proposals/`. Then the sets are closed apart from whatever the
comparison decides.
