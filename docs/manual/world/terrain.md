# Terrain

A `Terrain` is ground you shape rather than a floor made of parts. Raise hills,
dig pits, bore a tunnel through a mountain, paint the slopes rock. It collides,
it is saved with the scene, and a large one streams from disk as the player
moves.

**Underneath, it is a grid of voxels.** Space is cut into cubes `VoxelSize` on
a side, and each cube holds two things:

- a material;
- how full of that material it is, from 0 to 1.

The surface is wherever that fullness crosses one half. It is found by blending
between neighbouring cubes, so a ball carved out of a hillside lands where you
put it rather than on a grid line. Caves, arches, overhangs and flat ground are
all the same data, so nothing is special about any of them.

## Making one

```luau
--!strict
local terrain = Instance.new("Terrain")
terrain.VoxelSize = 1      -- metres per voxel: 1 resolves a path, 2 a hillside
terrain.MinHeight = -32    -- the world's floor: nothing is written below it
terrain.MaxHeight = 64     -- and its ceiling
terrain.Parent = workspace

-- Ground from below the floor up to y = 2, sixty-four metres square.
terrain:FillBlock(vector.create(0, -15, 0), vector.create(64, 34, 64), 1)
```

`workspace.Terrain` names the world's terrain once there is one.

`MinHeight` is also where ground starts: `WriteHeights`, Generate Flat Ground
and a first raise on empty terrain fill from there up. `VoxelSize` can only
change while the terrain is empty.

## Shaping it

| Call | What it does |
|---|---|
| `FillBall(center, radius, material)` | Adds a ball, or removes one with material 0. Near its rim a ball overhangs, which makes it the verb for a boulder or a tunnel. |
| `FillBlock(center, size, material)` | The same, as a box. |
| `FillCylinder(center, height, radius, material)` | The same, as an upright cylinder: a pillar, a well. |
| `RaiseBall(center, radius, amount)` | Pushes the surface up under a disc, falling smoothly to nothing at the rim, or down for a negative amount. It never makes an overhang, which makes it the verb for a hill. |
| `SmoothBall(center, radius, strength?)` | Softens the ground in a ball. |
| `FlattenBall(center, radius, height, strength?)` | Pulls the ground in a ball towards a level plane. |
| `PaintBall(center, radius, material)` | Changes what the ground is made of and leaves it where it is. |
| `ReplaceMaterial(minCorner, maxCorner, from, to)` | Every voxel of one material in a box becomes another. |
| `WriteHeights(corner, columns, heights, material)` | Writes a heightmap in one call, one height per column of voxels, row after row along +Z. It is the verb for ground from a generator or an image. |
| `ReadVoxels(minCorner, maxCorner)` | Materials and occupancies of a box of voxels, and how many there are on each axis. |
| `WriteVoxels(corner, size, materials, occupancies)` | Puts them back, or writes new ones. |
| `WorldToCell(position)` / `CellCenterToWorld(cell)` | Between a world position and a voxel's index. |
| `HeightAt(x, z)` | The top of the ground in a column, or nil where there is none. |
| `Clear()` | Removes everything. |

**Adding never takes ground away, and removing never adds it.** A voxel ends
up:

- when adding, as full as the fuller of it and the brush;
- when removing, as empty as the brush leaves it.

So a ball of sand pressed into grass leaves the grass under the surface as
grass.

**A tunnel is a line of balls with material 0**, and the ground above it stays
standing:

```luau
for along = -40, 40 do
    terrain:FillBall(vector.create(130 + along, 3, -90), 3, 0)
end
```

`HeightAt` answers the top of a column, even with a cave in it. To find the
first surface along a direction, such as the floor of a tunnel under a torch,
cast a ray with `Workspace:Raycast`. It meets terrain anywhere, near things
that move or not.

## Reading and writing voxels

`ReadVoxels` returns flat tables. The voxel at `x, y, z`, counting from 0 at
the low corner, is at index `1 + x + size.X * (y + size.Y * z)`. What
`ReadVoxels` reads, `WriteVoxels` puts back exactly, which is how to copy a
piece of terrain somewhere else:

```luau
local materials, occupancies, size = terrain:ReadVoxels(vector.create(0, -8, 0), vector.create(16, 8, 16))
terrain:WriteVoxels(vector.create(100, -8, 0), size, materials, occupancies)
```

**Occupancy fades across four voxels at a surface**, not one. Terrain laid
from heights has to keep its slopes smooth, and a one-voxel fade turns any
slope steeper than 45 degrees into steps. The four-voxel fade has two
consequences:

- a thin feature is partly full rather than full;
- a voxel inside a narrow well reads a little occupancy even though it is air
  at its centre.

The surface, where occupancy crosses one half, is where the brush put it.

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

**Heightmap** lays an image over the ground:

1. Choose a file.
2. Say how wide it is laid, and which heights black and white stand for.
3. Import.

The square is centred on the terrain's `Position`, and the image's top-left
pixel is its corner with the smallest x and z. One ctrl-Z takes the import
back.

- **File formats.** A 16-bit PNG, or a RAW file of square 16-bit little-endian
  samples (`.r16`, `.raw`), keeps a slope smooth. An 8-bit image has 256 steps,
  and over a hundred metres a character walks up those steps as stairs.
- **Ground under the square.** Each column's top moves to the image's height,
  and a cave under the top stays where it is.

**Settings** holds the three numbers a terrain is decided at:
- `VoxelSize`, which can change only while the terrain is empty;
- `MinHeight` and `MaxHeight`, the world's floor and ceiling.

## How it is drawn and collided

- **Drawn.** The ground is drawn as meshes, one per column of 32-voxel chunks
  near the camera, and coarser further away. A far mesh is built from each
  chunk's averages, so distant hills keep their shape with a fraction of the
  triangles. An edit rebuilds only the meshes of the chunks it changed.
- **Collided.** Collision is built near things that move: bodies that are not
  anchored, and characters. Each such chunk gets a mesh of its surface, which
  a body lands on the moment it arrives.

## Large worlds

A terrain saved with a scene streams from disk once it is sixteen cells or
more, each cell 64 m on a side. The world waits for the ground around the
player before the first frame, loads cells ahead of the camera and evicts them
behind it. **A cell somebody changed is never evicted**, so a crater stays a
crater. See [Streaming a large world](manual:assets/streaming).

A terrain does not replicate: a world's ground arrives with the world, on
every machine, from its scene.

**Worlds saved by an earlier version**, when terrain was stored as heights with
voxels only where caves were, open as they were and are saved as voxels from
then on.

## Where to look next

- [Block worlds](manual:world/blocks) -- cubes on a grid, which is a different
  thing from terrain
- [Physics queries](manual:physics/queries)
- `examples/13-terrain` and `examples/17-cave`
