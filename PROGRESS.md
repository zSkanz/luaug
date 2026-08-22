# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **E1 — The Editor Opens — BUILT, awaiting review (2026-08-22).** Post-v1 phase 1,
  opened by human decision the same day v1.0.0 shipped. `luaug edit
  examples/10-open-world` boots a windowed host that draws a dockspace instead of
  a game's overlay: explorer, a viewport rendering the world into its own texture,
  properties and stats, a console, and a layout remembered in the project's
  `.luaug/`. The picture is
  [`docs/images/e1/editor-first-light.png`](docs/images/e1/editor-first-light.png)
  and the brief is [`docs/briefs/e1-kickoff.md`](docs/briefs/e1-kickoff.md), with
  five Findings and a filled Gate Record.

  **The shape was decided by measurement rather than by taste** (ADR 0046). Five
  read-only passes over the repository before a line was written found that
  ADR 0011 had already named the editor on the ImGui side four milestones
  earlier; that a Luau editor is *blocked* rather than expensive, because the game
  VM has no filesystem and R4 does not bend for tooling; and that
  `engine/scene/src/world_hash.cpp:182-278` is a whole-world serializer with a
  `Hasher` where a writer should be — which is E3's answer, found in E1's week.

  **What is honestly missing**, and it is in the Gate Record rather than buried:
  the viewport draws no selection highlight, which was scope and was not built;
  nobody has clicked a part with a real mouse, so the pick path is proven
  headlessly and not visibly; and the ImGui overlay refuses to start without a
  window, so **the editor cannot be screenshotted headlessly at all** — every
  picture of it has to be captured from a real window. A milestone that wants a
  golden of the editor has to make the shell render headlessly first.

- **M8 — Flagship, Hardening, Docs, v1.0 — COMPLETE, signed off 2026-08-22, and
  RELEASED the same day**, tagged `milestone/m8` and `v1.0.0`. Every tag is on
  `origin` and the GitHub release carries the flagship's Windows folder:
  <https://github.com/zSkanz/luaug/releases/tag/v1.0.0>. **The repository is
  still private, so that release is visible to the account and to nobody else** —
  which is a decision rather than an oversight, and it is below.
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

  **Eight of its eleven defects were found by a human running the demo while
  every gate was green** -- D047 through D054, plus the inverted camera. The
  narrative moved to [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md);
  each one is `fixed` in [`docs/defects.md`](docs/defects.md) with its full
  diagnosis. The pattern is the entry's whole point and it has now repeated in
  every milestone since M4.

  **What is new that a reader should know about**: render interpolation
  (`render::TransformHistory`); the graphics settings family (ADR 0044) with
  presets, `luaug.toml` and flags, gated by a differential rather than a golden;
  `luaug build` producing a folder that runs, with the game's own icon read back
  out of the artifact (ADR 0045); the editor-seam proof, which is ADR 0017's
  four-milestone-old condition finally checked — two worlds, two VMs, two
  targets; a TOML reader in `core`; a licence audit as a standing check; the
  generated API reference under `docs/api/`; and `@luaug/camera`.

- **M7.5 and M7 — COMPLETE, signed off 2026-08-22 and 2026-08-21**, tagged.
  Their entries moved to [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md)
  when E1 was written up; the briefs carry the Gate Records. **The one to carry
  forward is D040**: header changes had been rebuilding NOTHING on Windows since
  the project started, which is why `chcp 65001` is in the gate.
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
- **The phase was re-cut at E1's review and ADR 0047 is why** (human decision,
  2026-08-22). The first split put manipulators, saving and play in three
  milestones; the review said in one sentence that edit, test and save are one
  loop and an editor that delivers a third of it three times is not usable in
  between. **E2 is now the loop, whole**, and underneath it the authored world
  becomes DATA and scripts become BEHAVIOUR — the Unity, Unreal and Roblox
  arrangement, asked for in those words. Code-first does not die: `Instance.new`
  at runtime stays first-class, and what moves is where the world a project
  *starts* with is written down.

  **Three things E2 needs that do not exist**, each already named in a comment by
  somebody who expected this day: `World::snapshot()`, cited five times across
  `engine/scene` as the reason every component is trivially copyable and never
  written; a file-writing capability, because `platform/file.h` reads and never
  writes and the game VM must not be the one given it (R4); and the scene format
  itself, whose strongest candidate is `world_hash.cpp:182-278` with a writer
  where its `Hasher` is.

- **D057 is the one open decision, and it has a third exit nobody had named.**
  `luaug build` packages a `dev`-profile host, so the released v1.0.0 binary
  carries the debug overlay and a Luau REPL — verified in the shipped bytes, and
  the release notes now say so. The two obvious fixes are both bad: shipping a
  profile that has never run a test, or reversing ADR 0045 to package bytecode.
  The third is that `LUAUG_DEBUG_UI` and `LUAUG_LUAU_COMPILER` are **independent
  options that only happen to be tied to the same profile string**, so a fourth
  profile — no ImGui, keeps the compiler — costs one `cmake_dependent_option`
  condition. That is a human's call and it is asked here rather than taken.
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

- **THE RELEASE IS PUBLISHED, and what is left of it is one setting.** The
  roadmap's last item — "tag `v1.0.0`, GitHub release with Windows binaries +
  source instructions" — was done on 2026-08-22 on the human's word, against a
  five-stage green gate. `LuauG-Open-World-v1.0.0-win64.zip` (6.7 MiB, from
  `luaug build examples/10-open-world`) is attached, the notes carry the build
  instructions and the known limits, and `milestone/m4`, `m7` and `m7.5` were
  pushed with it — they had never left this clone, while `milestone/m8` and
  `v1.0.0` already had, which is what the ledger got wrong before §2 corrected
  it against the repo.
  - **The repository is PRIVATE, so the release reaches nobody.** Making a repo
    public is a one-way door in practice — the whole history becomes readable
    and cloneable, and un-publishing does not un-clone — so it is §10's kind of
    decision and it is asked rather than taken. It also decides the Actions
    question below: public repositories do not spend quota.
  - **And Actions is still dark**, so the release carries a Tier-1 and Tier-2
    result and no macOS one. The notes say so in the open.

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

- **2026-08-22 (session 17, Claude Opus): E1 built — the editor opens.** Post-v1
  phase 1 opened by human decision, specified, and its first milestone built to
  the point where a person can look at it.

  **Did:** five parallel read-only reconnaissance passes over the repository
  before writing anything; ADR 0046; the phase's five milestones in the roadmap
  with E1 specified and gated; `luaug edit`; an editor mode with a dockspace, a
  viewport rendering into its own texture, and a layout built on first launch and
  remembered; picking as arithmetic with eleven tests aimed at the corners; enum
  identity and documentation reaching the runtime through the generated
  descriptors; and D056 fixed with a gate stage that builds and links the
  `shipping` profile every run.

  **Learned, and it is the same lesson this project keeps buying:** every test
  passed on the first launch of the editor, and the first launch was five panels
  in a pile with the viewport underneath them. `DockSpaceOverViewport` makes
  docking possible and docks nothing. One screenshot said so and nothing else
  could have.

  **Also learned:** a research claim is a lead rather than a fact. Testing one —
  that `shipping` should not compile — found a *different* first failure, and
  following it found that the release published an hour earlier is a development
  build carrying a debug overlay and a Luau REPL. Both are recorded (D056, D057);
  the notes on the release now say what the binary is.

  **Next:** E2 owes the selection highlight first, because it is the one E1 scope
  item that was written down and not built.

- **2026-08-22 (session 16, Claude Opus): v1.0.0 RELEASED.** Ran the §2 boot
  sequence on a repository whose milestones were all closed, and it earned its
  keep at step 2: the ledger said `milestone/m8` and `v1.0.0` were local and both
  were already on `origin`, while `milestone/m4`, `m7` and `m7.5` — which it did
  not mention — were not. The repo wins; the ledger was corrected and the three
  pushed.

  **Did:** the five-stage local gate green (docs 12.3 s, luau 9.7 s, format 14 s,
  windows 62.1 s, linux 77.2 s, 1,109 conformance cases) as the evidence for
  publishing; `luaug build examples/10-open-world` for the artifact; and the
  GitHub release at <https://github.com/zSkanz/luaug/releases/tag/v1.0.0>, with
  the flagship's Windows folder attached at 6.7 MiB and notes that state the
  build-from-source chain, the soak numbers, the known limits and the missing
  macOS tier rather than leaving a reader to find them.

  **Learned:** a tag and a release are different objects, and `gh release list`
  answering nothing while `git ls-remote --tags` answers plenty is what tells
  them apart — the ledger had recorded "tagged" and read it back as "released".

  **Open, and it is a person's:** the repository is private, so the release
  reaches the account and nobody else, and that same setting is what keeps
  Actions dark.

- **2026-08-21 (session 11, Claude Opus): M6 built and signed off.** Moved to
  the archive with the rest of M6.
