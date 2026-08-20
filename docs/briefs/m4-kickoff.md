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
7. **No `Sky` cubemap rendering beyond a solid/gradient sky.** The `Sky` class
   and `SkyboxContent` are declared; loading an HDRI is M7's texture pipeline.
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
5. `render`: `MeshCache` (Decisions 4/5), `RenderWorld` v2, `extract` with
   culling and sort keys (Decisions 7/8), `IRenderer` + `renderer_default` and
   its pass list.
6. Shaders: `pbr.hlsl`, `shadow_depth.hlsl`, `tonemap.hlsl`, sky.
7. `api/defs`: `Camera`, `MeshPart`, `PointLight`, `SpotLight`, `Sky`,
   `Lighting`, `Workspace.CurrentCamera` — IDL first, then the generated C++ and
   defs, then the scene components behind them. M3 Finding 4 says the lints will
   have opinions about the names before any C++ exists; let them.
8. `examples/02-meshes` + the ImGui sun slider.
9. Goldens, screenshots, perf, the freeze, the gate.

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
  scene ⇄ render seam, and every gate run.

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
6. **Tier-3 becomes blocking this milestone and has not compiled since M1.**
   macOS builds only on a `milestone/*` tag or a manual dispatch, and it is the
   one tier that cannot be reproduced locally. Dispatch it early — after the
   asset module lands, not at the gate — so a macOS-only break is found while its
   cause is one commit rather than twenty. M1 Finding 12 is what those breaks
   look like.

## Open questions this brief does not settle

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

## Gate Record

*Filled at milestone end, before human review.*
