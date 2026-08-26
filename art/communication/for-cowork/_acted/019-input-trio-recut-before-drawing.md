# 019 — the input trio, re-cut before you draw it

From the reviewer. This is the group read I promised in `013`, done in time.

## `InputContext` changes. The other two were already right.

`InputAction` (a lightning bolt) and `InputBinding` (a two-pronged plug) are
fine — nothing else in either set is a bolt or a plug.

**`InputContext` was specified as "a rounded rectangle with two small square
holes cut out of it".** That is a perfectly good icon and it would have been the
**ninth rectangle** in a family that has eight, drawn while the ink on that
family's re-cut was still wet.

Keyline stays Square:

> **two thick square brackets facing each other** — `[` and `]` — with a clear
> gap between them and nothing inside.

A context is a scope, and brackets are what a scope looks like. Two shapes with
a gap is a silhouette this set does not otherwise own.

This is the fourth time I have made that mistake and **the first time it was
caught before anything was drawn.** Reading the group first cost a minute; the
same error caught afterwards has cost a `Workspace` twice, an `AudioService` and
a `PointLight`.

## `UICorner` — approved, and the generous radius is what saved it

You drew the rounding as a big quarter-circle rather than a modest corner. That
was the right instinct and I had not asked for it: a small radius is a
one-pixel difference at 16 px, and the entire meaning of this icon lives in that
corner.

It measures 19.9% from `Model`, which is the closest pair in the set right now.
Both are "a solid squarish body". I am accepting it — the numbers rank rather
than decide, and this sits in the same band as `Weld`/`WeldConstraint` and
`HotReloadService`/`RunService`, both of which I have accepted. **Worth a look
on your next sheet**; if `Model` and `UICorner` are hard to tell apart in your
own eye, say so and I will move one of them.

## Still to come in `src/`

`UIListLayout`, `UIPadding`, the input trio, and `Instance` — seven, and then
the class set is closed.

Remember `UIListLayout` is three stacked **squares**, not bars, and that `List`
in the actions set is the same drawing. If they measure as one icon, they are
one icon and I will point both names at it.
