# Phase 3 — the 2D layer: kickoff

Opened by the owner on 2026-09-23 ("you can get going on the 2D and the navmesh",
the mandate's S6). ADR 0008 settled the physics library at planning: Box2D
3.1.1, behind an `IPhysics2D` of its own. This brief settles the rest before
any of it is built. It is read top to bottom by whoever continues it, and its
**Findings** section is filled in as the work corrects it.

## What "a 2D layer" means here

A complete 2D game: sprites with sheets and pixel-art sampling, tilemaps you
paint, 2D physics with triggers, a 2D camera, and a 2D workflow in the editor.
It is not "an orthographic camera pointed at 3D boxes", which the roadmap
ruled out in writing.

## Decisions

1. **2D lives in the same world, on the XY plane**, as Unity's 2D does.
   - A 2D object is an instance under `Workspace`. Its world position is
     `(x, y, depth)`, where its layer decides the depth.
   - Scenes, the editor, undo, streaming, scripts, the world hash and
     replication are therefore the same machinery, not a second copy.
   - A game may mix the two: a 2D level in front of a 3D background, or 3D
     characters on a tilemap.
2. **Two classes carry it.**
   - **`Part2D`** is a sprite and a body in one, as a `Part` is a mesh and a
     body.
     - Transform and look: `Position` (`Vector2`), `Rotation` (degrees),
       `Size`, `Shape` (Box, Circle, Capsule), `Color`, `Transparency`,
       `ZIndex`, `FlipX`/`FlipY`.
     - Its picture: `Image`, with `ImageRectOffset`/`ImageRectSize` for sheets
       and a `Filter` (Linear, Nearest) for pixel art.
     - Its body: `Anchored` (static), `CanCollide`, `Sensor`, `Density`,
       `Friction`, `Elasticity`, `FixedRotation`, `GravityScale`, `Velocity`,
       `AngularVelocity`, `CollisionGroup`.
     - `Touched` and `TouchEnded` fire with the other part, and
       `ApplyImpulse` pushes it.
   - **`Tilemap2D`** is a grid of tiles from one tileset image.
     - Properties: `CellSize`, `Tileset`, `TileSize`, `ZIndex`, `Collides`.
     - Methods: `SetCell`, `GetCell`, `FillRect`, `Clear`.
     - Its solid cells become chains of edges: one body, merged runs, no
       seams to catch on.
3. **The 2D camera is a `Camera` with `Projection = Orthographic`** and an
   `OrthographicSize` (half the view's height, in metres). It looks down -Z.
   Every place that read a perspective-only quantity from the projection is
   changed to ask the camera; six are named in the survey below.
4. **Rendering is one instanced sprite pass in the forward pass**, in the slot
   `architecture.md` reserved for it.
   - Each sprite is one instance: position, size, rotation, UV rect, colour
     and flip.
   - Sprites are sorted by `ZIndex`; at one `ZIndex` a tilemap is beneath a
     part, and otherwise the order is the order they were made. A painter's
     order a game controls. *(Amended in 2D-D: "tree order" would have been a
     tree walk per frame for a tie-break nobody asked for.)*
   - They are depth-tested against 3D and do not write depth.
   - Straight alpha, as a picture's transparent pixels are stored. *(Amended in
     2D-D from premultiplied: nothing upstream premultiplies an image.)*
   - Unlit: a sprite is art, and its `Color` is an sRGB colour decoded as the
     world UI decodes one.
   - A batch breaks on a change of texture or filter. Tile instances go
     through the same pass, culled to the view a block at a time.
5. **Physics is `PhysicsSync2D`, beside the 3D mirror and shaped like it.**
   - Script writes are applied at the start of the tick and the results
     written back quietly.
   - Contacts become deferred `Touched` signals.
   - Workspace gravity's x and y are the 2D world's gravity.
   - `workspace:Raycast2D(origin, direction, params?)` answers the first
     `Part2D` or tilemap hit.
6. **Determinism:** Box2D runs single-threaded (it is never handed a task
   system), carries its own trigonometry and orders its contacts. The world
   hash covers `Part2D` through its properties. A two-world test holds it bit
   for bit.
7. **Replication:** *(Amended in 2D-C.)* Neither class is on protocol 10;
   both are excluded by name with the reason. Carrying a `Part2D` is a field
   set of its own and so a protocol bump, and it is stage 2D-G, after the
   layer has an example that plays. A tilemap's cells will travel as a
   `Terrain`'s would (ADR 0069, decision 7): with the world, and an edit as a
   message.
8. **Editor:**
   - A 2D view: an orthographic camera looking down -Z, pan with the right or
     middle button, zoom with the wheel, and a grid in the XY plane.
   - A **Tiles** tool paints and erases the selected tilemap's cells from a
     palette of its tileset, one undo step per stroke. The Blocks tool is its
     model.
9. **Example:** `examples/20-platformer`, a character that runs and jumps on a
   painted tilemap, with coins as sensors and a camera that follows.

## Stages, each ending with the full gate green and a push

- **2D-A** Box2D vendored, `IPhysics2D` and its backend, tests. *(Done
  2026-09-23.)*
- **2D-B** `Part2D` and `Tilemap2D` in the IDL, their components, their
  accessors, the scene format, the world hash.
- **2D-C** `PhysicsSync2D`, the host's wiring, `Touched`, `Raycast2D`.
- **2D-D** Orthographic cameras, the sprite pass, sprite textures and pixel-art
  sampling, tilemap drawing.
  *(B, C and D done 2026-09-23, as one commit -- see Findings 1.)*
- **2D-E** The editor's 2D view and Tiles tool. *(Done 2026-09-23.)*
- **2D-F** `examples/20-platformer`, conformance specs, documentation.
  *(Done 2026-09-24: the example, `world/layer2d.spec.luau`, and the manual's
  "2D games" page.)*
- **2D-G** Replication: `Part2D` on protocol 11 (ADR 0088). *(Done
  2026-09-24.)*

## The survey this rests on (2026-09-23)

- **Cameras** are perspective only.
  - `core::perspective` is the one projection.
  - These read `projection.m[1][1]` as `1/tan(fov/2)` and must learn
    orthographic: the clusters (`clusters.cpp:130`), the shadow cascade fit
    (`renderer_default.cpp:2524`), LOD pixels per unit (`:1682`), picking
    (`picking.cpp:362`), contact-shadow uniforms (`shader_types.h:370`) and
    editor framing (`editor.cpp:2616`).
- **Textures:**
  - Loose PNGs decode with stb_image to one mip.
  - `MeshLoader::syncTextures` walks the pools that name textures, and a
    sprite pool is one more walk.
  - `pointSampler_` already exists; the voxel atlas uses it for pixel art.
- **The nearest existing path to a sprite** is the world UI (`ui_world.hlsl`:
  camera-relative textured quads, depth-tested, no depth write) and the
  particle pass's instancing (`SV_VertexID` corners and per-instance
  attributes).
- **The model for adding a render class** is `Decal`, across the IDL,
  components, `native_accessors.cpp`, extraction, the renderer, the shader, the
  texture walk, the wire schema and the tests.

## Findings

1. **B, C and D could not land apart.** `inertcheck` refuses a component
   field that nothing reads, and a sprite's picture is read only by the
   renderer while its body is read only by the mirror. Three commits would
   each have carried `Inert` markers for the next to delete; one commit
   carries none.
2. **The survey named six perspective assumptions; there were ten.** Beyond
   the six: the occlusion pass's uniforms, the world UI's billboard scale, the
   LOD statistic the engine counts beside the renderer, the jitter (an
   orthographic projection's x and y offset lives in the constant row, not the
   depth row), and the soft particles' depth linearisation. The fix is one
   question every consumer now asks, `core::viewSpread`: how wide the view is
   at a depth. It is two tangents under perspective and two constant
   half-extents under orthographic, and the same arithmetic serves both.
3. **The screen-space passes are off under an orthographic camera.** Ambient
   occlusion and contact shadows rebuild a position from depth as a
   perspective camera made it; teaching them the other shape is work for a
   2D game that does not want either. Soft particles were taught it, because
   a 2D game does want particles: a negative near plane in their uniforms is
   the flag.
4. **A tile outline needs a rule where two regions touch at a corner.** Two
   boundary edges start at that corner, and taking the wrong one fuses the
   regions into one figure-of-eight loop that Box2D refuses. The walk takes
   the sharpest LEFT turn, which keeps to the region it is going round.
5. **Box2D's closest-hit ray keeps whichever equal hit its tree reached
   first.** `Raycast2D` casts with its own callback that keeps the nearest
   and breaks a tie on the lower user data, so the answer is a fact about the
   world (R10) -- and the same callback applies the instance filter, which
   Box2D's category bits cannot express.
6. **A sprite sheet is measured in pixels, and the texture library did not
   know any.** It now keeps each image's size beside its handle.
7. **Tiles meet only if their shared edge is one number.** A centre plus a
   half-width puts a hairline between tiles at some zooms. A sprite is sent as
   its two corners, each tile's computed from the same expression as its
   neighbour's, and the shader leaves an unturned sprite's corners exactly as
   given.
8. **A remembered revision is not evidence after a restore.** An undo puts a
   tilemap's revision back, and the next edit can arrive at the number the
   physics mirror built from -- with different cells. `World::restores` counts
   restores and is never restored itself; a mirror that sees it move rebuilds.
9. **The 2D view is a lens, not a viewport.** The editor's camera gains an
   orthographic projection and pans instead of turning; picking, the
   manipulator and the Tiles tool then worked through the same ray code the 3D
   view uses, because `rayThroughPixel` already asked the projection. A
   `Part2D` joins the manipulator as a fifth kind: its frame is its position on
   the plane turned about Z, and a drag writes `Position` and `Rotation` back.
10. **The Tiles tool follows its panel**, as the terrain brush does since the
    owner's report that a brush acting with its panel closed was a trap. A
    stroke paints the Bresenham line between frames, so a fast drag leaves no
    gaps, and is one undo step.
11. **The first 2D frame found a renderer defect the 3D ones hid (D181).** The
    bloom chain sampled with a repeating sampler, so a bright bottom edge drew
    a band along the top; a 3D frame's top is sky and bright already. It also
    shows that **sprites bloom**: the post chain treats them as the scene they
    are in, and pixel art wants a neutral one. Deciding that is a question of
    the post chain's API -- `Lighting` has no bloom control today -- and is
    left to it rather than special-cased for sprites.
12. **A platformer's feel is the controller, not the physics.** The example
    sets velocity from input with acceleration, and adds coyote time, a jump
    buffer and variable jump height; the body is a fixed-rotation box with no
    friction, grounded by three short rays. Those are game code on purpose:
    each game tunes them, and the engine's job is to make them easy to write.
    Headless, a scripted pilot holding right and jumping on a timer runs at
    7 m/s, collects coins through sensor `Touched`, falls into the first pit
    and respawns -- the whole loop, with no input device.
