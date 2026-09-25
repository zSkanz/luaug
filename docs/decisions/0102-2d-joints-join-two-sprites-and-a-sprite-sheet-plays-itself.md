# 0102 — 2D joints join two sprites, and a sprite sheet plays itself

- Status: accepted
- Date: 2026-09-25
- Decided by: the owner's mandate of 2026-09-24 (*"faça tudo isso aqui"*),
  whose M3 lists both: 2D joints for scripts (a hinge, a spring, a weld between
  two `Part2D`s, through the same `IPhysics2D` seam), and sprite animation (a
  component that plays frames of a sprite sheet on the sim clock, so a walk
  cycle is data instead of script). The shapes below are the agent's, under that
  instruction.
- Relates to: [0008](0008-box2d-2d-physics-post-v1.md) (Box2D behind the 2D seam),
  [0088](0088-2d-on-the-wire.md) (what a `Part2D` sends), the 3D constraints
  of E9 (`BallSocketConstraint`, `HingeConstraint`, `FixedConstraint`)

## Context

The 2D layer (phase 3) has bodies, a tilemap, raycasts and contacts, and nothing
that holds two bodies together. A rope bridge, a door on a hinge, a swinging
lamp, a car's wheels and a ragdoll on the plane are all joints. And a character
that walks has had to animate its sprite from a script, advancing
`ImageRectOffset` by hand every few ticks: the same few lines in every game,
written slightly wrong in half of them.

## Decision

### Three joints between two `Part2D`s

An abstract **`Constraint2D`** and three concrete classes:

| Class | Holds | Box2D joint |
|---|---|---|
| `HingeConstraint2D` | a shared point both parts turn about | revolute |
| `SpringConstraint2D` | a distance, softly | distance, with its spring on |
| `WeldConstraint2D` | the two parts rigidly together | weld |

`Constraint2D` carries `Part0`, `Part1` (the two `Part2D`s), `Anchor0`,
`Anchor1` (where the joint is on each part, in that part's own metres, from its
middle), `Enabled` and `CollideConnected`.

**Two parts and two local anchors, not two attachments.** The 3D constraints
join `Attachment`s, because a 3D joint frame has an orientation that has to be
seen and moved. A 2D joint has a point. An `Attachment2D` class, made only to
hold a `Vector2`, would be an instance standing in for a property, which is
what ADR 0060 refused for a texture.

- **`HingeConstraint2D`**: `LimitsEnabled`, `LowerAngle`, `UpperAngle` (degrees,
  relative to how the parts stood when it was made), `MotorEnabled`,
  `MotorSpeed` (degrees per second), `MotorMaxTorque`.
- **`SpringConstraint2D`**: `Length` (the rest length, in metres),
  `Stiffness` (hertz: how fast it springs back; 0 makes it a rigid rod),
  `Damping` (the damping ratio: 0 bounces for ever, 1 settles without
  overshoot), and `MinLength` and `MaxLength` as hard limits.
- **`WeldConstraint2D`**: nothing more. The parts keep the relative placement
  they had when the weld was made.

A joint lives anywhere the parts do, is saved with the scene, and can be
created from a script.

### How the mirror holds a joint

The 2D mirror builds a joint after both its bodies exist, in the constraint
pool's order, every tick in which it is not already built as described. A body
the mirror rebuilds (its shape, motion or group changed) takes its joints with
it: Box2D destroys a body's joints when it destroys the body. So **a joint
remembers the two body handles it was built on**, and a changed handle rebuilds
it. A joint whose description changes (a new limit, a different length) is
rebuilt, not updated in place: joints change rarely, and one path is one path
to get right.

A joint that cannot be built is not an error at the joint. Examples: a `Part0`
that is not a `Part2D`, a part that is not in the world, or two anchored parts.
It simply holds nothing, and the Properties panel says why.

### A sprite sheet that plays itself: `SpriteAnimator`

A **`SpriteAnimator`**, parented to a `Part2D`, plays frames of that part's
image on the simulation clock:

- `FrameSize` (pixels), `Columns` (frames per row of the sheet),
  `SheetOffset` (pixels: where the animation's first frame starts, so one sheet
  can hold several animations);
- `FirstFrame`, `FrameCount`, `FramesPerSecond`;
- `Looped`, `Playing`, and `Frame` (read-only: the frame it is on, from 0).

Each tick it is playing, it advances by `FramesPerSecond` × the fixed step and
writes its parent's `ImageRectOffset` and `ImageRectSize` for the frame it is
on. **A non-looped animation that reaches its end stops, and writes
`Playing = false`**, which is its end event: `GetPropertyChangedSignal("Playing")`
is how a script hears it. That keeps the class free of methods and signals of its
own.

**It writes properties, so everything downstream already works.** The picture
is the `Part2D`'s, drawn as always. A replica sees the frame, because
`ImageRectOffset` is replicated state (ADR 0088, decision 3). The animator
itself is not sent: the authority plays it, and the wire carries its result.

It steps in `PreAnimation`, after that phase's drain, where tweens and skeletal
animation step. So a script that sets `Playing` in `PreAnimation` sees the
first frame that same tick, and the frame is a pure function of the ticks it has
played (R10).

## Consequences

- **Five classes join the API** (one abstract), and each needs an icon.
- **No determinism trace moves**: a new class is hashed only in a world that
  creates one.
- **Nothing reaches the wire from the joints.** A replica drives every replicated
  `Part2D` kinematically (ADR 0088, decision 4), so the authority's joints
  decide where the parts are, and the replica draws that.
- **The mandate's M3 is these two items.** 2D motors beyond the hinge's, pulleys,
  gears, prismatic and wheel joints are not in it, and are not decided here.

### Rejected

- **`Attachment2D`**: see above.
- **Updating a joint in place**: Box2D offers setters for most fields, but a
  second path that has to agree with rebuilding is where the two would drift.
- **Methods `Play` and `Stop` on the animator**: `Playing` already is both, it
  saves and replicates as a property does, and an undo can take it back.
