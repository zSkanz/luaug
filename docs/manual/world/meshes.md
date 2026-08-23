# Meshes and models

A `MeshPart` is a `BasePart` whose geometry comes from a file.

```luau
--!strict
local tree = Instance.new("MeshPart")
tree.MeshContent = "asset://models/tree.glb"
tree.CollisionFidelity = Enum.CollisionFidelity.Hull
tree.Position = vector.create(12, 0, -4)
tree.Anchored = true
tree.Parent = workspace
```

Everything a `Part` has, it has: `Size`, `Color`, `Transparency`, `Anchored`,
`CFrame`, the physics properties, `Touched`. What it swaps is where the shape
comes from.

## One file is one mesh

A model made of several pieces is several `MeshPart`s. That is deliberate rather
than a limitation: a part is the unit the engine transforms, culls, collides and
draws, so a file that produced a whole hierarchy would produce something that is
not a part.

Group them with a `Model` when they belong together.

## Setting the content

`MeshPart.MeshContent` is a `Content` — an `asset://` URI naming a file relative
to the project's content directory. Writing it replaces the geometry.

**A failed import leaves the previous mesh in place and reports why**, rather
than leaving a part with no shape. A part with no shape for one frame is a part
that falls through the floor.

glTF (`.gltf`, `.glb`) is the canonical format. The offline importer also reads
`.fbx`, `.obj`, `.dae`, `.ply` and `.stl` and converts them — see
[The asset pipeline](manual:assets/pipeline).

## Size against the mesh

`Size` is still the full extent in metres, and the mesh is fitted to it. A mesh
authored at the wrong scale is corrected by the part's `Size` rather than by
re-exporting — though re-exporting is usually the better answer, because the
number in the file is then right for everyone who uses it.

## Colliding with imported geometry

`MeshPart.CollisionFidelity` chooses how the collider is built:

| Item | Collides as |
|---|---|
| `Default` | Whatever the engine chooses. In this release, a convex hull. |
| `Hull` | A convex hull of the geometry. |
| `Box` | The bounding box. Names a shape rather than an accuracy, and is honoured exactly. |
| `Precise` | **Accepted and not yet implemented** — reads back as `Precise` and behaves as `Hull`. |

A concave triangle-mesh collider is a different shape class with different rules
— it cannot be dynamic — and it is not in this release.

Two cases fall back to the bounding box, and both are the honest choice:

- A mesh whose points have not arrived yet.
- A mesh too degenerate to hull. Three points are a triangle, not a hull.

**The property reads back what was written, not what it resolved to.** So
`Precise` stays `Precise` even while it behaves as `Hull`.

## Models

A `Model` is several parts handled as one object, with a pivot to move it by and
an extents box to measure it with.

```luau
--!strict
local cart = Instance.new("Model")
cart.Name = "Cart"
cart.Parent = workspace

body.Parent = cart
wheelLeft.Parent = cart
wheelRight.Parent = cart

cart.PrimaryPart = body
cart:PivotTo(CFrame.new(0, 0, -20))
print(cart:GetExtentsSize())
```

`Model.PrimaryPart` is which part the model considers itself to be for the
purpose of a pivot. `Model.GetExtentsSize` measures the box around everything in
it.

`PVInstance.PivotTo` on a `Model` moves **the whole subtree** — that, and the
pivot, is what a model is for.

## Skinned meshes

A `MeshPart` whose file carries a skeleton can be animated: parent an
`AnimationPlayer` to it and load its clips by name. See
[Skeletal animation](manual:animation/skeletal).

## Instancing, for free

A run of parts sharing a mesh **and** a material is submitted as one draw. That
is not something a script asks for — the renderer groups what it can, every
frame. A forest of four thousand trees is a handful of draw calls rather than
four thousand.

You can watch it happen:

```luau
--!strict
local DebugService = game:GetService("DebugService")
print(DebugService:GetStat("VisibleObjects"), DebugService:GetStat("DrawCalls"))
```

Two things are never instanced, and both are deliberate: a skinned draw carries
its own joint palette, and a transparent draw's **order is the result**.

## Where to look next

- [Content and asset URNs](manual:assets/content)
- [The asset pipeline](manual:assets/pipeline)
- [`MeshPart`](api:MeshPart) · [`Model`](api:Model)
