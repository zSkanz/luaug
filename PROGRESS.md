# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **E1 — The Editor — COMPLETE, signed off 2026-08-22**, tagged `milestone/e1`. Post-v1 phase 1,
  opened by human decision the same day v1.0.0 shipped. `luaug edit` is an
  application: a menu bar, dockable panels, a viewport you click and fly through,
  **play / pause / step / stop**, **save**, a **content browser** with folders and
  right-click menus, and **undo and redo**. The brief is
  [`docs/briefs/e1-kickoff.md`](docs/briefs/e1-kickoff.md), with the Gate Record,
  what the milestone became, and what it deliberately does not have.

  **Read the brief's first paragraph before planning E2.** This milestone was
  opened as "the editor opens" and closed as an editor, absorbing most of what
  the first cut called E2. Every addition came from a person using it and every
  one was right — but a milestone this size is not a template, and the roadmap
  now says so where the next one is planned.

  **The shape was decided by measurement** (ADR 0046): five read-only passes over
  the repository before a line was written found that ADR 0011 had already named
  the editor on the ImGui side four milestones earlier; that a Luau editor is
  *blocked* rather than expensive, because the game VM has no filesystem and R4
  does not bend for tooling; and that `world_hash.cpp`'s deterministic walk is a
  whole-world serializer with a `Hasher` where a writer should be.

  **And the world became data** (ADR 0047, human decision): a scene is an asset
  under `content/`, a project declares which one a RUN starts with, and the
  editor remembers which one a PERSON had open. `examples/06-scene` is the first
  project here whose world is not in its script. Code-first is not deprecated by
  it — `Instance.new` at runtime stays first-class the way it is in Unity.

  **Seven defects, six of them found by a person opening the thing**, and **five
  of those are one architectural mistake appearing five times**: the editor
  inheriting the game's decisions instead of taking them. The rule that resolves
  them is one sentence and it now governs the tick, the cursor, the audio, the
  camera and the keyboard — *while the editor is editing, the tool owns the
  machine, and pressing play hands it back.*

  **What it does not have**, in full at the end of the brief and in short here:
  no manipulators, no creating an instance, no multi-select; a stop that restores
  the world and not the Luau VM; and ADR 0047's boot order still inverted. And
  one thing that is not a gap but a limit worth knowing: **the ImGui shell cannot
  render headlessly and SDL does not accept injected input**, so there is no
  automated path to a picture of this editor or to a click inside it. Every image
  in `docs/images/e1/` was captured from a real window, and every visual claim in
  the Gate Record rests on a person having looked.

- **M8 — Flagship, Hardening, Docs, v1.0 — COMPLETE and RELEASED 2026-08-22**,
  tagged `milestone/m8` and `v1.0.0`, both on `origin`, with the GitHub release
  at <https://github.com/zSkanz/luaug/releases/tag/v1.0.0>. Its entry moved to
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) when E1
  was written up. **The repository is still private**, so that release reaches
  the account and nobody else — a decision, and it is under Blocked below.
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
- **E2 owes the manipulators and nothing else**, because E1 absorbed the rest.
  Translate, rotate and scale in the viewport; creating an instance; reparenting
  by drag; multi-select. The brief's "what E1 deliberately does not have" is the
  full list and nothing on it was discovered late.
- **`openworld_soak` flaked once and is D066.** It failed E1's first closing gate
  and passed twice immediately after, on the same binary. Not quarantined -- §12
  sets that at twice -- and written down so the second time is recognised. If the
  diagnosis holds, the check is measuring the machine as well as the engine, and
  widening the threshold would remove the only instrument watching a streamed
  world for a leak.

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

- **2026-08-22 (session 17, Claude Opus): E1 built — the editor.** Post-v1
  phase 1 opened by human decision, specified, and built through eight rounds of
  review with the person using it after every one.

  **Did:** five parallel read-only passes over the repository before writing
  anything; ADR 0046 and ADR 0047; the phase's milestones in the roadmap;
  `luaug edit`; the shell, dockspace, viewport, picking and fly camera; the loop
  — play, pause, step, stop, save — on three run states and `World::snapshot`;
  the scene format and `examples/06-scene`; the content browser; the application
  menu; context menus; undo and redo; and D056's `shipping` profile fixed with a
  gate stage that builds and links it every run.

  **Learned, and it is one lesson said five ways.** D058 the world already
  running, D059 the cursor vanishing, D060 the audio playing, D061 the camera
  flickering, D062 the input going two places — five reports, one architectural
  mistake: the editor was inheriting the game's decisions instead of taking them.
  No amount of arbitrating who wins settles that, because the disagreement IS the
  design. The camera was the clearest: it wrote `Workspace.CurrentCamera`, which
  made the tool and the game two authors of one transform, and the fix was for
  the editor to own a camera the world does not contain.

  **Also learned, and it cost a round of somebody's time**: a patch that asserts
  half way and writes at the end writes NOTHING, and the build passed anyway
  because the signature it would have changed had not changed either. I reported
  the context menus as done on the strength of having run the patch rather than
  having read the result (D064). Every script that edits this repository now
  re-reads the file after writing and fails if what it added is not there — **an
  edit that cannot be verified is an edit that did not happen.**

  **And a third time the same shape**: `setPointerLocked`'s result was discarded
  with a `(void)`, so a refused pointer lock and a granted one looked identical
  from outside. The log that replaced it is what turned D063 from three guesses
  into a fact in one run.

  **Next:** E2 owes the manipulators, creating an instance, reparenting by drag
  and multi-select — and nothing else, because E1 absorbed the rest.

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
