# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **M8 — Flagship, Hardening, Docs, v1.0 — COMPLETE, signed off 2026-08-22**,
  tagged `milestone/m8` and `v1.0.0`. **The tags are local: pushing them and
  creating the GitHub release need an account, and that is the one part of M8's
  scope still waiting on a person** (below).
  The brief is [`docs/briefs/m8-kickoff.md`](docs/briefs/m8-kickoff.md), with
  five decisions, twelve Findings and a filled Gate Record.

  **What the review rounds cost and were worth.** The sign-off came after five
  more defects the human found by playing it — D050 through D054 — every one of
  them invisible to every gate in this repository, and the last of them
  (D055) came from reading the gate record's own list of what the milestone had
  measured and not fixed. That is eleven defects in one milestone, eight of
  them from a person running the thing while the whole suite was green.

  **The deliverable is [`examples/10-open-world`](examples/10-open-world/)** — a
  third-person character walking 4.35 km of streamed terrain under a sun that
  crosses the sky, with a HUD, ambient sound, physics, mouse look and hot reload
  that puts you back where you were standing. **Play it before reading anything
  else**; the rest of this entry is what it took.

  **The numbers that bind.** The ten-minute soak at 1080p on the reference
  machine: median **5.35 ms**, p99 **8.79 ms**, **one frame of 35,939** over
  16.7 ms, zero streaming hitches, peak resident equal to final resident at
  168 MiB, instances flat at ~4,300. The absolute targets in
  `docs/perf-baselines.md` said "bind at M8" and now say what they measured.

  **Eight defects came from a human running the demo while every gate was green**,
  which is the pattern this project keeps paying for and keeps being right about.
  D047: the world vibrated as you walked — the engine had never interpolated
  between ticks, and `Frame::alpha` had been computed and read by nothing since
  M1. D048: the shadows crawled — three causes, and the root was a character
  sliding off a tile corner at 3 cm/s, which moved the camera, which moved the
  whole shadow lattice. D049: `InputService.PointerLocked` was stored and read by
  nothing, so a mouse look had nothing to switch on, and the camera turned the
  way you did not push. D050: the shadows flickered under a moving sun — the
  texel snap rounded an ABSOLUTE position in a light space that turns, so the
  lattice swept out from under every shadow in the picture; rounding the box's
  MOVEMENT instead is the fix, and it is the stronger statement anyway. D051:
  objects hovered over their own shadows — a normal offset of 1.41 filters and a
  2 cm depth bias, both paying for an acne that this renderer's front-face cull
  already prevents. D052: the shadows were coarse at a moderate distance, and
  the flagship's own project file was why — `shadow_distance = 180` on the
  preset's 1024 tile put everything past forty-four metres on a **half-metre
  grid**; 140 m on a 2048 tile measures 0.20 m per texel and costs nothing, the
  larger tile's fill being handed back by the shorter distance's culled casters.
  The same measurement convicted `ultra`, which had been spending its
  four-times atlas on range rather than density (0.32 m per texel against
  `high`'s 0.35), and the test written for it caught a third thing:
  `--quality=low` took the Low preset and then let the project file put its
  4096-pixel atlas back on top. D053: from about eleven in the morning of the
  in-game clock the world went temporally unstable with nothing moving — two
  defects under one symptom, separated by the report's own detail that it began
  at an HOUR rather than after a duration. The larger half was not a shadow at
  all: the diffuse ambient rode on the specular chain's rebuild threshold, so
  the light every matte surface receives held still for a hundred frames and
  then stepped by a per cent — 68% of the ground changing on one frame while the
  sky beside it did not. It has its own threshold now, and the shader's copy
  walks towards it: **peak-to-mean 6.99 became 1.27**, at 0.2 ms. The smaller
  half is the shadow edge and is inherent — a cascade's lattice lives in the
  light's rotating frame, so a fixed point drifts across it twenty-five times
  faster near noon than at dawn — and is mitigated by a penumbra one texel wider
  and by D052's finer texels. D054 is the third report about that same edge and
  the one that named the relation: the flicker scales with camera DISTANCE.
  Measuring it turned the relation around — the step is about the same size on
  screen at every distance, and what changes is the frequency, from every other
  frame in the near cascade to one jump every twenty-three frames in the far
  one, which is why one reads as motion and the other as a jump. The penumbra's
  floor is six texels now rather than two, paid for by rotating the 5×5 kernel
  per pixel so a wide fixed grid's banding becomes fine noise: pixels changing
  by more than four levels between consecutive frames fell from 62 to 14, and
  the worst single change from 37 to 14. **Its second round** measured the edge
  properly — a burst of 120 frames from one run, the edge's sub-pixel position
  tracked by a profile crossing that the ambient cannot move — and found it
  holding still for up to twenty-five frames, then jumping 3.6 pixels, twice
  backwards. The cause was the filter's own arithmetic: it returns a COUNT of
  taps, so one texel of the map flipping moves the edge by a twenty-fifth of
  the penumbra. Forty-nine taps rather than twenty-five made the worst step
  **0.75 pixels**, and the median frame did not move. Two more were found by the milestone's own work: D045
  (`luaug new` could not find its template and had not since M3) and D046 (two
  gates depended on a generated world that nothing generated).

  **What is new that a reader should know about**: render interpolation
  (`render::TransformHistory`); the graphics settings family (ADR 0044) with
  presets, `luaug.toml` and flags, gated by a differential rather than a golden;
  `luaug build` producing a folder that runs, with the game's own icon read back
  out of the artifact (ADR 0045); the editor-seam proof, which is ADR 0017's
  four-milestone-old condition finally checked — two worlds, two VMs, two
  targets; a TOML reader in `core`; a licence audit as a standing check; the
  generated API reference under `docs/api/`; and `@luaug/camera`.

- **M7.5 — Looking Like an Engine — COMPLETE, signed off 2026-08-22**, tagged
  `milestone/m7.5`. Four cascades, clustered forward shading, image-based
  lighting and the post chain; **15,390 draw calls became 22** for the same 4,002
  visible objects. Its sixteen decisions, seventeen Findings and filled Gate
  Record are in [`docs/briefs/m7.5-kickoff.md`](docs/briefs/m7.5-kickoff.md), and
  the pair of pictures that is its deliverable is in `docs/images/m7.5/`.
  **The one thing to carry forward**: five defects were recorded and seven more
  found inside the milestone, and seven of the ten were found by looking at a
  picture or a number rather than by a test — D043 most of all, where the
  instanced path shipped drawing nothing and three independent green instruments
  agreed with the empty frame.
- **M7 — Scaling the World — COMPLETE, signed off 2026-08-21**, tagged
  `milestone/m7`. The offline asset pipeline, the job system and async IO, the
  per-World floating origin, chunked streaming, runtime LOD, the transport seam
  and the assimp importer. Eleven Findings and a filled Gate Record with its six
  measured items are in [`docs/briefs/m7-kickoff.md`](docs/briefs/m7-kickoff.md).
  **The one to carry forward is D040**: header changes had been rebuilding
  NOTHING on Windows since the project started, so every incremental build was
  silently reusing stale objects — which is why `chcp 65001` is in the gate.
- **M6 — Playing the World — COMPLETE, signed off 2026-08-21**, tagged
  `milestone/m6`. Input, UI, tweens, audio and skeletal animation, with
  `examples/04-obby` playable end to end; three ADRs (0039, 0040, 0041), fifteen
  decisions and a filled Gate Record in
  [`docs/briefs/m6-kickoff.md`](docs/briefs/m6-kickoff.md). **Five of its nine
  defects were found by a person playing the deliverable**, which is the pattern
  every milestone since has repeated.
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

### The state before this one, and what does not exist yet

M5's and M6's entries, and the NOT-in-scope list they carried, are in
[`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) — moved
there at the M8 close, when this file passed its ~300-line cap again (§11).
Nothing was dropped: each milestone's brief carries its own Gate Record and
Findings, `CHANGELOG.md` §1.0.0 lists what v1 ships, and the roadmap's R15 list
says what v1 deliberately does not.

**The one part of that list a reader is most likely to mistake for a bug**:
`BasePart.Material` is not shipped, and neither is `RaycastResult.Material`.
Every other `Inert` property M6 shipped was made real by M7 or M7.5, and
`tools/repo/inertcheck.luau` — which sweeps both component pools and
`EngineState` since D055 — is what keeps a new one from joining them quietly.

## Now / Next

- **Every open defect is in [`docs/defects.md`](docs/defects.md)**, which is
  append-only and checked by the docs gate for gaps, states and dangling
  citations. That file exists because three human-reported defects were removed
  from this one while it was being rewritten to close M4. **A close rewrites this
  file wholesale; it can no longer take the open list with it.**
- **D004 — the inspector crash while dragging `Size`/`CFrame` — is still open and
  still not reproduced**, and it is the only open row in the register. Two halves
  are ruled out: the write path driven through zero, negative, 1e30 and infinity
  with a render extraction every frame, and 25 minimize/restore cycles over 900
  windowed frames. What remains is the ImGui half, and the crash handler is in
  place for the next occurrence — the next report should carry
  `luaug-crash-<pid>.dmp` and `luaug.log` from beside whatever was being run.
- **`inertcheck` sweeps `EngineState` now, and widening it found three more**
  (D055). The blind spot was real and it was the size of a service: a knob that
  belongs to a service with one instance per world lives in the world's own
  struct rather than in a component pool, so half the properties in the API were
  never swept — which is how `PointerLocked` sat stored and unread from M6 to
  M8 (D049). The three it caught the first time it ran: `DebugService.OverlayVisible`
  was stored and the overlay was toggled only by F3; `UIService.DisplayScale`
  said in its own comment that the host wrote it every frame and the host never
  did, so it was 1 on a doubled display; and `StreamingService.PauseOutsideLoadedArea`
  had a reader waiting for it — `minimumRingResident()`, whose comment says it
  "is what `PauseOutsideLoadedArea` reads" — that nothing ever called. All three
  are wired, and the pause is break-verified: with a minimum ring nothing can
  satisfy, 240 frames produce zero ticks with the property on and ticks with it
  off.
- **A sequence of GPU runs is not a sequence of measurements** (M8 Finding 5).
  The same baseline run measured 6.25 ms first in a sweep and 4.83 ms last, and
  an earlier sweep taken without a warm-up reported `--quality=low` as slower
  than `high`. `docs/perf-baselines.md` records the protocol; anybody comparing
  render features should read it before trusting a table.
- **The build agreeing is not evidence that the build read your file, and on
  Windows it was not evidence that it read your HEADER either.** D040: ninja
  recorded no header dependencies at all, so `--clean-first` was load-bearing
  for two milestones and nobody knew why. Fixed by `chcp 65001`, which the gate
  now sets. The older half still applies: a break-verification restored with
  `Copy-Item` keeps the source's old timestamp and rebuilds nothing — restore
  with `cp` and `touch`.
- **macOS is unverified for M7, M7.5 and M8.** CI has executed zero steps since
  2026-08-21 (the quota signature described above), so the last three tags carry
  a Tier-1 and Tier-2 result and no Tier-3 one. That is a gap in the evidence
  rather than a failure, and clearing Actions is what closes it.
- **What M8 deliberately does not have** is at the end of its Gate Record. The
  two worth carrying forward: `luaug build` produces a Windows folder and refuses
  every other target rather than approximating one, and the packaged game ships
  Luau SOURCE rather than bytecode (ADR 0045 says what it would take).

## Blocked — needs human

- **THE RELEASE ITSELF, and it is the last item in the roadmap.** M8's scope ends
  with "tag `v1.0.0`, GitHub release with Windows binaries + source
  instructions", and every part of that needs a person:
  - **The sign-off is DONE** (2026-08-22). `MASTER_PROMPT.md` §13 asks for a
    green M8 gate record and a human who has played `examples/10-open-world` and
    said ship; both happened, and `milestone/m8` and `v1.0.0` are tagged in this
    clone.
  - **Pushing them and creating the release need an account.** §10 puts
    "anything requiring accounts, credentials, secrets, or spending money" on
    this list without qualification, so the two `git push --tags` and the GitHub
    release are asked for rather than done. `CHANGELOG.md` dates `[1.0.0]` at
    2026-08-22 and its link points at the tag, which now exists locally.
  - **The artifact is buildable today**: `luaug build examples/10-open-world`
    produces the folder a release would attach, and `tests/packaging` runs that
    whole chain on every Windows gate.
  - **And Actions is still dark** (below), so the release would carry a Tier-1
    and Tier-2 result and no macOS one.

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

- **2026-08-22 (session 15, Claude Opus): M8 built AND SIGNED OFF — assemble,
  polish, prove, ship.** The flagship, the graphics settings family,
  `luaug build` and application identity, the editor-seam proof, the performance
  pass, the docs, the licence audit and the CHANGELOG. Five decisions, twelve
  Findings, a filled Gate Record, and eleven defects.

  **Did:** `examples/10-open-world`; render interpolation; ADR 0044 (graphics
  settings), 0045 (a packaged game is a folder), and ADR 0017's condition checked
  four milestones late; a TOML reader in `core`; `tools/iconpatch` and the
  identity chain; `tools/repo/licensecheck.luau`; `api/generator/gen_reference.luau`
  and `docs/api/`; `docs/coming-from-roblox.md` written for real; `@luaug/camera`;
  mouse look with a pointer lock that now actually locks.

  **Learned, and it is the same lesson eight times:** every one of the defects a
  human found — D047 through D054 and the inverted camera — was
  invisible to every gate in the repository, and two of them were invisible in
  any single screenshot because what moved was the CLOCK and a position readout
  showing `-0, 2, -0` for a character creeping in the ninth decimal. The
  instruments that found them afterwards took minutes to build; the D051 one is
  committed as `tests/screenshots/contact`, because the artifact is a single
  pixel at the framing the previous milestone's gate used and three metres of
  bright floor up close. What could not be manufactured was somebody playing it
  and saying what bothered them.

  **And the shadow pair is one lesson twice.** D050 and D051 are both a shadow
  parameter that was defensible in isolation and wrong against this scene: a snap
  reference that assumes a light which does not turn, and a bias sized for a
  renderer that does not cull front faces. Both were found by rendering the same
  scene with one number changed and subtracting the images — the differential
  method D043 forced on this project, applied to a number rather than a code
  path.

  **Also learned:** a sequence of GPU runs is not a sequence of measurements —
  the same baseline measured 6.25 ms first in a sweep and 4.83 ms last, and an
  hour went into a regression that was the measurement order.

  **Then five review rounds, and they were worth more than the build.** The
  human played it and reported, in order: shadows flickering under a moving sun
  (D050 — the texel snap rounded an absolute position in a light space that
  turns); objects hovering over their own shadows (D051 — two biases paying for
  an acne the front-face cull already prevents); shadows coarse at a moderate
  distance (D052 — this project asked for 180 m of shadow on a 1024 tile, which
  is half a metre per texel); the world pulsing after eleven in the morning
  (D053 — the diffuse ambient rode the specular chain's rebuild threshold and
  therefore arrived in steps); and the flicker scaling with camera distance
  (D054 — the filter returns a COUNT of taps, so one texel of the map flipping
  moved an edge by a twenty-fifth of the penumbra). Then the human read the gate
  record's own list of what the milestone had measured and not fixed, and asked
  for that too (D055 — the stored-and-unread lint swept component pools only,
  and widening it found three more properties doing nothing).

  **Signed off 2026-08-22**, `milestone/m8` and `v1.0.0` tagged locally. The
  push and the GitHub release need an account and are asked for.

- **2026-08-21 (session 11, Claude Opus): M6 built and signed off.** Moved to
  the archive with the rest of M6.
