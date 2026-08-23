# Raycasts and queries

Three questions you can ask the world, all on `Workspace`:

| Method | Asks |
|---|---|
| `Workspace.Raycast` | What does a line hit first? |
| `Workspace.Spherecast` | What does a sphere swept along a line hit first? |
| `Workspace.GetBodiesInBox` | What overlaps this oriented box? |

## The direction's length is the range

This is the one thing to learn on this page, because getting it wrong produces a
query that silently reaches too far or not far enough:

> **The direction passed to a cast is not normalised. Its length is how far the
> cast reaches.**

```luau
--!strict
-- Five metres straight down.
local hit = workspace:Raycast(origin, vector.create(0, -5, 0))

-- A hundred metres along the camera's forward.
local far = workspace:Raycast(eye, camera.CFrame.LookVector * 100)
```

So `RaycastResult.Distance` is a distance in metres, not a fraction of the cast.

## The result, or nothing

```luau
local result: RaycastResult? = workspace:Raycast(origin, direction, params)

if result ~= nil then
    print(result.Instance.Name, result.Position, result.Normal, result.Distance)
end
```

A cast that hits nothing returns `nil`. There is no "empty result" object to
check a field on, because a result that exists and means nothing is the shape of
every nil-check somebody forgets.

`RaycastResult` carries exactly four things — `RaycastResult.Instance`,
`RaycastResult.Position`, `RaycastResult.Normal` and `RaycastResult.Distance`,
all read-only. There is no material and no face index.

A tie between two surfaces at the same distance resolves the same way on every
run: a query whose answer depended on traversal order would be a replay
divergence waiting for a body count to change.

## Filtering

`RaycastParams` is built from a table rather than from positional arguments, so
a call site reads as the question it is asking.

```luau
--!strict
local params = RaycastParams.new({
    Filter = { character },
    FilterType = Enum.RaycastFilterType.Exclude,
})
```

`RaycastParams.Filter`, `RaycastParams.FilterType` and
`RaycastParams.CollisionGroup` are all **read-only after construction**. To
change a filter, build a new one.

Two facts about the filter that are exactly what the words say and are still
worth stating, because a query returning `nil` gives you no clue which one bit
you:

- An empty `Exclude` filter hits **everything**.
- An empty `Include` filter hits **nothing**.

Descendants of a named instance are covered too, so filtering a `Model` filters
its parts.

`RaycastParams.CollisionGroup` names the group a hit must be in; `""` means any.
A camera ray asking only for `"Terrain"` is cheaper than one that hits
everything and filters afterwards.

Independently of all of that, a part with `BasePart.CanQuery` set to `false` is
invisible to all three methods — and that has nothing to do with whether it
collides.

## Spherecast is the one you usually want

```luau
--!strict
-- A ground check. A ray slips through a gap narrower than the character;
-- a swept sphere does not.
local ground = workspace:Spherecast(
    character.Position,
    0.45,
    vector.create(0, -1.2, 0),
    params
)
```

This is what a camera boom and a character's ground check want. A ray is a
point, and a point finds gaps that the thing it stands in for could never fit
through.

## Boxes

```luau
--!strict
local nearby = workspace:GetBodiesInBox(
    CFrame.new(centre),
    vector.create(10, 4, 10),   -- FULL extent, matching BasePart.Size
    params
)

for _, part in nearby do
    -- one entry per part, however many of its surfaces are inside
end
```

A fresh array in a stable order, every time.

## What is not here

There is no multi-hit raycast, no blockcast, and no `RaycastParams` flag for
water or for respecting `CanCollide`. The three methods above and the four
result fields are the whole query surface.

## Where to look next

- [Collisions and contact](manual:physics/collisions) — `CanQuery` against
  `CanCollide`
- [`RaycastParams`](api:RaycastParams) · [`RaycastResult`](api:RaycastResult)
