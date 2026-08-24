# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **E3 — Content and Prefabs — COMPLETE, signed off 2026-08-23**, and it was
  specified by the human in a conversation rather than by a brief — one message
  at a time, while the thing was being built and used. `docs/briefs/e3-kickoff.md`
  is the record that would have been the brief, written at the close and labelled
  as such.

  **Four ADRs carry it, and three of them REVERSE something — one of them
  itself.** 0049 named the thing: a **Stamp**, chosen by the human over prefab,
  blueprint and model. 0050: a script is an ordinary instance carrying its own
  `Source`, which reverses 0048's "a Script is created as a FILE" — a script
  whose identity is a file cannot go in a prefab, be copied with the thing it
  belongs to, or live in a library. 0051: a prefab is INHERITED and an edit is
  an override, which reverses 0049's break-on-edit — break-on-edit says a prefab
  is a starting point, and this says it is a definition. **0052 is the one that
  reverses itself**: `content/` was given a global tree of instances beside its
  files, and it was taken out the same afternoon when the human asked whether
  Unity and Unreal have two contents. They do not — each has one folder of
  files, and a prefab in either is a file. The ADR stays as the record, because
  the reason is worth more than the decision was.

  **Each reversal was right when it was written and wrong when it was used**,
  which is the pattern worth naming: 0048 and 0049 were both written from the
  human's own words, shipped with tests, and corrected by the same person a day
  later once they had the thing in their hands. The register keeps both halves.

  **What a prefab does now**: convert any instance to one, place it linked or as
  a copy, open it onto a stage that is a world of its own, and instance it from
  code with `Instance.stamp`. Editing an instance overrides a property and keeps
  the link; changing the source moves every instance that has not overridden
  that property; a structural change is written in full and unlinked rather than
  refused, because a save that refuses is a save that loses work.

  **And the editor grew the things a person expects to already be there**:
  dragging an instance into the content browser makes a prefab of it and
  dragging one out places it, `Del` `F2` `Ctrl+D` `Ctrl+S`, and a clipboard that
  holds TEXT rather than ids — which is what lets a copy survive the delete, the
  scene load or the stamp session that happens between it and the paste.

  **Two manipulator defects, and the second was found by asking the first's
  question again.** D078: a comment said the snap was in the gizmo's frame and
  the code snapped in the world's, so a local drag left its own arm. D079: a
  `Size` is in the part's own frame, so a world-space scale arm on a turned part
  pointed one way and grew it another. Both are the same shape — a rule stated
  in one space and applied in another — and it is worth expecting a third.

- **E2 — Moving Things — COMPLETE, 2026-08-23**, seventeen commits, every item
  of scope built and tested. `docs/briefs/e2-kickoff.md` carries the Gate Record
  and the account; what is left of it is the pictures, which is what is left of
  E3 too and for the same reason.

  **Two findings outlive it.** D073's shape — a rule that is right for ONE and
  wrong for many — appeared three times in a milestone whose whole subject is
  "many", and the worst of them put a tool's cost inside
  `buildInstanceBatches`, which every pass of the frame depends on. And D076's:
  a `static_assert` that COUNTS is not a test that COVERS — the fixture asserted
  `variant_size_v<Value> == 13` and named nine of them, so four editor branches
  had never been executed by anything.

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
- **The next action, as a sentence:** decide what E4 is. E1, E2 and E3 are all
  signed off, and what the editor does not have is no longer a list of items
  inside a milestone — it is a choice about which of them matters next.
  `docs/briefs/e3-kickoff.md`'s closing section names four, and the roadmap's
  post-v1 phase names more.
- **What is left of E2 and E3 is one thing and it is the same thing:** the
  pictures. **The ImGui shell cannot render headlessly and SDL does not accept
  injected input**, so every visual claim rests on the human looking — which for
  E3 they did, message by message, while it was being built.
- **All four of E2's frozen interfaces are built and tested**, which is the point
  the plan said nothing fans out before: the selection set, the gesture and its
  extracted undo key, the manipulator arithmetic, and `Editor`'s verbs.
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

- **The phase was re-cut at E1's review and ADR 0047 is why** (human decision,
  2026-08-22). The first split put manipulators, saving and play in three
  milestones; the review said in one sentence that edit, test and save are one
  loop and an editor that delivers a third of it three times is not usable in
  between. **E2 is now the loop, whole**, and underneath it the authored world
  becomes DATA and scripts become BEHAVIOUR — the Unity, Unreal and Roblox
  arrangement, asked for in those words. Code-first does not die: `Instance.new`
  at runtime stays first-class, and what moves is where the world a project
  *starts* with is written down.

  **The three things E2 was told it needed and did not have all exist now**:
  `World::snapshot()`, a file-writing capability the game VM is not given (R4),
  and the scene format itself.

- **D057 is closed and the fourth profile exists.** `player` is Release, links
  the Luau compiler — which a packaged game needs, because `luaug build` ships
  its Luau as source (ADR 0045) — and declares no ImGui target at all, so "the
  artifact contains no overlay" is a link-time fact. `luaug build` REFUSES
  anything else rather than warning, with the two commands that fix it in the
  message; `--dev-host` is the door, and `LUAUG_HOST` is deliberately not
  consulted, because "I pointed the dev server somewhere" must not decide what a
  release contains. The gate builds it beside `shipping`, in the stage that
  exists because a profile nothing builds is a profile nobody knows is broken —
  which was this defect's own objection to shipping the `shipping` profile.
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

- **2026-08-23 (session 19, Claude Opus): E2 built and closed, E3 built and
  closed.** Thirty-eight commits, every one behind a green six-stage gate. The
  full entry is in
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md); the
  two briefs carry the Gate Records and what each cost.

  **The one thing to carry out of it**: three written decisions were reversed
  within a day of shipping, each by the person who asked for the first one, and
  each reversal took one sentence to argue — because the first one was in a file
  somebody could argue with. The third is mine: I offered a choice whose losing
  option's own description listed its costs, and a person choosing between two
  things they have not built yet is choosing on the framing.

- **2026-08-21 (session 11, Claude Opus): M6 built and signed off.** Moved to
  the archive with the rest of M6.
