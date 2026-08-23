# Transforms, pivots and units

## The conventions, once

- **Metres, kilograms, seconds.** Not studs.
- **Right-handed, Y-up, −Z forward**, matching glTF. Every sign in every
  transform follows from that.
- **Degrees** where an angle is a human-facing number — `Orientation`,
  `SpotLight.Angle`, `CharacterBody.MaxSlopeAngle`. **Radians** in the maths
  library, so `math.rad` is the bridge.

A character is about two metres tall. Gravity is 9.81 m/s². A jump speed that
felt right in studs is roughly four times too large here.

## CFrame is the transform

A `CFrame` is an orthonormal rotation basis plus a translation, and it is what
the engine actually stores.

```luau
--!strict
local cf = CFrame.new(vector.create(0, 5, 0))
local turned = cf * CFrame.fromEuler(0, math.rad(90), 0)

part.CFrame = turned
```

`CFrame * CFrame` composes: the right-hand side is applied **in the left-hand
side's space**. `CFrame * vector` transforms a **point** — the translation is
applied — which is what you want for "where is this local offset in the world".

`==` is exact and component-wise. It is an identity test, never a tolerance.

## Building one

One spelling per idea, deliberately:

| Constructor | Gives |
|---|---|
| `CFrame.new` | A position, optionally with a rotation. |
| `CFrame.fromEuler` | Euler angles, with an optional `Enum.RotationOrder`. |
| `CFrame.fromAxisAngle` | A rotation about an axis. |
| `CFrame.fromQuaternion` | A rotation from a quaternion. |
| `CFrame.fromMatrix` | A basis given as vectors. |
| `CFrame.lookAt` | A transform looking from one point at another. |
| `CFrame.identity` | No rotation, at the origin. |

There is no `CFrame.Angles` and no `fromEulerAnglesXYZ`. Three confusing
spellings became one explicit one, with the order named rather than encoded in
the function name.

## Reading one

| Member | Is |
|---|---|
| `CFrame.Position` | The translation, as an f32 vector. |
| `CFrame.Rotation` | The same transform with the translation removed. |
| `CFrame.LookVector` | The basis's **−Z** column: forward. |
| `CFrame.RightVector` | The +X column. |
| `CFrame.UpVector` | The +Y column. |

## Between spaces

```luau
--!strict
-- Where is the muzzle, in the world?
local muzzle = gun.CFrame * CFrame.new(0, 0, -1.2)

-- Where is that point, relative to the player?
local relative = player.CFrame:PointToObjectSpace(muzzle.Position)
```

`CFrame.PointToWorldSpace` and `CFrame.PointToObjectSpace` move a point;
`CFrame.VectorToWorldSpace` and `CFrame.VectorToObjectSpace` move a direction —
the difference being whether the translation is applied.
`CFrame.ToWorldSpace` and `CFrame.ToObjectSpace` do the same for a whole
transform. `CFrame.Inverse` is the transform that undoes it.

`CFrame.Lerp` interpolates position and rotation together, taking the short way
round. It is what a tween on a `CFrame` uses, which is why animating one by hand
and animating it with a tween produce the same intermediate values.

## Pivots

Every `PVInstance` — every `BasePart`, every `Model`, and the `Camera` — has a
pivot: the point it rotates about and the handle it is moved by.

```luau
door.PivotOffset = CFrame.new(-1.5, 0, 0)     -- the hinge, not the centre
door:PivotTo(CFrame.new(hingePosition) * CFrame.fromEuler(0, math.rad(80), 0))
```

`PVInstance.PivotOffset` is where the pivot sits relative to the object's own
origin; identity means the centre. `PVInstance.GetPivot` returns the pivot in
world space, and `PVInstance.PivotTo` moves the object so that its pivot lands
on the transform you give.

On a `Model`, `PivotTo` moves the **whole subtree**. On a single part it moves
that part. That difference is the reason a model is worth having.

## Precision, and the reason CFrame matters

`CFrame`'s translation is **f64**. `BasePart.Position` and every `vector` a
script touches are **f32**.

Near the origin that difference is invisible. At a few kilometres out it is not:
an f32 has about 24 bits of mantissa, so precision degrades with distance, and
maths that round-trips through `Position` throws away what `CFrame` was keeping.

So the rule for a large world is one line:

> **Past a few kilometres from the origin, keep gameplay maths on `CFrame` and
> do not round-trip through `Position` or a `vector`.**

The engine also moves the world under you to keep the numbers small — see
[Floating origin](manual:assets/floating-origin) — and that is invisible to a
script: a `CFrame` read after a rebase is the same `CFrame` read before it.

## Where to look next

- [Parts and solids](manual:world/parts)
- [Datatypes and units](manual:concepts/datatypes)
- [`CFrame`](api:CFrame) · [`PVInstance`](api:PVInstance)
