# 0071 — Terrain ground is drawn from a height atlas, and only caves are meshes

- Status: superseded by [0082](0082-terrain-is-a-grid-of-voxels.md)
- Date: 2026-09-22
- Milestone: F1 (post-v1 phase 2), the terrain rework
- Decided by: the agent, under the owner's standing instruction of 2026-08-26 to
  take the repository's decisions on their behalf. The **direction** was the
  owner's own, 2026-09-22: the terrain was *"todo lagado e bugado"*, looked wrong
  from below, and should be built the way the open-source Godot terrains are.
- Amends: [ADR 0067](0067-terrain-is-one-field-with-two-encodings.md) (how the
  field reaches the screen; the field itself is unchanged) and the RHI freeze of
  [ADR 0037](0037-rhi-interface-frozen-at-m4.md) by one method.

## Context

ADR 0067 stored terrain as one signed-distance field in two encodings -- height
tiles for ordinary ground, voxel bricks where the ground stops being a height
function -- and drew ALL of it by meshing the field on the CPU with marching
tetrahedra, one mesh per 32 by 32 tile. Three things were wrong with that, and
the owner found each of them by using the editor:

1. **Every edit was a remesh.** A brush stroke re-meshed and re-uploaded the
   tiles it touched; with levels of detail (the streaming pass) a camera move
   re-meshed every tile whose level changed. Measured on a 256 m terrain with one
   brush stamp a frame: a 49.5 ms median frame, 26.5 ms for painting alone.
2. **The level-of-detail seams were covered, not closed.** Skirts hung under
   every tile edge hid the cracks between strides from above and turned the
   underside of the world into a grid of curtains.
3. **Shading was per vertex of whatever level drew it.** Hills shaded in
   terraces and bands, and a painted edge was a staircase along triangle edges.

We studied the three open-source Godot terrains (Terrain3D, HTerrain,
godot_voxel) for their architecture -- concepts only, no code. The heightmap ones
agree: **the ground is a texture, the geometry is a fixed grid moved on the GPU,
and an edit is an upload.** Terrain3D draws a geometry clipmap displaced in the
vertex shader from region textures, seals level seams with a vertex morph rather
than skirts, computes normals per pixel from the heightmap, blends materials per
pixel from a packed control map, and never meshes the ground at all. godot_voxel,
the voxel one, remeshes only affected blocks and keeps meshes for what is
genuinely volumetric.

## Decision

**The height layer is drawn from textures; the bricks are drawn as meshes.**

- Each terrain's height tiles live in an `R32Float` atlas, one 32 by 32 slot per
  tile, with an `R8Unorm` atlas of material ids beside it and a small table from
  tile key to slot (`TerrainLoader`). An edit uploads the tiles it changed --
  decided by each tile's digest, never by the terrain-wide revision counter --
  and nothing is meshed.
- The renderer draws the ground as a CDLOD quadtree (Strugar) whose leaves are
  the field's own tiles (`terrain_lod.h`). Every node, at every level, is the
  same 32 by 32 grid; the vertex shader lifts it to the atlas's heights and
  morphs odd vertices onto the next level's grid towards the edge of each
  level's distance band (`luaug_terrain.hlsli`). Neighbouring nodes are never
  more than one level apart, so the morph closes every seam. **No skirts.**
- The fragment shader computes the normal and the material blend per pixel from
  the atlases, then lights the surface with the same `lightSurface` every other
  forward shader uses (split out of `shadeForward` for this).
- **CDLOD rather than a clipmap**, though both are the same idea: a CDLOD node is
  a draw with its own bounds, which is exactly what this renderer's per-pass
  culling (camera frustum, each shadow cascade's sphere) already consumes. A
  clipmap's rings would need their own culling path for the same result.
- **Caves stay meshes.** Near the viewer (inside level 0's morph start), every
  column carrying bricks is meshed on the CPU as before and drawn as an ordinary
  mesh, and its material bytes get a cave flag; the terrain shader discards the
  ground's triangles there, so the cave shows through an opening that lines up
  with it. Far away there is no cave mesh, no flag and no hole.
- **The sculpting brush moves heights.** A round `fillBall` brush on flat ground
  overhangs at its rim, which is voxels -- so every stroke used to lay a trail of
  bricks along its edges. The round brush is now `raiseBall`, a height brush with
  a smooth falloff, exposed to scripts as `Terrain:RaiseBall`. The box brush and
  `FillBall` keep the volumetric fill, which is what tunnels and ledges need.

### The RHI gains one method

`ICmdList::uploadTextureRegion(texture, x, y, width, height, data)` uploads a
rectangle of mip 0. The atlas is the size of the world and a stroke changes a 32
by 32 tile of it; the alternative was re-uploading the whole atlas per stroke,
megabytes to change kilobytes. It is the smallest addition that makes the design
work, on the same terms ADR 0043 added `perInstance`: implemented by every
backend, recorded by the capture backend, refused (not clipped) when the
rectangle does not fit.

## Consequences

- An edit costs O(tiles touched) uploads of five kilobytes each. The same scene
  that took 49.5 ms per sculpting frame takes 8.4 ms, the same as standing still.
- The ground has no underside and shows none; the curtains are gone because the
  skirts are.
- A project with no terrain pays nothing: the terrain pipelines and grid buffers
  are created the first time a frame has terrain, so no capture golden of a
  scene without terrain changed.
- **Caves are near-only.** A cave mouth beyond ~30 m is drawn as the hillside
  around it until the viewer approaches. At that distance it is what it looks
  like; this is the same trade Terrain3D makes by leaving caves to separate
  meshes.
- The CPU mesher, its skirts and its level-of-detail strides remain in `asset`
  (the collider and the caves still use the mesher); the loader no longer meshes
  height tiles at all.
- Texture sets per material (albedo, normal, roughness with triplanar on steep
  slopes) slot into the four material samplers the terrain shader already reads
  -- bound to white, flat, white and black today.
