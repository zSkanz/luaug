# Changelog

Every release of LuauG. The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the versions are [semantic](https://semver.org/spec/v2.0.0.html).

**The public surface is `api/api-dump.json`**, which is generated from the IDL
and diff-checked by CI. Anything that changes it is an entry here; anything that
does not is engine work and belongs in the git history rather than in this file.

## [Unreleased]

### Added

- **Terrain is a grid of voxels** (ADR 0082). Every voxel holds a material and
  an occupancy, and the surface is where the occupancy crosses one half:
  - caves, overhangs and flat ground are the same data, so nothing is special
    about any of them;
  - it is drawn as meshes with level of detail, built from each chunk's
    averages further away;
  - it collides chunk by chunk around whatever moves.

  New verbs: `FillCylinder`, `SmoothBall`, `FlattenBall`, `ReplaceMaterial`,
  `ReadVoxels`, `WriteVoxels`, `WorldToCell` and `CellCenterToWorld`. A world
  saved before this opens as it was and is saved as voxels from then on.
- `Workspace:Raycast` meets terrain anywhere, not only near things that move.
- **`TextLabel.RichText`** (F3): a label reads its text as markup -- `<b>`,
  `<i>`, `<u>`, `<s>`, `<font color size transparency>` and `<br/>`, with the
  five XML entities -- so colour, size and weight change part-way through one
  label. A tag it does not understand is drawn as text.
- **Multiplayer prediction, interpolation and interest** (ADR 0076):
  `Player.Character` names a player's part; a replica moves its own at once and
  the authority's snapshots correct it, draws everyone else between snapshots
  instead of stepping, and is sent only what is near its character. Decals and
  `Lighting` (the time of day, the light and the fog) replicate too.
- **`SurfaceGui` and `BillboardGui`** (F3): UI drawn in the world -- on a face of
  a part (`Enum.Face`, `PixelsPerMetre`) or over a point and facing the camera,
  sized in metres, in pixels, or both. The children are the screen's own
  classes, laid out the same way, and hidden by what is in front of them.
- **Terrain and block worlds stream from disk** (ADR 0075): a saved field of
  sixteen 64 m cells or more is cut into cells at play and streamed around the
  foci on the terrain radii. A cell somebody changed is never evicted, and the
  world waits for its ground on first load. `Terrain.CellSize` now names the
  grid it streams on.
- **Digging into a wall carves it**: the editor's dig aimed at steep ground, or
  with the box, takes volume out and bores forward at a steady speed while held.
- `examples/17-cave`: a tunnel into a mountain, dark inside and lit by its lamps.
- **Soft particles**: smoke and fire fade where they meet a surface instead of
  showing a hard line along it.
- **The terrain seam gate** (F1, H1): a tunnel crossing a streaming-cell
  boundary, a tile boundary and the edge between bricked and height-encoded
  ground. Every downward ray must hit the field, the collider and the drawn
  surface, and each hit must lie within a quarter-voxel of the field.
  `asset::sampleField` exposes the field's trilinear sampler, and
  `render::meshCaveColumn` exposes the cave mesh as it is drawn.
- **A dig near a hill no longer cuts it off, and a dig on a hill no longer
  deletes the ground below it** (D163): a column the brush never touched was
  written with the top or the bottom of the brush's range. This is what put
  floating plates and see-through holes in sculpted worlds.
- **Raising, lowering, smoothing and flattening work over caves** (D162): a
  column with a cave in it moves by its top and keeps the cave, where it used
  to be skipped and left a slot through the new ground. So do Generate Flat
  Ground and `WriteHeights`.
- **Digging into a selected terrain no longer draws a lid and boxes over the
  hole** (D161): the selection outline, which shows through everything, drew
  the cave meshes' buried sides. A cave draw is never outlined now.
- **A terrain larger than the atlas draws what is near the camera** (ADR
  0081). Past 16,384 tiles, a 2 km square at half a metre, the loader kept the
  first tiles in key order, and the editor drew a strip along one edge of the
  world. It now keeps the nearest, frees the far ones before uploading, and
  uploads outwards from the camera.
- **`ReplicatedStorage` and `ServerStorage`** (ADR 0080): two services that
  hold what is not the world, saved with the scene under a new optional
  `storage` key. What `ReplicatedStorage` keeps reaches every replica whatever
  its distance; `ServerStorage` stays on the authority. `scene.d.luau` types
  their contents on `game`. The Explorer takes a drop onto `Workspace` or a
  storage, which it refused before. Wire protocol 9.
- **`RemoteFunction`** (ADR 0079): a client asks with `InvokeServerAsync`,
  which yields until the authority's `OnServerInvoke` answers, and raises when
  the handler fails or there is none. The IDL gains callbacks, a function a
  script assigns and the engine calls, and the wire protocol is version 8.
  `examples/15-multiplayer` asks with H.
- **The editor's Terrain panel imports a heightmap and holds the terrain's
  settings**: a 16-bit PNG or RAW image laid over the ground at a size and
  between two heights, one undo step, and `VoxelSize`, `MinHeight` and
  `MaxHeight` beside it. The Blocks panel sets a type's images and opacity.
- **A dot reaches a child, and the scene is typed** (ADR 0078, superseding
  0061): `workspace.Player.Walker` reaches the child after the members, and
  `.luaug/types/scene.d.luau` declares the scene's tree so the analyzer types
  the path and still catches a typo. The editor writes it on open and on every
  save; `luaug setup` and `luaug-host --write-types` write it; `luaug check`
  and the starter's VS Code settings load it. Completion offers children after
  a dot.
- **`RemoteEvent`** (N2, ADR 0077): a game's own messages between machines.
  `FireServer`, `FireClient` and `FireAllClients`, received as
  `ServerReceived(player, ...)` and `ClientReceived(...)`. The authority names
  the sender from the connection. Values, instances and tables travel,
  reliably and in order. Solo and hosting, every call is delivered locally, so
  one script runs in every posture. `examples/15-multiplayer` has a horn. The
  wire protocol is version 7. A manual page, *Multiplayer*, covers the whole
  networked surface.
- **Buttons in the world are pressed** (F3): a `TextButton` on a `SurfaceGui`
  or `BillboardGui` fires `Activated` and the hover events like one on the
  screen. The pointer's ray finds it, anything solid in front hides it, and
  `AlwaysOnTop` wins. `examples/18-world-ui` has a "Next round" button on its
  scoreboard.
- **`Terrain:WriteHeights(corner, columns, heights, material?)`**: a heightmap
  in one call, one height per column, row after row. It is the verb for ground
  that comes from a generator or an image. The flagship's middle is written
  with it in under a second, where one `FillBlock` per column took 28.
- **Water flows** (V1): `VoxelService:SetBlockFluid(id, reach, ticksPerStep)`
  makes a block type a fluid. It pours down first, spreads up to seven blocks
  sideways (shallower with each one), and drains when its source is taken. It
  does not collide, `Raycast` passes through it, and `GetFluidDepth` says how
  full a block is. A world now holds up to 4,095 block types: the rest of a
  stored id is the block's state. `SetFluidReaction(from, touching, result)`
  says what one fluid becomes where it touches another: lava meeting water
  sets as stone.
- **The flagship stands on terrain** (F1, H3): the middle 512 m of
  `examples/10-open-world` is one streamed `Terrain`, with a hill and a tunnel
  through it, in place of 1,024 16-metre boxes. `tools/sculpt-ground` and
  `tools/merge_ground.luau` make and place it.
- **A replica keeps what a script holds.** An instance that leaves a replica's
  interest becomes a husk, reparented to nil, and fires
  `StreamingService.InstanceStreamedOut`, exactly as an evicted chunk's does.
  One the authority destroyed is destroyed. The wire protocol is version 6.

### Changed

- **`Terrain.VoxelSize` defaults to one metre** (was half a metre). A terrain
  that was already sculpted keeps its own.
- `Terrain.HeightAt` answers the top of the ground in a column, over a cave
  too, and between columns it blends the four around the point.
- `Terrain` brushes return how many voxels they changed, and `WriteHeights`
  counts voxels rather than columns.
- `Terrain.MinHeight` and `MaxHeight` are the world's floor and ceiling: no
  voxel is written outside them.
- `Terrain.Compact` has nothing left to do and returns 0; every edit leaves the
  voxels compact.
- `Terrain.CellCount` counts chunks of 32 voxels a side.

### Fixed

- **Digging caves no longer lags, and a cave no longer breaks where it meets the
  ground** (D164): terrain is one grid of voxels, so there is no join between
  two encodings to fault. A dig rebuilds only the mesh and the collider of the
  chunk it changed.
- A partitioned scene kept its block world (D157); a terrain larger than about a
  square kilometre reopened after a save (D159); terrain edges stopped hanging
  curtains to the floor as the level of detail changed.
- A tree of `Cutout` leaf blocks casts its leaves, holes and all, rather than a
  solid square.
- A script holding a streamed instance keeps a working handle when its chunk is
  evicted, and a husk nothing holds any more is destroyed (D160).

## [1.1.0] — 2026-09-23

**The editor.** v1.0.0 shipped an engine you wrote games for in a text editor;
this is the phase that gave it a window. Nine milestones, E1 through E9.

### The editor

- **`luaug edit`** — an application with a menu bar and dockable panels, a
  viewport you click, fly and select in, play/pause/step/stop/save, a content
  browser whose folders and context menus open a scene by double-clicking it,
  undo and redo, and a scene format that is what a project's world data IS
  rather than an export of it.
- **Direct manipulation**: translate, rotate and scale gizmos, creating
  instances, reparenting by drag, multi-select, `Group` and `Ungroup`, a snap
  step you can change with a grid that shows it, and a gizmo that sits where you
  say over a selection rather than where an average lands.
- **Prefabs as stamps** (ADRs 0048, 0051, 0060): `content/` holds SOURCES and an
  instance in the world may be a LINK to one. Editing a linked instance is an
  OVERRIDE and the link survives — the reverse of what ADR 0048 first said,
  reversed while the milestone was building it. A placed stamp can be asked
  which of its properties are its own, and each one reverted or applied back.
- **A launcher** (ADR 0055): `luaug-host` with no project opens a project
  browser instead of printing a usage error — recent projects, a new one from
  a template, a folder picker.
- **A script editor** (ADR 0057): any number of scripts as tabs beside the
  viewport, Luau colour from the engine's own lexer, find and replace, errors
  underlined where the parser puts them, autocomplete from the reflection
  tables, and a working debugger with breakpoints, stepping, the call stack and
  the locals — the script parked and the frame loop still drawing.
- **A look of its own** (ADR 0056): one theme as data rather than
  `StyleColorsDark` plus nine literals, Inter instead of a 13 px bitmap face,
  and a palette measured against WCAG rather than argued about.
- **A downloadable folder** (ADR 0054), and an Explorer that costs what is open
  rather than what exists.

### Assets

- **A world you author becomes a world that streams** (ADR 0053), with no
  generator script and nothing sorted by hand. `Model.StreamingMode` makes the
  model the unit rather than the part.
- **Everything arrives compiled** (ADR 0065). A loose `.gltf` is a source, not
  an asset: opening a project compiles what has no compiled form, in every host
  mode, so a clone from git works with no command. What this buys is what the
  compiler was always producing and the runtime was not reading — LOD chains,
  meshlets, and BC7 with mips instead of raw RGBA8. It is also the only reason
  an FBX has ever loaded.
- **A model becomes a `Model` of named parts**, one per primitive, each
  addressable as a URN fragment and each able to carry its own material.

### The public surface

New classes: `Attachment`, `Bone`, `Constraint`, `BallSocketConstraint`,
`HingeConstraint`, `FixedConstraint`, `Ragdoll`, `Material`, `BaseScript`,
`ModuleScript`. New enum: `AlphaMode`.

- `BasePart.Material` — a part points at a material, and `BasePart.Color`
  multiplies it, so white on both sides is the identity and no existing scene
  changes (ADR 0060). `BasePart.CanTouch`.
- `Model.Scale` — absolute rather than cumulative, about the pivot, and one
  undo takes it back. `Model.StreamingMode`.
- `MeshPart.MeshSize` — authored rather than derived, so a headless run and a
  rendered one cannot disagree about how big a mesh is.
- `PointLight.Enabled` and `SpotLight.Enabled` stop being inert and cast.
- `StreamingService` gains separate structure and terrain radii.

### Also in this release: the first of phases 2 and 4

Built while the editor phase waited for its sign-off, and shipped with it
rather than held back. Each works end to end and each has limits of its own,
listed below.

- **Terrain** (ADRs 0066, 0067, 0071): `workspace.Terrain`, one signed-distance
  field stored as height tiles where the ground is a height and as voxel bricks
  where it is not, so caves and overhangs cost only where they are. Sculpted
  from a script or the editor's brush, drawn from a height atlas on the GPU,
  collided, saved with the scene.
- **Block worlds** — `VoxelService`, which is not the terrain: registered block
  types with images per face, cutout and translucent blocks, place and break,
  a raycast to the block under the crosshair, colliders, and a block tool in
  the editor.
- **Multiplayer on a LAN** (ADRs 0069, 0070): `--host`, `--serve` and `--join`,
  snapshots diffed against acknowledged baselines, `NetworkService` and
  `Player`, and what a player did reaching the authority as intent.
- **Particles and decals** (ADR 0072): `ParticleEmitter` and `Decal`.
- **Sharper shadows and contact shadows**, and **colour textures that are
  finally drawn in the colours they were painted in** (ADR 0073): every
  compiled colour texture had been uploaded as linear and drawn pale.

New classes: `Terrain`, `VoxelService`, `NetworkService`, `Player`,
`ParticleEmitter`, `Decal`. New enums: `NetworkTopology`, `ParticleShape`,
`BlockOpacity`.

### Known limits, stated plainly

Still not built, each with an owner in the roadmap's post-v1 phases: world-space
UI (`SurfaceGui`, billboards), rich text, navmesh pathfinding, a 2D workflow and
mobile. Terrain and block worlds do not stream from disk yet; multiplayer has no
client prediction or interest management and its transport is unencrypted, so
it is for a LAN; particles are not soft; a decal darkens and tints and cannot
brighten.

**E5, E7, E8 and E9 were signed off on 2026-09-23 on the owner's delegation.**
The one row each had that only a person could close is named in its brief.

## [1.0.0] — 2026-08-22

The first release. Eleven milestones, M0 through M8, each signed off by a human
after playing what it built.

### The engine

- **A sandboxed Luau 0.734 VM** embedded directly, with `luaL_sandbox` on in
  every profile, `--!strict` under the new type solver everywhere, and the
  legacy scheduling globals (`wait`, `spawn`, `delay`, `tick`) absent rather
  than deprecated.
- **An Instance tree over a hand-rolled deterministic ECS**: `game`,
  `GetService`, `Instance.new`, `Parent`, `FindFirstChild`, attributes, tags,
  and `Clone`. A destroyed instance's handle stops resolving at the end of the
  drain in which `Destroying` fired, which is a keyed error instead of a silent
  read of a corpse.
- **Deferred-only signals** (ADR 0015) with `:Connect`, `:Once`, `:Wait` and
  `GetPropertyChangedSignal`, and `Signal<T...>` as a global datatype in place
  of `BindableEvent`.
- **A fixed 60 Hz simulation tick** with a variable render clock, five phase
  signals, and `task.spawn/defer/delay/wait/cancel`. **The renderer interpolates
  between ticks**, so a display faster than the simulation shows smooth motion.
- **Determinism as a gate, not a claim**: same build, same platform, same seed
  and inputs produce the same world hash at every checkpoint, replayed over
  10,000 ticks in CI (ADR 0025).

### Rendering

- **Forward PBR** with metallic-roughness materials, glTF 2.0 meshes, primitive
  parts, and a `Lighting` service that drives a physically-derived sky.
- **Four cascaded shadow maps** in one atlas, with a world-constant filter
  radius, normal-offset bias, a blend band rather than a switch at a plane, and
  a fit that keeps its box while the box still covers what casts.
- **Clustered forward shading** on a 16×9×24 grid: the light count stopped being
  eight.
- **Image-based lighting** by the split sum, with the sky as the environment, so
  a metal reflects the hour the script set.
- **A post chain**: depth prepass, screen-space ambient occlusion, automatic
  exposure with an artist control in EV stops, bloom, and FXAA.
- **Instanced draws**: 4,002 visible objects in 22 draw calls, against 15,390
  before.
- **A graphics settings family** (ADR 0044): `low`, `medium`, `high`, `ultra`,
  plus render scale, shadow resolution, cascade count and distance, light budget
  and post toggles — from `luaug.toml` and the command line, never from a scene.

### Physics

- **Jolt 5.6**, with rigid bodies, contacts surfaced as `Touched`, collision
  groups, raycasts and shapecasts, transform welds, and a `CharacterBody` that
  walks, jumps, climbs steps and rides moving platforms.

### The world

- **An offline asset pipeline**: assimp to canonical glTF, a content-addressed
  pack, and a build that is byte-identical across processes.
- **Chunk streaming** with a minimum ring that must be resident before a focus
  advances into it, hysteresis on eviction, and a materialisation budget in
  milliseconds rather than in chunks.
- **A per-world floating origin** (ADR 0014), verified identical at 10⁷ metres.
- **A job system** with work stealing, dependencies and a stable-merge commit.

### The game layer

- **The Input Action System** (ADR 0029): actions, contexts, bindings,
  rebindable at runtime, promptable, with a non-device channel a HUD button
  drives.
- **A UI tree** with `ScreenGui`, frames, labels, buttons, images, scrolling,
  `UDim2` layout, and text through a vendored Inter.
- **Tweens**, **audio** on the simulation timeline, and **skeletal animation**.

### Tooling

- **`luaug new`**, **`dev`** (hot reload under 500 ms, measured at 1.7 ms),
  **`run`**, **`test`**, **`check`**, **`fmt`**, **`build-assets`**, and
  **`build`** — which produces a folder you can send to somebody: the player,
  the engine's content, and the game.
- **Application identity**: a game built with `luaug build` carries its own icon
  in the artifact, verified by reading the resource back out of it.
- **A typed IDL** as the single source of truth for the public surface, from
  which the C++ registration, the `.d.luau` definitions, the api-dump and the
  API reference are all generated and freshness-gated.

### Known limits, stated plainly

Not in v1, each with an owner in the roadmap's post-v1 phases: a visual editor,
particles, decals, terrain, `SurfaceGui`, rich text, navmesh pathfinding, a 2D
workflow, multiplayer and replication, and mobile. `Sound.Content` plays a
generated tone rather than a file. `Enum.CollisionFidelity` round-trips while
every value collides as a box. Properties the engine stores and does not read
are marked `Inert` in the inspector and the api-dump, and a gate stops a new one
appearing quietly.

[1.1.0]: https://github.com/zSkanz/LuauG/releases/tag/v1.1.0
[1.0.0]: https://github.com/zSkanz/LuauG/releases/tag/v1.0.0
