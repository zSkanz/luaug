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

- **The campaign in [`docs/finish-line.md`](docs/finish-line.md) is finished,
  and what is left is the owner's.** They left on 2026-08-26 having asked for
  one thing — finish everything the audit found, take every repository-level
  decision on their behalf, and do not stop to ask. **Eighty-seven of its
  eighty-eight rows are closed.** The one that is not is S1.7, and it is not
  work: `milestone/e5`, `e7` and `e8` wait on a person signing those milestones
  off, and S1.7 now names exactly what each is waiting for rather than leaving
  it as "review".

  **The next action, as a sentence:** read S8.7, which is the handback and lists
  the four things that need the owner, in order — sign off three milestones,
  cut `v1.1.0`, fix the billing so Actions runs again, and make the repository
  public.
- **`v1.1.0` is prepared but not tagged.** The tree declares it, the changelog
  carries the editor phase, and `lute tools/repo/package.luau` writes
  `LuauG-1.1.0-win64` whose binary says `LuauG 1.1.0 (editor)`. The tag and the
  release page are deliberately not done here: both are outward-facing, and CI
  publishes no release job in any case.
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
make room for the next.

- **Session 28 — the campaign closes, 2026-08-27.** Twenty-seven rows left at
  the start, one at the end, and it is not work: S1.7's three tags wait on a
  person signing three milestones off.

  **E9 closed**, and step 14 — the cut-over, the only irreversible step —
  found that the plan had named ONE loose feed where there were three.
  `MeshLoader::sync`'s branch was the known one. `syncSkeletons` was a second,
  parsing the source on the SIM thread and unable to resolve a fragment at all,
  so a ragdoll built from one piece of a split model found no rig. And
  `syncTextures` was a third nobody had counted, because it had no compiled
  branch AT ALL: it read the raw PNG beside a `.ktx2` the compiler had already
  written, every frame, for every editor session since step 12 landed. "BC7 and
  mips reach editor content" was in the plan as a delivered consequence and was
  false. [ADR 0065](docs/decisions/0065-a-loose-gltf-is-not-a-runtime-format.md).

  **Verified from the only side that proves it**: compile a project, delete
  every source file, require that the mesh and the map still load. A test
  asserting "the mesh loaded" would have passed on the old build too — the
  loose reader loaded meshes perfectly well, which was the problem with it.

  **`v1.1.0` prepared, and cutting it found D148.** The world hash reaches class
  state through the generated accessors, and that walk reached
  `DataModel.EngineVersion`: the version bump broke every determinism trace at
  tick ZERO with nothing about any world having changed. The file already argued
  against exactly this twenty lines above, about `ClassId`. **The churn is not
  the defect; what it hides is** — a release expected to move every trace is a
  release in which a real R10 regression looks exactly like the expected
  movement, and re-recording was the obvious response that would have masked
  one. Fixed with `HostFact` in the IDL, break-verified at version 9.9.9.

  **Two half-built rows finished**, and the first was wrong about itself: S5.12
  recorded that the F3 overlay had the render counters and the editor's panel
  did not. Neither did, for nine milestones, in a function both shells call.
  S5.18 gave the Explorer its drop bands, and the landing arithmetic earned its
  test twice — `moveChild` warns that a downward drag lands one place short,
  and the first version of the test asserted two cases backwards.

  **Also re-measured**: both soak ceilings. 192 MiB against a 25 MiB peak was
  not a backstop. `openworld_soak` turned up a stale record rather than a
  cheaper program, claiming 169 MiB where the peak is 47, because the 169
  predates ADR 0053.

  **Next:** [`docs/finish-line.md`](docs/finish-line.md) S8.7, which is the
  handback and is addressed to the owner rather than to a session.
