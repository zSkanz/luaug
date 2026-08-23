# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **E2 — Moving Things — IN PROGRESS, opened 2026-08-22.** Sixteen commits in,
  every one behind a green six-stage gate. **What is left is two things**:
  Properties over a multi-selection (common properties, and a differing value
  marked as mixed — not started, and there is no mixed-value mechanism anywhere
  in the tree yet), and reparenting by DRAG in the Explorer (`Editor::reparent`
  is built and tested; the drag source and drop target are not). Then the Gate
  Record and a person's sign-off. The brief's "Where E2 stands" section is the
  full account.

  **The manipulators exist**: translate, rotate and scale, world or local axes, a
  grid with a modifier that suspends it, W E R to switch. The rule they are built
  on is one sentence — *a drag is solved against where it STARTED, never against
  last frame* — and two properties fall out of it that are both tests: a drag is
  exact however slowly it is made, and three parts a metre apart are still a metre
  apart afterwards.

  **Nine defects, seven found by a person using the thing**, and three of them
  are the same shape: D070, D071 and D073 are each a piece of arithmetic that is
  right for ONE and wrong for many — one instance, one world, one draw. A
  milestone whose whole subject is "many" was always going to find them.

  **Specified at kickoff from four read-only reconnaissance passes** — the
  method ADR 0046 used to size E1 — in
  [`docs/roadmap.md`](docs/roadmap.md#e2--moving-things-l) and
  [`docs/briefs/e2-kickoff.md`](docs/briefs/e2-kickoff.md). All three things
  those passes found broken are fixed: the undo key that only computed when one
  write was pending, the selection outline that shook four kilometres out, and
  `isEngineOwned` not knowing about `generated`.

  **And a great deal arrived that this brief never mentioned**, which is the
  pattern E1 recorded and this milestone repeated: the icon set was wired end to
  end and then tinted by role, a scene became creatable from the editor as a FILE
  (ADR 0048), and the flagship's world moved into one (D074). Each was asked for
  by the person using the editor, and each is written up where it belongs rather
  than folded into the milestone's own scope.

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

  **Two decisions carry it and both are their own documents**: ADR 0046, the
  editor is a mode of the engine binary drawn in ImGui, decided by five
  reconnaissance passes rather than by taste; and ADR 0047, the authored world is
  data and scripts are behaviour, which is what `examples/06-scene` exists to
  demonstrate. Code-first is not deprecated by either — `Instance.new` at runtime
  stays first-class the way it is in Unity.

  **Seven defects, six of them found by a person opening the thing**, and five
  of those were one architectural mistake appearing five times: the editor
  inheriting the game's decisions instead of taking them. One sentence resolves
  them and it governs the tick, the cursor, the audio, the camera and the
  keyboard — *while the editor is editing, the tool owns the machine, and
  pressing play hands it back.*

  **What it does not have** is in full at the end of the brief; E2 owns the
  manipulators, creating an instance and multi-select, and what remains after
  that is a stop that restores the world and not the Luau VM. **ADR 0047's boot
  order is no longer on that list** — it was fixed as D067, and the bill for
  shipping it inverted is written there. One limit is not a gap and is worth
  knowing: **the ImGui shell cannot render headlessly and SDL does not accept
  injected input**, so there is no automated path to a picture of this editor or
  to a click inside it. Every image in `docs/images/e1/` was captured from a real
  window.

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
- **M6, M5 — COMPLETE, signed off 2026-08-21 and 2026-08-20**, tagged. Their
  entries and their Gate Records are in the briefs and in
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md). The one
  to carry forward is the pattern rather than the content: **five of M6's nine
  defects were found by a person playing the deliverable**, which every milestone
  since has repeated and E2 repeated seven times.
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
- **E2's next action, as a sentence:** add `collectCommonProperties` and a
  shared-value query to `inspector.h` as free functions — the panel cannot be
  driven headlessly, so what it DECIDES has to live where a test can reach it —
  then make `drawEditor` take a span and mark a differing value as mixed. After
  that, `BeginDragDropSource` on the Explorer's row and `AcceptDragDropPayload`
  on it, feeding `EditorCommands::reparentTo`, which is already drained.
- **All four of E2's frozen interfaces are built and tested**, which is the point
  the plan said nothing fans out before: the selection set, the gesture and its
  extracted undo key, the manipulator arithmetic, and `Editor`'s verbs.
- **Nine defects closed in this milestone and seven of them came from a person
  using the editor**, which is now the pattern every milestone since M4 has
  repeated. D067 and D068 are why the editor was unusable on the flagship at all. A boot scene was applied AFTER the entry scripts had built the
  world, so it destroyed every instance they made while the Luau VM kept the
  references and the connections — `instance_dead` once a frame, forever, and a
  world that never arrived. The order ADR 0047 specifies is load-then-start and
  `WorldHost::boot` does it now. It also fixes a hot reload that used to drop the
  scene entirely. D068 is how that project got a scene at all: Save Scene As
  wrote into `content/content/` and said nothing.
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

- **2026-08-23 (session 19, Claude Opus): E2's manipulators, and eight defects a
  person found.** Sixteen commits, each behind a green six-stage gate.

  **Did:** the four frozen interfaces — the selection as a set, the edit as a
  gesture, the manipulator arithmetic, and `Editor`'s verbs — then the
  manipulators themselves: translate, rotate and scale, world or local, a grid
  and a modifier that suspends it. The selection outline became a silhouette. The
  icon set was wired end to end and then tinted by role. `Script` became creatable
  as a FILE (ADR 0048), and the flagship's world moved into a scene (D074), which
  is the arrangement the human asked for in four words: like Unity and Unreal.

  **Learned, and it is one shape said three times.** D070, D071 and D073 are each
  a piece of arithmetic or a rule that is right for ONE and wrong for many — one
  world's transform history, one selection, one draw in a batch. Nothing was
  wrong with any of them when it was written; a milestone whose whole subject is
  "many" is what asks the bigger question. D073 is the one to carry: I solved a
  tool's problem inside `buildInstanceBatches`, which every pass of the frame
  depends on, and it took every boulder and every tree canopy out of the flagship.
  The cost of a tool belongs in the tool's pass.

  **Also learned, twice in one day:** a POST_BUILD stage only runs when the target
  relinks. An edit to `@luaug/camera` did not reach the STAGED copy, so a fix that
  was correct in the source read as broken in the run — the same trap `CLAUDE.md`
  documents for shaders, at the same price.

  **Next:** `collectCommonProperties` and a shared-value query as free functions
  in `inspector.h`, then `drawEditor` over a span with a mixed marker; then
  drag-and-drop on the Explorer's rows feeding `EditorCommands::reparentTo`,
  which is already drained.

- **2026-08-21 (session 11, Claude Opus): M6 built and signed off.** Moved to
  the archive with the rest of M6.
