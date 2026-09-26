# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **Post-v1 phases 2 and 4 opened 2026-08-27 by the owner**, in the same
  instruction, and phase 1 (the visual editor, E1–E9) is complete behind them.
  Phase 2 is effects and world content — **voxel terrain with a sculpting tool**
  first, then particles, decals, `SurfaceGui`/billboards and rich text. Phase 4
  is multiplayer/replication, whose shape was designed and approved on
  2026-08-21 and is inherited rather than re-decided; what its milestone owes is
  the part that section deliberately did not commit — replication semantics.
  **Phase 3 (the 2D layer and navmesh) opened 2026-09-23 and is built**: see
  the mandate ledger, [`docs/briefs/mandate-2026-09-23.md`](docs/briefs/mandate-2026-09-23.md),
  whose S1 to S7 are all done. Phase 5 (mobile) stays closed.

- **Phase 1, the editor (E1–E9), is complete and every milestone is tagged.**
  E1, E2, E3, E4 and E6 were signed off by the owner; **E5, E7, E8 and E9 were
  signed off on 2026-09-23 by the agent on the owner's explicit delegation**,
  and tagged at `45f9285e`, where the campaign that finished them closed. Each
  brief says which one row was closed by that delegation rather than by a
  person looking. Each brief in `docs/briefs/` carries its Gate
  Record and Findings, and the State bullets this file carried for them moved to
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) on
  2026-09-22. Three lessons from them are worth keeping in front of a reader:
  **a person using the thing finds what a test does not** (E2: seven of nine
  defects); **real Win32 input drives this editor** (E8 corrected five
  milestones that said nothing could click it); and **the ImGui shell cannot
  render headlessly**, so there is still no automated PICTURE of the editor.

- **M8 — Flagship, Hardening, Docs, v1.0 — COMPLETE and RELEASED 2026-08-22**,
  tagged `milestone/m8` and `v1.0.0`, both on `origin`, with the GitHub release
  at <https://github.com/zSkanz/luaug/releases/tag/v1.0.0>. Its entry moved to
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) when E1
  was written up, and **the repository is still private**, so that release
  reaches the account and nobody else — which is the first item under Blocked.
- **M7.5, M7, M6, M5 — COMPLETE and tagged**, signed off between 2026-08-20 and
  2026-08-22. Their entries are in
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) and the
  briefs carry the Gate Records. What carries forward is the pattern rather
  than any of the content — **five of M6's nine defects were found by a person
  playing the deliverable**, which every milestone since has repeated and E2
  repeated seven times.
- **M4.5 — Correcting the World — COMPLETE, signed off 2026-08-20**, tagged
  `milestone/m4.5`. **M4 — Seeing the World — signed off 2026-08-20**
  (`milestone/m4`), its five gate items green against re-recorded artifacts.
  **M3** (`milestone/m3`), **M2** (`milestone/m2`), **M1**, **M0** — all signed
  off.
- **CI is running again, and has been since 2026-08-27**, when the repository
  went public and the billing block stopped applying. Every push of the
  2026-09-22 session went green on all three tiers, macOS included -- the first
  unbroken macOS record since M5.

### The state before this one, and what does not exist yet

M5's and M6's entries, and the NOT-in-scope list they carried, are in
[`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md). Nothing
was dropped: each milestone's brief carries its own Gate Record and Findings,
`CHANGELOG.md` §1.0.0 lists what v1 ships, and the roadmap's R15 list says what
v1 deliberately does not.

**One of v1's stated absences has closed and one has not.** `BasePart.Material`
was the item on that list a reader was most likely to mistake for a bug, and it
is shipped: E3 made a material an INSTANCE with a stamp behind it, and a part
points at one. `RaycastResult.Material` still does not exist. The `Inert` set is
down to two properties -- `PointLight.Shadows` and `SpotLight.Shadows` -- and
`tools/repo/inertcheck.luau`, which has swept `EngineState` as well as the
component pools since D055, is what keeps a new one from joining them quietly.
That widening found three, and the blind spot was the size of a service: a knob
belonging to a service with one instance per world does not live in a component
pool at all.

**Two things M8 deliberately does not have, and both still hold:** `luaug build`
produces a Windows folder and refuses every other target rather than
approximating one, and the packaged game ships Luau SOURCE rather than bytecode
(ADR 0045 says what it would take).

## Now / Next

- **Post-v1 phases 2 and 4 are OPEN and being built.** The owner opened both on
  2026-08-27 in one instruction — *"terrain editor multiplayer voxels etc."* —
  which also answers the terrain's one recorded open question: **voxel, not
  height field, so caves and overhangs are possible.** Phase 3 (2D layer,
  navmesh) was skipped over rather than dropped, and the owner opened it on
  2026-09-23; it is built. Phase 5 (mobile) stays closed. The roadmap's
  numbering is intent, not a queue. R15 is not a permanent
  ban and never was — it says v1's scope is closed and that a scope change is an
  escalation item, and the post-v1 phase list is that escalation.

  **F1, the terrain milestone, is built through Part G, and its ground was
  rebuilt on 2026-09-22.** One signed-distance field under two encodings
  (ADR 0067), sculpted from a script or from the editor's brush, collided, saved
  with the scene, and reached as `workspace.Terrain`. **What changed is how it is
  drawn** ([ADR 0071](docs/decisions/0071-terrain-ground-is-drawn-from-a-height-atlas.md)):
  the height layer is a GPU atlas under a CDLOD quadtree, so a brush stroke
  uploads the tiles it touched instead of re-meshing them, and only bricked
  columns -- caves and overhangs -- are CPU meshes, now surface nets. Cave
  surfaces collide, built lazily within reach of something that moves.

  **Caves became usable on 2026-09-23**, from a person reporting that digging
  sideways went nowhere: a dig aimed at a wall now carves into it and bores on
  the clock, caves are dark inside (a sky-visibility term baked per cave
  vertex), open per pixel so they are drawn to 256 m, and stop streaking on
  steep walls (triplanar detail). `examples/17-cave` flies through one.

  **Terrain became one grid of voxels on 2026-09-23** ([ADR 0082](docs/decisions/0082-terrain-is-a-grid-of-voxels.md)),
  on the owner's word after building caves kept breaking and digging lagged
  (D164). Each voxel is a material and an occupancy, stored in row-packed
  32-cubed chunks. It is drawn as a quadtree of CPU meshes built from each
  chunk's mips, and collided chunk by chunk near things that move. It
  supersedes ADR 0067's two encodings and ADR 0071's height atlas, and a
  world saved before it opens converted. A dig now rebuilds one mesh and one
  collider; the sculpting bench went from 4.27 to 0.19 ms a tick.

  **Terrain and block worlds stream from disk since 2026-09-23** (ADR 0075,
  Part E): a saved field of sixteen 64 m cells or more is cut into cells at
  play, streamed by a second manager on the terrain radii, and never evicted
  once somebody changed it; the world waits for its ground on first load, and
  colliders are built nearest a mover first. Building it found two shipped
  defects (D157, D159) and one latent one (D158).

  **The seam gate (H1) is built**: 3,808 rays through a tunnel crossing a cell
  boundary, a tile boundary and the bricked/height edge. Each ray hits the
  field, the collider and the drawn surface, and no hit is more than a
  quarter-voxel off the field. **The flagship has terrain (H2, H3)**: the
  middle 512 m of `examples/10-open-world` is streamed `Terrain`, written as
  one heightmap by `Terrain:WriteHeights`, with a hill and a tunnel, and its
  soak holds at 56 MiB and 3.10 ms p99. What is left of F1 is editing a world
  larger than memory, which ADR 0075 puts further off: the editor holds the
  whole field.

  **V1, `VoxelService`, is built** -- a block world with a registry, place and
  break, a DDA raycast, a greedy mesher with corner occlusion, colliders near
  movers, and `examples/14-voxels`. It is not `Terrain` and shares nothing with
  it but a word; [`docs/briefs/phase-2-4-plan.md`](docs/briefs/phase-2-4-plan.md)
  says why. Since then it has gained an editor tool, see-through blocks, cells
  streamed from disk and fluids (below). A type's images and opacity are set
  from the editor's panel since 2026-09-23. Fluids react with each
  other through `SetFluidReaction`.

  **Water is a fluid since 2026-09-23**: `SetBlockFluid` makes a type pour,
  spread and drain on the simulation clock, deterministically, and never
  collide. `examples/14-voxels` has a spring running down its hill into the
  pond. Leaves cast their holes in shadow.

  The editor has a block tool for it since 2026-09-22: place, break and replace,
  a palette of registered types with their three face colours, and one undo
  step per stroke. Block faces have images and blocks can be see-through --
  leaves, glass, water -- since 2026-09-23, and making the images work found
  that every compiled colour texture in the engine had been drawing pale
  (ADR 0073, fixed).

  **F2's particles and decals are built** (ADR 0072): `ParticleEmitter` and
  `Decal`, with `examples/16-particles`.

  **F3 is built (2026-09-23)**: `TextLabel.RichText`, and `SurfaceGui` and
  `BillboardGui` drawing the screen's own UI classes in the world, with
  `examples/18-world-ui`. Their buttons are pressed like the screen's since
  the same day: the pointer's ray finds them in the world, and what stands in
  front hides them.

  **N1, multiplayer, is playable over a LAN** (2026-09-22 and 23). One project
  runs solo, as a host (`--host`), as a dedicated server (`--serve`) or as a
  replica (`--join=address`); the authority sends each replica a snapshot
  diffed against the state it last acknowledged, checked by checksum, and each
  replica sends back what its player did as intent. `examples/15-multiplayer`
  gives every player a racer, and `replica_seam` requires a replica booted in
  the same process to draw its authority's world.
  [`docs/briefs/phase-2-4-plan.md`](docs/briefs/phase-2-4-plan.md) lists what is
  not built: client prediction, the husk despawn, interest management and
  service properties.

  **N1's three named gaps closed on 2026-09-23** (ADR 0076): a replica
  predicts its own character from `Player.Character` and is corrected by the
  authority rather than overwritten, draws everyone else between snapshots, and
  is sent only what is near its character. **N2's first piece is built the
  same day** (ADR 0077): `RemoteEvent` carries a game's messages both ways,
  with the sender named by the connection, and `examples/15-multiplayer` has
  a horn. Losing interest is streaming out
  since the same day (protocol 6): a copy a script holds becomes a husk and
  fires `InstanceStreamedOut`, and building it found D160 -- the chunk
  streamer's half of that contract had never been wired.

  **The editor's smaller items are built (2026-09-23)**: the Terrain panel
  imports a heightmap and holds `VoxelSize` and the height range, and the
  Blocks panel sets a type's images and opacity. They live in
  `engine/app/src/world_panels.cpp` and hook into the shell in three lines,
  so the owner's uncommitted work in `debug_overlay.cpp` stays theirs.

  **`RemoteFunction` is built (2026-09-23, ADR 0079)**: `InvokeServerAsync`
  yields until the authority's `OnServerInvoke` answers, the IDL has callbacks
  for it, and the protocol is version 8.

  **`ReplicatedStorage` and `ServerStorage` are built (2026-09-23, ADR
  0080)**: saved with the scene, one replicated to everybody and one kept on the
  authority, typed on `game`, and droppable into from the Explorer.

  **Editing a large world was measured before it was built (ADR 0081).** The
  field in memory is 19 MiB per km² at half a metre, so the editor holds a
  4 km square whole. The GPU atlas was the real wall: past a 2 km square the
  editor drew a strip along one edge, and it now keeps the tiles nearest the
  camera. A scene that is a folder of cells waits for a 1 GiB field.

  **The next action, as a sentence:** nothing on the list the owner left is
  open except Android, which waits on their word.
- **The campaign in [`docs/finish-line.md`](docs/finish-line.md) closed first**,
  and it is the reason the tree is in a state worth building on. **Eighty-seven
  of its eighty-eight rows are done.** The one that is not is S1.7, and it is
  not work: `milestone/e5`, `e7` and `e8` wait on a person signing those
  milestones off, and S1.7 names exactly what each waits for rather than leaving
  it as "review".

  **Four things still need the owner** and none of them blocks the phases above.
  S8.7 is the handback and lists them in order: sign off three milestones, cut
  `v1.1.0`, fix the billing so Actions runs again, and make the repository
  public.

  **Two of those four closed on 2026-08-27, in the same hour.** CI had been
  dark for a billing block — confirmed rather than inferred: every job of every
  run failed in 8 to 12 seconds with **zero steps recorded** and no log to
  fetch, because they never started. **The owner made the repository public,
  and Actions came back on the first dispatch.** That also removes the reason
  the quota ever mattered: standard GitHub-hosted runners are free on a public
  repository, macOS included, so the 1x/2x/10x multipliers this file and
  `CLAUDE.md` were both written around no longer bill anything. **macOS is
  therefore verifiable for the first time since the block** — it is the one tier
  that cannot run locally, and every commit since the block landed with it
  unproven.

  Both of those closed on 2026-09-23 on the owner's delegation: the four
  milestones are signed and tagged, and `v1.1.0` is cut.
- **`v2.0.0` is released, 2026-09-26**, on the owner's word ("mete marcha na
  tag"): a major version because ADR 0090 removed `BasePart.Color` and
  `BasePart.Transparency`. It carries the 2026-09-24 mandate, ADR 0091's
  surface shaders and the wire at protocol 15.
- **`v1.1.0` is released, 2026-09-23**, tagged on `main` with the editor phase
  and the first of phases 2 and 4 in it; the archive is built from a clean
  checkout of the tag, and the GitHub release carries it.
- **Jolt runs cross-platform deterministic since 2026-09-23** (ADR 0074),
  measured at under 1% and closing audit row 4; the guarantee stays level B
  because the engine's own transcendentals still part Windows from Linux.
- **The work is published, and that is new.** 164 commits reached `origin` on
  2026-08-26. `origin/main` had not moved since E1's sign-off on 2026-08-22, so
  seven milestones of post-v1 work existed on one machine with no backup and
  nothing for CI or a reviewer to look at. `milestone/e4` and `milestone/e6` had
  existed only here and went with them; `milestone/e2` and `milestone/e3` were
  created and pushed the same day; five further commits landed the untracked
  tree and the mission file behind all of it.
- **Every milestone is tagged**, `milestone/m0` through `milestone/e9`. The
  last three, `e5`, `e7` and `e8`, waited on a sign-off pass
  (`docs/finish-line.md` S1.7) and were signed and tagged on 2026-09-23 on the
  owner's delegation.
- **The full gate is green and it is nine stages now** (S1.5, 2026-08-27): the
  seven that run by default, plus `asan`, `winprofiles` and `lavapipe` on
  request. 57 tests on Windows, 56 on Linux, 1,168 conformance cases on each
  tier. The sanitizers had their first fully clean run in the same pass.
  **Two instruments are still switched off and both are deliberate**:
  `openworld_soak`'s instance-growth check is QUARANTINED at its second flake
  (D066), still measuring and still logging at warn level, no longer failing the
  run; and macOS has no local instrument at all, so the only thing that can
  answer for Tier 3 is Actions, which is not running.
- **One writer, settled 2026-08-26.** Three agent sessions were writing this
  tree while the protocol in this file described two; all three were asked, all
  three declared what they held, confirmed nothing was mid-edit, and stood down.
  The divided-tree protocol that stood under Blocked since 2026-08-21 -- a
  builder and a reviewer with named paths -- is superseded by that, and the two
  rules that came out of what the three reported are in
  [`docs/finish-line.md`](docs/finish-line.md): never restore a path you did not
  write, and check `git status` before chasing a red gate.
- **The harness that drives the editor is not in the tree.** E8's correction
  above rests on real Win32 input -- `SetCursorPos`, `mouse_event`, `SendKeys`
  -- and nothing in `git ls-files` implements it, so the next session that wants
  a click has to write it again. That is `docs/finish-line.md` S7.11.
- **Every open defect is in [`docs/defects.md`](docs/defects.md)**, which is
  append-only and checked by the docs gate for gaps, states and dangling
  citations. That file exists because three human-reported defects were removed
  from this one while it was being rewritten to close M4. **A close rewrites this
  file wholesale; it can no longer take the open list with it.**
- **There are five profiles now and the gate builds three of them.** `player`
  (D057) carries the Luau compiler and no ImGui, which is what a packaged game
  needs; `editor` (ADR 0054) is what somebody downloads. Both exist because a
  profile nothing builds is a profile nobody knows is broken, and both are
  compiled and linked by `scripts/gates/shipping-build.sh` for that reason.
  `LUAUG_HOST` is deliberately not consulted by `luaug build`, because "I pointed
  the dev server somewhere" must not decide what a release contains.
- **The build agreeing is not evidence that the build read your file, and on
  Windows it was not evidence that it read your HEADER either.** D040: ninja
  recorded no header dependencies at all, so `--clean-first` was load-bearing
  for two milestones and nobody knew why. Fixed by `chcp 65001`, which the gate
  now sets. The older half still applies: a break-verification restored with
  `Copy-Item` keeps the source's old timestamp and rebuilds nothing — restore
  with `cp` and `touch`.

## Blocked — needs human

- **The Android run of `examples/02-meshes` is due**, and it is the one item
  here. Phase 5 (mobile) is closed until the owner opens it (R15), and a device
  check is the owner's to run. It is listed so that it is asked for rather than
  forgotten.

What this section used to hold, resolved, for whoever remembers it:

- **The repository is public** (2026-08-27). That also ended the Actions
  billing block: standard runners are free on a public repository.
- **v1.0.0 and v1.1.0 are released** on GitHub, v1.1.0 (the editor) on
  2026-09-23.

## Decisions taken

- **Atmosphere, post effects and a skybox are instances under `Lighting`**
  (the owner, 2026-09-24; [ADR 0096](docs/decisions/0096-atmosphere-post-effects-and-a-sky-are-instances-under-lighting.md)).
  `Atmosphere`, a six-image `Sky` with clouds, and bloom, colour correction,
  blur, depth of field and sun rays. A world with none of them draws as
  before, and the sun stays on the clock. It folds in the mandate's M1. The
  ledger is [`docs/briefs/atmosphere-post-kickoff.md`](docs/briefs/atmosphere-post-kickoff.md).
- **A material is an asset, not an instance** (the owner, 2026-09-24;
  [ADR 0090](docs/decisions/0090-a-material-is-an-asset-a-part-wears-one-and-a-script-clones-one.md),
  superseding 0060). `.material.json` in `content/`, variants by parent, a part
  with no `Color` or `Transparency` of its own -- only the parameters its
  material declares -- and `Clone()` for the copy a running script changes. It
  is a breaking API change, so the release carrying it is a major version, and
  tagging it is the owner's.
- **A material may name a surface shader the user writes** (the owner,
  2026-09-24; [ADR 0091](docs/decisions/0091-a-material-may-name-a-surface-shader-the-user-writes.md)).
  Two HLSL functions against a versioned `surface.hlsli`, from which the engine
  builds every pass, and the editor ships a DXC built from source. The owner
  approved that dependency in the same message. The ocean becomes user code
  over it, and a node graph is the second tier, later.
- **`churn10k`'s 7.32 ms/tick is accepted** (the owner, 2026-09-24). Two
  thirds of its anchored parts are written every tick, so D031 makes them
  kinematic, and Jolt re-fits that broadphase layer every tick. That is the
  semantic the fix exists for, applied to a scene that was never a physics test,
  and it is under the scenario's own budget, which is 32 ms since `8f80ccf1`. Neither of the two alternatives -- a
  dirty-flag mirror, or a scene whose moving parts are not anchored -- is taken.
  [`docs/finish-line.md`](docs/finish-line.md) decision 3 had already ruled it
  not a regression; the owner's word closes the question.
- **Jolt's `CROSS_PLATFORM_DETERMINISTIC` is ON** (ADR 0074, 2026-09-23), at
  upstream's documented cost of about 8% of the library's speed. Decision 4 in
  `docs/finish-line.md` is closed by it.

## Now

The 2026-09-24 mandate: every piece of depth the phases deferred --
navigation's crowds, links, costs, sizes and 2D; multiplayer's ownership, teams,
a public protocol and rollback; 2D joints, sprite animation, and 2D on the wire;
a post-processing API; the terrain's one-sided skirt. The ledger is
[`docs/briefs/mandate-2026-09-24.md`](docs/briefs/mandate-2026-09-24.md).

**Materials as assets (ADR 0090) are built**, all eight stages of
[`docs/briefs/materials-kickoff.md`](docs/briefs/materials-kickoff.md), which
carries twenty-one findings. A part wears a `.material.json` and has no `Color`
or `Transparency` of its own; `Material.load` and `Clone()` are the script
surface; the scene format is version 2 and `luaug migrate materials` converts a
project; the wire is protocol 12 and carries a part's material, overrides and
runtime copy; the editor has a Material panel. **It is a breaking change to the
public API, so the release that carries it is a major version, and tagging it
is the owner's.** The mandate's unstarted stages are next, built against
protocol 12.

**Atmosphere, post effects and a sky (ADR 0096) are built**, all ten stages
of [`docs/briefs/atmosphere-post-kickoff.md`](docs/briefs/atmosphere-post-kickoff.md),
with twenty-four findings: `Atmosphere`, `Sky` (six pictures, sun, moon,
stars, clouds), `BloomEffect`, `ColorCorrectionEffect`, `BlurEffect`,
`DepthOfFieldEffect` and `SunRaysEffect` as instances under `Lighting` or the
camera, five new `Lighting` properties, protocol 13, and
`examples/22-atmosphere`. A world with none draws as before, to the byte of
the command-stream goldens. **The owner accepted the look on 2026-09-25**, and
it is held by goldens of its own: `capture_gate_look` (every effect in one
command stream, blocking on every tier) and ten exact lavapipe images, one per
look, named in `tests/look/goldens.txt`.

**User surface shaders (ADR 0091) are built**, stages 0 through 8 of
[`docs/briefs/user-shaders-kickoff.md`](docs/briefs/user-shaders-kickoff.md),
with its findings. A material names a `.surface.hlsl`; the editor compiles it
in the background, writes one from a template, edits it and shows its
parameters; `assetc` packs it compiled for SPIR-V, DXIL and MSL and a player
draws it with no compiler; DXC is built from source and ships beside the
editor. `examples/11-ocean` is user code now (0.67 ms a frame) and
`examples/23-surfaces` shows three more. **Not built, and said so in the
ledger**: a shader parameter a part overrides (`instanceParameters`), surviving
a GPU hang, and pipeline creation times in `docs/perf-baselines.md`.

## Session Log

Entries for the planning session, for M0 through M4, and for sessions 11 and 19
through 26 are in
[`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md), moved
there each time this file passed its ~300-line cap (§11). What was worth
carrying out of session 19 is under E3 above. Session 26's -- the campaign
opening, and what a `git push` found -- and session 27's -- S7 closed, and what
a gate reports when it did not run -- both went there on 2026-08-27, each to
make room for the next. Session 29's -- the ground, and two ways a brush can lie
-- followed them on 2026-09-22, and session 30's -- the ground drawn by the GPU,
and a block world -- went to
[`docs/progress-archive/2026-09.md`](docs/progress-archive/2026-09.md) on
2026-09-23, where sessions 32 to 34 -- voxel terrain, the 2D layer and
navigation, and materials as assets -- joined them on 2026-09-25.

- **Session 36 — atmosphere, post effects and a sky, 2026-09-25.** ADR
  0096's ten stages, each on the full local gate and pushed behind a green CI.
  The rule that held them together: **a new effect never touches an old
  shader** -- each is its own pipeline made the first frame it is needed, so a
  world without it keeps its command stream byte for byte, and the one commit
  that moved anything moved only the determinism traces (Stage 7's five
  `Lighting` properties, ADR 0060). Two things worth keeping: **a headless
  frame's time is the CPU's** -- the GPU cost of a post pass was measured as
  the slope of whole-run wall time between two run lengths ending in a
  screenshot, and every effect sits under a tenth of a millisecond at 1080p;
  and **D186 is fixed, on the fourth attempt** -- what is resident where a
  moving focus stands depends on how far streaming trails it, so ONE revisited
  place (1194 instances on one lap, 1689 on another, in a run averaging 1938)
  is noise, and the first quarter is the world still arriving on a loaded
  machine. The check now compares the median of every place revisited between
  the second quarter and the fourth, and gates again. The owner's CityBench against
  Godot did not move (LuauG 1.50-1.54 ms against 1.55-1.63 before).
- **Session 35 — a user's game, and the script editor, 2026-09-24/25.**
  D182-D185 from benchmarking the owner's friend's game, each row closed
  with its commit and how it was verified: the SDL D3D12 descriptor heap
  patch (`605c2995`, R13, upstream still had it), the GPU debug layer kept
  to `debug`/`dev` or `--gpu-debug`, a UTF-8 byte-order mark in `luaug.toml`
  (`7a572239`), and instancing by material FAMILY (`7929c198`), which took
  the game from 756 draws to 4 and did not move its frame time -- the A/B and
  the likely reason are in the baselines. Two lessons worth keeping: **the
  D3D12 debug layer does not catch every out-of-bounds descriptor write**,
  only those that land outside every heap, so a scene that runs clean
  unpatched proves nothing (D182's row); and **a windowed frame here is its
  presentation** -- ~8.6 ms with nothing drawn -- so only headless numbers
  measure work. Around it, the script editor the owner and a friend were
  using: multi-cursor, user colours and keys, tabs, scoped completion, the
  automatic `end` after a return type; and lights that stand on their own
  (ADR 0095). One full gate for the whole batch, at the owner's request
  rather than one per commit.
