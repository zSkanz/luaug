# M4 Kickoff — Seeing the World: Meshes, Materials, Camera, Lighting

- Started: 2026-08-20
- Roadmap section: [`../roadmap.md`](../roadmap.md) § M4
- Previous milestone: [`m3-kickoff.md`](m3-kickoff.md) — read its **Findings**
  first; [`m1-kickoff.md`](m1-kickoff.md)'s findings 6, 7, 9 and 10 are the
  rendering ones and are equally load-bearing here.

## Goal (restated)

M1 put pixels on screen and M2 put a world behind them, but the two have never
met: everything visible since M1 is a wire box drawn by the debug path, and
`RenderPart` carries a size, a colour and a shape enum because that is all a
wire box needs. M4 is the milestone where the engine stops drawing diagrams of
its world and starts drawing the world — a glTF file is imported at runtime, its
meshes and PBR materials reach a real forward pass, a directional sun and point
lights light it, one shadow map grounds it, and the result is tonemapped out of
an HDR target.

Two things ride along that are not "rendering features" and matter more than any
of them.

**The RHI interface freezes at the end of this milestone.** Every call the
renderer needs must exist and be exercised by a real pass before then, because
after the freeze a missing call is an ADR. `rhi_capture` and `rhi_null` are the
proof that the additions stayed neutral — if a new call cannot be recorded
deterministically, it is shaped wrong.

**Three seams must stay open** (roadmap § M4, "Design constraints (not scope)").
They are not features of M4 and nothing is built *for* them; they are shapes the
render module takes now because taking them later is a refactor. Decisions 4, 5,
6 and 8 below are those seams, and each says what reopening it would have cost.

A third thing was added to the milestone by human decision on 2026-08-20, after
this brief was first written: **the `DebugShell`'s explorer and properties
panel**. It is not a rendering feature, and it belongs here for the reason the
roadmap gives — ADR 0017 declines a visual editor for v1 *on the grounds that an
in-game ImGui shell stands in for inspection*, and four milestones in, that
shell does not exist. The compensating control the no-editor decision rests on
has been a promise. Decisions 14 and 15 are how it lands.

The dogfooding claim from M3 binds here for the first time, and honestly: the
*scene* is developed inside `luaug dev` — camera, sun angle, what is in the
world, which material goes on what — while the renderer itself is C++ and needs
a rebuild like any C++. Shader hot swap is on M3's NOT-in-scope list and stays
there. Decision 13 records what that split actually buys and how it is measured,
so the claim ends the milestone either true with evidence or as a finding.

## Scope checklist (from roadmap)

- [ ] fastgltf runtime import (the offline pipeline is M7; the runtime path
      stays as the dev-mode path forever)
- [ ] meshoptimizer on import
- [ ] forward PBR (albedo/normal/metal-rough)
- [ ] directional + point lights
- [ ] single-cascade shadow map
- [ ] HDR + tonemap
- [ ] Camera as an Instance
- [ ] MeshPart-equivalent and material handling per `api-design.md`
- [ ] frustum culling
- [ ] render pass list kept behind the `IRenderer` contract
- [ ] **End of M4 = RHI interface freeze**
- [ ] the human Android-device checkpoint must have happened by now

Added to M4 by human decision on 2026-08-20 (roadmap § M4, "The `DebugShell` —
explorer and properties"):

- [ ] `DebugShell` tree explorer
- [ ] properties panel that reads **and writes** through the generated
      descriptors, honouring `readOnly` and going through the same setters a
      script goes through — never a second write path

The triangle sample and its Android package, added by human decision on
2026-08-20 after the same gap was hit from two directions on the same day:

- [ ] a standalone triangle sample — window, clear, one triangle through
      `rhi_sdlgpu` — deliberately **not** `luaug-host`, which links the Luau VM
      and answers a much larger question
- [ ] an Android project around it, from SDL3's own vendored template, with the
      shaders shipped as SPIR-V
- [ ] the nightly Android job builds and packages it

Carried debt, scheduled into M4 by the same decision:

- [ ] **Trim `Luau.Analysis`** (carried from M0) — a patch under
      `third_party/patches/luau/`, which only became possible this milestone
      (Findings 2 and 3)
- [ ] **`api-dump.json`** (carried from M3) — generated and diff-checked in CI
- [ ] **`luaug --version`** — advertised by `--help`, answered with "Unknown
      command"

The roadmap's reasoning on the api-dump is the one worth restating, because it
is the only carried item that *loses value by waiting*: it is the gate that
notices the public surface changing, and M4 is the milestone that grows that
surface the most. Shipped now it guards M5 through M8; shipped at M8 it guarded
nothing.

## The decisions this brief makes

Each names the alternative it rejects, because the alternative is what a future
session will otherwise re-derive.

### 1. `asset` is born as a loader, not as a streaming system

`engine/asset` (L2) appears this milestone because `render` depends on it and
because glTF has to land somewhere below `scene`. What it contains in M4 is:
mount points over the content directory, `asset://` URN resolution, `MeshAsset`
and `TextureAsset`, and loading on the `jobs` pool with the result applied at the
FrameStart safe point like every other mutation (architecture § 3).

What it does **not** contain: `StreamingManager`, `ChunkManifest`, `.lpack`,
content addressing by BLAKE3, `PrefabDef`, and the `AssetService` Luau surface.
Those are M7's, and `core::ContentHash` does not exist yet — nothing needs it
until content is addressed rather than named.

*Rejected:* building the module as architecture § 2 describes it in full, because
that ships a streaming policy engine with nothing to stream and a
content-addressing scheme with no pack format to address into. The seam that
matters — a handle whose state is `Unloaded/Loading/Ready/Failed` — is cheap and
is built; the policy behind it is not.

### 2. Textures come in through `stb_image`; basis/KTX2 stay unvendored

`stb` is already vendored and its row is a real pin. glTF sample assets ship PNG
and JPEG. basis_universal and KTX-Software exist to be *produced* by an offline
transcoder (ADR 0010), and that transcoder is M7's importer.

*Rejected:* vendoring two more dependencies now so that M4 can load a texture
format nothing in the repository can currently write. Their manifest rows keep
their `TBD-AT-M0` commits and gain them at M7. Note for that milestone: the
research report flags XUASTC LDR as non-standardized as of Feb 2026 — stay on
UASTC/ETC1S for anything portable.

### 3. Vendoring fastgltf and meshoptimizer is a **row fill**, not a new dependency

Both have approved manifest rows carrying `"commit": "TBD-AT-M0"`, and M0's scope
text permits exactly this: "others as needed by later milestones may be vendored
lazily but the manifest rows must exist". So no ADR is needed to bring them in —
but the recorded targets are ranges (`0.9.x`, `1.x`) and R5 wants a pin, so the
commit SHA and the resolved version go into the manifest in the same commit as
the vendored tree, and `THIRD_PARTY_NOTICES.md` is regenerated with it (both
MIT).

**If the version that actually builds falls outside the recorded target range,
that is an escalation** (§10), not a quiet edit of the row.

### 4. A mesh reaches the renderer as a handle the renderer owns — never as a path

`RenderWorld` names geometry by `render::MeshHandle`. The importer produces a
CPU-side `asset::MeshData`; `render::MeshCache::create(const MeshData&)` turns
one into GPU buffers and hands back the handle. **Procedural geometry calls the
same function** with vertices it built in memory and never touches a file.

This is the roadmap's first design constraint, and it costs nothing today: one
function signature that takes data rather than a URN.

*Rejected:* `RenderWorld` carrying a `Content` string or a `ContentHash` and the
renderer resolving it. That is the shape that assumes every mesh is an imported
asset, and voxel meshing — the named future caller — has no file to name. It
would also put an asset lookup inside the render path, which is the wrong place
for it whatever the mesh's origin.

### 5. Static and dynamic uploads are two paths behind one call, and the dynamic one has a caller today

`MeshCache::create` takes a `Usage`: `Static` allocates an immutable device
buffer; `Dynamic` writes into a per-frame ring and returns a handle valid for
that frame only. The ring is sized once and grown, never reallocated per frame.

The roadmap asks for this decision now rather than as a retrofit. It is not
speculative abstraction (§5 forbids that) because the dynamic path gets a real
caller in this milestone: **`DebugRenderer`'s grow-never-shrink vertex buffer is
migrated onto the ring**, deleting its private copy of the same idea. One
implementation, two callers, no virtual interface.

*Rejected:* one path with a "just upload every frame" comment and a promise to
fix it, which is how a per-frame GPU allocation reaches a profile capture in M7.

### 6. A material names its shader by name; the default is `"pbr"`

`asset::MaterialDef` carries `shader: NameAtom` (defaulting to `"pbr"`), a fixed
parameter block, and texture references. `ShaderLibrary` has looked shaders up by
name from a build-emitted manifest since M1, so pointing a material at a
different one is a string, not a mechanism.

This is the roadmap's second design constraint. The named future caller is
vertex-displaced water; nothing in M4 ships a second shader, and no material
authoring surface is exposed to Luau this milestone (`BasePart.Material` stays
the small `Enum.Material` set api-design § 2.3 already declares).

*Rejected:* `enum class MaterialKind` in the renderer. It compiles into every
call site that switches on it, and reopening it means finding all of them.

### 7. Draw order and batching are computed in `extract`, and the order is deterministic

`extract` emits a list of draw items already sorted, each carrying a precomputed
`u64` sort key: pass, then pipeline, then material, then quantized depth. The
sort is **stable and the tie-break is the extraction index**, which is the pool's
dense order — a pure function of the operation sequence, the same property
`RenderWorld` already documents and what R10 requires of anything reaching
observable output. Two runs of the same world produce the same command stream,
which is what makes the capture golden a gate rather than a coin flip.

This is the roadmap's third design constraint: a backend that sorted internally
would be work bgfx has to repeat.

*Rejected:* sorting inside `rhi_sdlgpu`, and equally: sorting by anything a
`std::unordered_map` iteration produced.

### 8. `extract` emits camera-relative f32; `CFrameD` never leaves `scene`

`core::toRenderMatrix(cf, origin)` already takes an origin, which is ADR 0014
having anticipated exactly this. `extract` passes the camera's f64 position as
the origin, so every matrix the renderer sees is f32 and small. The floating
origin itself (the 4 km rebase, architecture § 10) is M7; **where the f64 → f32
conversion happens** is decided here, because it is one argument now and an audit
of every matrix in the renderer later.

*Rejected:* handing the renderer world-space f32, which is correct until the
world is bigger than 8 km and then is quietly wrong (ADR 0013 addendum's
precision table).

### 9. `Lighting.ClockTime` drives the sun, and the sun is a pure function of it

`SunDirection` is derived from `ClockTime` and `GeographicLatitude` and nothing
else — no wall clock, no accumulated state, no drift (R10). Same `ClockTime`,
same direction, in a replay as in a live run. The ImGui slider the deliverable
asks for writes `ClockTime`; it is a view of the property, not a second source of
truth.

*Rejected:* advancing the sun from render dt, which would make the lit result
depend on how busy the machine was and put a wall clock in the one part of the
frame that gets compared against a golden.

### 10. One shadow map, one cascade, and no `DirectionalLight` class

The roadmap says single-cascade, and api-design § 2.2 declares `PointLight` and
`SpotLight` as instance classes but **no** `DirectionalLight`: the sun is
`Lighting`, service state, exactly as Roblox shapes it. So the shadow-casting
light in v1 is the sun and there is only one of it. The map is a fixed-size
`D32Float` fitted orthographically to a bounded box around the camera.

Point-light shadows (`PointLight.Shadows`) are declared in the API and **not
implemented in M4** — the property is accepted and stored, and cube-map shadows
belong to a later milestone. Recorded here rather than discovered by a user: a
declared property that silently does nothing is a bug, so it must be either
implemented or documented, and this brief chooses documented.

### 11. The HDR target is `Rgba16Float`; tonemap is a fullscreen pass to the swapchain

Both formats exist in `rhi::TextureFormat` already. The tonemap pass is the first
fullscreen triangle in the engine and the first time the swapchain is not the
only colour target — which is precisely the pair of RHI capabilities the freeze
needs exercised.

### 12. The capture golden's camera path is scripted and absolute, never live

Three camera angles × two lighting states = six captures, each produced by a
scenario that sets `CurrentCamera.CFrame` and `Lighting.ClockTime` to literal
values at a named tick and captures there. M3's Finding 15 is the precedent: the
only thing that means the same in two different runs is an absolute tick.

*Rejected:* an orbit driven by elapsed time, which is M1 Finding 9 waiting to
happen — a one-tick shift that a tolerance-2 image comparison forgives and a
command-stream diff catches, except that here the shift would be *in* the golden.

### 13. What "developed inside `luaug dev`" means this milestone, and how it is measured

The claim in `PROGRESS.md` is that M4's meshes are developed by editing a `.luau`
file rather than by rebuilding the engine. Precisely: the **scene** is — which
mesh is loaded, where the camera sits, what the sun is doing, which material is
on what — and the renderer is not, because it is C++.

That split is honest but it is also weaker than the claim, so the brief commits
to recording the real number in the Gate Record: how many of this milestone's
scene iterations went through a reload versus a rebuild. If the answer is that
the loop was not usable for this kind of work, that is the finding, and it is
worth more than any feature it blocks.

### 14. The properties panel writes through `World::setProperty`, and nowhere else

`scene::World::setProperty` already returns a `SetResult` that distinguishes
`Changed`, `Unchanged`, `UnknownProperty`, `ReadOnly` and `InvalidValue`
(`world.h:190-201`), and `PropertyDesc::set` already returns false rather than
raising, so the caller owns the error. The panel is one more caller of that, and
it reports the same five outcomes.

This is what the roadmap means by "never a second write path". A panel that
poked components directly would bypass the change queue, so `Changed` would not
enqueue the property-changed fire, a `readOnly` property would be writable from
the overlay and not from a script, and the world hash would move without anything
in the log saying why.

*Rejected:* a component-level write with a comment saying it is only for
debugging. Every editor that ever diverged from its runtime started there.

### 15. Overlay writes are applied at the FrameStart safe point, like every other external mutation

The overlay draws at frame step 8 — after the sim ticks and after `extract`. A
write applied there would mutate the world after the tick that the frame it is
being drawn over came from, which is exactly the mid-frame mutation the
scheduler already refuses for hot reload (architecture § 3, "the watcher thread
only enqueues"). So the panel enqueues, and the queue drains at the next
FrameStart.

The cost is one frame of latency on a value the developer typed. The benefit is
that a replay is still a replay: an overlay edit is an external input arriving at
a tick boundary, the same shape as a reload, and R10's within-run determinism
survives having an inspector open.

*Rejected:* writing immediately because "it is only the debug overlay". M3
Finding 3 is the precedent — a path that is exempt from the rules is a path that
produces a world nobody can reproduce.

### 16. The panel is one generic sweep, and it is the first spend of ADR 0017's promise

`ClassDescriptor::properties` is a view over generated static storage, and each
`PropertyDesc` carries `name`, `type`, `threadSafety`, `readOnly`, `docKey` and
the `get`/`set` function pointers (`class_registry.h:71-95`). So the panel is a
loop over that view with one editor widget per `ValueType` — around eight — and
**no code per class**. A class added in M5 or M6 appears in the inspector with
nothing written for it.

That is precisely the "reflection layer editor-ready by construction" ADR 0017
promises in exchange for having no editor, spent for the first time. If it turns
out *not* to be one generic sweep, the promise was wrong and that is a finding
worth more than the panel.

**One precision on the roadmap's wording:** it says the descriptor tables carry
"per-property type, `readOnly` and default". Type and `readOnly` are there;
there is **no per-property default** — `ClassDescriptor` carries a `defaultName`
for the instance, which is a different thing. A "reset to default" button would
need the IDL to start emitting one, so the panel does not offer one in M4.

### 18. The triangle sample links `asset` for its eyes, and that is why the asset module came first

§8's observation rule is not satisfied by "the code looks right": a change with
visible output is verified by a screenshot. So the sample needs a PNG writer,
and until this milestone the only one lived in `engine/app` — which links the
Luau VM, the scene, the script host and everything else the sample exists to
avoid.

That is the whole reason `engine/asset` was created before the sample rather
than after: `app::writePng` moved into `asset::writePng`, exactly as its own
header had promised since M1 that M4 would do. The sample links
core + platform + rhi + render + asset, and no VM.

*Rejected:* a second PNG writer inside the sample, and linking `luaug_app` to
borrow the first one. The first duplicates the thing the move just consolidated;
the second puts a Luau VM inside the artifact whose entire purpose is to not
have one.

### 19. The sample answers one question, and it is not "does the engine work"

It draws a triangle through `rhi_sdlgpu` and nothing else. No Luau, no scene, no
`RenderWorld`, no content directory. The question the human's device checkpoint
asks is narrow — **does SDL3 GPU rasterize on this phone** — and ADR 0005 says
why it is worth asking: Android support there is officially "limited", with bgfx
as the hedge.

A sample that also booted a VM and loaded a project would fail on Android for a
dozen reasons that have nothing to do with the answer, and each one would have to
be excluded before the result meant anything.

Its shaders ship as SPIR-V because Android is Vulkan, and it reads them the way
the host does — `ShaderLibrary` over a content directory — with **the APK's
asset staging as the one open question**, because `std::filesystem` does not
read an APK. That is named in the risks rather than assumed away.

### 17. One glTF file is one mesh, and `api-design.md` §2.6 already said so

A `MeshPart` names a file and renders it. That is not an invention: §2.6's
prefab example builds a tree from `MeshContent = "asset://models/trunk.glb"`
and a sibling `MeshPart` naming `leaves.glb`, so a multi-part model is multiple
MeshParts rather than one file addressed piecewise. Node transforms inside the
file are baked into the vertices.

A file whose mesh carries several primitives with different materials is
ordinary glTF and becomes several **submeshes**, each with its own material and
its own bounds — which is also the unit the renderer culls and sorts, since a
draw is a submesh.

*Rejected:* a URN fragment syntax (`asset://models/scene.glb#Mesh3`) so one file
could feed many MeshParts. It would need a spec change in api-design, and the
prefab path that M7 builds is the answer the document already has for
assembling many meshes into one thing.

*Also rejected:* flattening every primitive into one material, the way a Roblox
MeshPart has one texture. The file has the materials; discarding them to match
another engine's limitation would make "material handling per api-design.md"
mean less than the file already offers.

### 20. The render module's components are stored in `scene`, and that is not the layering violation it looks like

`World` holds one `ComponentPool<T>` member per component type and has no
extension point. So `Camera`, `MeshPart`, `PointLight`, `SpotLight` and
`Lighting` get five more pools there, and their POD structs are declared in
`scene/components.h`.

Architecture §2 rule 3 says `scene` never includes `render`, and it still does
not: these are plain structs `scene` stores and never interprets, exactly as it
already stores `PartComponent` for a renderer that reads it. What the rule
forbids is a dependency edge, and none is created.

*Rejected:* a type-erased pool registry keyed by type id, which is the shape
that would let a higher module own its own storage. It reworks the ECS core to
buy an indirection on every component access, on a milestone that is not about
the ECS. The day this file grows past the point of paying for itself — physics
at M5 is the next candidate — that is the answer, and it is written down in
`components.h` so the day is a decision rather than a discovery.

**One thing checked rather than assumed:** `World::worldHash` walks instances and
reaches class state *through the generated accessors*, not through the pools. So
a new pool is covered by the determinism hash the moment its properties are
declared, with nothing to remember. The comment there says why it was built that
way — hashing components directly "would silently stop covering a property whose
storage moved" — and it is the reason adding five pools did not open a blind
spot in the one gate that would not have complained.

### 21. Three members of the M4 surface are not shipped, because shipping them would mean shipping a lie

`instances.api.luau`'s own header states the rule: *a property that accepts a
write and changes nothing is worse than a missing one, because it type-checks.*
Applying it honestly removed three things this brief had planned to declare.

- **`Sky` and `SkyboxContent`** need the texture pipeline that arrives at M7.
  The class goes with it. M4's sky is `Lighting`'s gradient, which is real.
- **`Camera.ViewportSize`** is a `Vector2`, and `Vector2` is not a declared
  datatype yet — it arrives with the UI at M6. The renderer knows the viewport;
  a script does not need to until there is UI to lay out against it.
- **`MeshPart.CollisionFidelity`** is a physics property and physics is M5, the
  same call `BasePart` already made by shipping its structural half without
  `Anchored` or `CanCollide`.

**And one that is shipped despite doing nothing yet, deliberately:**
`PointLight.Shadows` and `SpotLight.Shadows` are stored and read back
faithfully while this release casts shadows from the sun alone (Decision 10). A
property that round-trips is honest; the failure the rule names is one that
*silently* discards the write.

### 22. `PointLight` and `SpotLight` are siblings, not a hierarchy

api-design §2.2 lists them on one line sharing four properties, with no `Light`
base class anywhere in the document. Two independent classes extending
`Instance` is what the spec says, so that is what ships, duplicated properties
and all.

*Rejected:* inventing an abstract `Light` for `FindFirstChildWhichIsA("Light")`
to find. It is a real convenience and Roblox has it — but adding a class the
authority does not name is a spec change, and §5 says a spec change is an ADR
and a doc edit, not a quiet generosity in the IDL.

## The three seams, and what reopening each would have cost

| Roadmap constraint | Where it lands | Cost if deferred |
|---|---|---|
| Engine-generated geometry must reach the renderer | Decision 4 — `MeshCache::create(MeshData)` | Every `RenderWorld` producer and consumer, once a `Content` string is baked into the draw item |
| Geometry that changes every frame needs a non-allocating upload path | Decision 5 — `Usage::Dynamic` ring, debug draw as its first caller | A per-frame GPU allocation discovered in a profile, plus the buffer-lifetime rules that come with fixing it |
| A material must be able to name a non-default shader | Decision 6 — `MaterialDef::shader` as a `NameAtom` | Every `switch` on a material enum |

## NOT in scope

Named explicitly so that a later session reads a decision rather than an
oversight:

1. **No `DirectionalLight` instance class** — the sun is `Lighting` (Decision 10).
2. **No point-light or spot-light shadows.** `Shadows` is stored and ignored.
3. **No cascaded shadow maps.** One cascade, per the roadmap.
4. **No clustered/forward+ light culling.** `renderer_default`'s eventual
   clustered pass (ADR 0027) is later; M4 is a plain forward pass with a small
   bounded light count per draw, and the bound is documented.
5. **No skinning, no animation.** `AnimationPlayer` ships in M6; glTF skins and
   clips are skipped rather than half-imported.
6. **No IBL, no reflection probes, no ambient occlusion.** Ambient is
   `Lighting.Ambient`, flat.
7. **No `Sky` class at all**, and this brief said the opposite until step 7
   made it concrete. The plan was to declare `Sky` and `SkyboxContent` and load
   nothing — which is exactly what `instances.api.luau`'s own header forbids: *a
   property that accepts a write and changes nothing is worse than a missing
   one, because it type-checks.* `Sky` needs the texture pipeline that arrives
   at M7, so it arrives then. M4's sky is `Lighting`'s gradient, which is real.
8. **No KTX2/basis, no GPU texture compression, no GPU mip generation**
   (Decision 2). Mips come from a CPU box filter or not at all.
9. **No offline importer, no `assimp`, no `.lpack`, no content addressing**
   (Decision 1).
10. **No streaming and no floating-origin rebase** — only the conversion point
    (Decision 8).
11. **No `AssetService` Luau surface and no prefabs.**
12. **No material authoring from Luau.** `BasePart.Material` stays the declared
    enum; a `MeshPart` gets its material from the glTF.
13. **No shader hot reload and no asset hot swap.** Carried from M3's
    NOT-in-scope list, unchanged.
14. **No GPU-driven path and no meshlets consumed.** meshoptimizer *emits*
    meshlet data (ADR 0010 wants it from day one); nothing reads it in M4.
15. **No render thread.** `RenderWorld` is the seam for one; M4 does not open it.

And, for the `DebugShell` half, the exclusions the roadmap's own addition names:

16. **No log/REPL panel.** `eval` is deferred by M3's protocol decision —
    running arbitrary source in a live world touches R4 and needs its own
    design.
17. **No streaming map** (arrives with streaming, M7) and **no physics
    wireframe** (arrives with Jolt, M5).
18. **No instance creation, deletion or reparenting from the panel**, and no
    multi-select. It reads the tree and edits property values; building a world
    by mouse is the editor ADR 0017 declines.
19. **No "reset to default" button** — the descriptors carry no per-property
    default to reset to (Decision 16).

And three carried items that were considered for M4 and given a named
destination instead, because a debt scheduled where it does harm is not
scheduled — it is moved:

20. **The shipping profile still does not configure.** It also needs a
    bytecode-loading path that does not exist, so it belongs with `luaug build`
    at **M8**.
21. **DXIL signing on Linux stays unverified.** It has no consumer until a Linux
    job produces a Windows shader pack, which is packaging — **M8**.
22. **The clang-format gate is not turned on here.** It requires reformatting
    the whole C++ tree and pinning a toolchain version; doing that while the
    renderer is being written buys a milestone of diff noise, so it lands at the
    **start of M5**, on a quiet tree.

The remaining M3 artifacts — the `@std`/`@luaug` typed stubs and
`docs/reference/**` — stay carried. Both are DX surface with no gate behind
them, and neither degrades by waiting the way the api-dump does.

## Build order

Interfaces before implementations, and the freeze last:

1. Vendor fastgltf + meshoptimizer; fill the manifest rows; regenerate notices;
   wire both into the build as SYSTEM includes. Gate stays green.
2. `core`: `AABB`, `Frustum`, and the plane/box tests, with unit tests. Both are
   absent today for a stated reason (math.h's header comment says the sign
   conventions had not been checked); M4 is the milestone that checks them.
3. `engine/asset` (L2): mount, URN resolution, `MeshData`, `MaterialDef`,
   `TextureData`, the handle/state machine, and the glTF importer behind it.
   Tested against a checked-in `.gltf` fixture before any of it draws.
4. `rhi_api` additions the passes need — exercised by `rhi_capture` in the same
   commit, or they are not shaped right.
5. `render`: `MeshCache` (Decisions 4/5). It takes `asset::Mesh` and produces
   GPU buffers, so it depends on nothing above it and can be built now.
   **`RenderWorld` v2 and `extract` move to after step 7**, because this brief
   had them before it and that was wrong: `extract` cannot emit a camera or a
   light until `Camera` and `Lighting` exist as classes, and defining their POD
   snapshot structs ahead of anything that fills them is the speculative
   abstraction §5 rejects. Then `IRenderer` + `renderer_default` and its pass
   list.
6. Shaders: `pbr.hlsl`, `shadow_depth.hlsl`, `tonemap.hlsl`, sky.
7. `api/defs`: `Camera`, `MeshPart`, `PointLight`, `SpotLight`,
   `Lighting`, `Workspace.CurrentCamera` — IDL first, then the generated C++ and
   defs, then the scene components behind them. M3 Finding 4 says the lints will
   have opinions about the names before any C++ exists; let them. **Declared
   with their `Native` backing, not ahead of it**: `instances.api.luau`'s own
   header states the rule — a property that accepts a write and changes nothing
   is worse than a missing one, because it type-checks. Then `RenderWorld` v2
   and `extract` (Decisions 7/8), which this step is what unblocks.
8. `examples/02-meshes` + the ImGui sun slider.
9. `DebugShell`: the explorer, then the properties panel (Decisions 14–16).
   Deliberately after the new classes exist, so its first run is against a
   `Camera`, a `MeshPart` and a `PointLight` rather than against `Part` alone —
   a generic sweep that has only ever swept one class has not been tested.
10. The triangle sample, then its Android project and the nightly packaging
    step. The sample is verifiable here on both tiers with a screenshot; the
    APK is not — there is no Android SDK, NDK or JDK on this machine, so only a
    runner can build it and only the human can run it.
11. The three carried-debt items. `luaug --version` and the `Luau.Analysis`
    patch are independent of everything above and can be taken whenever the
    tree is quiet; **`api-dump.json` must land before step 7's IDL edits**, or
    its first diff is the whole of M4's new surface arriving at once and it
    guards nothing.
12. Goldens, screenshots, perf, the freeze, the gate.

## Subagent plan

`MASTER_PROMPT.md` § 7 permits fan-out for modules whose interfaces are already
frozen. **This session runs orchestrator-only** — the operator has not enabled
subagents — so the plan below is what *would* fan out, recorded so that enabling
it later does not require re-deriving the split:

- **Fan-out-ready once the headers exist:** the glTF importer (against a fixture
  and `MeshData`, no renderer), the shader set (against the reflection-sidecar
  contract), `AABB`/`Frustum` in core, and the conformance specs for the new
  classes — the last written from `api-design.md` alone, never from the
  implementation.
- **Orchestrator-only, always:** the `rhi_api` additions and the freeze,
  `extract`'s ordering and culling, the pass list, anything touching the
  scene ⇄ render seam, the `DebugShell`'s write path (it crosses app ⇄ scene and
  Decision 15 puts it in the scheduler), and every gate run.

## Gate checklist (verbatim from roadmap)

- [ ] capture-stream goldens for 3 camera angles × 2 lighting states (blocking)
- [ ] Tier-2 lavapipe image goldens attempted (non-blocking)
- [ ] frame time baseline at 1080p recorded
- [ ] GPU validation clean
- [ ] Tier-3 compile gate becomes blocking

Plus the roadmap's three non-gate obligations for this milestone, tracked here so
they cannot be forgotten at the end:

- [ ] **RHI interface freeze** declared, with the frozen surface recorded
- [ ] **Human Android-device checkpoint** performed (see below)
- [ ] The performance recording carries **draw calls and triangles** beside frame
      time — the roadmap asks for the *why* next to the *what*, and the capture
      stream has counted commands deterministically since M1

## The Android checkpoint — a scheduled escalation

The roadmap requires a human to run the triangle APK on a real device before the
RHI freezes, and `MASTER_PROMPT.md` § 10 makes it an escalation item: the agent
does not hold phones. It is **not blocking now** — it blocks the freeze, which is
the last step of the milestone — so it is raised here at kickoff and asked for
before step 9 of the build order, not at the end of the session.

What it is for: ADR 0005 records SDL3 GPU's Android support as officially
"limited", with bgfx as the hedge. The nightly cross-compile job proves it
builds. Only a device proves it runs, and finding out after the interface is
frozen is the expensive order.

## Entering risks

1. **The RHI freeze is a one-way door and its deadline is inside this
   milestone.** Every pass M4 does not write is a call M4 does not discover it
   needs. Mitigation: build the pass list early (build-order step 5 before the
   example, not after) and treat "capture cannot record this call
   deterministically" as a design bug, not a capture bug.
2. **glTF has a long tail, and a hand-authored fixture tests only what we emit.**
   A file we wrote exercises the paths we thought of. At least one real
   exporter-produced asset must be in the gate — see the open question below.
3. **`-Wconversion` and a brand-new math surface.** Frustum/AABB code is where
   f32/f64 and signed/unsigned mix. M1's finding 8 and M3's finding 8 are both
   this family, and the Linux tier caught the second one. Run both tiers, every
   time.
4. **A golden that passes while showing nothing.** M1 Finding 14 caught one
   version of this and M3 caught three more. A lit-PBR golden must be asserted to
   contain draws with the PBR pipeline and a shadow pass, not merely to hash
   equal to a previous run of itself.
5. **This is the first perf baseline with a GPU in it.** `perf-baselines.md`
   § Methodology is explicit that a busy machine invalidates a number by more
   than any regression is worth; the same now applies to a busy GPU. Record the
   reduced-CPU row the methodology asks for.
6. **A generic sweep that has only ever swept one class is not generic.** The
   properties panel is one loop over the descriptor tables (Decision 16), and
   the way that claim fails is quietly: a `ValueType` with no editor widget
   renders as nothing, and nobody notices until the class that uses it ships.
   Build-order step 9 puts the panel after `Camera`, `MeshPart` and
   `PointLight` exist for exactly that reason, and the panel must render
   *something* — a disabled read-only field — for every `ValueType` the registry
   can hold, rather than skipping the ones it has no widget for.
7. **Tier-3 becomes blocking this milestone and has not compiled since M1.**
   macOS builds only on a `milestone/*` tag or a manual dispatch, and it is the
   one tier that cannot be reproduced locally. Dispatch it early — after the
   asset module lands, not at the gate — so a macOS-only break is found while its
   cause is one commit rather than twenty. M1 Finding 12 is what those breaks
   look like.

## Open questions this brief does not settle

- **How the triangle sample reads its shaders inside an APK.** `ShaderLibrary`
  resolves a content directory through `std::filesystem`, and an APK's assets
  are not a filesystem — they are a zip the platform reads through its own API.
  Three ways out: embed the SPIR-V in the binary as generated bytes, extract the
  assets to internal storage on first run, or teach `ShaderLibrary` to read
  through `SDL_IOStream`. The third is the one the engine will eventually need
  and the largest; the first is the smallest thing that answers the checkpoint's
  actual question. Decide when the packaging is attempted, and record which,
  because M7's packaging inherits it.


- **Which real glTF asset ships in the repository, and under which licence.** The
  roadmap authorises "permissively-licensed sample assets, licenses recorded in
  `THIRD_PARTY_NOTICES.md`", so the *policy* is settled; the choice of file is
  not, and it would be the first binary content this repository carries.
  Preference: a CC0 asset under ~1 MB, beside a small hand-authored `.gltf`
  fixture for the importer's unit tests — the fixture is deterministic and
  diffable, the real asset is the one that finds the long tail (risk 2). Raise
  with the human before committing binary content.

## Attempted / abandoned

*(appended during the milestone; §12)*

## Findings

*(the things the docs assumed that reality corrected — appended as they are
learned, not at the end)*

**1. fastgltf is not a leaf dependency, and its own CMake downloads the one it
needs into our vendored tree.** ADR 0010 chose fastgltf on its merits and no
document in this repository mentions that it requires **simdjson**. It does,
unconditionally: `CMakeLists.txt:77-82` links `simdjson::simdjson` if that
target exists and otherwise compiles `deps/simdjson` into the library. There is
no option to turn it off — the fifteen `option()` lines at the top of the file
do not include one.

When the target does not exist and `find_package(simdjson CONFIG)` fails,
`cmake/dependencies.cmake:17-20` runs `file(DOWNLOAD)` twice against
`raw.githubusercontent.com` for simdjson 3.12.3's single-header pair, and writes
them to `${CMAKE_CURRENT_SOURCE_DIR}/deps/simdjson` — that is, **inside
`third_party/fastgltf/`**. There is no hash check of any kind, and a failed
download is detected only by the file's absence or a version string parsed back
out of the header it just wrote.

That single default breaks four things this repository has already decided:

- **R5** — simdjson is a dependency with no manifest row, at a version nothing
  here approved.
- **R13** — the write lands inside a vendored tree, which is never edited in
  place.
- **R14** — and inside the source tree, which never receives build output.
- **ADR 0032's rule** — an artifact fetched at configure time is pinned by
  SHA256 and cached under `$LUAUG_BUILD_ROOT`. This one is pinned by nothing.

It also means no offline configure and a network round trip on every clean CI
configure, on a dependency whose whole selling point is load speed.

This was found before a line of build wiring was written, by reading the
vendored `CMakeLists.txt` rather than the library's documentation — which is M3
Finding 1 repeating with a different vendor, and the second time in this project
that the pinned artifact contradicted the plan at kickoff instead of at the
gate.

**Escalated rather than decided** (§10, R5): the fix needs simdjson on the
manifest, and adding a dependency is a human call. The recommendation is in
`PROGRESS.md` under "Blocked — needs human".

*(meshoptimizer, checked at the same time, is clean: its `CMakeLists.txt`
fetches nothing and its only `find_package` is `Threads`.)*

**Resolved the same day:** the human approved vendoring simdjson (ADR 0036).
It is pinned at v3.12.3 — the version fastgltf's own CMake targets, on the
`spirv_cross` row's precedent — the `simdjson::simdjson` target will be defined
before `add_subdirectory(fastgltf)`, and a patch under
`third_party/patches/fastgltf/` turns the download branch into a `FATAL_ERROR`
so a future version cannot quietly resume fetching.

**2. The patch mechanism R13 rests on had never been run, and it reported
success while doing nothing.** Every manifest row until now carried
`"patches": []`, so `applyPatches` had never applied a patch in four
milestones. The first one it was given did not land, and the tool said
`applying 0001-no-simdjson-download.patch` and moved on.

The cause: `vendor.luau` ran `git apply` with the working directory set to the
vendored tree. **`git apply` resolves a patch's paths against the *repository*
root even when invoked from a subdirectory, and a path that lands outside the
current directory is not an error** — it prints `Skipped patch` to stderr and
exits **0**. So `a/cmake/dependencies.cmake` was read as
`<repo-root>/cmake/dependencies.cmake`, found to be outside
`third_party/fastgltf`, skipped, and reported as done.

Patches now apply from the repository root with `--directory=`, any `Skipped
patch` in stderr is turned back into a failure, and — because a patch that
quietly did not take is worse than one that failed — each patch is verified
immediately afterwards with `--check --reverse`, which succeeds only against
content that already carries it.

This is the fourth "a gate that can pass while doing nothing" in three
milestones, and the first one in a mechanism that a *rule* depends on rather
than a test.

**3. No vendored tree in this repository has ever been byte-identical to its
pinned commit, and `.gitattributes` is what made the mangling durable.**
Finding 2's patch still refused to apply after the path fix, because the
vendored file was CRLF and the patch was LF. It is CRLF because `vendor.luau`
checks out through **its own git dir**, which our `.gitattributes` cannot reach,
so the user's `core.autocrlf=true` applied and every file landed with CRLF on
Windows.

Then the rule written to protect byte-exactness preserved the damage instead:
`third_party/** -text` disables normalization, so git faithfully committed the
CRLF. Checked across the repository, **every** vendored tree is affected — luau,
sdl3, imgui, stb, doctest, spirv_cross, and both trees vendored today.

ADR 0021's central claim is that a vendored tree is exact upstream content at
the pinned commit. It has not been true since M0, and nothing noticed because
compilers do not care about line endings and no patch had ever been applied.
The checkout now forces `core.autocrlf=false core.eol=lf`, so a vendored tree is
upstream's bytes on every platform.

The trees vendored this milestone are correct as of this commit. **The
historical trees are not, and re-vendoring them is a ~20,000-file mechanical
rewrite** — recorded in the ledger as a decision for the human rather than done
on the way past.

**4. The far plane is the least accurate of the six, and by a factor that grows
with the depth range.** `signedDistance` is documented as metric, and it is —
to about **1e-5 relative**, not to 1e-5 absolute. The first frustum test
asserted 90.0 with the file's existing absolute epsilon and got 90.0005.

The sign convention was right; the tolerance was the bug. Gribb-Hartmann
extracts the far plane as `row3 - row2`, a difference of two nearly equal
numbers whose magnitude falls as far/near grows — about 0.01 for a 1-to-100
range — and normalizing then divides the f32 error back up by that same factor.
Culling only compares against zero, so it does not care; anything that wants a
far-plane distance to be exact does, and the header now says so.

Recorded because the instinct on seeing 90.0005 is to widen the epsilon and move
on, and the number is telling you something about the algorithm rather than
about the test.

**5. The Linux tier caught a `-Wdouble-promotion` that MSVC did not, for the
fourth time in this family of milestones.** `doctest::Approx(90.0f)` takes a
`double`, so the float literal promotes, and `-Wdouble-promotion -Werror` is a
Clang-only combination here. The check is written in f32 throughout now, which
also states "relative" in the expression rather than in a comment. CLAUDE.md's
rule about not skipping the Linux stage keeps paying for itself.

**6. Two of the manifest's three "deliberate deviations from upstream" were not
deviations.** Re-vendoring every tree meant checking what the notes claimed
against what the repository holds, and the imgui row described excluding
`examples/libs/glfw/lib-vc2010-{32,64}/glfw3.lib` — 291 KB of prebuilt MSVC
static library — which has in fact been tracked here since M0. Its other
claimed exclusion, and stb's `oversample.exe`, are real absences with the wrong
cause recorded: upstream's own `.gitignore` leaves those files untracked, so
they were never checked out and no decision of ours removed them.

The notes are corrected rather than the trees, because the alternative was to
delete files by hand and thereby create the first genuine deviation — with no
mechanism to keep it, on the same day this milestone learned what an
unenforced invariant costs. A `paths` field on the manifest row is what a real
exclusion would need; nothing needs one yet.

The pattern across findings 1, 2, 3 and 6 is one thing: **this repository has
been trusting its own prose about `third_party/` instead of checking it.** Four
claims, four wrong, none of them expensive to verify.

**7. The dead third of every build was removable without a patch, and the doc
that described it was wrong in a way nobody could have noticed.**
`architecture.md` §8 said "Compiler+Analysis gated by `LUAUG_LUAU_COMPILER`".
Analysis was linked under **no** profile — `cmake/luaug_luau.cmake`'s own comment
said so — and upstream compiled it anyway, because
`third_party/luau/CMakeLists.txt:31-57` creates all twelve libraries
unconditionally and guards only the CLI, test and web executables.

So the carried M0 item was never about a gate: it was 407 s of the 1178 s a cold
build spent compiling, thrown away every time. The fix is not the patch the
roadmap expected but `EXCLUDE_FROM_ALL` on our own `add_subdirectory` — the
targets stay declared, the `all` target stops reaching them, and CMake still
builds transitively whatever the six libraries we link require. **53.9 s → 34.8 s
cold on twenty cores, −35%**, and 22/22 tests unchanged. It also survives a pin
bump better than a patch would, because it names no upstream target.

Verified by deleting `Luau.Analysis.lib` from an existing build tree and
rebuilding: it does not come back. An incremental build directory still holding
yesterday's artifacts is not evidence of anything, which is the trap this check
exists to avoid.

**8. Wiring fastgltf broke the configure, and the error moved twice before it
was right.** fastgltf exposes no install option and its
`install(EXPORT fastgltf-targets)` is unconditional, so it refuses to generate
while anything in its link interface sits outside an export set — and
`luaug_simdjson`, linked `PRIVATE`, is still recorded as `$<LINK_ONLY:...>` in a
static library's interface. Naming our target in the same export set fixed that
and produced a second error: an exported target may not advertise a source-tree
include path. `$<BUILD_INTERFACE:...>` is the answer, and it is how fastgltf
writes its own.

Nothing is ever installed from this build, so both are generate-time formalities
— but they stop the whole configure, which is how a subagent building in an
isolated tree found it before the orchestrator did.

**9. A sample-only shader parked in the engine's shader directory rides into the
engine's content.** `shaders/src/` is globbed wholesale into `luaug-host`'s
content directory, so `triangle.hlsl` — which only the sample loads — was
compiled and staged beside the host, and would have gone into a shipped pack at
M8. Content nobody asked for, inherited rather than decided. The sample's shader
lives under `samples/triangle/shaders/` now and the host's glob no longer sees
it.

Found by looking at the build output rather than at the CMake: the first check
"is it still there?" answered **yes** against an incremental build directory
that was simply holding yesterday's artifacts. Deleting them and rebuilding is
what actually answers the question — the same trap as `Luau.Analysis.lib` in
Finding 7, twice in one day, and the general form is that **a build directory is
evidence of what was built once, never of what would be built now.**

**10. The sample prints plain English on purpose, and that needed writing down.**
R3 is absolute about user-facing strings, and a developer diagnostic in a sample
that exists to be run on a phone by the person building the engine is not
product text. The precedent is `debug_overlay.h`, which carries the same
exemption in the same shape for the same reason. Engine errors that reach the
sample — device creation, shader loading — arrive already catalog-formatted and
are printed verbatim, so the exemption covers the sample's own three
diagnostics and nothing else.

**11. `fastgltf::validate` is stricter than real exporters, and that is a choice
with a cost.** It rejects a POSITION accessor without `min`/`max`, and
`extensionsRequired` that is not a subset of `extensionsUsed` — both spec-correct
and both things exporters get wrong in the wild. Accepted deliberately, because
without validation an out-of-range index reaches fastgltf's
`DefaultBufferDataAdapter`, which `subspan`s with no bounds check: undefined
behaviour instead of an error.

So entering risk 2 has a sharper shape than it was written with. The first
exporter-produced asset is likelier to be refused by fastgltf's validator than by
anything we wrote, and the error will name which check fired. That is the right
failure, but it means "import a real asset" is a step that will need a look
rather than a tick.

What fastgltf's validator does **not** check, and the importer therefore does:
that a buffer view fits its buffer, that an accessor fits its view, that indices
are below the vertex count, that an index accessor is Scalar, and that the node
graph is acyclic.

**12. Twenty-two deliberate defects, twenty caught, and the two survivors were
the useful ones.** The importer's tests were mutation-tested rather than
declared correct: patch one thing, rebuild, run, revert. Two mutations survived
and both were real gaps.

Deleting the reset on the failure path survived because the only failure fixture
failed during *parse*, before anything had been built — so a second fixture now
fails on its second primitive, with a submesh, a material and a vertex buffer
already in hand. And removing the per-submesh loop from the optimizer survived
because a three-triangle fixture is beneath meshoptimizer's notice; a first
attempt at a bigger one used two *disjoint* grids and still missed it, because
the cache optimizer walks one connected component at a time and kept them apart
by accident. One connected 16×16 grid split across two primitives catches it.

Two survive knowingly and are recorded rather than faked: skipping the overdraw
pass changes no observable output on these fixtures, and removing only the
POSITION bounds check is masked by the sibling NORMAL check.

This is M1 Finding 11 — "a property test that has never failed is decoration" —
applied to a whole module instead of one suite, and it found more than the code
review that preceded it.

**13. I wrote two assertions that could not fail, and mutation testing found
both within a minute of the suite going green.** `MeshCache`'s first test suite
passed, and then passed again with the deferred ring destruction deleted *and*
with the dynamic-handle expiry check deleted. Six times in three milestones this
repository has found a check that passes while doing nothing; this is the first
time the check was mine and was written the same hour.

The two causes were different and both worth keeping:

- **A buffer handle reveals nothing about whether it has been destroyed.** The
  null device's `destroy` is a no-op and the handle stays comparable, so
  "the old ring is still there" was asserting on a value that never changes.
  `pendingRingReleases()` makes the rule assertable — and is worth exposing
  anyway, for the same reason as the high-water marks: a count that does not
  return to zero is GPU memory nobody is releasing.
- **The second was a design fault dressed as a test gap.** `Entry::frame` was
  supposed to make "a dynamic handle does not survive its frame" a checked
  contract. Deleting it changed nothing, because `beginFrame` already clears
  `live` on every dynamic entry and a recycled slot already bumps its
  generation. The field was dead weight and its comment claimed a guarantee two
  other mechanisms were providing. It is gone, and the test now asserts the
  mechanism that actually holds: same slot, new generation, stale handle
  resolves to nothing.

The general form, and it is sharper than "write good tests": **a test written
from the same understanding as the code inherits that understanding's blind
spots.** Planting the defect is what separates the two.

## Gate Record

*Filled at milestone end, before human review.*
