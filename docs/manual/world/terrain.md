# Terrain

A `Terrain` is ground you shape rather than a floor made of parts. Raise hills,
dig pits, bore a tunnel through a mountain, paint the slopes rock. It collides,
it is saved with the scene, and a large one streams from disk as the player
moves.

**Underneath, it is one volume stored two ways.** Most ground is a height: one
number per column, which is cheap to store, draw and collide with. Where the
ground stops being a height -- a cave, an overhang, a tunnel -- those columns
switch to a small grid of voxels. Nothing above the field needs to know which
one answered, and a hillside costs what a hillside costs.

## Making one

```luau
--!strict
local terrain = Instance.new("Terrain")
terrain.VoxelSize = 1      -- metres between samples: 0.5 resolves a doorway, 2 a hillside
terrain.MinHeight = -32    -- the deepest anything can ever be dug
terrain.MaxHeight = 64     -- the highest anything can ever be raised
terrain.Parent = workspace

-- Ground from below the floor up to y = 2, sixty-four metres square.
terrain:FillBlock(vector.create(0, -15, 0), vector.create(64, 34, 64), 1)
```

`workspace.Terrain` names the world's terrain once there is one.

**Set `MinHeight` and `MaxHeight` before sculpting.** A collider spreads its
height precision across that range when it is built and cannot widen it later,
so digging past `MinHeight` does not deepen the world -- it stops there.
`VoxelSize` can only change while the terrain is empty.

## Shaping it

| Call | What it does |
|---|---|
| `FillBlock(center, size, material)` | Adds a box of ground, or removes one with material 0. |
| `FillBall(center, radius, material)` | Adds a ball, or removes one with material 0. Near its rim a ball overhangs, which makes it the verb for a boulder or a tunnel. |
| `RaiseBall(center, radius, amount)` | Lifts the surface under a disc, falling smoothly to nothing at the rim, or lowers it for a negative amount. It never makes an overhang, which makes it the verb for a hill. |
| `PaintBall(center, radius, material)` | Changes the material near the surface and leaves the ground where it is. |
| `WriteHeights(corner, columns, heights, material)` | Writes a heightmap in one call: one height per column, row after row along +Z. It is the verb for ground from a generator or an image. |
| `HeightAt(x, z)` | The height of the ground in a column, or nil where there is none. |
| `Clear()` | Removes everything. |
| `Compact()` | Turns back into heights every voxel column that no longer needs to be one. |

**A tunnel is a line of balls with material 0.** The columns it passes under
become voxels, and the ground above them stays standing:

```luau
for along = -40, 40 do
    terrain:FillBall(vector.create(130 + along, 3, -90), 3, 0)
end
```

`HeightAt` still answers the top of a column with a cave in it, because a
column with a cave has no single height. To find the first surface along a
direction -- the floor of the tunnel under a torch -- cast a ray with
`Workspace:Raycast`.

## Materials

| Id | Material |
|---|---|
| 1 | Grass |
| 2 | Sand |
| 3 | Rock |
| 4 | Snow |
| 5 | Mud |
| 6 | Sandstone |
| 7 | Basalt |
| 8 | Ice |

Material 0 is not ground: it is what digging writes.

## In the editor

The terrain tools sculpt, dig and paint with a brush, one undo step per stroke.
Aimed at steep ground, **dig carves into the wall** and keeps boring forward
while the button is held, which is how a cave is started from a cliff face.

## Large worlds

A terrain saved with a scene streams from disk once it is sixteen cells or
more, each cell 64 m on a side. The world waits for the ground around the
player before the first frame, loads cells ahead of the camera and evicts them
behind it. **A cell somebody changed is never evicted**, so a crater stays a
crater. See [Streaming a large world](manual:assets/streaming).

A terrain does not replicate: a world's ground arrives with the world, on
every machine, from its scene.

## Where to look next

- [Block worlds](manual:world/blocks) -- cubes on a grid, which is a different
  thing from terrain
- [Physics queries](manual:physics/queries)
- `examples/13-terrain` and `examples/17-cave`
