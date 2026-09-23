# 14 — Voxels

A world made of blocks: the service for block games.

```
luaug run examples/14-voxels
```

**This is not the terrain.** `Terrain` (example 13) is a sculpted landscape, a
smooth surface at any angle. `VoxelService` is a grid of cubes, each one a block
type, placed and broken one at a time: the Minecraft shape of game.

## What to look at

- **Grass is green on top and earth on its sides.** `RegisterBlock` takes a top
  colour and, optionally, a side and a bottom colour.
- **Each corner is shaded by the blocks around it**, and a flat floor of one
  block type is a single quad: the mesher merges faces whose block and corner
  shading agree, and drops every face hidden by a neighbour.
- **A trench is mined while you watch.** Every few ticks the script casts a ray
  straight down with `VoxelService:Raycast` and breaks the first block it
  enters, which is how a pickaxe works. Place a block at `block + normal` and
  you have building.
- **The crate falls onto the hill and stays there.** Chunks near anything that
  moves get a collider built from the same faces that are drawn.

## Images

Every block type here has images, set with `VoxelService:SetBlockTextures`:
grass with a green top and a fringe down its sides, dirt, stone, logs with rings
on their ends, and leaves. The images are sixteen-pixel PNGs under
`content/textures/`, generated for this example and licensed with it. They are
drawn into a block atlas on the GPU the first frame each loads -- which is what
lets a compiled image, which has no pixels the CPU could pack, be used at all --
and sampled without smoothing, so they stay crisp however close the camera is.

