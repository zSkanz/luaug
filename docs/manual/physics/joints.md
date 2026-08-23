# Welds and constraints

There is one joint in this release and it is a rigid weld. It comes in two
spellings, and choosing between them is the whole of what there is to learn.

| Class | The offset is |
|---|---|
| `Weld` | **Authored** — you say where the two parts sit relative to each other. |
| `WeldConstraint` | **Captured** — it reads the relationship off the world when it activates. |

Both extend `Instance` rather than `PVInstance`: a joint is not a thing in
space. Neither needs to be parented to either part.

## A transform weld, not a solver constraint

> `Weld.Part1` stops being independently simulated and is driven from
> `Weld.Part0` every tick.

That is what this joint is. The solver is not involved, which is why a
`MeshPart` can be welded to a `CharacterBody` — a character controller is not a
body the solver could constrain anyway.

The consequences follow directly:

- **While the weld is active, gravity does not reach `Part1`**, and writing its
  `CFrame` is overwritten at the next tick.
- **`Part0` may be anything** — simulated, anchored, or a character. Whatever
  moves it, `Part1` goes with it.
- **A welded part is kinematic, not static.** It still collides and still pushes
  what it runs into.
- **Welding two dynamic parts so the solver treats them as one rigid assembly is
  a different feature, and it is not this one.**

## The offsets, in one equation

```text
Part0.CFrame * C0  ==  Part1.CFrame * C1
```

`Weld.C0` is the attachment on `Part0`; `Weld.C1` is the attachment on `Part1`.
That equation is the one sentence that says where both offsets go.

```luau
--!strict
local banner = Instance.new("Part")
banner.Size = vector.create(0.4, 1.6, 0.4)
banner.Parent = workspace

local weld = Instance.new("Weld")
weld.Part0 = character          -- a CharacterBody is a legal anchor
weld.Part1 = banner
weld.C0 = CFrame.new(0, 4.2, 0) -- 4.2 metres above the anchor
weld.Parent = workspace
```

## Releasing

```luau
weld.Enabled = false
```

`Weld.Enabled` hands `Part1` back to the simulation **where it stands, with no
velocity**. It is released rather than thrown.

## WeldConstraint: capture instead of author

`WeldConstraint` records where the two parts already are and holds that. Use it
when the parts are already in the right place, which is most of the time — and
use `Weld` when you are *specifying* a relationship rather than freezing one.

Getting that backwards is worth avoiding in both directions: authoring an offset
by hand for two parts already in position is arithmetic nobody should have to
do, and capturing one when you meant to specify it is a joint that silently
depends on where things happened to be.

```luau
--!strict
local joint = Instance.new("WeldConstraint")
joint.Part0 = wall
joint.Part1 = painting     -- captured where it currently hangs
joint.Parent = workspace
```

`WeldConstraint.Enabled` going from `false` to `true` **captures the relative
transform afresh**. That is how a part is re-welded somewhere else: move it,
then enable.

`WeldConstraint.Active` is read-only and reports whether it is currently
holding — enabled, with both parts set, and both in the world. The distinction
matters because a joint with one part missing is not an error: it is half-built,
which is what every script that assigns the two properties on separate lines
briefly produces.

## What is refused

A part welded to itself, and a weld whose two parts are already joined by
another weld — directly or through a chain. Welds form a graph and a cycle has
no resolution order, so the write that would create one is refused at the write,
where the caller can be told which write it was.

## What is not here

**Every constraint except the rigid weld.** There is no `HingeConstraint`, no
`SpringConstraint`, no `Motor6D`, and no solver joint of any kind. There is also
no `Attachment` class — a weld's offsets are `CFrame` properties on the joint
itself.

A hinge is expressible as physics: rest a plank on a fulcrum and let contact and
centre of mass do it. That is what the physics example does, and it is the
honest answer for this release.

## Where to look next

- [Rigid bodies](manual:physics/bodies)
- [`Weld`](api:Weld) · [`WeldConstraint`](api:WeldConstraint)
