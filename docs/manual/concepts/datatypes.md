# Datatypes and units

A datatype is a value rather than an instance: nothing parents a `CFrame`, and
nothing finds a `Color3` in the tree. They are globals — you never require one.

There are sixteen. The five you will touch on the first day are `Vector3`,
`CFrame`, `Color3`, `UDim2` and `Signal`.

## Units are SI

Metres, kilograms, seconds, and degrees where an angle is a human-facing number.
Not studs. A `Part` with `Size = vector.create(1, 1, 1)` is a one-metre cube, a
`Density` of 1 is a kilogram in that cube, and `Workspace.Gravity` defaults to
`vector.create(0, -9.81, 0)`.

The world is **right-handed, Y-up, −Z forward**, matching glTF. Every sign in
every transform follows from that.

## Vector3 IS the Luau vector

This is the one that surprises people, so it is stated first: `Vector3` is not a
table, not a userdata, and not a class. It is Luau's own native `vector`
primitive, three-wide and 32-bit float, with arithmetic implemented in the VM.

```luau
local a = vector.create(1, 2, 3)
local b = Vector3.new(1, 2, 3)   -- the same thing, spelled the familiar way
print(typeof(a))                  -- "vector"
print(a + b)                      -- VM arithmetic, no metamethod dispatch
```

Two consequences worth knowing:

- **Components are lowercase.** `v.x`, `v.y`, `v.z`, because that is what the
  primitive itself exposes. `v.Magnitude` and `v.Unit` are PascalCase, because
  those are ours.
- **Namespace functions are camelCase and there are a lot of them.**
  `vector.create`, `Vector3.dot`, `Vector3.cross`, `Vector3.magnitude`,
  `Vector3.normalize`, `Vector3.lerp`, `Vector3.clamp`, `Vector3.floor`,
  `Vector3.ceil`, `Vector3.abs`, `Vector3.sign`, `Vector3.min`, `Vector3.max`,
  `Vector3.angle`. Several also exist as methods on a value — `v:Dot(w)` and
  `Vector3.dot(v, w)` are the same operation.

Why it matters is [its own page](manual:why/vector).

## CFrame is the transform, and it is the precise one

A `CFrame` is an orthonormal rotation basis plus a translation. The translation
is **f64**, and it is the world-precision source of truth the whole engine is
built around.

`CFrame.Position` is that translation rounded to f32. So past a few kilometres
from the origin, gameplay maths must stay in `CFrame` rather than round-tripping
through a vector — see [Floating origin](manual:assets/floating-origin).

```luau
local cf = CFrame.new(vector.create(0, 5, 0))
local turned = cf * CFrame.fromEuler(0, math.rad(90), 0)

local forward = turned.LookVector
local worldPoint = turned * vector.create(0, 0, -1)   -- transforms a POINT
```

`CFrame * CFrame` composes. `CFrame * vector` transforms a **point**, meaning
the translation is applied. `==` is exact and component-wise — an identity test,
never a tolerance.

Construction is deliberately one spelling per idea: `CFrame.new`,
`CFrame.fromEuler` with an optional `Enum.RotationOrder`, `CFrame.fromAxisAngle`,
`CFrame.fromQuaternion`, `CFrame.fromMatrix`, `CFrame.lookAt`, and the constant
`CFrame.identity`. There is no `CFrame.Angles` and no `fromEulerAnglesXYZ`;
three confusing spellings became one explicit one.

## Color3 has no "3" anywhere else

```luau
local orange = Color3.fromRGB(240, 120, 40)
local half = orange:Lerp(Color3.new(0, 0, 0), 0.5)
```

`Color3.new` takes components from 0 to 1; `Color3.fromRGB` takes 0 to 255;
`Color3.fromHSV` and `Color3.fromHex` exist too. Components are
`Color3.R`, `Color3.G`, `Color3.B`.

The type is `Color3`, but the **properties are not**: it is
`TextLabel.TextColor` and `Frame.BackgroundColor`, not `TextColor3`. The suffix
was a historical artefact and it is gone.

## UDim and UDim2 are the layout numbers

A `UDim` is a pair — a scale fraction of the parent, and a pixel offset. A
`UDim2` is two of them, one per axis.

```luau
frame.Size = UDim2.new(0.5, 0, 0, 40)      -- half the parent wide, 40 px tall
frame.Position = UDim2.fromScale(0.5, 0.5)
```

`UDim2.fromScale` and `UDim2.fromOffset` exist for the common cases. Layout is
arithmetic rather than a constraint solve, and [Layout with UDim2](manual:ui/layout)
explains what that buys and what it costs.

## Random is a stream, not a function

```luau
local rng = Random.new(1234)
print(rng:NextInteger(1, 6))
print(rng:NextNumber(0, 1))
print(rng:NextUnitVector())
```

`Random.Clone` gives you an independent copy at the same position. A seeded
stream replays; an ambient `math.random` does not — see
[Determinism](manual:concepts/determinism).

## The rest

| Datatype | What it is |
|---|---|
| `Vector2` | A 2D vector, with full operator support. |
| `Rect` | Two corners. |
| `TweenInfo` | The timing of a tween: duration, style, direction, repeats, reversal, delay. |
| `Tween` | A running tween. Returned by `TweenService`, never constructed. |
| `RaycastParams` | Filtering for a spatial query. |
| `RaycastResult` | What a query hit. Returned, never constructed. |
| `Signal` | An event you can fire. |
| `Connection` | What `Signal.Connect` returned. |
| `InputObject` | One raw input event. Returned, never constructed. |
| `AnimationTrack` | One clip playing on one `AnimationPlayer`. A handle, and the one mutable datatype. |

Five of those have **no constructor at all** — the engine hands them to you, and
that is the whole of how you get one.

## Immutability

Every datatype except `AnimationTrack` is immutable: `cf.Position = v` does not
compile, because a `CFrame` is a value and writing to it would write to a copy
you are about to drop. You build a new one.

`AnimationTrack` is the exception, and it is the exception because it is a
**handle**: `track.Looped = true` names the one track that every holder of that
handle can see.

## Where to look next

- [Transforms, pivots and units](manual:world/transforms) — `CFrame` in practice
- [The datatype reference](site:reference) — all sixteen, with every member
