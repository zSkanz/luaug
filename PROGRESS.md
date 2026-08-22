# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **M7.5 — Looking Like an Engine — done and awaiting review.**
  The brief is [`docs/briefs/m7.5-kickoff.md`](docs/briefs/m7.5-kickoff.md), with
  sixteen decisions, seventeen Findings and a filled Gate Record. **Every scope item
  and every gate item is done**: four cascades in one atlas with a
  world-constant filter and a blend band, clustered forward shading on a 16×9×24
  grid, image-based lighting by the split sum, the post chain — depth prepass,
  ambient occlusion, automatic exposure, bloom, FXAA — instanced draws, and both
  design constraints answered while the pass list was open.
  **The number that is not about looking**: the horde scene goes from
  31.45 ms and **15,390 draw calls** to 3.83 ms and
  **22**, for the same 4,002 visible objects — and those are the numbers from
  AFTER D043, because the first set was measured on a frame that was drawing
  almost none of the scene. The whole new chain costs about a
  millisecond at 1080p, priced per feature.
  **The deliverable is a pair of pictures** in `docs/images/m7.5/`, the
  `milestone/m7` build beside this one at the same camera and clock. The day
  strip is the one to look at: in M7 the sky is the same pale blue at every hour
  and there is no sunrise or sunset at all.
  **The RHI freeze held for everything except one additive field.** ADR 0038
  predicted four places it would break and three of the predictions were wrong in
  the useful direction — the technique changed instead of the interface.
  **ADR 0043 is PROPOSED and needs the human**, because ADR 0037 requires a
  human-approved ADR to touch the frozen surface and this touches it.
  **The review pass also added Decision 16**: geometric specular antialiasing
  ships, measured, and the measurement says it buys nothing in current content —
  both of this engine's specular sources are already band-limited, one of them by
  a roughness floor M4 added for exactly this reason. Written down as a cost with
  its numbers rather than as a feature.
  **Three defects recorded (D041, D042, D043)** and seven more found and fixed
  inside the milestone; seven of the ten were found by looking at a picture or a
  number rather than by a test. **D043 is the one a reviewer should read first**:
  the instanced path shipped drawing nothing — both instanced shaders built the
  per-instance model matrix transposed, so every instanced vertex left the
  frustum — and the milestone's three independent instruments all agreed with
  that frame. The draw-call gate read 22 against 4,002, which is what working
  instancing looks like; the capture goldens matched, because the command stream
  was correct; and the frame was eight times faster, because it was empty. It was
  found by rendering a picture. `screenshot_gate_instanced` is the standing check,
  and the proof is a differential: instanced and non-instanced are now
  byte-identical, where 87% of pixels differed before. **D042 is the other one to
  read**: a human asked what a dark
  square above the obby's character was, and the answer was that skinning had
  been broken since M6 -- every joint but index 0 read past the end of its
  palette, because the indices travel as floats and both skinned shaders
  declared their input as integers. `capture_gate_skinned` ran green through it
  all, because it watches the palette and the palette was right.
- **M7 — Scaling the World — COMPLETE, signed off 2026-08-21**, tagged
  `milestone/m7`.
  The brief is [`docs/briefs/m7-kickoff.md`](docs/briefs/m7-kickoff.md), with a
  filled Gate Record and eleven Findings. **Every scope item and every gate item
  is done**: the offline pipeline, the job system and async IO, the per-World
  floating origin, chunked streaming with `StreamingService`, runtime LOD
  selection by projected screen error, `ITransport` with ENet behind it,
  `@std/net`, the Recast seam with no integration, the assimp importer, Inter,
  mid-air `Jump()`, `examples/05-streaming`, and all five `Inert` markers that
  named this milestone. Two `Inert` markers remain and both are M7.5's.
  **The six gate items, with numbers**: 25 MiB peak against a declared 192 MiB
  ceiling over 17,939 frames; zero hitches over 33 ms inside streaming, worst
  pump 1.9 ms; identical hashes at 1e7 with a differential; 292 files
  byte-identical across two processes; a loopback echo in both directions; 4,000
  bit flips all refused.
  **Nine defects found and fixed (D032–D040)**, eight of them by instruments this
  milestone built — including D040, which turned out to be what M6 Finding 17
  had been all along: header changes were rebuilding NOTHING, so every
  incremental Windows build in the project's history had been silently reusing
  stale objects.
  **The last local gate**: all five stages green, 39 ctest targets on Windows and
  38 in the Tier-2 container, 1,108 conformance cases.
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
  2026-08-20**, tagged `milestone/m5`. Its Gate Record, its seventeen Findings
  and the review round that found D025 are in
  [`docs/briefs/m5-kickoff.md`](docs/briefs/m5-kickoff.md).
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
- **`Touched` fires for every contact a character has**, the wall it walks into
  as well as the surface under its feet (D028, closed 2026-08-21).
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
- **The build agreeing is not evidence that the build read your file, and on
  Windows it was not evidence that it read your HEADER either.** D040: ninja
  recorded no header dependencies at all, so `--clean-first` was load-bearing
  for two milestones and nobody knew why. Fixed by `chcp 65001`, which the gate
  now sets. The older half still applies: a break-verification restored with
  `Copy-Item` keeps the source's old timestamp and rebuilds nothing — restore
  with `cp` and `touch`.
- **D028 is closed, and the seam did not move.** The row predicted a widening of
  `IPhysics3D`; what it took was reading the character's own active contacts
  after the step and putting them through the diff the rigid pairs already go
  through. The ground half moved there too — two mechanisms for one signal is
  how the two come to disagree.
- **D026 is fixed**: an upload records what it carried, not how many bytes it
  was. The obvious fix -- the same content hash `bindUniforms` has used since
  M4.5 -- does not work, and the Tier-2 run is what said so: **a hash has no
  tolerance**, so a computed float one bit from a quantization boundary hashes to
  an unrelated number, and over a few thousand of them something always lands on
  a boundary. What ships is continuous -- the mean of the float words and the
  mean step between them -- beside an exact hash of the words that were never
  floats. The margin is measured: the two compilers agree to better than 1e-4,
  and moving one UI quad four tenths of a pixel moves the mean by 1.4e-2.
- **ADR 0043 is PROPOSED and needs a human.** ADR 0037 requires a human-approved
  ADR to touch the frozen RHI, and M7.5 touches it: one additive field,
  `VertexBufferLayout::perInstance`, defaulting to false. The alternatives that
  needed nothing new are in the ADR and each fails on a number. A rejection is
  contained -- the fallback is a change to `render` alone.
- **The job pool M7 built had no caller until now**, and that is worth carrying
  forward rather than burying in a Finding: nothing in the engine called
  `jobs::init`, so every `parallelFor` had been serial since M7. It is started
  in `engine.cpp` now. What has NOT been done is give it the other callers M7
  built it for -- asset decode, and the Jolt integration ADR 0025 leaves as a
  determinism question.
- **macOS is unverified for M7**, as it was for M6: CI has executed zero steps
  since 2026-08-21 (the quota signature described above), so the `milestone/m7`
  tag carries a Tier-1 and Tier-2 result and no Tier-3 one. That is a gap in
  the evidence rather than a failure, and clearing Actions is what closes it.
- **What M7 deliberately does not have** is listed at the end of its Gate
  Record, and the two worth carrying forward are: no shipped example has a mesh
  dense enough to exercise LOD selection, so `MeshLodDraws` reads zero until one
  does; and `@std/net` is client-side only, with `net.serve` reserved.
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

- **2026-08-21 (session 14, Claude Opus): M7.5 review pass — three items from
  the human, and the instanced path was drawing nothing.**

  **The SSAO noise was not the SSAO** and the three ranked hypotheses were all
  wrong, each refuted by a number rather than by reading. The shipped
  16-sample-plus-blur frame matches a 256-SAMPLE reference to 0.4% on the
  reporter's own metric; its error against that reference is 0.122 where the
  unblurred frame's is 2.110. Relaxing the bilateral tolerance fifty-fold moves
  it by 0.001, both separable passes are present and on different axes, and
  widening the blur makes the image worse rather than better. The variation the
  report measured is there and survives forcing occlusion to one — it is
  silhouette and shadow-edge aliasing on a shallow transparent edge, which is
  FXAA's limit, not the blur's.

  **Geometric specular antialiasing implemented** (`antiAliasedAlpha`,
  Kaplanyan/Tokuyoshi, Filament's constants read from its source and registered as
  U-57 — the recalled 0.25/0.18 were both wrong and the recalled algebra was one
  square out). It ships, and Decision 16 records that it currently buys nothing:
  no reduction in measured temporal flicker and double the error against a 16x
  supersampled reference, because both specular sources here are already
  band-limited — the environment by its own 128-texel octahedral resolution, and
  punctual specular by the `LuaugMinRoughness` floor M4 added for this exact
  reason. Toksvig named as out.

  **D043 — the instanced path shipped drawing nothing**, and the scene built for
  the specular question is what found it: eight identical spheres over an empty
  floor. Both instanced shaders assembled the model matrix from four columns with
  a constructor that fills rows, so every instanced vertex left the frustum. The
  milestone's three instruments all agreed with the empty frame — the draw-call
  gate, the capture goldens and the frame time. Fixed, re-measured (3.83 ms, and
  the control 31.45 ms), and covered by `screenshot_gate_instanced`; instanced and
  non-instanced renders are byte-identical now, against 87% of pixels differing
  before.

  **Motion vectors: recorded, both ways.** FXAA ships, no velocity buffer exists,
  and temporal upscaling is as far away as it was before M7.5 —
  `docs/architecture.md` says it where somebody planning the work will look.

- **2026-08-21 (session 13, Claude Opus): M7.5 built — the renderer's second
  half.** Four cascades in one atlas with a world-constant filter and a blend
  band; clustered forward shading, so the light count stops being eight; image-
  based lighting by the split sum, with the SKY as the environment so a metal
  reflects the hour the script set; a depth prepass, ambient occlusion, automatic
  exposure, bloom and FXAA; instanced draws. Fifteen decisions, twelve Findings,
  a filled Gate Record, and ADR 0043 proposed.

  **The number that is not about looking**: the horde scene goes from 30.99 ms
  and 15,390 draw calls to 3.72 ms and 22, for the same 4,002 visible objects.

  **Six of the eight defects were found by looking at a picture or a number**,
  which is what a milestone whose output is pixels should expect. A pool of light
  clipped into hard bands found a cluster-index layout that seven passing unit
  tests could not see (D041). A frame that cost the same at 320x180 and at 1080p
  found a CPU prefilter priced at zero — and behind it, that **the job pool M7
  built had never been started by anything**. An instrumented shader that drew
  its own contrast found the second of three separate bugs all wearing "the
  anti-aliasing does nothing".

  And a golden was nearly re-recorded against a defect, which is M4's mistake
  exactly. What said otherwise was that the re-record came out byte-identical.

- **2026-08-21 (session 11, Claude Opus): M6 built and signed off.** Moved to
  the archive with the rest of M6.
