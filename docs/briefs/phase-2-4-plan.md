# Post-v1 Phases 2 and 4 — Milestone Plan

**Written before any of it is built, which is the difference between this file
and [`e9-kickoff.md`](e9-kickoff.md).** E9 was specified outside the
repository and documented two thirds of the way through; the cost is visible in
its Gate Record, which has eleven rows reading *Declared and absent*. This plan
exists so the next four milestones are gate-first.

- Phases **2** (effects and world content) and **4** (multiplayer) were both
  opened 2026-08-27 by the owner, in one instruction, verbatim *"terrain editor
  multiplayer voxels etc."* Phase 3 (2D layer, navmesh) and phase 5 (mobile)
  stay closed.

  **R15 is not "lifted" and that wording is worth getting right**, because a
  future session will read it. R15 says v1's scope is closed and that a scope
  change is an escalation item; the post-v1 phase list IS that escalation, and
  a phase is open when the owner opens it. Three are. What R15 forbids is taking
  that decision on their behalf, not the work
  (`MASTER_PROMPT.md` R15, `docs/roadmap.md` post-v1 phases).
- Two design competitions have already run — three independent proposals each,
  judged by a fourth agent told to check every claim against the code rather
  than trust it. **Terrain is proposal 3**: one signed-distance field with two
  encodings, a dense height layer plus sparse voxel bricks, composed below one
  Marching-Cubes mesher. **Replication is also proposal 3**: a wire schema
  declared in `api/wire/`, one `extract` diffed per peer against that peer's
  last-acknowledged baseline. Both judgements are inherited, not re-decided.
- Decision records: **`docs/decisions/` ends at 0065**, so the numbers this plan
  claims are 0066 through 0070. Every one is a human approval gate, not an
  agent's choice.

---

## Assumptions taken

Four, and each is overturnable by the owner with a rename or a sentence.

1. **Naming.** v1 was `M0`–`M8`; the editor phase was `E1`–`E9`. This plan calls
   phase 2 **`F1`–`F4`** (effects and world content) and phase 4 **`N1`–`N2`**
   (network). Nothing in the repository derives from the prefix — `PROGRESS.md`,
   the roadmap table and the tag namespace all take whatever letters the owner
   prefers.
2. **The owner's word order is the milestone order.** "terrain editor
   multiplayer voxels" puts terrain first and multiplayer second, and there is a
   technical tiebreak in the same direction (§ Order, below). If the owner
   wanted the visible gap closed first — the roadmap calls particles "the most
   visible gap of the group" with "no workaround" — F2 moves ahead of N1 at the
   cost of one extra determinism re-record and nothing else.
3. **`Terrain` is `Module = "scene"`, not a new module.** Verified rather than
   assumed: `api/generator/gen_cpp.luau:429-431` skips the strictly-lower-layer
   check when the parent is emitted in the same module, and `Instance` is
   `Module = "scene"` (`api/defs/instances.api.luau:27`). So no
   `gen_cpp.luau:85` row, no `checklayers.luau:30` row, no `scene_types.cpp`
   wrapper, no `world_host.cpp:207-212` call. `NetworkService` takes the same
   route for the same reason, and `engine/replication` at L4 holds only the
   engine, never a descriptor file.
4. **Every milestone below ends on a green six-stage `scripts/localgate.ps1`,
   including the Linux tier.** These four milestones touch nine modules between
   them and Clang diagnoses what MSVC does not. `-SkipLinux` is not available to
   this work.

---

## The split, and why the rest of phase 2 is a third milestone rather than deferred

| | Milestone | Size | What it is |
|---|---|---|---|
| **F1** | **Terrain You Can Dig** | XXL | The hybrid field, the mesher, two new static shapes on the physics seam, a `Terrain` class, its own streaming, and a sculpt tool in the editor. |
| **N1** | **Two Worlds, One Match** | XXL | `api/wire/`, `gen_wire.luau`, `engine/replication` at L4, `NetworkService` and `Player`, four postures from one binary, the `runTwoWorldsGate` inversion. |
| **F2** | **Particles and Decals** | L | `ParticleEmitter`, projected decals, soft particles — and the one RHI change all three need. |
| **F3** | **World-Space UI and Rich Text** | M | `SurfaceGui`, billboards, per-run colour/weight/size inside one `TextLabel`. |

**F2 is a third milestone and is not deferred.** Particles, soft particles,
decals, water foam and SSAO's second half are five callers of exactly one
missing thing: a fragment shader that can sample the depth buffer its own pass
is attached to. `rhi::DepthStencilAttachment` (`engine/rhi/include/luaug/rhi/descs.h:141-147`)
has `texture`, `loadOp`, `storeOp`, `clearDepth` and **no read-only flag**, and
`ICmdList` (`device.h:59-99`) has no copy call — so there is no way to get the
opaque depth into a sampler while the blended pass is writing depth.
`docs/briefs/m7.5-kickoff.md:194-212` and `:879-882` state this as a seam
deliberately left open and name three of the five callers; `roadmap.md:1215` adds
decals as the fourth. **ADR 0037 froze the RHI**, so this is a human-approved
ADR (0068 below), exactly as a pinned-dependency bump is under R5.

That dependency is the whole argument for the split. F2 cannot start until a
person approves an RHI unfreeze; F1 and N1 need no such approval and would sit
idle behind it. Merging F2 into F1 would make one milestone hostage to two
unrelated human gates.

**F3 is a fourth milestone and is deferred behind N1, on purpose.** It touches
`ui` (L5) and `render` (L4) and blocks nothing: the tree, the layout and the
`ui2d` pass have all existed since M6, and the glyph cache is already keyed by
face, size and codepoint, so a label carrying three sizes and two weights
already fits the cache that exists (`roadmap.md:1240-1246`). Nothing else in
either phase waits on it, and it is the only milestone here that adds no new
seam anybody else has to build against. It goes last because that is where the
work that nobody is blocked by belongs — not because it is unimportant.

**Nothing in phase 2 or 4 is dropped.** Global illumination, navmesh, the 2D
layer, mobile and the ecosystem work stay where the roadmap has them: closed.

## Order, and why each one leaves `main` green

```
F1  ──►  N1  ──►  F2  ──►  F3
```

- **F1 before N1** because F1 changes the physics seam and the world hash, and
  N1's wire schema is written against a settled hash walk. It also settles the
  streaming-foci story before `Player.Character` multiplies foci: N1's interest
  management reuses `StreamingFocus::layers[]` and `loadRadiusFor`, and it is
  cheaper to have terrain already living on layer 2 than to retrofit it.
  Concretely: **every class that exists when N1 writes `api/wire/state.wire.luau`
  is a class the schema must decide about.** `Terrain`, whose bulk is not a
  property and is therefore explicitly *not* replicated, is the hardest such
  decision, and having it exist and be excluded on the record is better than
  adding it afterwards to a schema whose field ids are permanent.
- **N1 before F2** because N1 is the milestone with the largest unknown and the
  most human gates ahead of it (ADR 0035's "the engine opens no port in any
  profile" needs narrowing before a line is written), and F2's blocker is a
  single well-scoped ADR that a person can approve at any time in parallel.
- **F2 before F3** because F2's `ParticleEmitter` is the roadmap's stated most
  visible gap and F3's is a refinement of something that already draws.

Each of the four is independently landable. There is no step in any of them
whose absence breaks the previous milestone's gate.

---

# F1 — Terrain You Can Dig

## Goal

**You pick up a brush, drag it across the ground, and the ground changes —
including under itself.** Caves and overhangs, because the owner answered the
representation question with *voxel* on 2026-08-27 and the roadmap records the
cost of that answer as "a per-chunk collider and its rebuild cost become part of
the milestone rather than a line in it" (`docs/roadmap.md:1233-1239`).

What is being built is one signed-distance field `sd(p)` sampled on one lattice,
stored two ways: a dense per-column height layer for the columns that are a
single-valued height function, and sparse 16³ voxel bricks for the columns that
are not. **One mesher, Marching Cubes on the primal lattice, cannot tell which
encoding answered a lattice point.** The seam between the two encodings is an
equality rather than a stitch, because for `sd(p) = p.y − H(x,z)` the
vertical-edge crossing is at exactly `y = H(x,z)`, which is the height grid's
own vertex.

The payoff, in order of how load-bearing it is:

1. **A terrain cell fits `ChunkId` unchanged.** `asset::ChunkId` is `{x, z, layer}`
   with no vertical coordinate (`engine/asset/include/luaug/asset/chunk.h:61-72`),
   and `chunkBounds` returns a vertically-infinite box (`chunk.cpp:154-164`). A
   pure-voxel 64 m × 64 m column at 0.5 m is 16.7 M samples and cannot be one
   cell; a sparse column of 17 height tiles plus whatever bricks the caves
   actually need is 87 KB and can. **No `ChunkFormatVersion` bump, no
   `PartitionRules` bump, no `ChunkLayerCount` change, no new
   `StreamingService` IDL properties.**
2. **The common edit stops being a shape rebuild.** There is no `setBodyShape`
   on the seam; `updateBody` is `RemoveBody` + `DestroyBody` + `CreateAndAddBody`
   (`engine/physics/src/jolt/jolt_physics.cpp:847-890`) and it calls `forgetPairs`,
   which is an erase-remove over every previous contact pair in the world
   (`:1895-1918`) — so the next tick reports `Began` for every pair still in
   contact and never reports `Ended`. **That is a correctness defect, not a
   performance one**: a character standing on ground being dragged gets a
   `Touched` every tick of the drag. `HeightFieldShape::SetHeights` rewrites a
   sub-rectangle in place — verified at
   `third_party/jolt/Jolt/Physics/Collision/Shape/HeightFieldShape.cpp:994-1002`,
   with the block-alignment assert at `:1000` — same shape object, same
   `JPH::BodyID`, contacts preserved, no broadphase churn.
3. **LOD is exact on the majority.** A height layer decimates by striding with a
   computable error bound. Nothing in `render` simplifies a mesh —
   `MeshLodRange` chains reach `MeshCache::create` from exactly one place
   (`engine/render/src/mesh_loader.cpp:594-615`), flattening what the offline
   compiler produced.
4. **Undo stays affordable.** `UndoStack::record` pushes `world.snapshot()`, a
   by-value copy of all 33 pools (`engine/scene/include/luaug/scene/world.h:207-282`),
   capped at 64. Copy-on-write immutable tiles and bricks make a snapshot a
   vector of `shared_ptr` and a refcount bump. **Verified rather than assumed**:
   `ComponentPool::m_dense` is a plain `std::vector<T>`
   (`engine/scene/include/luaug/scene/component_pool.h:153`) and `World::snapshot`
   copies pools as values, so a `shared_ptr` member copies correctly today. The
   "POD and memcpy-able" comment at `component_pool.h:10` becomes stale; the code
   does not break.

## Decisions this milestone needs a person for

| ADR | What it decides | Why it cannot be an agent's call |
|---|---|---|
| **0066** — *the physics seam learns two static shapes* | `ShapeType::HeightField`, `ShapeType::TriangleMesh`, `ShapeDesc`'s triangle and height payloads, the forced-Static rule for zero-volume `MustBeStatic()` shapes, `IPhysics3D::updateHeightField`, and a quiet-rebuild flag that suppresses `forgetPairs`. | It widens a frozen public seam and changes what `Enum.CollisionFidelity.Precise` means. |
| **0067** — *terrain is one field with two encodings* | The hybrid, the promotion rule and its slope precondition, the `.lterrain` format, the copy-on-write component, and **reusing the already-linked `basis_zstd` for cell compression**. | The last clause is a new dependency edge. `third_party/CMakeLists.txt:576` builds `basis_zstd` from the *full* amalgamation and `engine/asset/CMakeLists.txt:45` already links `basis::transcoder`, which links it `PUBLIC` with a `SYSTEM PUBLIC` include — so `#include <zstd.h>` costs nothing to build. It is still R5's spirit: a paragraph of ADR, not a silent include. |

## Assumptions this milestone takes, stated so they can be falsified

1. **The 45° slope precondition holds on real terrain.** Promotion continues
   outward until every boundary quad satisfies `|ΔH| ≤ voxelSize`, giving up at 8
   columns and converting the whole cell to bricks. Real mountainsides are
   steeper than 45° routinely. **Step A5 measures this on
   `examples/10-open-world` before the mesher is written** — if most cliffs
   promote whole, the hybrid pays pure voxel's memory *and* its
   destroy-and-recreate collider *and* carries ~600 extra lines, which is the
   worst of both.
2. **`MeshUsage::Dynamic` is usable for a live stroke.** It has zero production
   callers — grep confirms `engine/render/src/mesh_cache.cpp:293` and
   `engine/render/tests/mesh_cache_tests.cpp` and nothing else. Its own header
   reserved it in writing for "procedural and voxel meshing later"
   (`mesh_cache.h:45-53`). `SdlGpuCmdList::upload` calls `SDL_UploadToGPUBuffer(…, cycle=false)`
   (`engine/rhi/src/sdlgpu/sdlgpu_device.cpp:790`) into ring memory that
   `beginFrame` rewinds, and `submitAndPresent` (`:331-339`) issues no fence.
   **Step A4 measures whether that is a real write-after-read hazard.** The
   fallback if it is — `Static` release+create per stroke frame, two device
   allocations and two frees per cell per frame — is precisely what M4 Decision
   5 exists to prevent, so the answer changes the design of Part F1.
3. **A Marching-Cubes case with four corners below and four above yields the
   quad across the four vertical-edge crossings.** Argued from first principles
   and **not verified against a table implementation**. The whole seam-equality
   claim rests on it. Step B2 verifies it against the actual table and pins the
   emitted diagonal to Jolt's `(x,y)→(x+1,y+1)` explicitly, verified at
   `HeightFieldShape.cpp:898-925` and again in `ProjectOntoSurface` at `:1501-1537`.

## Order of work

Compile order, bottom-up by layer. Every row ends somewhere the repository is
green and committable.

### Part A — Measure and decide (nothing below is designed on a guess)

| # | Step | Files | Green because | Goldens |
|---|---|---|---|---|
| **A1** | ADR 0066 and 0067 written and approved. The two doc corrections that are **already wrong** land in the same commit: `coming-from-roblox.md:342` still says terrain is a height-field authoring question, which the 2026-08-27 voxel decision superseded. | `docs/decisions/0066-*.md`, `docs/decisions/0067-*.md` (new), `docs/coming-from-roblox.md`, `docs/roadmap.md`, `PROGRESS.md` | Docs only. | none |
| **A2** | The physics seam learns two static shapes. `ShapeType` gains `HeightField` and `TriangleMesh`; `ShapeDesc` gains `std::span<const u32> indices`, `std::span<const f32> heights`, `u32 heightSampleCount`, `u32 heightBlockSize`, `f32 heightMin/heightMax`, `u64 geometryRevision`. `buildShape` gains two cases. `instantiate` forces `Static` for any kind reporting `MustBeStatic()` — required, because both report `GetVolume() == 0` and `instantiate` computes mass as `volume × density` clamped to 0.001 with `EActivation::Activate` (`jolt_physics.cpp:1863-1873`). `updateHeightField(world, body, x, z, sizeX, sizeZ, heights)`. A `quiet` flag on the update path suppressing `forgetPairs`. `sameShape`/`withoutPoints` widen to strip every new span, since **no span may outlive the call** (`types.h:96-114`). | `engine/physics/include/luaug/physics/types.h`, `.../physics.h`, `engine/physics/src/jolt/jolt_physics.cpp`, `engine/physics/tests/terrain_shape_tests.cpp` (new), `engine/scene/src/physics_sync.cpp:52-64`, `engine/scene/tests/physics_sync_tests.cpp` | Two new enumerators with a `buildShape` case and a test each. Nothing above L2 names them yet. | none |
| **A3** | **The bench this repository does not have.** Nothing measures `MeshShapeSettings::Create`, a `HeightFieldShape` construction, or a `SetHeights` sub-rect; `physics1k` and `churn10k` move and re-target bodies without ever reshaping one, and the only `updateBody` coverage is one correctness case (`constraint_tests.cpp:280`). Every cost figure below the seam is currently a guess. Driven through `IPhysics3D`, which A2 just made possible. | `engine/physics/tests/shape_build_bench.cpp` (new), `docs/perf-baselines.md` | Measurement, gated only on a catastrophe ceiling. | none |
| **A4** | The Dynamic-ring hazard, measured. A ring create every frame for 600 frames, windowed, contents verified by readback where the backend allows and by capture-stream hash where it does not. If the hazard is real, the fix is a fence or a per-frame ring segment in `sdlgpu_device.cpp` — **our code, not `third_party/`**. | `engine/render/tests/mesh_cache_tests.cpp`, `engine/rhi/src/sdlgpu/sdlgpu_device.cpp` (conditionally), `docs/perf-baselines.md` | Test-only unless the hazard is real. | capture hash if the fix lands |
| **A5** | The slope survey. Walk `examples/10-open-world`'s ground and report what fraction of 64 m cells would promote whole under `|ΔH| ≤ 0.5 m`. **Written into this brief's Findings before Part B starts.** | `tools/repo/terrainsurvey.luau` (new, one-off), this brief | Tool only. | none |

### Part B — The field and the mesher, `engine/asset` (L2)

`asset` and not a new module: a new `engine/voxel` at L2 could not include
`asset` (same layer; `checklayers.luau` allows exactly one same-layer edge,
`script→ui`), and L4 would put it beside `render` where only `app` sees both.
`asset` already generates geometry — `asset::makePrimitive` builds the five
`Enum.PartShape` solids — so a mesher here is precedented.

| # | Step | Files | Green because | Goldens |
|---|---|---|---|---|
| **B1** | The field. `TileKey`/`BrickKey` as `{i32 x, i32 y, i32 z}` POD with `operator<=>`; `HeightTile` (32×32: `f32 height[1024]`, `u8 material[1024]`, `u64 digest` = 5,128 B, immutable); `Brick` (16³: `u8 sd[4096]`, `u8 material[4096]`, `u64 digest` = 8,200 B, immutable); `TerrainField` holding two **sorted flat vectors** of `std::shared_ptr<const T>` (R10 — never a hash map); the sampler; the promotion rule with its dilate-by-one-voxel invariant and its 8-column give-up; clone-on-write edits; per-object xxh3 digests computed once at construction. **The SDF is an integer plane and the reason is written down**: `Hasher::pod` static_asserts `has_unique_object_representations` and therefore excludes floating point (`engine/scene/src/world_hash.cpp:50-57`), so a per-object digest is what keeps `worldHash` O(objects) rather than O(bytes). | `engine/asset/include/luaug/asset/terrain.h`, `engine/asset/src/terrain.cpp` (new), `engine/asset/CMakeLists.txt`, `engine/asset/tests/terrain_tests.cpp` (new) | Pure data structure with tests; no caller. | none |
| **B2** | The mesher. Marching Cubes on the primal lattice over the sampler, emitting in one pass: an `asset::Mesh` of 48-byte `asset::Vertex` and u32 indices (the layout is fixed and static_asserted; every world pipeline hardcodes `strideBytes = 48` at `renderer_default.cpp:615,623,813`), a per-section material split, the collider triangle list for bricked columns, the height-sample block plus its `cNoCollisionValue` hole mask for the rest, LOD strides 1/2/4/8, and downward skirts of depth `voxelSize × stride`. **Step 1 of this step is verifying the four-below/four-above MC case against the table and pinning the diagonal to `(x,y)→(x+1,y+1)` in code rather than inheriting it.** | `engine/asset/include/luaug/asset/terrain_mesher.h`, `engine/asset/src/terrain_mesher.cpp` (new), `engine/asset/tests/terrain_mesher_tests.cpp` (new) | Free functions over numbers, testable headlessly. | none |
| **B3** | The cell format. `.lterrain`: magic `LGTF`, version 1, a `flags` u32 that must decode as exactly zero (`chunk.cpp:245-247`'s rule), cell x/z, voxelSize, cellSize, sampleCount, real min/max Y as f64, tile and brick counts, uncompressed size, compression byte; then two key-sorted directories and the zstd payload. Every count checked against a named ceiling **before** it is allocated against — `chunk.cpp:263-274`'s stated rule, "a bounds-checked reader is not a safe reader, because the allocation happens first". A key-sorted `index.json` beside it, mirroring `.lchunk`'s TOC. A round trip is byte-identical and that is a test. | `engine/asset/include/luaug/asset/terrain_cell.h`, `engine/asset/src/terrain_cell.cpp` (new), `engine/asset/tests/terrain_cell_tests.cpp` (new), `engine/asset/CMakeLists.txt` | Codec plus round-trip test. | none |
| **B4** | The DDA raycast. `asset::raycastField(const TerrainField&, core::RayD) -> std::optional<TerrainHit>`, f64 origin, exact. **It needs no physics**, which matters: `PhysicsSync::mirror()` is called from exactly one site gated on `paused && collision-view-on` (`engine/app/src/engine.cpp:2526-2535`), so a paused editor world with the wireframe closed holds no bodies at all, and `pickNearest` cannot help either (`PickHit` is `{instance, distance}` and every part is tested as its bounding box, `picking.h:50-100`). | `engine/asset/include/luaug/asset/terrain.h`, `engine/asset/src/terrain_raycast.cpp` (new), `engine/asset/tests/terrain_tests.cpp` | Free function. | none |

### Part C — The class and the mirror, `engine/scene` (L3)

| # | Step | Files | Green because | Goldens |
|---|---|---|---|---|
| **C1** | The IDL, and the generated files it moves. `Terrain : Instance`, `Module = "scene"`, `Storage = "Terrain"`, creatable. Properties `VoxelSize` (`ErrorKey = "scene.err.terrain_not_empty"`), `CellSize` (ReadOnly), `MinHeight`, `MaxHeight`, `MaterialPalette : Instance?`, `Source : Content` (`ContentKind = "Terrain"`), `CellCount` (ReadOnly). Methods `FillBall`, `FillBlock`, `ReadVoxels`, `WriteVoxels`, `HeightAt`, `Clear`, `Compact` — **all non-yielding**, so the `Async` biconditional (`api/schema.luau:449-461`) is satisfied vacuously. **No events**, deliberately: `EventDesc` carries a slot and firing is hand-written, and nothing counts declared events against firing sites, so a declared event nothing fires connects successfully and stays silent forever. Plus `Workspace.Terrain : Instance?` ReadOnly. **`buffer` is legal for a method parameter and is not legal for a property** — `scene::Value`'s thirteen-member variant is what `Inspector::enqueue` carries and its index is the wire tag (`value.h:35-43`); `MethodDesc` carries only name/yields/threadSafety/doc and every argument is hand-checked with `luaL_check*`. That is why the field is not a property. | `api/defs/instances.api.luau`, `api/defs/services.api.luau`, plus regenerated `engine/scene/generated/class_descriptors.gen.{h,cpp}`, `runtime/types/engine.d.luau`, `api/api-dump.json`, `docs/api/terrain.md`, `docs/api/workspace.md`, `docs/api/index.md` | Descriptors declare the accessors; the bodies land in C2 in the same commit or the link fails. | **api-dump, engine.d.luau, docs/api, class_descriptors — all four, byte-diffed by `scripts/gates/luau-check.sh`** |
| **C2** | Storage. `TerrainComponent` (the two sorted `shared_ptr` vectors, `voxelSize`, `cellSize`, `minHeight`, `maxHeight`, `palette`, `source`, `fieldRevision`); one row in `LUAUG_SCENE_POOL_LIST`; a hand-written `terrains()` accessor pair — **there is no `pool<T>()`**, `world.h:617-622` says the generic one was deferred and never built; `attachTerrainComponents`/`detachTerrainComponents` and seven `get*`/`set*` pairs. `heightTiles` and `bricks` have no property and will be rejected by `inertcheck` as unmapped names, so they go in its `StorageOnly` list with the argument that the field *is* the terrain and is read by the mesher and the collider builder through the pool rather than through a property. | `engine/scene/include/luaug/scene/components.h`, `.../world.h`, `engine/scene/src/native_accessors.cpp`, `tools/repo/inertcheck.luau` | The link closes. | none beyond C1 |
| **C3** | The hash. A hand-written contribution beside the `RigidBody`/`CharacterBody`/`InputAction` block at `engine/scene/src/world_hash.cpp:245-268`, folding each tile's and brick's precomputed digest **in key order**. Three rules land as code, not comments: hash the key's fields **separately** (a `{i32 x,y,z}` POD has padding and `Hasher::pod`'s static_assert refuses it — the guard working as designed); never hash a pointer, a refcount or an allocation address, so two runs that share a brick and two runs that clone it hash identically; and the *representation* is state — promotion happens exactly at an edit, demotion only at an explicit `Terrain:Compact()`, and `.lterrain` preserves it, so a save/load round trip is hash-preserving. | `engine/scene/src/world_hash.cpp`, `engine/scene/tests/world_hash_tests.cpp` | Hash of an absent component is the hash without it. | see § What moves goldens |
| **C4** | The mirror. `PhysicsSync` learns terrain: two `Static` bodies at most per cell inside `TerrainMinRadius` — a `HeightField` body always, a `TriangleMesh` body only where the cell has bricks. Terrain colliders are created **first** in `applyScene`'s walk so body-slot assignment does not depend on the interleave of terrain and parts. `mirror()` (`physics_sync.h:103`) is the call an editor-time sculpt rebuilds through. **Within a stroke, collect the affected keys, sort, then promote and write** — a brush that promoted bricks as it visited voxels would put allocation order into observable state. Rebuild budget is a **count per tick, never a millisecond budget**: streaming's wall-clock exemption (`engine/asset/src/streaming.cpp:22-30`, "nothing measured reaches the world hash") explicitly does not extend to a collider, because a collider *is* the world hash. | `engine/scene/include/luaug/scene/physics_sync.h`, `engine/scene/src/physics_sync.cpp`, `engine/scene/tests/physics_sync_tests.cpp` | A world with no `Terrain` mirrors exactly as before. | none |
| **C5** | **The `CollisionFidelity.Precise` graft.** `enums.api.luau:120-126` already declares Precise as "accepted and not yet implemented: this release collides against a hull and says so", and `ShapeType`'s own comment calls a triangle mesh "asset-pipeline work (M7)". A2 filled that hole; this step hands it to ordinary static `MeshPart`s. `MeshLibrary::Entry` gains `indices` beside `positions`; `fillEntry` fills it; the L6 hand-over at `engine.cpp:2861` widens from `setCollisionPoints` to `setCollisionTriangles`; `shapeOf` (`physics_sync.cpp:140`) resolves `Precise` to `TriangleMesh` for a **static** part and refuses with `physics.err.precise_needs_anchored` for a dynamic one, because `MeshShape::MustBeStatic()` is true. Same commit rewrites the enum's `Doc` and **inverts** `engine/scene/tests/physics_sync_tests.cpp:1785` ("Precise asks for the triangles and gets the hull, which the enum says it will"). **Memory cost, stated**: indices ride in every library entry, roughly 12 B per triangle beside the existing 12 B per vertex. **Cut line**: if F1 runs long this step moves to F2 — the honest consequence is that `ShapeType::TriangleMesh` ships with exactly one caller and the enum's Doc keeps lying. | `engine/render/include/luaug/render/render_world.h`, `engine/render/src/mesh_loader.cpp`, `engine/scene/include/luaug/scene/physics_sync.h`, `engine/scene/src/physics_sync.cpp`, `engine/app/src/engine.cpp`, `api/defs/enums.api.luau`, `engine/scene/tests/physics_sync_tests.cpp`, `i18n/en.json` | Every existing fidelity behaves as before; only `Precise` changes, and it changes to what its Doc promised. | **api-dump + docs/api** (Doc text); determinism traces only if a fixture uses `Precise` |

### Part D — Drawing it, `engine/render` (L4)

| # | Step | Files | Green because | Goldens |
|---|---|---|---|---|
| **D1** | `render::extract` gains a **third loop** over `world.terrains()`, after `meshParts()` (`render_world.cpp:660`) and `parts()` (`:775`), emitting one `DrawItem` per resident cell × section. **Not generated `MeshPart`s**, and the reason is mechanical rather than aesthetic: `attachPartComponents` adds a `RigidBodyComponent` to every `BasePart` with no condition (`engine/scene/src/native_accessors.cpp:97-105`) and `applyScene` has no skip — `canCollide`/`canQuery` are only flags on the resulting `BodyDesc` — so 225 terrain `MeshPart`s would be 225 phantom 64 m box bodies in the broadphase, 225 more instances in every `world.snapshot()`, and 225 rows in the Explorer. One entry per resident cell goes into `MeshLibrary` under a `terrain://cell/x,z` URN with `Entry::positions` left **empty**; materials are assigned **only through `RenderMaterial::setMaps`**, because assigning the four handles directly leaves `textureFlags` at zero and draws exactly like a material whose maps were never set — the D116 defect. `MeshLodRange` is deliberately unused: level is a residency decision here, not a per-draw projected-error one. | `engine/render/include/luaug/render/render_world.h`, `engine/render/src/render_world.cpp`, `engine/render/tests/render_world_tests.cpp` | A world with no `Terrain` extracts an identical `RenderWorld`. | none |
| **D2** | **The reserved-scheme guard, which is a real latent defect independent of terrain.** `failed_` is cleared only at `engine/render/src/mesh_loader.cpp:196`, `forget` never clears it, and `sync`'s `binary_search` at `:519` permanently blacklists any `MeshContent` the library does not hold. `luaug://primitive/*` survives today only because `syncPrimitives` runs first. One line skipping reserved URN schemes. **Worth landing on its own merits even though terrain never names a `MeshPart`.** | `engine/render/src/mesh_loader.cpp`, `engine/render/tests/mesh_loader_tests.cpp` | Strictly fewer blacklist entries. | none |

### Part E — Making it exist, `engine/app` (L6)

| # | Step | Files | Green because | Goldens |
|---|---|---|---|---|
| **E1** | `TerrainHost`: a **second `StreamingManager`** with its own index, its own 64 m cell size and its own callbacks. Required, not preferred: `StreamingManager::Entry::decoded` is a concrete `asset::Chunk` (`streaming.h:204`), `onChunkLoaded` calls `decodeChunk` unconditionally (`streaming.cpp:96-105`), and `materialize` is typed `f64(ChunkId, const asset::Chunk&)` (`streaming.h:135`). The "one `ChunkId`, one owner" refusal (`streaming_host.cpp:95-100`) cannot fire because it is a different index. Everything above the decode is reused unchanged: score by squared distance, 1.25× evict hysteresis, ties broken by `ChunkId`, unbudgeted eviction before loading, terminal failure, `IoPriority` banding. Meshing runs on the `jobs` pool and **commits at FrameStart sorted by `ChunkId`** — the rule `drainContacts` and the sorted `collectActiveBodies` already follow. `ShapeDesc::pointsRevision`'s pattern is reused verbatim as the geometry-identity counter for a re-meshed cell, so `sameShape` (`physics_sync.cpp:52-56`) already decides to rebuild and already leaves an unchanged cell alone. **Ownership of a bricked cell's triangles for the duration of `applyScene`'s synchronous walk is explicit**, since no span may outlive the call and a worker must not re-mesh that cell underneath it. | `engine/app/include/luaug/app/terrain_host.h`, `engine/app/src/terrain_host.cpp` (new), `engine/app/CMakeLists.txt`, `engine/app/tests/terrain_host_tests.cpp` (new) | New type, no call site yet. | none |
| **E2** | Wiring, at existing lines rather than new phases. Terrain streaming pump beside `streaming.pump` (`engine/app/src/engine.cpp:2126`). Meshing commit at the mesh loader's own safe point (`:3066-3069`, command list open and no pass) — **before `render::extract` at `:2930`**, which is not optional: anything registered after extraction is invisible for exactly one frame, recorded as a flicker nobody could reproduce. Collider commit beside the one existing L6 geometry hand-over at `:2861`. The `TerrainMinRadius`/`TerrainLoadRadius` knobs stop being reserved and start meaning their names — already wired end to end: `api/defs/services.api.luau:439,449` → `class_descriptors.gen.cpp:1470-1481` → `EngineState::streamingTerrain*Radius` (`world.h:164-165`) → `collectFoci` writing `focus.layers[2]` (`streaming_host.cpp:243,279`). | `engine/app/src/engine.cpp`, `engine/app/include/luaug/app/engine.h` | Null host, no-op. | none |
| **E3** | The budget split. Two `StreamingManager`s are two frame budgets that do not know about each other and can overrun together. One `StreamingBudget` split explicitly at the host, **terrain first**, because a missing collider is a fall-through and a missing prop is not. | `engine/app/src/streaming_host.cpp`, `engine/app/src/terrain_host.cpp`, `engine/app/src/debug_overlay.cpp` (the per-layer chunk-state overlay at `:5996-6056` gains the terrain index) | Additive. | screenshot only |
| **E4** | **Terrain streams in the editor, where nothing else does.** `partitionScene` early-returns for an editor run (`engine.cpp:892-913`, "the editor holds the whole world because holding it is what editing it means"), and terrain's field cannot be wholly resident. A stroke on a non-resident cell **refuses with a status message** rather than silently doing nothing, and `Editor::authorable`/`canParentInto`'s generated-subtree refusal (`editor.cpp:866-889`) is told about terrain explicitly. A person can fly somewhere and find the ground temporarily unsculptable; that is a user-visible consequence of an architectural decision and it belongs in the status line, not in a brief. | `engine/app/src/engine.cpp`, `engine/app/src/editor.cpp`, `engine/app/include/luaug/app/editor.h`, `engine/app/tests/editor_tests.cpp` | Refusal is the conservative branch. | none |

### Part F — The brush, `engine/app` editor

| # | Step | Files | Green because | Goldens |
|---|---|---|---|---|
| **F1** | `Editor::Tool { Select, Sculpt, Paint }` and `driveSculpt(authored(), terrainDirty)` called at `engine.cpp:2041` **immediately before `driveGizmo`**, returning `true` to claim the pointer — which is what suppresses `resolvePick` at `:2052` and is the only thing that stops a sculpt click also re-selecting whatever is behind the brush. `editor.touch()` follows exactly as `gizmoTook` does at `:2044`, because a drag changes the document without ever producing an `EditorCommands` field. **Not a fourth `GizmoMode`**: `snapStep` indexes `f32 m_snapStep[3]` by the raw enum with no bounds check (`editor.cpp:2583`, `editor.h:1913`). A tool switch mid-stroke refuses the way `setGizmoMode` refuses mid-drag (`editor.cpp:2564-2572`). The stroke writes the voxel field **directly** at the safe point — legal, same place, after `applyPending` — because `Inspector::enqueue` carries a `scene::Value` and the variant has no buffer; the consequence is that the change does not appear in the Write Log and does not go through `setProperty`'s refusal, and **both are written down rather than discovered**. | `engine/app/include/luaug/app/editor.h`, `engine/app/src/editor.cpp`, `engine/app/src/engine.cpp`, `engine/app/tests/editor_tests.cpp` | Tool defaults to `Select`; `driveSculpt` returns false. | none |
| **F2** | Stroke interpolation and the preview, both as free functions over numbers in the `reference_grid.h` / `chunk_overlay.h` / `skeleton_overlay.h` tradition — "a bug that reproduces by dragging is one nobody fixes twice" (`picking.h:160-172`). `app::strokeStamps(lastPoint, thisPoint, radius, spacing)`: the pointer `driveSculpt` reads was recorded by `drawViewportBody` during the **previous** frame's `overlay->render` (`engine.cpp:3224`, latency stated at `:1204-1207`) — invisible for a manipulator, which solves against the drag's start; a dotted line for a brush. `app::drawBrushRing(centre, normal, radius, DebugDraw&)` submitted beside `submitGizmo` at `engine.cpp:3060`, **after `debugDraw.rebaseTo` at `:3048`** and therefore already camera-relative, because `rebaseTo` subtracts in f32 and a world-space submission costs half a millimetre at four kilometres on the one thing being placed precisely. Lines only — `DebugDraw` is a line list and stays one. A shaded falloff decal is **not expressible** and that is stated, not attempted: it needs a second pipeline (the debug one has no depth state at all, `debug_renderer.cpp:72-83`) or an editor-appended `DrawItem`, and no hook exists after `extract`. | `engine/app/include/luaug/app/brush_overlay.h`, `engine/app/src/brush_overlay.cpp` (new), `engine/app/src/engine.cpp`, `engine/app/tests/brush_overlay_tests.cpp` (new) | Free functions; drawn only when the tool is active. | screenshot |
| **F3** | Chrome. A fourth toolbar button beside the three `modeButton` calls (`debug_overlay.cpp:3531-3533`), a `View > Terrain Wireframe` toggle beside `showGrid`/`showCollision`/`showChunkGrid`, and radius/strength/material persisted in the `tools` object of `.luaug/editor.json` (`editor.cpp:459-482`) — "what a person set, rather than what they did". **The modifier budget is nearly full and this is a decision, not a detail**: the wheel changes fly speed over the image (`:3693`), Alt suspends snapping (`:5844`), Ctrl is additive pick (`:3835`), Shift is fly sprint (`:3713`), right-drag is the latched camera. Proposal, needing the owner's eye: while `Tool == Sculpt`, wheel = radius, Shift+wheel = strength, Alt = carve, and fly speed moves to Ctrl+wheel. | `engine/app/src/debug_overlay.cpp`, `engine/app/src/editor.cpp`, `engine/app/include/luaug/app/editor.h` | Additive toggles. | screenshot |
| **F4** | Undo and dirtiness. One undo step per stroke falls out of machinery that already works: `beginGesture` mints an id with the top bit set (`inspector.cpp:621`), `coalesceKeyFor` returns it outright (`:627`), and `UndoStack::record` returns **before** `world.snapshot()` when the key repeats (`editor.cpp:634`) — so a 60-frame drag is one snapshot, which `editor_tests.cpp:2003` already pins for the gizmo. The existing `history().record` at `engine.cpp:1185` is gated on `pendingCount() > 0` and would record nothing, so it gains a sibling `if`. New `EditorCommands` fields (`sculptBrush`/`sculptRadius`/`sculptMaterial`) go in `mutatesWorld()` — a world-changing field omitted from that hand-written list silently loses work on quit. | `engine/app/include/luaug/app/editor.h`, `engine/app/src/editor.cpp`, `engine/app/src/engine.cpp`, `engine/app/src/debug_overlay.cpp`, `engine/app/tests/editor_tests.cpp` | Existing coalescing, existing stack. | none |

### Part G — Script surface and documentation

| # | Step | Files | Green because | Goldens |
|---|---|---|---|---|
| **G1** | Seven rows in `InstanceMethodBinding` (`engine/script/src/instance_binding.cpp:974`), and **the pinned count at `engine/script/tests/instance_binding_tests.cpp:543` moves 71 → 78 by hand**, with the history paragraph extended. `declaredWithoutBinding == 0` at `:503` is what makes a declared-but-unbound method impossible to ship. Three i18n keys or `i18nlint` fails: `scene.err.terrain_not_empty`, `scene.err.terrain_cell_not_resident`, `render.err.terrain_palette_too_large`. `ContentKind::Terrain` on `content_tree.h:35` and its six switch arms. | `engine/script/src/instance_binding.cpp`, `engine/script/tests/instance_binding_tests.cpp`, `i18n/en.json`, `engine/app/include/luaug/app/content_tree.h`, `engine/app/src/content_tree.cpp`, `engine/app/src/debug_overlay.cpp`, `api/schema.luau` (`ContentKind` facet) | The count test fails loudly and correctly until the row lands. | none |
| **G2** | The absence paragraphs move **in the same commit that makes them false**, not before — `docs/api-design.md:479-492` says that paragraph "stays until each one ships rather than being edited in advance". `docs/manual/roblox/not-here.md:26` ("Terrain \| **Planned**, next phase. The open question is authoring.") goes. The roadmap gains the F1 detail section. | `docs/api-design.md`, `docs/manual/roblox/not-here.md`, `docs/roadmap.md`, `PROGRESS.md`, `docs/briefs/f1-kickoff.md` | Docs. | doc-lint |

### Part H — Gates

| # | Step | Files | Green because | Goldens |
|---|---|---|---|---|
| **H1** | **The seam-continuity gate — the thing that converts "is the hybrid a defect factory" from an argument into a measurement.** A fixture world with a cave crossing both a cell boundary and a brick/height boundary; one downward ray per column; assert every ray hits, that the physics hit height matches the sampler within the stated quantization bound, and that the rasterised render mesh matches the same. Plus conformance specs and a determinism trace. | `tests/conformance/terrain/{sculpt,sampling,streaming}.spec.luau` (new), `tests/determinism/terrain/{init.luau,scenario.json,trace.windows.txt,trace.linux.txt}` (new), `engine/app/tests/terrain_seam_tests.cpp` (new) | New tests. | **new traces recorded on both tiers** |
| **H2** | Benches, soak and baselines. `tests/bench/terrain_sculpt` (a scripted `FillBall` drag) and `tests/bench/terrain_stream` (a fly path across cell boundaries). The five existing benches unchanged within noise. `streamsoak` and `openworld_soak` ceilings re-measured. The A3 shape-build numbers promoted from measurement to recorded baseline. | `tests/bench/terrain_sculpt/{init.luau,scenario.json}`, `tests/bench/terrain_stream/{…}` (new), `tests/streamsoak/*`, `docs/perf-baselines.md` | New scenarios. | perf-baselines |
| **H3** | The flagship gets terrain. `examples/10-open-world`'s 18,496 authored 32 m tiles are the *thing terrain replaces*, and swapping a representative region for sculpted ground is the only end-to-end proof there is. Capture goldens and screenshots re-recorded. | `examples/10-open-world/**`, `tests/rendercapture/*`, `tests/screenshots/*` | Last step, after everything it exercises. | **capture hashes + screenshots** |

## What moves goldens

Stated up front so no re-record is a surprise, and separated into one-time and
recurring.

- **`api-dump.json`, `runtime/types/engine.d.luau`, `docs/api/*`, and
  `engine/scene/generated/class_descriptors.gen.{h,cpp}`** — at C1 and again at
  C5 (the `Precise` Doc rewrite). All four are byte-diffed by
  `scripts/gates/luau-check.sh` against copies taken **before** regenerating,
  and the descriptor set is discovered by `find`, so nothing in the gate needs
  editing.
- **Every determinism trace, on both tiers, at tick zero.** This is E9 Finding
  4's shape and it is worse than E9's, so it is called out rather than
  discovered: `World::worldHash` skips a property only when `get == nullptr` or
  `hostFact` is set (`world_hash.cpp:271-285`) — **`readOnly` is not skipped**.
  So `Workspace.Terrain`, a read-only instance reference with a getter, adds a
  hashed property name and value to *every* world that has a `Workspace`, which
  is all of them. `churn`, `character`, `example01` and `ragdoll` all move, on
  Windows and on Linux. It is **one-time**, not recurring, and marking the
  property `HostFact` to avoid it would be a lie — terrain is world state.
  The alternative is to not add the property and let scripts write
  `workspace:FindFirstChildOfClass("Terrain")`; it is cheaper and it is worse,
  and the trade is recorded here so the owner can take it.
- **Capture-stream hashes and screenshots**, at F2 (the brush ring), E3 (the
  overlay) and H3 (the flagship). Not before: nothing in Parts A–D draws.
- **`docs/perf-baselines.md`**, at A3, A4 and H2.
- **What must NOT move**: `.lchunk` bytes, `ChunkFormatVersion`,
  `PartitionRules`, `.luaug/partition/<hash>/` caches, `ChunkLayerCount`, and
  every `StreamingService` property. That is the hybrid's entire structural
  argument, and a diff touching any of them means the design drifted.

**And the trap E9 Finding 3 already paid for**: a stale binary writes the *old*
hashes, and a trace that did not move is the same picture as a trace that was
never recomputed. Check the build's exit code, not the diff.

## Gate (definition of done)

- [ ] **A person drags a brush across the ground and the ground changes**, at
      60 Hz with no dotted line across a fast drag, and one Ctrl+Z takes the
      whole stroke back. The last clause is `editor_tests.cpp`'s `DragRig`,
      headless. The first is a person at a window and nothing automated
      replaces it.
- [ ] **A cave has a roof you can stand under and a floor you can stand on**,
      and both collide — asserted by the seam-continuity gate (H1), per column,
      across a boundary that is simultaneously a cell boundary and a
      brick/height boundary.
- [ ] **The render mesh and the collider are the same triangles where the cell
      has no bricks**, asserted as an equality rather than a tolerance, with the
      diagonal pinned to Jolt's `(x,y)→(x+1,y+1)`.
- [ ] **A raise-ground stroke fires no spurious `Touched`.** A character stands
      on a cell, the cell is sculpted for 60 frames, and the contact-pair diff
      reports zero `Began` and zero `Ended`. Break-verified by removing the
      `SetHeights` path and watching it fire 60 times.
- [ ] **A cell that promotes whole degrades into pure voxel and says so**, and
      the slope survey (A5) records what fraction of the flagship's ground does.
- [ ] **A `.lterrain` round trip is byte-identical**, keys in order,
      quantization deterministic, and every count checked against its ceiling
      before allocation.
- [ ] **A save/load round trip is hash-preserving**, representation included.
- [ ] **`worldHash` is O(objects), not O(bytes)**, asserted as a timing ratio
      across two worlds differing only in resident cell count.
- [ ] **Two worlds built by the same call sequence are bit-identical after 3000
      ticks with a scripted sculpt in the middle**, and
      `tests/determinism/terrain` says so on both tiers.
- [ ] **Terrain costs nothing to a scene that does not use it.**
      `tests/bench/{physics1k, churn10k, instances500, crowd50, platforms200,
      ragdoll10, sockets200}` unchanged within noise.
- [ ] **`Enum.CollisionFidelity.Precise` collides against the triangles**, and
      `physics_sync_tests.cpp:1785` asserts the opposite of what it asserts
      today. *(Cut to F2 if F1 runs long; if cut, the enum's Doc must keep
      saying it is not implemented.)*
- [ ] **A `MeshContent` naming a reserved scheme is never blacklisted** (D2).
- [ ] **The shape-build bench exists and its numbers are in
      `perf-baselines.md`** — `MeshShapeSettings::Create`,
      `HeightFieldShapeSettings::Create`, a `SetHeights` sub-rect, and a full
      `updateBody` round trip. Nothing in this milestone may cite a cost this
      table does not contain.
- [ ] **The `MeshUsage::Dynamic` hazard is measured, not assumed**, and the
      answer is recorded whichever way it went.
- [ ] **`scripts/localgate.ps1` green on every stage, Linux included.**
- [ ] **The end to end, by hand**: open the flagship, sculpt a valley, dig a
      cave under it, walk into the cave, save, reopen, and have the cave still
      be there. The one row nothing automated closes.

## Findings

Written as each step answers something, before the step below it starts. This is
the section E9's brief carries for the same reason: so the same discovery is not
paid for twice.

### A5 — the slope precondition, measured (2026-08-27)

`tools/repo/terrainsurvey.luau`. Assumption 1 asked whether the hybrid's
`|dH| <= voxelSize` precondition survives real ground. **It does, and the curve
that matters is not the one the assumption expected.**

**The flagship's ground: 0.0000% of sample pairs are steep, worst slope 6.3°.**
Nowhere near the 45° limit, with two orders of magnitude of margin. Stated with
its bias rather than as a triumph: `examples/10-open-world` is the only analytic
ground in this repository and it is *deliberately* gentle — `generate_world.luau`
refuses to write a world whose step over a 16 m tile exceeds the character's
`AutoStepHeight` of 0.6 m. It is the gentlest ground here by construction, so
on its own it answers a weaker question than the one asked.

**A cliff promotes the cliff, not the terrain around it.** Sweeping a 4 m band
from a 1 m drop to a 64 m drop, the steep fraction rises and then *saturates* at
16.667% — which is exactly `(4 m band / 16 m extent) × (2 of 3 sampled
directions)`, so the measurement is self-consistent. The fraction is bounded by
**how much of the world is cliff**, never by how steep the cliff is. That is the
property the hybrid needs and it holds.

**The real finding inverts the intuition, and it is about WIDTH rather than
steepness.** Promotion dilates outward until the boundary is shallow again and
gives up at 8 columns — which at a 0.5 m voxel is **4 metres of sustained 45°+
slope**. Sweeping a 16 m drop by the width it falls over:

| band | steep columns | outcome |
|---|---|---|
| 0.5 m — a near-vertical cliff | 1 | height layer survives |
| 4 m | 8 | survives, exactly at the limit |
| 6 m | 10 | whole cell bricks |
| 16 m — a 45° mountainside | 18 | whole cell bricks |

**The dramatic case is the cheap one.** A vertical cliff is one steep column. A
long moderate ramp is what bricks a cell, and a mountainside is a long moderate
ramp — much longer than four metres. So assumption 1's worry ("real mountainsides
are steeper than 45° routinely") is right, and it lands on a threshold nobody had
put a number to.

**Three consequences for Part B, and none of them is "the design is wrong".**

1. **The failure is graceful and that was the design's own claim.** A cell that
   gives up converts to pure voxel, which is the baseline the hybrid was compared
   against. It loses the saving; it does not break.
2. **8 columns must be a tunable, not a constant.** The number decides how much
   of a world pays for the second encoding, it is worth different values on a
   rolling island and an alpine map, and nothing about the algorithm needs it
   fixed. It goes in the `.lterrain` cell header so a world records the value it
   was built with.
3. **The claim to make in ADR 0067 is narrower than the one the design made.**
   Not "the height layer carries the majority" — that is a statement about worlds
   nobody has authored yet. What is measured is: *the height layer carries every
   surface shallower than 45°, and a steep region costs its own area plus one
   dilation column, until that region is sustained for longer than the give-up
   width.* That is falsifiable and it is what was tested.

### A2 — the two static shapes, built (2026-08-27)

`ShapeType::HeightField` and `ShapeType::TriangleMesh`, their `buildShape` cases,
`IPhysics3D::updateHeightField`, the forced-Static rule, and six cases in
`engine/physics/tests/terrain_shape_tests.cpp`. **Three things went wrong on the
way and each is worth the next person's time.**

**A height field's editable range is baked when the shape is built**, and this is
the one that would have cost a milestone. Jolt quantises samples across
`[min(mMinHeightValue, samples…), max(mMaxHeightValue, samples…)]` and
`SetHeights` clamps into that mapping for ever after. A field built from flat
samples has a zero-wide range, so **an edit clamps back to the height it already
had, returns true, and moves nothing.** Two tests — dig a pit, raise a plateau —
both left the ground exactly where it was with the call reporting success. The
plan's own draft of `ShapeDesc` listed `heightMin`/`heightMax` and they were
dropped as redundant with `size` while writing it; they are not redundant, they
are the reservation, and they are back with the reason on them.

**A `NotifyShapeChanged` inside a `BodyLockWrite` is a real deadlock**, not a
theoretical one. `SetHeights` changes the shape's local bounds and nothing
recomputes the body's world bounds, so a pit dug below the old minimum falls
outside what the broadphase culls against and the collider quietly has a hole in
it — hence the notify. Taking it while still holding the body lock is a
lock-order violation Jolt detects, reports, and then hangs on: a test process
alive for ten minutes at 0.015 seconds of CPU.

**And it was invisible.** `assertFailedImpl` reports Jolt's file, line and
expression through the message catalogue, and the physics tests loaded no
catalogue — so two asserts arrived as `[i18n:missing:78f42142]` twice, which says
an assert happened and nothing about which. The target now gets
`LUAUG_TEST_CATALOG` like every other test binary, and the assert named the file
and the line on the first run afterwards.

The third was mine and not the engine's: a test quad wound so its normal pointed
down. A `MeshShape` is one-sided, so the slab was there, facing away, and the
cube fell through the floor it should have landed on.

### A3 — what a terrain collider costs, measured (2026-08-27)

Full numbers in `docs/perf-baselines.md`. **ADR 0066's central claim survives
with room to spare, and a second number changes a design decision.**

`SetHeights` on a 16² rectangle costs **0.0078 ms** against a rebuild's
**0.457 ms** at 128² samples — 58× — and the gap widens to 217× at 256², because
the two scale differently: an in-place edit is O(the rectangle) and a rebuild is
O(the field). That is the difference between a brush that drags at any framerate
and one that gets slower as the world gets more detailed.

**A triangle mesh costs 12.1 ms for 32,258 triangles**, which is most of a frame.
So a cell that gives up on the height encoding and converts to bricks **cannot
have its collider rebuilt synchronously during a drag**, while a height-encoded
one costs eight microseconds and can. The gap across that boundary is about
1,500×, and it lands exactly on the give-up width A5 already concluded must be
tunable.

The consequence for Part C: **"the failure mode is the baseline" is true of
correctness and false of cost.** A bricked cell needs a budgeted, off-frame
collider rebuild that a height-encoded cell does not, and that path is now part
of the milestone rather than a detail of it.

### B1 and B2 — the field and the mesher, built (2026-08-27)

**One deviation from the plan, with its reason and its price.** B2 says Marching
Cubes; what was built is marching **tetrahedra**. Every property ADR 0067 claims
survives — vertices land on edge crossings by linear interpolation, so
`sd(p) = p.y − H(x, z)` still puts the vertical crossing at exactly `y = H`, and
there is a test asserting that height is exact rather than approximate. What
changed is how a cell is subdivided first.

Three reasons, in the order they decided it. **Marching cubes needs a 256-entry
triangulation table**, which is somebody else's work to vendor — R5 and R6 make
that an ADR rather than an `#include`, and deriving it from the fifteen base
cases under the cube's symmetry group is real work with a silent failure mode. **A
tetrahedron has no ambiguous face**, so the case where two neighbouring cells
disagree about whether a surface connects — a crack, which in terrain is a hole
somebody falls through — does not exist. And **the winding is derived from the
field's gradient rather than from a table**, so "which way does this face" stops
being something a table can have backwards; that exact bug had already cost this
milestone a failing test in the physics seam an hour earlier.

**The price is about twice the triangles, and worse-shaped ones.** Against A3's
numbers that matters for a bricked cell's collider and not for a height-encoded
one, whose collider is a height field. If the count becomes the problem the fix
is the derived marching-cubes table and it is a change to one file.

Nine cases, 6,444 assertions: no surface where there is no sign change; a flat
field meshing at exactly its height and facing up; **watertightness asserted as
edge use** — no edge used more than twice, which is the non-manifold failure —
a spherical cave inside solid ground meshing as a sphere, the collider being the
same triangles as the render mesh at the level actually meshed, a coarser stride
being the same plane with fewer triangles, and the same field meshing to the same
bytes twice.

Two `std::map`s rather than hash maps, in the field and in the mesher's vertex
cache, and both for R10: the walk order decides the vertex order, which decides
the mesh's bytes, which reaches a content hash.

Clang caught the last defect in each of the two commits, and it was the same one
both times — `doctest::Approx` takes a `double` and an `f32` compared against it
is a promotion MSVC does not diagnose.

### B3, B4 and Part C — built (2026-08-27)

The `.lterrain` format, the field raycast, the `Terrain` class, its storage, the
world hash's contribution and the collider mirror. Four things worth carrying.

**The raycast's first version snapped to the nearest lattice sample**, which
makes the field a step function one voxel wide — so the bisection converged on a
step's edge and every hit landed half a voxel out. On a flat field at y = 4 it
reported 4.25, which reads as a rounding detail rather than a defect. Trilinear
interpolation fixes it and buys something the snapped version could not have: the
raycast now agrees with the MESHER, which interpolates along an edge for the same
reason, so a hit sits on the triangle that was drawn there.

**Promotion allocated four bricks where one carried the cave.** The examined
column runs a brick above and below the brush, and the rest only repeated the
height layer. `compact` reclaimed them — which is how it was found, by a test
that expected zero reclaims and got three — but paying four times the memory on
every dig and relying on a verb nobody calls automatically is the wrong shape. A
partly-bricked column is legal, so those levels are simply not created.

**Two gates earned their keep inside an hour.** `inertcheck` refused
`fieldRevision` and `cellSize` as stored-and-unread, correctly: the mirror that
reads them was the next step, so `fieldRevision` was removed until C4 brought its
reader and `CellSize` carries `Inert` naming what will act on it. And the
method-coverage test failed on the very next build after five methods were
declared in the IDL — it counts declared against bound, which is `Inert` for a
method, and it caught them before anything could reach a script and find a name
that answered nothing.

**A stale binary nearly produced false traces.** `ninja` failed inside the
container and the replay ran the previous build anyway, recording Linux traces
that predated the property that had moved them. Caught because the Clang errors
were read rather than the recording being trusted — which is D040's lesson
arriving through a different door.

## Risks entering F1

1. **The slope precondition cascades.** Measured at A5 before anything depends
   on it, and if it fails the milestone is still buildable — the hybrid degrades
   per cell into the pure-voxel design it was judged against — but the ~600
   extra lines buy nothing on that world.
2. **`SetHeights` drifts.** It "requires decompressing and recompressing a
   border of size `mBlockSize` in the negative x/y direction so will cause some
   precision loss" (`HeightFieldShape.h:221`), and it clamps silently to the
   range fixed at construction (`:227`, and the quantize-and-`Clamp` at
   `.cpp:1102`). **Drift in a collider is drift in the world hash.** The field
   stays authoritative and the collider is rebuilt from it on a fixed schedule;
   the brush-footprint-to-block-rect rounding is specified in B2, not left to
   the caller. And sculpting past `MaxHeight` mid-stroke **refuses the stamp**
   rather than clamping silently — the failure mode of not choosing is a
   mountain that stops colliding at a height with no error message.
3. **`SetHeights` is not thread-safe against parallel queries** — Jolt's own
   header says so. It runs at the FrameStart safe point and nowhere else.
4. **Vertical density behind one `(x, z, layer)` address is unbounded.** The
   whole no-`y`-on-`ChunkId` argument rests on a bricked column being sparse and
   nothing enforces it; `MaxTerrainBricks` is a decode-time ceiling, not a
   residency policy. **A residency policy is owed in B3**, or the honest answer
   is that a `y` arrives in a later milestone — deferred rather than avoided.
   The per-layer chunk overlay is a 2-D map and cannot show it.
5. **A heap-owning, refcounted component in a pool whose stated virtue is being
   POD.** `ScriptComponent::source` already bent that rule toward a deep copy;
   this bends it toward a shared immutable, which is a different contract with a
   different failure mode. If anybody ever mutates through a
   `shared_ptr<const>`, terrain breaks silently and an undo step starts changing
   frozen state.
6. **Ownership is split four ways and needs one named owner.** The field and
   mesher are `asset` (L2), the component and class are `scene` (L3), the
   extract loop is `render` (L4), and the meshing pump, collider hand-over and
   jobs pool are `app` (L6). That is legal and it is why `Module = "scene"`
   works. **`TerrainHost` owns the dirty set, and nothing else computes one** —
   four modules touching one subsystem with no stated owner is how a dirty set
   ends up computed in two places that disagree.
7. **Undo is of the world, not the disk** (`editor.h:645-650`), and terrain's
   bulk lives in `content/terrain/`. Sculpt, save, Ctrl+Z, and the world no
   longer matches the files. Same promise the editor already makes; first
   subsystem where the divergence is megabytes rather than a filename, so the
   status line has to say so.
8. **`basis_zstd` is inside `if(EXISTS …/basisu_transcoder.cpp)`** in
   `third_party/CMakeLists.txt:568`. `luaug_asset`'s link to `basis::transcoder`
   is unconditional, so basis is always vendored in practice — but a terrain
   codec that assumes `<zstd.h>` inherits that conditional, and it should be
   named in ADR 0067 rather than found by a contributor with a partial checkout.

## Subagent plan

**None.** One flow across five modules' seams, which `MASTER_PROMPT.md` §7 names
as orchestrator-only: the interfaces cannot be frozen before the thing is built,
because the milestone's content *is* the interfaces — the physics shape seam,
the sampler, the mesher's collider outputs and the terrain streaming callbacks.
The one exception §7 would sanction is **after A2 lands and `physics/types.h`
compiles**: B1/B2 (the field and the mesher, pure functions over numbers with no
engine dependency) and A3/A4 (two independent measurements) are four
independent pieces of work against frozen headers. An adversarial reviewer on
the `ShapeDesc` widening, briefed to attack R17 leaks, spans outliving their
call, and unordered iteration reaching the hash, is worth its cost.

---

# N1 — Two Worlds, One Match

Opened after F1 lands. The judged design is the declared wire schema and is
inherited; what follows is the milestone shape, not a re-derivation.

**Goal.** One binary, four postures decided at runtime: solo, host-and-play,
dedicated server (`--headless` with a server script), and replica. A script
writes `if NetworkService.Authority then …` and that branch is present *and
taken* in solo — a gameplay branch ("do I decide this"), never a configuration
branch ("am I networked"), which is the distinction `roadmap.md:1301-1302`
draws.

**The one thing worth restating, because it will otherwise be re-proposed in six
months.** The obvious delta source is `scene::ChangeQueue` and it cannot work:
`PhysicsSync` writes transforms straight into the component under a comment
naming itself *the QUIET write* (`engine/scene/src/physics_sync.cpp:963-966`),
and `World::setProperty` enqueues only for a subscriber
(`engine/scene/src/world.cpp:626-634`). **The most-replicated fact in any game
never reaches the queue.** Replication therefore reads state and diffs it; it
does not listen. That paragraph, with those two citations, goes in ADR 0069
itself.

**Parts, in compile order.** (A) ADR 0069 (the wire schema and the replication
model) and **ADR 0070 narrowing ADR 0035** — that ADR says "the engine opens no
port in any profile", and a dedicated server contradicts the sentence while
honouring its reason; the narrowing must be specific enough that a *service* can
never be the thing that opens a socket, which is ADR 0041's rule for input.
Plus the bench nobody has: `extract` / `fieldHash` / diff / encode / send,
reported separately over `churn10k`'s scene, before any send rate or interest
radius is trusted. (B) `api/wire/{schema,protocol.wire,state.wire,init}.luau`,
`api/generator/gen_wire.luau`, `tools/repo/wirecheck.luau`, and the
`luau-check.sh` freshness rows — field ids are **permanent**, reuse refused, a
removed id goes to a `Retired` list with the version it died in. (C)
`engine/replication/` as two targets at L4 on the `physics/` pattern, gated by
`option(LUAUG_ENABLE_REPLICATION … OFF)`, with `app` holding the one factory
switch (ADR 0023). (D) `NetworkService` and `Player` in the IDL with
`Module = "scene"`, backed by three `EngineState` fields whose defaults *are*
the solo truth. (E) `replication->receive()` at `engine.cpp:2133` and
`replication->send()` after the tick loop, both no-ops through a null pointer.
(F) The gate: **`runTwoWorldsGate` inverted.** It already runs two `WorldHost`s
and two Luau VMs in one process with a pixel differential proving the two worlds
render *different* images (`engine/app/include/luaug/app/two_worlds.h:66`, whose
own header names networking as the second caller). Driving world B as world A's
replica over a loopback `createEnetTransport()` makes the acceptance test that
they render the **same** one. Headless, deterministic, no network, and the
harness exists.

**Grafts that are not optional.** The two-line change at `physics_sync.cpp:212`
treating a replicated instance in a replica world as `driven` — its body becomes
Kinematic, and `writeBack` skips kinematic bodies (`:948-958`), so the solver
neither fights the incoming deltas nor overwrites them, with no branch in game
script. And despawn uses the streaming husk contract verbatim: an instance a
script still holds is reparented to nil and reported through
`InstanceStreamedOut` (`streaming_glue.h:9-14,49-57`) — a replica losing
interest and a chunk being evicted are the same event and the API already has
the word for it.

**Two defects to fix before the design trusts itself.**
`ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT` is never set in `flagsFor`
(`engine/net/src/enet_transport.cpp:40-54`), so any payload over ~1 MTU sent as
`UnreliableSequenced` takes the reliable-fragmented branch
(`third_party/enet/peer.c:135-145`) — head-of-line blocking, which is exactly
what the mode exists to avoid, and no test catches it because the loopback case
sends four bytes. That is **our** code, R13-safe, and it needs a 4 KB loss test.
And `ITransport` has never had a production caller, so N1 owes a deterministic
loss-and-reorder decorator over the six virtuals, seeded from `core::Pcg32` and
never from a clock — the tests themselves say a bug that only appears with loss
or reordering is invisible today (`transport_tests.cpp:6-10`).

**Goldens.** `NetworkService` is `Service`-tagged, so `registerServices` sweeps
`ClassFlags::Service` and instantiates it at boot (`services.cpp:1097-1133`) —
plus one `Player` in solo. **Every determinism trace moves at tick zero, again.**
One-time; `Authority`/`Topology`/`ServerTick` are `HostFact` and that is what
stops it recurring.

**The roadmap sentence that is false and stays false.**
"A solo game compiles without `engine/net`" — `add_subdirectory(engine/net)` is
unconditional (`CMakeLists.txt:75`), `net_api` is a hard `DEPS` entry of both
`luaug_script` and `luaug_app`, `services.h:22` holds a `unique_ptr<net::AsyncClient>`
by value in a public header, and `dev_control.cpp:7` uses `net::WebSocketClient`
for the ADR 0035 connection. **The sentence must be corrected to name
`engine/replication` in the same commit that opens N1**, or somebody discovers it
on the first build.

**Deliberately not in N1:** rollback (ADR 0025 records level B rather than
enforcing it, and `CROSS_PLATFORM_DETERMINISTIC` is OFF at
`third_party/CMakeLists.txt:440`), a public protocol commitment, `RemoteEvent`,
`NetworkOwnership`, `Team`. Channel 3 is reserved and named in the wire schema so
the numbering cannot shift, and nothing more.

**The one thing N1 must state honestly and F1's judge caught a rival design
overclaiming:** the replica's hash is **not** the server's and must not be
compared to it — interest management gives the replica a strict subset. The
per-baseline checksum catches apply bugs, which is what it can honestly catch,
and it cannot detect simulation divergence.

---

# F2 — Particles and Decals

**Blocked on one human decision and nothing else: ADR 0071, unfreezing exactly
one thing in the RHI.** `DepthStencilAttachment` gains a read-only flag, or
`ICmdList` gains a copy — one field or one call, and ADR 0037 makes it a human
decision either way. Five callers wait on it: soft particles, projected decals,
water foam, SSAO's second half, and anything else that wants to know what is
behind a fragment.

`ParticleEmitter` needs almost nothing else: the sorted blended pass (M4.5),
instanced draws with `VertexBufferLayout::perInstance` (ADR 0043) and the
compiled texture path (M7) all exist. Decals are **projected, never per-face**
by human decision 2026-08-21, and the canonical input has existed since M5 —
`Workspace:Raycast` returns `Position` and `Normal`, so
`CFrame.lookAt(hit.Position, hit.Position + hit.Normal)` plus a size is the
placement, with no parent part and no face to choose. Whatever else the class
grows, that call stays a one-liner. Mesh decals (clipping the hit triangles into
a small hugging mesh) are the alternative — more exact on complicated geometry,
no depth read, CPU work per decal — and F2 chooses **with the reason written
down**, which is what `roadmap.md:1220-1228` asks for.

Two things F2 should pick up while it is in that code. The **velocity buffer**:
`RenderCamera::jitter` ships and is zero everywhere (`render_world.h:83-98`) and
the other half of the temporal prerequisite does not exist — a second
`ColorAttachment` on the forward passes, a `previousTransform` on `DrawItem`,
one more matrix in the `GpuInstance` stride. F2 is the milestone standing in the
forward pass with an ADR already open. And **`asset::MaterialDef::shader`**,
which exists, defaults to `"pbr"` (`model.h:87`), is serialized both ways
(`mesh_format.cpp:545,888`), and is read by **nothing** in `render` — a
`RenderMaterial` has no shader field and `DefaultRenderer` builds a fixed
pipeline set at `create` time. A particle system wants its own shader; that is
the M4 seam that was declared and never wired, and F2 is its first honest
caller.

---

# F3 — World-Space UI and Rich Text

Last, because nothing waits on it. `SurfaceGui` and billboards are the M6 UI
tree, layout and `ui2d` pass put somewhere other than the screen — and **the
note for whoever builds it**: this is world-space UI and therefore part of the
world image, *not* the screen-space UI pass that the frame-generation constraint
says must be composited last (`roadmap.md:1234-1239`). Two different things with
the same word in them.

Rich text is colour, weight and size varying inside one label, and the glyph
cache is already keyed by face, size and codepoint from the owner's own M6 font
decision — so a label carrying three sizes and two weights already fits the
cache that exists. That decision was made for user-supplied fonts and pays here a
second time.

---

## V1 — `VoxelService`, and it is NOT `Terrain`

**Recorded 2026-08-27, on the owner's correction**, because the agent had read
"voxels" in the opening instruction as *the terrain's representation* and that is
not what was asked for:

> "o Terrain e o VoxelService não é a mesma coisa, VoxelService é uma solução
> para jogos voxel e não tem nada a ver com o terrain"

They are two features that share a word and nothing else:

| | `Terrain` (F1) | `VoxelService` (V1) |
|---|---|---|
| What it is | a sculpted landscape | a world made of blocks |
| Surface | an isosurface, smooth, any angle | axis-aligned cube faces |
| The unit | a signed distance sample | a block with a type |
| Editing | a brush with a radius and a falloff | place one, break one |
| The game | an open world, a hillside, a cave | Minecraft-shaped: mining, building, chunks |
| Storage | two encodings under one field (ADR 0067) | a dense chunk of block ids |
| Mesher | marching tetrahedra | greedy face merging, hidden faces culled |

**Sharing the mesher would be wrong**, and it is worth saying now rather than
discovering it: a marching mesher rounds every corner, which is exactly what a
voxel game must not do. A block world's surface is quads on lattice planes, and
the interesting problem is the opposite one -- merging coplanar faces so a flat
wall of a thousand blocks is a few triangles rather than two thousand.

What it needs, in order: a chunk store keyed on `(x, y, z)` with a `y` this time
(unlike `ChunkId`, because a block world is as tall as it is wide); a block
registry with per-face texture ids; a greedy mesher; `PlaceBlock`/`BreakBlock`/
`GetBlock` on the service; a collider built from the same chunk; and the
streaming grid it already fits. It does **not** need a new physics shape: a chunk
of blocks is a `TriangleMesh`, which ADR 0066 already added.

Sized L rather than XXL: the field, the seam and the streaming it would need all
exist, and what it adds is one representation and one mesher.

## The unresolved list, carried forward rather than closed

These are the questions the two design competitions surfaced and did not answer.
They are listed here so the milestone that hits one knows it was seen.

**F1 owns:** the slope survey's answer (A5); the `MeshUsage::Dynamic` hazard's
answer (A4); `SetHeights` rounding and cumulative drift; behaviour at
`MaxHeight`; vertical density policy behind one `(x, z, layer)`; the MC table
verification; who owns a bricked cell's triangles across `applyScene`; and the
editor's terrain-streaming plumbing, its status message, and its interaction
with `authorable`'s generated-subtree refusal.

**N1 owns:** the correspondence gate between the wire schema's non-property
`Source` names and `world_hash.cpp:245-268`'s hand-written block — two
hand-maintained lists of the same simulation state with nothing comparing them,
and a field added to one and not the other diverges a replica silently in
exactly the state that decides the next tick; who writes `EngineState`'s three
network fields and what `--join` does in a build compiled without replication;
`--headless`'s second exit condition, since it currently refuses without
`--frames=N` (`main.cpp:408-410`) and a server ends on a match rather than a
count; the interest escape hatch's dynamic cases (a projectile, a straddling
platform, a vehicle) and **the missing vertical axis** — `ChunkId` has no `y`
and `chunkBounds` is a vertically-infinite column, so a player in a cave
receives the entire column above them, **which is F1's problem too and neither
milestone owns it**; per-peer baseline memory with no knob; `TransportConfig::maxPeers`
unvalidated against ENet's 4,095 cap; `Player` identity across a reconnect; and
whether an optional replication module survives ADR 0045's copied player binary.

**Both own:** every performance number in both designs is extrapolated from the
physics mirror's ~160 ns/body/tick. The first commit of each milestone is the
bench, not the feature.