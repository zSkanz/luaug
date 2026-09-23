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
  Phase 3 and phase 5 stay closed.

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
  navmesh) and phase 5 (mobile) were skipped over rather than dropped and stay
  closed; the roadmap's numbering is intent, not a queue. R15 is not a permanent
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

  **Terrain and block worlds stream from disk since 2026-09-23** (ADR 0075,
  Part E): a saved field of sixteen 64 m cells or more is cut into cells at
  play, streamed by a second manager on the terrain radii, and never evicted
  once somebody changed it; the world waits for its ground on first load, and
  colliders are built nearest a mover first. Building it found two shipped
  defects (D157, D159) and one latent one (D158). What is left of F1: Part H's
  seam gate and flagship swap, and streaming while editing.

  **V1, `VoxelService`, is built** -- a block world with a registry, place and
  break, a DDA raycast, a greedy mesher with corner occlusion, colliders near
  movers, and `examples/14-voxels`. It is not `Terrain` and shares nothing with
  it but a word; [`docs/briefs/phase-2-4-plan.md`](docs/briefs/phase-2-4-plan.md)
  says why. What it does not have yet: an editor tool that places a block,
  transparent blocks, and chunks streamed from disk.

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
  `examples/18-world-ui`. Not clickable yet.

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

  **The next action, as a sentence:** in the order the owner agreed on
  2026-09-23 -- terrain streaming and F3 are done, so N1's prediction and
  interest management, then the editor's smaller items. Android waits.
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
- **Three milestone tags are still missing -- `e5`, `e7` and `e8` -- and that is
  deliberate.** All three are BUILT and awaiting review, and a tag for a
  milestone still awaiting review would be a durable record of something that
  has not happened; they wait on the campaign's sign-off pass
  (`docs/finish-line.md` S1.7). **What each is waiting for is not the same
  thing.** E7: nothing but the sign-off -- every row of its Gate Record is
  green, the pictures included. E5: two rows, both of them a person at a window.
  E8: two rows -- a photograph of the debugger stopped, which the eight headless
  cases cannot stand in for, and a `localgate` row its brief marks *(filled
  below)* and then does not fill.
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

- **Making the repository public is the one act reserved for the owner.** It is
  a one-way door in practice -- the whole history becomes readable and
  cloneable, and un-publishing does not un-clone -- so it is §10's kind of
  decision, asked rather than taken
  ([`docs/finish-line.md`](docs/finish-line.md), decision 1). It also decides
  the item below: a public repository does not spend Actions quota. **The
  v1.0.0 release is published**, with `LuauG-Open-World-v1.0.0-win64.zip`
  attached, and reaches nobody until this is answered. The second archive
  ADR 0054 makes possible -- the editor itself -- is **prepared and not tagged**
  (S8.6): the tree declares 1.1.0, the changelog carries the editor phase, and
  `lute tools/repo/package.luau` writes `LuauG-1.1.0-win64` whose binary says
  `LuauG 1.1.0 (editor)`. `git tag v1.1.0 && git push --tags` and attaching the
  archive are the owner's, beside this.
- **Actions is blocked on billing, and only the account holder can clear it.**
  GitHub's own annotation is quoted under State above. Until it is cleared there
  is no Tier-3 at all, so every tag from M6 onward carries a Tier-1 and a Tier-2
  result and no macOS one -- a gap in the evidence rather than a failure, and
  the notes say so in the open.
- **The Android run of `examples/02-meshes` is due.** Deferred by the human
  until M4.5 closed, and M4.5 and M5 have both closed since. It is a device
  checkpoint rather than milestone work, and it is here so that it is asked for
  rather than forgotten.

## Decisions pending ADR

- **`churn10k` reads 7.32 ms/tick, and whether that is a regression is a
  judgement somebody may want to remake.** 2.02 at M2, 4.96 at M5 when its scene
  became ten thousand rigid bodies, 7.32 since M6: two thirds of its anchored
  parts are written every tick, so D031 makes two thirds of them kinematic
  bodies in the broadphase layer Jolt re-fits each tick. That is the semantic
  the fix exists to provide, applied to a scene that was never written to be a
  physics test. It is under its 16 ms budget at every step and itemised in the
  baselines. If the answer is "that is a regression", §8 wants an ADR; the two
  candidate answers are the mirror's dirty-flag design and a `churn10k` whose
  moving parts stop being anchored. Decision 3 in
  [`docs/finish-line.md`](docs/finish-line.md).
- **Jolt's `CROSS_PLATFORM_DETERMINISTIC` build switch is off**, which is
  ADR 0025's level B rather than an oversight -- upstream documents it as buying
  determinism across compiler, OS and architecture for about 8% of the library's
  speed (`third_party/jolt/Docs/Architecture.md`). Turning it on is one line and
  would likely make the win↔linux comparison green; it has a performance bill,
  which is what makes it a record rather than an edit. Decision 4 in
  [`docs/finish-line.md`](docs/finish-line.md).
- **The rest of the ADR debt is tracked in the campaign rather than here.**
  `docs/finish-line.md` S2.7 names the two records that were owed when it
  opened: the material reversal, and the Part B deviation justified only in
  commit messages.

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
2026-09-23.

- **Session 31 — releases, a cave you can dig and see, and a benchmark decided
  by measurement, 2026-09-23.** The owner delegated the sign-offs, the tags and
  the benchmark questions, and reported two terrain defects by playing the
  package.

  **"Sculpting past the edge of the terrain is deformed."** Reproduced
  headlessly with a flat square and no brush at all: a vertex past the edge of
  the ground stopped being a hole once it had morphed more than halfway onto a
  neighbour that was ground, and kept the height of an empty column. Each end of
  the morph now borrows the other's height when it has none.

  **"Digging sideways does not go."** It could not: a dig was a height brush,
  and a stroke aims at the field as it was when it began. A dig aimed at a wall
  now carves volume and bores at a speed, and the flow the owner tried -- flat
  ground, down, then out -- is a test. Making the result worth looking at took
  four renderer changes, and one of them was a defect this session made and
  caught: the colour pass and the depth prepass compiled the same vertex maths
  differently once the shaders changed, and the ground showed the sky in
  specks. `precise` fixes it and the comment says why.

  **Jolt's cross-platform switch was decided by running it**, after finding
  that the audit's experiment could not answer: its scenario simulated nothing.
  ADR 0074 has the table.
