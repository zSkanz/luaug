# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **M7 — Scaling the World — IN PROGRESS, started 2026-08-21.** The brief is
  [`docs/briefs/m7-kickoff.md`](docs/briefs/m7-kickoff.md), with its eleven
  decisions, its build order in seven phases and the gate copied verbatim. The
  milestone builds the open-world substrate: an offline asset pipeline with
  deterministic content-addressed output, a job system and async IO, a
  per-World floating origin, and chunked streaming — plus the five `Inert`
  markers that were waiting for a way to hand the engine a file, Inter, and
  `CharacterBody:Jump()` in mid-air. The previous milestone's gate was re-run
  green before any work started: 34 ctest targets on Windows, 33 in the Tier-2
  container, 1,081 conformance cases, docs and format clean.
- **M6 — Playing the World — COMPLETE, signed off 2026-08-21**, tagged
  `milestone/m6`. The brief is
  [`docs/briefs/m6-kickoff.md`](docs/briefs/m6-kickoff.md), with its fifteen
  decisions, its Findings and a filled Gate Record. **Every scope item and every
  gate item is done**: the five systems, solid `Part` rendering, `InputService`'s
  raw event surface (ADR 0041), the non-device input seam, `examples/04-obby`,
  D017, and the six gates. The Gate Record is in the brief.
  **Three ADRs**: 0039 (a context declares its dispatch rate), 0040 (a `UDim2`
  placement is arithmetic, so v1 does not call Clay — and Clay is un-vendored),
  0041 (`InputService` gains raw events, fed from the IAS's own pipeline).
  **Eight defects closed and half of a ninth** — D017, D021, D022, D027, D029,
  D030, D031, D032, and the ground half of D028 — of which five were found by a
  person playing the deliverable rather than by a test.
- **M5 — Feeling the World: Jolt Physics + Character — COMPLETE, signed off
  2026-08-20**, tagged `milestone/m5`. Signed after a review round that found
  something: two `CharacterBody` were reported as passing through each other,
  which did not reproduce — and the investigation found the real defect
  underneath, a character that ignored `CollisionGroup` (D025). The Gate Record
  is in [`docs/briefs/m5-kickoff.md`](docs/briefs/m5-kickoff.md), with seventeen
  Findings and the section written for a reviewer.
- **M4.5 — Correcting the World — COMPLETE, signed off 2026-08-20**, tagged
  `milestone/m4.5`. **M4 — Seeing the World — signed off 2026-08-20**
  (`milestone/m4`), its five gate items green against re-recorded artifacts.
  **M3** (`milestone/m3`), **M2** (`milestone/m2`), **M1**, **M0** — all signed
  off.
- **CI stopped running on 2026-08-21 and it is not the code.** Every job since
  `5a542b7a` completes in about two seconds having executed ZERO steps and
  produced no log, which is a job that never checked the repository out — the
  signature of an Actions quota or a billing block on this private repository,
  and the human's to clear. The last run that really executed
  (`5a542b7a`) was green except for `perf_budget`, which `8f80ccf1` answers.
  **So macOS is unverified for M6**, and the `milestone/m6` tag's macOS job is
  what will say otherwise when Actions runs again. The last all-green run across
  all three tiers is 32429107275, at M5.
- **The last local gate before the tag**: all five stages green, 34 ctest targets
  on Windows and 33 in the Tier-2 container, 1,081 conformance cases.

### M6: what a game can do that it could not

Moved to [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md)
with M5's, for the same reason and on the same day. The filled Gate Record and
the milestone's eighteen Findings are in
[`docs/briefs/m6-kickoff.md`](docs/briefs/m6-kickoff.md).

### M5: what the world can do that it could not

Moved to [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md)
when this file passed its ~300-line cap (§11). Nothing was dropped: the M5 brief
carries the same milestone's seventeen Findings and its Gate Record.

### What does NOT exist yet

M6's seventeen NOT-in-scope items and M5's fifteen, and the ones most likely to
be mistaken for bugs. Six of them are `Inert` properties, which means the engine
says so in the inspector and the api-dump rather than only here — and
`tools/repo/inertcheck.luau` is the gate that keeps a seventh from joining them
quietly.

- **Text is one built-in ASCII face.** `TextLabel.Font` is `Inert` until M7
  vendors Inter; a codepoint the face cannot draw is a visible box, on purpose.
  No `RichText`, no shaping, no kerning.
- **`ImageLabel` draws a flat tint.** `Image`, `ScaleType` and `SliceCenter` are
  all `Inert` until there is a texture pipeline to hand one over (M7).
- **`ScrollFrame` scrolls and draws no bar**; `ScrollBarThickness` is `Inert`.
- **A `Sound` plays a generated tone of its declared length.** `Content` is
  `Inert`. The timeline, the events, the mixing, the spatialization and the
  group volumes are real; the file is not.
- **`Touched` does not fire for a character's SIDE contacts** — D028's remaining
  half. The surface under its feet does.
- **`BasePart.Material` is not shipped**, and neither is `RaycastResult.Material`.
- **`Enum.CollisionFidelity` round-trips and every value collides as a box**;
  a hull needs mesh geometry the mirror cannot see until M7. `Inert`.
- No joints or solver constraints beyond the transform weld; no sleeping policy
  exposed; no `saveState`/`restoreState` (declared, refuses); Jolt runs
  single-threaded until M7 wires the job system.

## Now / Next

- **Every open defect is in [`docs/defects.md`](docs/defects.md)**, which is
  append-only and checked by the docs gate for gaps, states and dangling
  citations. That file exists because three human-reported defects were removed
  from this one while it was being rewritten to close M4. **A close rewrites this
  file wholesale; it can no longer take the open list with it.**
- **D004 — the inspector crash while dragging `Size`/`CFrame` — is still open and
  still not reproduced.** Two halves are ruled out: the write path driven through
  zero, negative, 1e30 and infinity with a render extraction every frame, and 25
  minimize/restore cycles over 900 windowed frames. What remains is the ImGui
  half, and the crash handler is in place for the next occurrence — the next
  report should carry `luaug-crash-<pid>.dmp` and `luaug.log` from beside
  whatever was being run.
- **D018 — `luaug_net_tests` hung once** on Windows and passed on a re-run. §12
  quarantines on the second occurrence; this is the entry that makes a second
  one countable.
- **The build agreeing is not evidence that the build read your file.** A
  break-verification restored with `Copy-Item` kept the source's old timestamp,
  Ninja rebuilt nothing, and a passing fix was reported as failing against code
  that was no longer on disk. Restore with `cp` and `touch`. **M6 found the same
  shape from the other end**: a `DrawQuad` that grew by one field left a stale
  object behind and produced a segfault in a test that had nothing to do with the
  change. `--clean-first` on the module was the answer, and "a mysterious crash
  right after a struct changed size" is now a known first thing to check.
- **D028's remaining half — `Touched` for a character's SIDE contacts.** The
  ground half is fixed; a wall walked into still fires nothing, because the
  character's non-ground contacts are not on the `IPhysics3D` seam. That is a
  seam widening rather than a fix.
- **D026 — the capture gate records an upload's SIZE and not its contents**, so
  the quads a frame draws are invisible to the blocking render gate. What holds
  the line meanwhile is in the row.
- **M7's first step, written out so it is not re-derived**: the asset pipeline is
  what four `Inert` markers now name. `TextLabel.Font`, `ImageLabel.Image`,
  `ImageLabel.ScaleType`, `ImageLabel.SliceCenter`, `ScrollFrame.ScrollBarThickness`
  and `MeshPart.CollisionFidelity` are each stored, read back, and acted on by
  nothing — and `tools/repo/inertcheck.luau` is the gate that will not let a
  seventh join them quietly.
- **Three of M6's decisions went against a document, and all three are ADRs.**
  0039 (an `InputContext` declares its dispatch rate; the IAS's enums are total),
  0040 (a `UDim2` placement is arithmetic, so v1 does not call Clay), and 0041
  (`InputService` gains the raw event surface ADR 0029 had ruled out). 0040 is
  the one a reviewer should read for the process: the milestone declined to use a
  dependency it had vendored, did NOT remove it because that is not the agent's
  call, and the human answered on 2026-08-21 — so Clay is un-vendored now. 0041
  is the one to read for the judgement: the technical objection to raw events was
  never to events, it was to events read from the OS, and every part of it
  dissolved once they came out of the IAS's own pipeline. What changed the answer
  was a PRODUCT argument the agent had not weighed.
- **The renderer submits one draw call per visible object, and that is the
  engine's real ceiling for a crowd.** Measured 2026-08-20 against a
  survivors-like horde: two thousand enemies run at 11.1 ms a frame, of which
  1.8 ms is the whole simulation and 6.9 ms is submitting 2,092 forward draws —
  and the same scene costs the same 10.2 ms at 320×180 as at 4K, for twelve
  thousand triangles. Instanced draws are now named M7.5 scope with that number
  attached; the table is in `docs/perf-baselines.md`.
- **The physics mirror costs ~160 ns per body per tick to decide nothing
  changed.** Recorded in `docs/perf-baselines.md`. The remaining fix is a
  dirty-flag design, and it belongs with M7's streaming rather than with M6.
- **A check on a moving thing names a window, not a moment.** Three M5 test
  cases in a row failed by measuring after the thing they were testing: a
  character crosses a six-metre ledge in a second and a half, so a check taken at
  the end reads "never climbed" and means "climbed and kept going".
- **A picture of two things at once catches what neither test can.** The Jolt
  debug-draw bridge found, on the frame it first drew, that `CharacterBody` was
  the one `BasePart` whose `Position` meant its feet. Every test passed.
- **Physics arriving changed what every scene already in the repository meant.**
  An unanchored part is a rigid body, and every example, fixture and benchmark
  predates that. The proof the change is inert where it should be is that
  `capture_gate_meshes` passes against the unchanged M4.5 golden.
- **A gate can be stronger than the guarantee it rests on, and get away with it
  until the day it cannot.** Committing a determinism trace and checking it on CI
  is a cross-BUILD check; ADR 0025 promises same-build. Four milestones of
  integer and tree state paid nothing for the gap, and the first floating-point
  scenario spent it — on CI, at tick 600 of 3,600, with nothing about the gate
  having changed.
- **A gate that can pass while doing nothing keeps being built by accident.**
  Twelve instances in six milestones. M5's was a conformance run reporting "938
  passed, 0 failed" over a suite that had silently lost seventeen cases to a
  syntax error.
- **A golden cannot detect a defect that was present when it was recorded.** It
  asserts stability, not correctness, and it *feels* like the other one. The
  shape that catches this is a differential.
- **Test the step that resolves, not the step that computes.** M5 acted on this:
  the host test asserts that a part falls in a world no script touched, because a
  mirror that was never handed a `Workspace` produces a world where nothing does
  and every API-level test still passes.
- **The Linux tier is the only thing between this repository and a whole family
  of defects** MSVC does not mention — M5 added an ABI one to the family: Jolt
  compiled `-fno-rtti` emits no typeinfo, so a class deriving from one of its
  types fails to link on Clang and links fine on MSVC.
- Carried forward, none blocking:
  - **Two of the five generated artifacts api-design.md §5 lists do not exist**:
    the typed `@std`/`@luaug` stubs and `docs/reference/**`.
  - **The shipping profile does not configure**, and needs a bytecode-loading
    path that does not exist. Scheduled with `luaug build` at M8.
  - **DXIL produced on Linux is never verified as signed.** M8.
  - **The message catalog does not load inside the APK.** `Catalog::loadFromFile`
    uses `std::filesystem`; `loadFromJson` plus `platform::readTextFile` is the
    fix.

## Blocked — needs human

- **ANSWERED 2026-08-20 — the font is an asset, and the engine ships exactly
  one.** `TextLabel.Font` is already typed `Content`, so a game supplies its own
  face by URI the way a `MeshPart` supplies a mesh; that half of the design is
  done. What the human decided is the default: **Inter (OFL 1.1)**, chosen for
  legibility at UI sizes. Roboto (Apache-2.0) is the alternative if matching the
  repository's own licence family is worth more than the typeface, and it is a
  one-word change. Not several faces: one good default plus "bring your own"
  covers the ground, and three vendored faces are three things to licence,
  update and explain.

  **Three consequences the human named, and one of them binds M6 rather than
  M7.** They are recorded in the roadmap so they are not rediscovered:
  - **The glyph store must be a CACHE, not a boot-time bake** — keyed by face,
    size and codepoint, filled on demand. **Done at M6**: the key is all three,
    the bound is 2,048 entries with a clear-on-full policy and a log line, and
    sizes are quantized to a quarter pixel so a tweened `TextSize` does not mint
    an entry per frame.
  - **Unicode stops being optional.** **Done at M6**: text is decoded as UTF-8
    into codepoints and one the face cannot draw gets a visible replacement box —
    not nothing, not a question mark, and not the mojibake reading the bytes one
    at a time produced. `missingGlyphs` counts distinct characters the face could
    not draw, so a label full of boxes is a number before it is a bug report.
  - **`TextLabel.Font` stops being `Inert`** when the face lands, and the marker
    goes with it. Still M7's.

- **ANSWERED 2026-08-20 — the Clay row is removed.** ADR 0040 established that a
  `UDim2` placement is arithmetic and that Clay cannot express an unclamped scale
  or a fractional anchor point, so this is not "not used yet", it is "does not
  fit the model". A vendored dependency nobody calls still enters every build,
  every notices file and every future reader's half hour. The ADR stays in the
  tree if a genuinely flow-shaped feature (`UIGridLayout` was the example) ever
  wants it back. **Done 2026-08-21**: `third_party/clay/`, its manifest row and
  its notices row are gone, and ADR 0040 carries the answer so that re-vendoring
  is an ADR and a manifest row rather than a rediscovery.

- **ANSWERED 2026-08-21 — narrowing is allowed, on the condition that a machine
  checks it (ADR 0042).** `basis_universal` at `v2_50` is **302 MB**, of which
  275 MB is test images, WebGL demos, Python wheels and **49 MB of prebuilt
  binaries** — the last of which is exactly what ADR 0032 exists to keep out of
  git history, while ADR 0021 says a vendored tree is "exact upstream content".
  One of the two had to give, and giving on 0021 costs less: what landed is
  still byte-exact at the pinned commit, and 302 MB per pin bump is paid by
  every future clone forever. 13 MB instead of 302.

  **The condition is about the future, not about now**: a hand-curated `include`
  list rots — at the next pin bump upstream moves a file out of the listed paths
  and the build either breaks obscurely or quietly stops compiling something.
  **Done**: `vendor.luau status` verifies a narrowed row and the Luau gate runs
  it, checking that every listed pathspec still selects something AND that every
  path this repository's own build files name inside the tree is covered by the
  list. Break-verified both ways. The check is textual on purpose: asking CMake
  would need a configured build tree, and a guard nobody runs is not a guard.

- **ANSWERED 2026-08-21 — ENet only; GameNetworkingSockets waits, with the
  reason written down.** v1 ships primitives and M7's scope is a loopback echo,
  so what v1 needs is the SEAM rather than the implementation behind it.
  `ITransport` is frozen with ENet as its one implementation. GNS depends on
  OpenSSL-or-libsodium plus protobuf — a code generator that invades the build
  and a compilation problem on four tiers — bought for a seam with no v1 caller.
  Its manifest row keeps `TBD` and now carries the reasoning, the way the Jolt
  row did until M5: **a decision deferred with a reason on it is a different
  thing from a decision forgotten**, and whoever builds replication post-v1
  finds the argument instead of re-deriving it.

- **Two agent sessions work in this repository, and the tree is divided
  (human protocol, 2026-08-21).** The other window is a REVIEW agent: it plays
  the engine, finds defects, and records decisions. Five of M6's defects came
  from there (D027, D029, D030, D031 and the waver confirmation), as did the
  roadmap items marked "human decision".

  - **`PROGRESS.md`, `engine/`, `api/`, `tests/`, `shaders/` and `tools/` are
    the builder's.** The reviewer does not write them.
  - **`docs/roadmap.md`, `docs/defects.md`, `docs/decisions/`, `README.md`,
    `CLAUDE.md` and the briefs are where the reviewer writes.**
  - **Scope can arrive after a kickoff imported it**, and it has four times
    (pivot, weld, solid `Part`, `@luaug/input`). The reviewer annotates the
    brief saying it came out of order; the builder **re-reads the roadmap and
    the brief before each big step**. This is why: the M4 close rewrote
    `PROGRESS.md` and dropped three open defects, which is what
    `docs/defects.md` was born from.

- **The Android run of `examples/02-meshes` is due.** Deferred by the human
  until M4.5 closed, and M4.5 and M5 have both closed since. It is a device
  checkpoint rather than milestone work, and it is here so that it is asked for
  rather than forgotten.
- **A judgement I made that a reviewer may want to remake, and M6 moved the
  number again.** `churn10k` read 2.02 ms/tick at M2, 4.96 at M5 when its scene
  became ten thousand rigid bodies, and **7.32 now**: two thirds of its anchored
  parts are written every tick, so D031 makes two thirds of them kinematic bodies
  in the broadphase layer Jolt re-fits each tick. That is the semantic the fix
  exists to provide, applied to a scene that was never written to be a physics
  test. It is under its 16 ms budget at every step and itemised in the baselines.
  If the answer is "that is a regression", §8 wants an ADR; the two candidate
  answers are the mirror's dirty-flag design and a `churn10k` whose moving parts
  stop being anchored.
- **One decision worth a word, and it is cheap either way.** Jolt's
  `CROSS_PLATFORM_DETERMINISTIC` build switch is off, which is ADR 0025's level B
  rather than an oversight — upstream documents it as buying determinism across
  compiler, OS and architecture for about 8% of the library's speed
  (`third_party/jolt/Docs/Architecture.md:792`). Turning it on is one line and
  would likely make the win↔linux comparison green; it is a human decision with a
  performance bill, so it is asked rather than assumed.

## Decisions pending ADR

- (none — ADR 0035 was written at M3 kickoff)

## Session Log

Entries for the planning session and for M0 through M4 are in
[`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md), moved
there when this file passed its ~300-line cap.

- **2026-08-21 (session 12, Claude Opus): M7 started; the substrate and the
  offline pipeline are in.** The brief with its eleven decisions, then
  `engine/jobs` (work-stealing pool, dependencies, `parallelFor`, and
  `StableCommit` as R10's commit rule made into a type), `platform`'s async IO
  with a priority queue SDL does not have, `core::ContentHash` on a vendored
  BLAKE3, the `.lpack` container, the `.lmesh` format with LOD chains and
  meshlets, basis_universal in two targets, `tools/assetc`, and
  `luaug build-assets --verify`. Five stages green throughout; 36 ctest targets
  on Windows and 35 in the container.

  **The proof the pipeline is real is a differential, not a screenshot**:
  `examples/02-meshes` renders byte-identically -- 0 differing pixels at
  1280x720 -- with its `content/` directory REMOVED and only the pack mounted.
  A screenshot would have proved something drew; that removal is what proves
  the pack drew it.

  Learned, and the one to keep: **a bounds-checked reader is not a safe reader,
  because the allocation happens first.** Every offset in the mesh format was
  checked before it was followed and the corruption case still threw
  `bad_alloc` -- a flipped bit in a section's element COUNT reached
  `vector::resize` before anything compared it against the bytes the section
  had. A count is an input too, and it is the one that does not look like a
  pointer. The second: **D018 reproduced and was cheaper to look at than to
  quarantine.** Two commands on the hung process -- one socket in `LISTEN`, two
  threads waiting -- and it was `::accept` with no deadline, turning the state a
  FAILING test leaves behind into a suite that hangs forever.

- **2026-08-21 (session 11, Claude Opus): M6 built and signed off.** Moved to
  the archive with the rest of M6.
