# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **E6 — The Launcher — COMPLETE, signed off 2026-08-24**, tagged
  `milestone/e6`. Opened on the
  human's word an hour after E4 was signed off, for the reason E4's own scope
  list predicted: the archive was unzipped and the first question was *what do I
  open*.
  [ADR 0055](docs/decisions/0055-the-launcher-is-the-engine-with-no-project-open.md)
  settles the shape, [`docs/roadmap.md` § E6](docs/roadmap.md#e6--the-launcher-m)
  is the scope, and [`docs/briefs/e6-kickoff.md`](docs/briefs/e6-kickoff.md)
  carries the gate.

  **You double-click the engine and it asks which project you want.** A third
  shell beside the F3 overlay and the editor's dockspace, with no world, no Luau
  VM and no physics behind it -- a launcher that booted a simulation to show a
  list of folders would be absurd. Choosing a project starts the editor as a new
  process, because everything a project decides is resolved at boot.

  **The quality bar was given as Unity and Unreal, and those two do not agree
  with each other.** Unity's Hub is a separate application whose real job is
  choosing which editor VERSION a project opens with; this engine has one engine
  per installation, and a folder holding two is two folders. Unreal and Godot put
  the browser in the editor binary, which is what ADR 0046 already decided here
  for the editor itself and for the same measured reason.

  **Five reconnaissance passes and all five found the seam already cut**, which
  is why this was one pass: the overlay already had shells, `platform.h` had said
  since M1 that the user directory would "arrive with its first consumer", and
  `SDL_CreateProcess`, `SDL_GetPrefPath` and `SDL_ShowOpenFolderDialog` are all
  in the pinned SDL. `SDL_DIALOG` was one of a sweep of subsystems turned off
  because nothing called them; this is the first caller.

  **And the smoke test found the thing the plan got wrong.** The New Project
  field was seeded from `userDir`, and `SDL_GetPrefPath` returns a trailing
  separator, so `parent_path()` answered the directory itself -- new projects
  landed inside `AppData\Roaming`, where nobody would look for them. Found by
  listing that directory and seeing a whole scaffolded project in it.
  `SDL_FOLDER_DOCUMENTS` is the answer and SDL's own header says so.

  **Signed off with the picture still outstanding**, like E5's: the shell cannot
  render headlessly, so every visual claim rests on the human looking -- which
  for this one they did, message by message, while it was being built, and which
  is where D088 came from.

  **Three things arrived after the sign-off and before the session ended**, all
  asked for while using it: **F frames the selection** -- the one camera shortcut
  every editor in this shape shares, and it keeps the direction and moves only
  the position -- a **Clear and a filter on the console**, and the flagship's
  soak pointed at the scene its generator writes now, which was the last red on
  the tree and is E5's migration finally landing.

  **A defect this milestone did not cause but had to fix**: D086's version bump
  moved `LUAUG_VERSION_STRING`, `WorldHost::boot` writes it into `EngineState`,
  and the world hash walks `EngineState` -- so every determinism trace changed.
  Re-recorded on both tiers. The engine version is observable world state, and a
  hash that ignored part of the world would be the worse answer.

- **E4 — The Editor Ships — COMPLETE, signed off 2026-08-24**, tagged
  `milestone/e4`. Its entry moved to
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) when E6
  was written up. [`docs/briefs/e4-kickoff.md`](docs/briefs/e4-kickoff.md)
  carries the Gate Record, and the one row still pending at sign-off is recorded
  there as pending. **The archive exists**: `LuauG-1.0.0-win64.zip`, built by
  `scripts/package.ps1`, which proves the folder works from outside this
  repository before it compresses it.

- **E5 — The World You Build — BUILT, awaiting review, 2026-08-24.** Landed in
  one pass behind a green six-stage gate, which is what
  [`docs/roadmap.md` § E5](docs/roadmap.md#e5--the-world-you-build-l) asked for:
  a partitioner with no `StreamingMode` writes the wrong cells and a
  `StreamingMode` with no partitioner has no caller.
  [ADR 0053](docs/decisions/0053-the-grid-decides-when-and-the-model-decides-what.md)
  settles it and [`docs/briefs/e5-kickoff.md`](docs/briefs/e5-kickoff.md) carries
  the reconnaissance, the design decisions and the gate.

  **A person builds in `Workspace` and presses play.** The scene is partitioned
  on the way to the first frame, cached by a hash of the file, and `luaug build`
  pre-warms the same directory through the same code -- a `--partition` mode that
  boots and stops, rather than a second path that can go stale.

  **Seven things the specification assumed were already true and were not**, and
  they are the reason this milestone was larger than the ADR's own sizing. Three
  were fixes rather than additions: the chunk index carried a world-space box in
  ONE axis and derived x and z from the cell footprint, so an atomic model that
  overhangs was scored as though it did not; a cell record could not say
  `CanCollide`, a collision group, a friction or a collision fidelity, which
  costs nothing while only a generator writes cells and silently changes an
  AUTHORED world; and a record carried no tags at all, without which the ADR's
  own rule 5 -- address by tag, never by path -- cannot work for a single
  streamed instance. The Streaming panel `docs/manual` has promised since M7 also
  did not exist: `StreamingManager::view` was written "for the overlay the
  deliverable owes" and had no caller anywhere in the tree.

  **And one measurement overturned the design.** The size-class cuts were
  proposed at 8 m and 64 m; measured against the flagship's 26,884 instances they
  put the ground -- 69% of the world, and the one thing that has to be visible at
  distance -- in the middle class and left the long-radius class empty. The
  reason is worth more than the number: the flagship's terrain is not made of
  large features, it is 18,496 tiles of 32 m, and classifying by a PART's extent
  will never find terrain authored as many small pieces. The cuts are 12 m and
  24 m, in the gap the data has, and an `Atomic` model is classified by the whole
  model's extent.

  **The default changes nothing, and it is provable rather than argued.**
  `examples/10-open-world`'s authored scene has exactly one streamable candidate
  and a `Weld` pins it, so the flagship produces zero cells and its residual
  scene is byte-identical to the original. That falls out of the splice: what
  stays authored is copied verbatim and no number is ever reformatted.

  **What is left is a person looking.** The gate item that is deliberately not
  automatable, plus the chunk-state screenshot -- and the same limit E1 recorded
  applies: the ImGui shell cannot render headlessly.

- **E3 — Content and Prefabs — COMPLETE, signed off 2026-08-23.** Its entry
  moved to
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) with
  E1's and E2's when E4 was written up and this file passed its ~300-line cap
  again (§11). [`docs/briefs/e3-kickoff.md`](docs/briefs/e3-kickoff.md) carries
  the Gate Record. The one thing worth keeping here: **three written decisions
  were reversed within a day of shipping**, each by the person who asked for the
  first one, and each reversal took one sentence to argue — because the first
  one was in a file somebody could argue with.

- **E2 — Moving Things — COMPLETE, 2026-08-23.** Its entry moved to
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) with
  E1's, and [`docs/briefs/e2-kickoff.md`](docs/briefs/e2-kickoff.md) carries the
  Gate Record. The pattern worth keeping here: **nine defects, seven found by a
  person using the thing**, and three of them were the same shape -- a piece of
  arithmetic that is right for ONE and wrong for many.

- **E1 — The Editor — COMPLETE, signed off 2026-08-22**, tagged `milestone/e1`.
  Its entry moved to
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) when E5
  was written up and this file passed its ~300-line cap again (§11).
  [`docs/briefs/e1-kickoff.md`](docs/briefs/e1-kickoff.md) carries the Gate
  Record. One limit from it is not a gap and is worth keeping here: **the ImGui
  shell cannot render headlessly and SDL does not accept injected input**, so
  there is no automated path to a picture of the editor or to a click inside it.

- **M8 — Flagship, Hardening, Docs, v1.0 — COMPLETE and RELEASED 2026-08-22**,
  tagged `milestone/m8` and `v1.0.0`, both on `origin`, with the GitHub release
  at <https://github.com/zSkanz/luaug/releases/tag/v1.0.0>. Its entry moved to
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) when E1
  was written up. **The repository is still private**, so that release reaches
  the account and nobody else — a decision, and it is under Blocked below.
- **M7.5, M7, M6, M5 — COMPLETE and tagged**, signed off between 2026-08-20 and
  2026-08-22. Their entries are in
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) and the
  briefs carry the Gate Records. Two things carry forward. **D040**: header
  changes had been rebuilding NOTHING on Windows since the project started, which
  is why `chcp 65001` is in the gate. And the pattern rather than the content —
  **five of M6's nine defects were found by a person playing the deliverable**,
  which every milestone since has repeated and E2 repeated seven times.
- **M4.5 — Correcting the World — COMPLETE, signed off 2026-08-20**, tagged
  `milestone/m4.5`. **M4 — Seeing the World — signed off 2026-08-20**
  (`milestone/m4`), its five gate items green against re-recorded artifacts.
  **M3** (`milestone/m3`), **M2** (`milestone/m2`), **M1**, **M0** — all signed
  off.
- **CI stopped running on 2026-08-21 and it is not the code.** Every job since
  `5a542b7a` completes in about two seconds having executed ZERO steps and
  produced no log — a job that never checked the repository out, which is the
  signature of an Actions quota or a billing block on this private repository and
  is the human's to clear. **So macOS is unverified from M6 onward**; the last
  all-green run across all three tiers is 32429107275, at M5.

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
- **The next action, as a sentence:** capture the chunk-state overlay on
  `examples/06-scene` and fill E5's last gate row, which is the only milestone
  still awaiting review.
- **`main` has no red of its own.** `openworld_soak` was the last one and it is
  fixed: the flagship's generator writes a scene now and the gate ensures that
  rather than a directory of chunk sources — E5's migration, landed. **What is
  red in the WORKING TREE belongs to the other session**: `ui_theme_tests.cpp`
  does not compile under Clang (`-Wdouble-promotion` on six
  `doctest::Approx(float)` calls, the same diagnostic E5 hit), and their
  appearance work in `debug_overlay.cpp` is uncommitted beside it.
- **Two sessions edited `engine/app/src/debug_overlay.cpp` at the same time**,
  which the protocol below does not anticipate — `engine/` is the builder's, and
  something else is writing it. Nothing was lost: this session committed only its
  own hunks and put the other's back on top. The next collision may not be
  separable, and the protocol needs a sentence about it.
- **E5 is still awaiting review**, and what it is waiting for is a person: the
  chunk-state overlay on `examples/06-scene`. Its Gate Record carries every
  other item with a result.
- **What is left of E2, E3, E4 and E5 is one thing and it is the same thing:**
  the pictures. **The ImGui shell cannot render headlessly and SDL does not
  accept injected input**, so every visual claim rests on the human looking —
  which for E3 they did, message by message, while it was being built.
- **Fifteen defects closed across E2 and E3, twelve of them found by a person
  using the editor**, which is now the pattern every milestone since M4 has
  repeated. D067 is why the editor was unusable on the flagship at all: a boot
  scene was applied AFTER the entry scripts had built the world, destroying every
  instance they made while the Luau VM kept the references — `instance_dead` once
  a frame, forever, and a world that never arrived. ADR 0047's order is
  load-then-start and `WorldHost::boot` does it now.
- **`openworld_soak` is QUARANTINED at its second flake** (D066), which is what
  §12 sets. If the diagnosis holds it is measuring the machine as well as the
  engine — and widening the threshold instead would remove the only instrument
  watching a streamed world for a leak.

- **There are five profiles now and the gate builds three of them.** `player`
  (D057) carries the Luau compiler and no ImGui, which is what a packaged game
  needs; `editor` (ADR 0054) is what somebody downloads. Both exist because a
  profile nothing builds is a profile nobody knows is broken, and both are
  compiled and linked by `scripts/gates/shipping-build.sh` for that reason.
  `LUAUG_HOST` is deliberately not consulted by `luaug build`, because "I pointed
  the dev server somewhere" must not decide what a release contains.
- **`inertcheck` sweeps `EngineState` too** (D055), and widening it found three
  properties that were stored and read by nothing -- the blind spot was the size
  of a service, because a knob belonging to a service with one instance per world
  lives in `EngineState` rather than in a component pool.
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

- **THE RELEASE IS PUBLISHED, and what is left of it is one setting.** Tagged
  `v1.0.0` on 2026-08-22 on the human's word, against a five-stage green gate,
  with `LuauG-Open-World-v1.0.0-win64.zip` attached. **There is a second archive
  to attach now** (ADR 0054): `LuauG-1.0.0-win64.zip`, the editor itself, which
  is what turns a release from "here is a game somebody built" into "here is what
  they built it with".
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

- **2026-08-24 (session 22, Claude Opus): E4 built.** The milestone was ordered
  intent and nothing else when the session opened -- the ledger's own next action
  was "decide what E4 is" -- so the session wrote the roadmap section, ADR 0054
  and the brief before touching code, and then built both halves of the row.

  **Did:** the `editor` profile and its presets, built by the shipping gate;
  `tools/repo/package.luau` and `scripts/package.ps1`, which build, write, VERIFY
  and only then compress; `installRoot()` and the three searches it feeds, as
  pure functions a test drives; the version stamp; `tests/installed`, which runs
  the packaged CLI from a scratch directory with the environment emptied;
  `collectVisibleTree` and the Explorer rewired onto it; and the manual's install
  page, where downloading now comes before building from source.

  **Learned, and the packager found it rather than a reading:** `v1.0.0` had been
  tagged and released while the version declaration still said `0.0.1`, so the
  released binary reported 0.0.1 and nothing was wrong anywhere -- every consumer
  derived it faithfully, which is exactly what ADR 0031 asks for. **A derivation
  chain is only as true as the thing at the top of it, and that one had no
  owner.** It stayed invisible for a whole release because no artifact put the
  number in front of a person; the first one that did was a folder named after
  it. D086, fixed, with the packager now refusing a folder whose name would
  disagree with the newest release tag.

  **And a second thing the shape of the last milestone's:** the Explorer's cost
  was invisible to a profiler for the same reason E5's scratch peak was invisible
  to a test -- the expensive part was the bookkeeping, not the visible work.
  Asserting equality rather than smallness is what makes either of them
  falsifiable.

  **Next:** unzip `LuauG-1.0.0-win64.zip` on a machine with no build tree, open
  `examples/06-scene` in it, and fill E4's and E5's Gate Records with what it
  looks like.

- **2026-08-24 (session 21, Claude Opus): E5 built in one pass.** Two commits,
  behind a green six-stage gate.

  **Did:** `Model.StreamingMode` with `Nonatomic` the default; the partitioner,
  which reads the scene as TEXT and builds one authored node at a time into a
  scratch world; the residual splice; the cache under `.luaug/partition/` keyed
  by the scene's hash and validated against the stamps it read; chunk format 2
  with groups, tags and the rest of `BasePart`; size classes on `ChunkId::layer`
  with a radius pair each on `StreamingService`; the streaming map in the debug
  overlay; `--partition` and its use by `luaug build`; and `examples/06-scene`
  grown from six instances to four hundred with no generator in the project.

  **Learned, and a test found it rather than a reading:** the peak measurement --
  partition two worlds an order of magnitude apart and require the SAME peak, not
  merely a small one -- failed on its first run. `World::destroy` marks a subtree
  and defers the generation bump to `retireDestroyed`, which the scheduler calls
  at the end of a drain, and a partitioner has no drain. The scratch world was
  keeping every node it had ever built, which is exactly the "holds the world"
  the class exists to make impossible. A peak that is merely small would have
  passed; requiring it to be EQUAL is what caught it.

  **Learned twice more, both at the seams.** `-Wmissing-field-initializers` fires
  for a field a designated initialiser skips and that has no default member
  initializer -- position in the struct has nothing to do with it, which is what
  `world_host.h`'s own note used to say and now does not. And a partitioner that
  read `StreamingMode` out of the JSON left the component field stored and unread,
  which `inertcheck` refused: reading it off the built component instead is both
  what the check wanted and the better answer, since what an enum name MEANS is
  `readSceneNode`'s to say.

  **Next:** capture the chunk-state overlay on `examples/06-scene` and write E5's
  Gate Record, then migrate the examples still using the generator path.

- **Sessions 19 and 11 (2026-08-23 and 2026-08-21)** are in
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md), moved
  when E4 was written up. The one thing to carry out of session 19: three
  written decisions were reversed within a day of shipping, each by the person
  who asked for the first one, and each reversal took one sentence to argue —
  because the first one was in a file somebody could argue with.
