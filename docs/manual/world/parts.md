# Parts and solids

A `Part` is the primitive solid: a box, a ball, a cylinder, a capsule or a
wedge. A `MeshPart` is a part whose geometry is an imported mesh. Both are
`BasePart`s, and `BasePart` is where almost everything about them lives.

```luau
--!strict
local platform = Instance.new("Part")
platform.Shape = Enum.PartShape.Block
platform.Size = vector.create(8, 1, 8)
platform.Position = vector.create(0, 3, 0)
platform.Color = Color3.fromRGB(200, 90, 40)
platform.Anchored = true
platform.Parent = workspace
```

`Instance.new("BasePart")` raises — it is abstract, and exists so that
everything below it shares one set of members.

## Size is the full extent

`BasePart.Size` is metres, along the part's own axes, and it is the **full**
extent rather than a half-extent. `vector.create(2, 2, 2)` is a two-metre cube.

Two shapes read it slightly differently so that what is drawn agrees with what
is simulated:

- A **`Ball`** is a sphere of the *largest* half-extent. A non-uniform `Size`
  gives a ball that sticks out of its own box, not an ellipsoid.
- A **`Cylinder`** and a **`Capsule`** stand along Y, take their diameter from
  the larger of X and Z, and their full height from Y — caps included.

A capsule's caps are hemispheres the collider never stretches, so
`Size.y == 2 * Size.x` is where the drawn shape and the simulated one agree
exactly.

A **`Wedge`** is a ramp with its tall face towards −Z, so an unrotated one is
walked up as you walk towards −Z.

## How it looks

| Property | Default | Notes |
|---|---|---|
| `BasePart.Color` | white | |
| `BasePart.Transparency` | 0 | 0 opaque, 1 invisible. |

That is the whole of a part's appearance in this release. **There is no
`Material`** — it is a surface look rather than rigid-body state, nothing would
read it, and a property that type-checks and does nothing looks more like a
working API than a missing member does.

A transparent part still collides and still casts a full shadow.

## Where it is

Four properties describe one thing, and knowing which is authoritative matters:

| Property | Is |
|---|---|
| `BasePart.CFrame` | The transform, and the **f64 source of truth**. |
| `BasePart.Position` | The f32 rounding of that transform's translation. |
| `BasePart.Orientation` | Intrinsic YXZ euler angles, in degrees. |
| `PVInstance.PivotOffset` | Where the pivot sits relative to the part's centre. |

Near the origin, use whichever reads best. Past a few kilometres out, do the
arithmetic on `CFrame` — see [Transforms, pivots and units](manual:world/transforms).

## Grouping

A `Model` is several parts handled as one object. It is a `PVInstance`, so it has
a pivot: `PVInstance.PivotTo` moves the whole subtree, where the same call on a
single part moves only that part.

```luau
--!strict
local house = Instance.new("Model")
house.Name = "House"
house.Parent = workspace

wall.Parent = house
roof.Parent = house

house.PrimaryPart = wall
house:PivotTo(CFrame.new(20, 0, -8))
```

`Model.GetExtentsSize` gives the size of the box around everything in it.

A `Folder` groups without any of that: no transform, no pivot, no behaviour. Use
a `Model` when the group is a *thing*, and a `Folder` when it is a *drawer*.

## Making many

```luau
--!strict
local template = Instance.new("Part")
template.Size = vector.create(1, 1, 1)
template.Anchored = true
-- deliberately not parented: it is a template, not scenery

for index = 1, 100 do
    local copy = template:Clone()
    copy.Position = vector.create(index * 2, 0.5, 0)
    copy.Parent = workspace
end
```

`Instance.Clone` deep-copies with children, properties, attributes and tags,
fixing up internal references. That is the prefab pattern in this release — see
[Prefabs](manual:world/prefabs).

Set everything **before** assigning `Parent`. A part that is parented and then
configured makes the engine react to every intermediate value.

## Where to look next

- [Transforms, pivots and units](manual:world/transforms)
- [Meshes and models](manual:world/meshes)
- [Rigid bodies](manual:physics/bodies) — what `Anchored` and `Shape` mean to
  the simulation
- [`BasePart`](api:BasePart) · [`Part`](api:Part)
