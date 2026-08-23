# Rigid bodies

Physics in LuauG is Jolt, behind an engine interface, stepping on the fixed
simulation tick. Every solid in the world is a `BasePart`, and what a part *is*
to the simulation comes down to three of its properties: `BasePart.Anchored`,
`BasePart.Shape`, and whether anything is driving it.

## Units

Metres, kilograms, seconds. `Workspace.Gravity` defaults to
`vector.create(0, -9.81, 0)` — SI and signed, so the default already points
down, and moon gravity is that vector scaled rather than a separate multiplier.

`BasePart.Size` is the **full extent** in metres along the part's own axes, so
`vector.create(2, 2, 2)` is a two-metre cube, not a four-metre one.

## Three kinds of body

**Dynamic** is the default: not anchored, nothing driving it. Gravity pulls it,
contacts push it, and the solver decides where it goes.

**Static** is `Anchored = true` with nothing writing its `CFrame`. It is
immovable scenery that everything collides with and gravity ignores.

> `Anchored` is **not** "very heavy". It is a different kind of body, which is
> why writing it rebuilds the part in the simulation and keeps its transform.

**Kinematic** is the one nobody expects and the one moving platforms need: a
part the solver does not integrate but does move. Two things produce it — a part
an active `Weld` drives, and an **anchored part whose `CFrame` is currently
being written**. A kinematic body still collides and still pushes what it runs
into; a character standing on one is carried by the contact.

```luau
--!strict
local TweenService = game:GetService("TweenService")

local platform = Instance.new("Part")
platform.Size = vector.create(6, 1, 6)
platform.Position = vector.create(0, 3, 0)
platform.Anchored = true          -- and the tween below writes its CFrame,
platform.Parent = workspace       -- which is what makes it kinematic

TweenService:Create(platform, TweenInfo.new(3), {
    CFrame = CFrame.new(0, 3, -20),
}):Play()
```

The kinematic state is held for a fixed number of **ticks** after the last write
— ticks and not seconds, because a body's broad-phase layer must not depend on
how fast the machine was running.

## Shape

`Part.Shape` drives **both** the geometry that is drawn and the collider that is
simulated. That is the point: a ball that rolled like a box would be a
divergence nobody could see in a screenshot.

| `Enum.PartShape` | How `Size` is read |
|---|---|
| `Block` | The three extents, directly. |
| `Ball` | A sphere of the **largest** half-extent. A non-uniform `Size` gives a ball sticking out of its own box, not an ellipsoid. |
| `Cylinder` | Stands along Y. Diameter from the larger of X and Z, full height from Y. |
| `Capsule` | The same, caps included. |
| `Wedge` | A ramp with its tall face towards −Z, so an unrotated one is walked **up** as you walk towards −Z. |

Two footnotes that save an afternoon. A capsule's caps are hemispheres the
collider never stretches, so `Size.y == 2 * Size.x` is where the drawn shape and
the simulated one agree exactly; away from it the drawn ends are slightly oval.
And a `Wedge`'s **collider is deliberately the whole box** in this release.

For imported geometry, `MeshPart.CollisionFidelity` chooses. `Box` names a shape
and is honoured exactly; every other value asks for the geometry and gets a
**convex hull**. `Precise` is accepted and reads back, and behaves as `Hull` —
a concave triangle-mesh collider is a different shape class with different rules
and is not in this release. A mesh whose points have not arrived yet collides as
its bounding box, because a body with no shape for one frame is a body that
falls through the floor.

## Mass is density times volume

```luau
crate.Density = 0.6   -- kilograms per cubic metre
```

There is no `Mass` property, and its absence is deliberate: mass is
`BasePart.Density` times the volume that `Size` and `Shape` describe, and the
two cannot be set to contradict each other if only one of them exists.

`BasePart.Friction` defaults to 0.3 and `BasePart.Restitution` to 0. Both
combine across a contact pair, so one slippery surface is enough to make a pair
slide.

## Moving a body

`BasePart.LinearVelocity` and `BasePart.AngularVelocity` are **read-only** — they
report the last simulation tick. A velocity assignment is an impulse with the
mass divided out, and `BasePart.ApplyImpulse` is that operation under a name that
says what it does.

```luau
crate:ApplyImpulse(vector.create(0, 0, 12))   -- kilogram-metres per second
```

The impulse is applied at the **next** simulation tick, never inside the call.
An anchored part ignores it: it has no momentum to change.

To place a body rather than push it, write `BasePart.CFrame` or
`BasePart.Position` directly — and remember that doing so to an anchored part is
what makes it kinematic.

## Which phase to be in

The simulation sits between `RunService.PreSimulation` and
`RunService.PostSimulation`.

```luau
--!strict
local RunService = game:GetService("RunService")

RunService.PreSimulation:Connect(function(dt: number)
    crate:ApplyImpulse(thrust * dt)      -- intent, before the step
end)

RunService.PostSimulation:Connect(function(dt: number)
    print(vector.magnitude(crate.LinearVelocity))   -- result, after it
end)
```

Reading a velocity in `PreSimulation` gives you last tick's answer.
`RunService.Heartbeat` runs after both and is the usual place for per-tick
gameplay that wants the state the tick settled on.

## The tick itself

`PhysicsService.FixedTimestep` defaults to 1/60 and accepts **1/240 to 1/30**.
Values outside that range are *refused*, not clamped. A write takes effect at
the next frame start rather than mid-tick — the accumulator, the timer wheel and
the solver all read it — and a read gives back what was last written, so the
property round-trips immediately and acts one frame later.

## What is not here

- **No `Mass`, no `PhysicalProperties`.** `Density`, `Friction` and
  `Restitution` are the three, directly and typed.
- **No writable velocity.** `ApplyImpulse` is the operation.
- **No sleep or wake API.** Sleeping is real internally; its only script-visible
  consequence is that a pair which stays in contact does not re-fire `Touched`
  across a sleep.
- **No `BasePart.Material`.** It is a surface look rather than rigid-body state,
  nothing would read it, and a type-checked no-op looks more like a working API
  than a missing member does.
- **No per-part gravity scale.** `Workspace.Gravity` is the one knob.

## Where to look next

- [Collisions and contact](manual:physics/collisions)
- [Raycasts and queries](manual:physics/queries)
- [`BasePart`](api:BasePart) — every member
