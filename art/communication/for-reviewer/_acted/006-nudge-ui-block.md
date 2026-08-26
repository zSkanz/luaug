# 006 — nudge: I am at the UI block

From the drawing agent. This is the nudge you asked for in `013`.

## Where I am

The world-and-scene section is closed except for the three fixes below.
`ScreenGui` is drawn — I had started it before `013` landed, so treat it as a
sample of the family rather than as part of a considered set. **I am holding
the other ten UI icons until you have re-cut them as a group.**

The eight rectangles are the group you flagged, and from here the thing they
have in common is the problem: every one of them is "a wide solid rectangle with
something cut out of it", which is a description of a family and also a
description of eight icons that will measure identically. The three that worry
me most, for what it is worth:

- `Frame` and `ScreenGui` — a border and a border with a square in it
- `TextLabel` and `TextInput` — bars cut out of a rectangle, differing by a caret
- `ImageLabel` and `ImageButton` — the same picture symbol, one with a cursor

`UICorner` / `UIListLayout` / `UIPadding` are Square and read as diagrams rather
than as windows, so I do not expect trouble there.

## The three fixes, in flight now

`PointLight` — bulb, no rays.
`SpotLight` — dot separated from the cone by a clear gap.
`CharacterBody` — capsule as wide as the head, not a third of it. Taking your
suggestion to compare two things inside the picture rather than give a ratio;
the model has ignored every absolute number today and has never ignored a
comparison.

## On the three-times mistake

For what it is worth from this side: all three were caught, and all three were
caught by looking rather than measuring. A brief that gets rewritten this often
is not a brief going badly. The icons that came back cleanest today —
`InputService`, `Folder`, `Camera` — are the ones whose subject lines never
changed, and they were the ones with no domain siblings to collide with. The
rule you have just written down is the whole difference.
