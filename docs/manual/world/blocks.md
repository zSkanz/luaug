# Block worlds

`VoxelService` is a world made of cubes: every block is one type, on a grid,
placed and broken whole. It is the service for mining, building, and games whose
world is a grid of blocks. **It is not terrain.** [Terrain](manual:world/terrain)
is a smooth surface at any angle; a block world's surface is axis-aligned
faces.

## Registering block types

A game registers the types it uses once, and gets back their ids. Id 0 is air.

```luau
--!strict
local voxels = game:GetService("VoxelService")

local White = Color3.new(1, 1, 1)
local Grass = voxels:RegisterBlock("Grass", White)
voxels:SetBlockTextures(Grass, "asset://textures/grass_top.png", "asset://textures/grass_side.png",
    "asset://textures/dirt.png")
local Stone = voxels:RegisterBlock("Stone", Color3.fromRGB(128, 128, 128))
```

- **Ids come in registration order**, so the same script gets the same ids on
  every machine. Registering a name twice returns the id it already has.
- **A type has a top, sides and a bottom**, in colour and optionally in images,
  and the colour tints the image: register white to show an image as drawn.
  Images stay crisp, one per block face.
- **A world holds up to 4,095 types.**

## Placing and breaking

| Call | What it does |
|---|---|
| `SetBlock(block, id)` | Places a block, or breaks one with id 0. `block` is a block coordinate, rounded down. |
| `GetBlock(block)` | The type at a block coordinate, 0 for air. |
| `FillBlocks(from, to, id)` | Fills a box, both corners included, in one call. |
| `Raycast(origin, direction)` | The first block a ray enters and the face it came through: the block a pickaxe breaks, and beside it where a placed block goes. |
| `WorldToBlock(position)`, `BlockToWorld(block)` | Converts between world positions and block coordinates. |
| `Clear()` | Removes every block; the types stay. |

`BlockSize` is how large a block is, in metres, and can only change while the
world is empty.

```luau
-- A pickaxe: break the first block the camera is looking at, within 6 m.
local camera = workspace.CurrentCamera :: Camera
local block = voxels:Raycast(camera.CFrame.Position, camera.CFrame.LookVector * 6)
if block then
    voxels:SetBlock(block, 0)
end
```

## See-through blocks

`SetBlockOpacity(id, opacity, transparency?)` says what shows through a type:

- **`Opaque`**, the default: nothing does.
- **`Cutout`**: leaves, a grate, a fence. A pixel of the image is there or it is
  not, and the shadow a cutout block casts has the same holes.
- **`Translucent`**: glass. It blends, by its image's alpha or by
  `transparency` where the image does not say.

See-through blocks still collide: glass is a wall.

## Fluids

`SetBlockFluid(id, reach, ticksPerStep?)` makes a type flow, in the way players
of block games already expect:

- **A block of it that you place is a source**, and it stays.
- **It falls first.** Every empty block below a fluid fills, and falling water
  is full.
- **Where it cannot fall, it spreads sideways**, one level shallower per block,
  for `reach` blocks (1 to 7), and only from a block that stands on something.
- **Take the source away and what it fed drains.**

It moves once every `ticksPerStep` simulation ticks, 5 by default. A slower
fluid is a larger number. A fluid is drawn see-through at the depth its level
gives, **it never collides** so a character wades in, and **`Raycast` passes
through it** so a pickaxe hits the lake bed. `GetBlock` answers its type at
every level; `GetFluidDepth(block)` says how full it is, from 0 to 1.

```luau
local Water = voxels:RegisterBlock("Water", Color3.new(0.1, 0.3, 0.6))
voxels:SetBlockOpacity(Water, Enum.BlockOpacity.Translucent, 0.45)
voxels:SetBlockFluid(Water, 5)
voxels:SetBlock(vector.create(10, 11, 0), Water) -- a spring on a hilltop
```

**Two fluids can react.** `SetFluidReaction(from, touching, result)` says what
a block of one fluid becomes where it touches another:

```luau
voxels:SetFluidReaction(Lava, Water, Stone) -- lava meeting water sets as stone
```

The reaction goes one way: the water stays water. Give the pair the other way
round a reaction of its own if both should change.

The water moves with the simulation, so the same world and the same ticks make
the same water on every machine.

## In the editor

The block tool places, breaks and replaces blocks from a palette of the
registered types, one undo step per stroke. It aims through fluids, as a
game's pickaxe does.

Under the palette, the selected type's colours, its three images and its
opacity are edited in place. These are what `SetBlockTextures` and
`SetBlockOpacity` set from a script. The images are picked from the project's
`content/`, and a colour tints its image, so white shows an image as drawn.

## Large worlds

A block world saved with a scene streams from disk in 64 m cells once it is
sixteen cells or more, and a cell somebody built in is never evicted. See
[Streaming a large world](manual:assets/streaming).

## Where to look next

- [Terrain](manual:world/terrain)
- `examples/14-voxels`: a hill of textured blocks, trees with leaves that cast
  their holes, a glass window, and a spring running into a pond
