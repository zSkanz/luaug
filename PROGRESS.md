# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **E8 — The Script Editor — BUILT, awaiting review, 2026-08-24.** Its entry
  moved to
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) with
  E7's, E6's and E5's on 2026-08-26, when this file was archived back under its
  cap (§11). ADR 0057 settles it and
  [`docs/briefs/e8-kickoff.md`](docs/briefs/e8-kickoff.md) carries the Gate
  Record and ten findings. **You can write Luau inside the engine**, debugger
  included, and one of those findings corrects five milestones of this ledger:
  E1 recorded that "SDL does not accept injected input", which is true of
  ImGui-level injection and **false of real Win32 input** -- `SetCursorPos` +
  `mouse_event` + `SendKeys` drives this editor exactly as a person does, and
  three defects were found that way. It does not replace a person, since it
  cannot judge whether a colour is pleasant, but "there is no automated path to
  a click inside the editor" should stop being repeated.

- **E7 — The Look — BUILT, awaiting review, 2026-08-24.** Its entry moved to the
  same archive on the same day.
  [`docs/briefs/e7-kickoff.md`](docs/briefs/e7-kickoff.md) carries the Gate
  Record, and **every row of it is green, the pictures included** -- the four in
  `docs/images/e7/` were looked at by a person, which is what that row records,
  so E7 is waiting on the sign-off itself rather than on evidence for it. The
  one thing worth keeping here: **the palette is measured, and the measurement
  found two failures on its first run.** `textMuted` and `danger` cleared 4.5:1
  against the window and failed against a HOVERED ROW, which is the ground
  somebody is looking at exactly when they are about to act — and none of that
  is visible by looking, which is the argument for measuring it.

- **E6 — The Launcher — COMPLETE, signed off 2026-08-24**, tagged
  `milestone/e6`; entry archived with the others.
  [`docs/briefs/e6-kickoff.md`](docs/briefs/e6-kickoff.md) carries the Gate
  Record. The one thing worth keeping here: **the smoke test found what the plan
  got wrong**, and reading it could not have. `SDL_GetPrefPath` returns a
  trailing separator, so `parent_path()` answered the directory itself and every
  new project was scaffolded inside `AppData\Roaming`, where nobody would look
  for one.

- **E5 — The World You Build — BUILT, awaiting review, 2026-08-24.** Entry
  archived with the others; the reconnaissance and the Gate Record are in
  [`docs/briefs/e5-kickoff.md`](docs/briefs/e5-kickoff.md), and **two of its
  rows are still PENDING**, both of them a person at a window: the screenshot of
  the chunk-state overlay on `examples/06-scene`, and somebody playing it. The
  one thing worth keeping here: **a measurement overturned the design.** The
  size-class cuts were proposed at 8 m and 64 m and put the flagship's ground --
  69% of its world, and the one thing that has to be visible at distance -- in
  the middle class, because that terrain is 18,496 tiles of 32 m and classifying
  by a PART's extent will never find terrain authored as many small pieces. The
  cuts are 12 m and 24 m, in the gap the data has.

- **E4 — The Editor Ships — COMPLETE, signed off 2026-08-24**, tagged
  `milestone/e4`. Its entry moved to
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) when E6
  was written up. [`docs/briefs/e4-kickoff.md`](docs/briefs/e4-kickoff.md)
  carries the Gate Record, and the one row still pending at sign-off is recorded
  there as pending. **The archive exists**: `LuauG-1.0.0-win64.zip`, built by
  `scripts/package.ps1`, which proves the folder works from outside this
  repository before it compresses it.

- **E3 — Content and Prefabs — COMPLETE, signed off 2026-08-23**, tagged
  `milestone/e3` on 2026-08-26. Its entry is in
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) and
  [`docs/briefs/e3-kickoff.md`](docs/briefs/e3-kickoff.md) carries the Gate
  Record. The one thing worth keeping here: **three written decisions were
  reversed within a day of shipping**, each by the person who asked for the
  first one, and each reversal took one sentence to argue — because the first
  one was in a file somebody could argue with.

- **E2 — Moving Things — COMPLETE, 2026-08-23**, tagged `milestone/e2` on
  2026-08-26; entry archived beside E1's, and
  [`docs/briefs/e2-kickoff.md`](docs/briefs/e2-kickoff.md) carries the Gate
  Record. The pattern worth keeping here: **nine defects, seven found by a
  person using the thing**, and three of them were the same shape -- a piece of
  arithmetic that is right for ONE and wrong for many.

- **E1 — The Editor — COMPLETE, signed off 2026-08-22**, tagged `milestone/e1`.
  Its entry moved to
  [`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md) when E5
  was written up. [`docs/briefs/e1-kickoff.md`](docs/briefs/e1-kickoff.md)
  carries the Gate Record. One limit from it is worth keeping here, in the
  narrower form E8 measured it into: **the ImGui shell cannot render
  headlessly**, so there is no automated path to a PICTURE of the editor. The
  other half of the sentence E1 wrote — that there is no path to a click either
  — is false, and E8's entry above says what does drive it.

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
- **CI has executed no step since 2026-08-21, and GitHub now says why in words.**
  Every job since `5a542b7a` completes in a few seconds having run ZERO steps,
  and the annotation on each of them is *"The job was not started because recent
  account payments have failed or your spending limit needs to be increased"* --
  a billing block on this private repository, which is the owner's to clear and
  is under Blocked below. The 2026-08-26 push re-tested it and nothing had
  changed. **So macOS is unverified from M6 onward**; the last all-green run
  across all three tiers is 32429107275, at M5.

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

- **The campaign in [`docs/finish-line.md`](docs/finish-line.md) is what "next"
  means.** The owner left on 2026-08-26 having asked for one thing -- finish
  everything the audit found, take every repository-level decision on their
  behalf, and do not stop to ask -- and that file is the mission, the stage
  list, and the record of what was decided in their absence. **The next action,
  as a sentence:** open it, find the first unticked box, and continue. This
  ledger records what that costs; it is no longer where the whole of the plan
  lives.
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
- **The full six-stage gate has not been run since the untracked tree landed**,
  which is `docs/finish-line.md` S1.5: those five commits are unverified on this
  machine, and CI cannot answer for them either. And a green gate is green with
  one instrument switched off -- `openworld_soak`'s instance-growth check is
  QUARANTINED at its second flake (D066), still measuring and still logging at
  warn level, no longer failing the run.
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
  attached, and reaches nobody until this is answered; the second archive
  ADR 0054 makes possible -- the editor itself -- is the campaign's S8.6.
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
through 25 are in
[`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md), moved
there each time this file passed its ~300-line cap (§11). What was worth
carrying out of session 19 is under E3 above.

- **2026-08-26 (session 26, Claude Opus): the campaign opened, and the first
  thing it did was push.**
  [`docs/finish-line.md`](docs/finish-line.md) is the mission it was given and
  the record of every decision taken in the owner's absence.

  **Did:** claimed the tree, which took asking rather than inferring -- three
  sessions were writing it, all three declared what they held and stood down;
  pushed the 164 commits that existed on one machine only; pushed
  `milestone/e4` and `milestone/e6`, which existed only there too, and created
  `milestone/e2` and `milestone/e3`; landed the orphaned untracked tree in four
  commits -- the documentation site with the two gate edits that already invoked
  it, the branding set, `examples/11-ocean` with the island rework, and the icon
  masters that `icons/bake.py` reads to generate the committed theme; swept the
  scratch; and archived this file back under its cap.

  **Learned, and a `git push` found it rather than a reading:** `origin/main`
  had not moved since 2026-08-22. Eight milestones were recorded here as built
  and three of them as tagged, while every commit since E1's sign-off and two of
  those three tags existed nowhere but this machine -- no backup, no review, and
  nothing for CI to run against. **A ledger cannot tell a commit from a
  published one**, which is why "and pushed" is part of what ends a stage now.

  **Learned, from this file itself:** it named a next action -- "the only
  milestone still awaiting review" -- that two of its own entries contradicted,
  and it repeated as a live limit the exact claim its newest entry had spent a
  paragraph disproving. Neither survived because somebody believed it; both
  survived because a section that is appended to and never re-read is a pile of
  facts nobody compares. The archiving is not tidiness for its own sake: this
  file was 707 lines against a ~300 cap, and a section nobody scrolls to is
  where a contradiction lives undisturbed.

  **Next:** open [`docs/finish-line.md`](docs/finish-line.md), find the first
  unticked box, and continue.
