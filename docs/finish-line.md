# The finish line

**This file is the mission.** It exists because the owner left for an extended
period on 2026-08-26 having asked for one thing: *finish everything the audit
found*, take every repository-level decision on their behalf, and do not stop to
ask. The only act reserved for them is making the repository public, at the end.

An agent session that loses its context and comes back reads this file first,
finds the first unchecked item, and continues. Nothing else in the repository
records the whole of it: `roadmap.md` carries milestones, `defects.md` carries
defects, `../PROGRESS.md` carries the running narrative. This carries the finish
line, and it is deleted the day the last box is ticked.

## What "finished" means

Concretely, and in the order consequence falls:

1. **E9 closed** — its remaining steps built, a brief written, a gate to close
   against, and the record made.
2. **The work published** — the commits and tags that exist on one machine
   reach `origin`, and CI can confirm them.
3. **The defect tail emptied** — every open row closed or explicitly scheduled
   with its reason, and the defects nobody had filed given rows.
4. **The editor's declared verbs built** — the things the documentation and the
   decision records say a person can do, that a person cannot currently do.
5. **The inert surfaces resolved** — every property that is stored, read back
   and honoured by nothing is either honoured or removed. No third state.
6. **The gates made honest** — a green gate proves what it claims to prove, on
   every tier, including the ones that currently pass by skipping.

Phases 2 through 6 of the post-v1 roadmap (effects, 2D, multiplayer, mobile,
ecosystem) are **not** on this list. They are the roadmap, not a backlog, and
R15 still governs them.

## The standing rules this campaign adds

- **One writer.** Three agent sessions were writing this tree while the protocol
  described two. This session claimed it on 2026-08-26; all three peers replied,
  confirmed nothing was mid-edit, and stood down of their own accord.
- **Never restore a path you did not write.** Uncommitted edits belonging to
  another session were reverted twice in one afternoon by a `checkout`/`restore`
  of a path the reverting session did not own. Both were caught only because the
  build broke. This is the failure mode that actually bit; it is not
  hypothetical.
- **Check `git status` before chasing a red gate.** The full gate went red four
  separate times in one day on files that were mid-edit and belonged to somebody
  else. `HEAD` was green on retry each time.
- **Every stage ends green and pushed.** `scripts/localgate.ps1` in full, then a
  push. A stage that cannot go green does not end — it gets an entry under
  "Blocked" below, with what it is waiting on.
- **A decision taken on the owner's behalf is written down here**, with its
  reasoning and its date. They are away; this record is the only thing that lets
  them audit what was decided in their absence.
- **Senior practice, not novelty.** Where a mature engine has already settled a
  question, settle it the same way and say which practice it is. R7 still holds:
  study concepts, never copy code, assets or branding.

---

## Who owned what, at the moment the campaign opened

Established by asking, not by guessing, on 2026-08-26. Recorded because half the
untracked tree turned out to belong to nobody still working, and the other half
had constraints that were not visible from a path list.

| Path | Owner | Disposition |
|---|---|---|
| `api/generator/gen_site.luau`, `api/generator/site/` | **nobody** — all three peers disclaim it | Land it, together with the two gate edits that already invoke it |
| `scripts/gates/luau-check.sh`, `scripts/gates/docs-lint.sh` (uncommitted diffs) | **nobody** | Land with the generator or `main` goes red everywhere |
| `scripts/docs.ps1`, `scripts/docs.sh` | **nobody** | Land with the generator; they are its front door |
| `examples/05-streaming/content/` | **nobody** | `world/` is ignored; `match.scene.json` and the misspelled `schenes/` are scratch |
| `tempdiag.txt` | **nobody** — one line naming a settings path | Delete |
| `art/` (53 MB, 239 files) | reviewer session | **Landed minus the working files** — see decision 11. The masters are `icons/bake.py`'s input and the committed theme's only source; the retired drafts, `temporary/` and the rejected candidates are now ignored |
| `branding/luaug-lockup-*.png`, `luaug-mark-512.png`, `luaug-social-card.png` | reviewer session, finished | Land. The committed `../README.md` already points at the lockup |
| `branding/luaug-logo.svg`, `luaug-logotipo-original.svg` (deleted) | reviewer session | **Deletion is deliberate** — autotraced originals replaced by a stated recipe in `../branding/README.md`. Do not restore |
| `branding/icon/*` (7 PNGs + `.ico`) | reviewer session | The `.ico` is hand-built with every entry as PNG, which `engine/platform/tests/platform_tests.cpp` asserts. Structurally validated; **the test has not been run against it** |
| `examples/11-ocean/` | reviewer session, finished | Land. Verified 300 frames, exit 0. The committed `../examples/README.md` advertises it |
| `examples/10-open-world/**` (island rework) | reviewer session, finished | Land. Verified: 115 cells cold, 3000-frame soak `ok`, 0 hitches, worst 1.712 ms, median 5.90 ms |
| `examples/10-open-world/content/stamps/lanternpost.stamp.json` | reviewer session | Landed with the scene. Reported as required by it; the committed scene in fact carries no `"stamp"` mark, and the only tracked reference is a doc comment in `src/scripts/init.luau`. Committed anyway on the asymmetry — a missing stamp is skipped with a count rather than refused, so being wrong the other way is four lanterns quietly absent |
| `examples/10-open-world/content/stamps/charactertesting.stamp.json` | **the owner**, by hand | Their scratch prefab, made while specifying E3 and the subject of three defect reports. Not agent output, not cleanup. Left alone |
| `docs/progress-archive/2026-08.md` | reviewer session | One sentence closing E1's stated VM limit. Land |
| `icons/default/overlay/`, `docs/decisions/0049`–`0052`, `docs/api/basescript.md`, `docs/api/modulescript.md` | already tracked | Nothing to do; the audit's snapshot was stale |

---

## Decisions taken on the owner's behalf

These are the nine the audit reserved for them, plus what the handover raised.
Each is a decision, not a proposal; each says what would change it.

| # | Question | Decision | Why |
|---|---|---|---|
| 1 | Make the repository public? | **Theirs, at the end.** Everything else is sequenced so it is the last act and unblocks the rest. | They reserved it explicitly. |
| 2 | Three sessions writing one tree | **Settled: one writer.** Asked; all three declared, confirmed nothing mid-edit, and stood down. Two protocol rules added above from what they reported. | R11 cannot hold under uncoordinated writers, and two sessions had already reverted each other's uncommitted work. |
| 3 | `churn10k` 2.02 → 7.32 ms/tick | **Not a regression. No decision record owed.** But the audit was right that something is wrong nearby -- see the note below the table. | The number rose twice and both are recorded in `perf-baselines.md`'s own tables with the apply/step/writeback split beside them: at M5 the benchmark's world gained a physics simulation, so its parts became Jolt bodies and it stopped measuring what M2 measured; at M6, D031 made two thirds of its anchored parts KINEMATIC because something writes their `CFrame` every tick. Both are more work correctly priced. The greater-than-ten-per-cent clause is about a regression at constant work, and this is neither. |
| 4 | Jolt `CROSS_PLATFORM_DETERMINISTIC` | **Decided by experiment, and the experiment is specified rather than guessed.** Turn the flag on, regenerate both tiers' traces for `tests/determinism/churn`, and measure the cost on `physics1k` and `churn10k`. If the traces converge, cross-platform hash equality is one build flag away and the flag stays on with the cost recorded. If they do not, it buys nothing and comes back off. Scheduled in S6. | Reasoning by argument was about to get this wrong in both directions, so evidence settles it. The facts that force an experiment: `tests/determinism/example01`'s Windows and Linux traces are **byte-identical**, so the engine's own simulation already IS cross-platform deterministic; `tests/determinism/churn`'s **differ from tick 500 onward while tick 0 matches**, and `churn` is the one whose parts Jolt simulates. That points hard at Jolt being the only divergence -- which would make level C one flag away, the opposite of what ADR 0025 assumed when it called level C research-grade. It is still only a pointer: the divergence could equally be our own float math on the kinematic path. One flag and two trace regenerations answers it, and guessing does not. |
| 5 | `PointLight.Shadows` / `SpotLight.Shadows` | **Honour them.** A shadow atlas, a 2D map per spot light and a cube map per point light, a per-frame caster budget ordered by screen-space size, sampled from the clustered light pass that already exists. Built in S6. | Four reasons. R18 makes visual fidelity a v1 target, and a lamp that casts no shadow is one of the most visible things a renderer can get wrong -- it is the same complaint as "one environment lights the whole world, so a cave is lit by the sky". The property has now shipped inert across THREE milestones, and every attempt to defer it produced a third answer instead of one of the two the roadmap asked for; ending that is what this campaign is. Removing it is strictly worse: it is in the api-dump, the inspector and the generated reference, so removing a shipped property would be a breaking change that buys nothing and the milestone that renders it would put it straight back. And the machinery is mostly here -- cascaded maps in `engine/render/src/shadow.cpp`, clustered lights from M7.5 -- so what is missing is an atlas, two projections and a budget, not a shadow system. |
| 6 | `Ragdoll:Build` / `Blend` | **Build them**, as E9's plan specified. | The owner's own constraint in the plan is verbatim *"ragdoll is for now"*, so this is implementing a decision rather than taking one. Everything underneath it landed: the constraint seam in `IPhysics3D` with its Jolt backend, `Attachment` and `Bone`, and the skeleton override path. What is missing is the authoring verb on top. And the current state is the worst of the three available: a class that ships one boolean whose own documentation argues out loud against the design it came from. |
| 7 | The split-piece URN form | **A fragment: `asset://models/horse.gltf#Body`.** A URN with no fragment, for a file that was split, resolves to nothing and produces a keyed error naming the primitives it should have named. | Four reasons, in order of weight. The convention ALREADY EXISTS and is already parsed -- `engine/script/src/animation.cpp:188` splits on `#` for a clip name -- so this is a second use of a rule, not a new rule. `engine/asset/include/luaug/asset/model_split.h` already states in its own contract that "the name becomes the URN fragment AND the instance's name", so the code was written to this answer and leaving it undecided was the only inconsistency. `isValidUrn` accepts it unchanged -- it refuses `\`, empty segments, `.` and `..`, and has no opinion about `#`. And a fragment keeps the SOURCE file visible in the name, which is what makes the error message useful and a scene diff readable; a synthetic separate URN would need a rule guaranteeing it cannot collide with a file the project actually has, which is a namespace problem invented for nothing. |
| 8 | `examples/05-streaming` golden row | **Restate the row as regenerate-and-re-measure.** The content bytes are not committed and will not be. | This one was already decided elsewhere and only the row did not know. `.gitignore` carries the reasoning in full: that example's world is GENERATED, what is committed is the forty-line generator and not the megabyte and a half it writes, and the roadmap's own deliverable line asks for no giant binary assets in the repository. A golden row that asks somebody to compare committed bytes against a directory that is deliberately ignored cannot be ticked by anyone on any machine, which is why nobody has. |
| 9 | Where the editor archive attaches | **Neither. It attaches to a new `v1.1.0`, cut when the editor phase closes** -- which is not yet, because E9 is open. Until then no archive is attached anywhere. | A release names what it contains. `v1.0.0` points at `milestone/m8` and attaching an editor built from a tree nine milestones newer would make that tag say something false about what it built, permanently and to everybody who downloads it. And the editor is not a fix to v1.0.0; it is a feature phase on top of it, which is a minor version. Cutting it now would also be premature in the ordinary sense: E9 is two thirds built and the thing would be shipped mid-milestone. |
| 10 | Dot-access to children (`api-design.md` divergence #26) | **Keep the divergence. Make the refusal legible, at edit time and at run time, and fix the completion to match.** A decision record states the price in the owner's terms so that reversing it later is reversing a decision rather than discovering one. Built in S6. | The cost of allowing it is not "child access is weakly typed", which is how the divergence note undersells it. Allowing `script.Nested` requires a string indexer on `Instance`, and an indexer does not only type the child access -- it makes EVERY unknown key on EVERY instance resolve to `Instance?` instead of erroring, so `part.Positon = ...` stops being a type error and becomes a silent nil write. The price is typo detection across the whole language, and R2 makes every file here strict. The familiarity argument is also weaker than it looks: the platform this borrows from flags the same expression under its own strict mode, so what would be imported is a habit that is non-strict there too. What the owner is reporting twice is almost certainly not "give me the indexer" but "this fails and tells me nothing" -- so the work is a diagnostic that names the live child and the accessor, at edit time via the lint pass that already walks the AST, and the completion offering children only inside `WaitForChild("` and `FindFirstChild("`. The diagnostic names `FindFirstChild`, not `WaitForChild`: scripts start when play starts and the tree is already built, so recommending the yielding one would teach exactly the load-order habit the divergence exists to kill. |
| 11 | `art/` in git | **Masters in, working files out.** `art/` lands minus retired drafts, `temporary/` and rejected candidates -- 39.5 MB, 203 files. | `icons/bake.py` is TRACKED and reads `art/editor-icons/` to generate the COMMITTED `icons/default/**`. A generated artifact whose input is absent cannot be regenerated by anyone. (A peer's sweep for readers of `art/` missed this because it did not include `*.py`.) The rejected and retired piles are working files, and a public repository cannot remove them from its history afterwards. |

**Two things decision 3 turned up that are not decision 3**, both in
`perf-baselines.md`, the file whose entire purpose is to be the instrument.

- **It states the wrong budget for `churn10k`, in all three of its rows.** They
  say 16 ms; `tests/bench/churn10k/scenario.json` says **32**. Commit `8f80ccf1`
  doubled it -- *"churn10k's budget doubles, because D031 changed what the scene
  is"* -- and the table was not told, including the M6 row written after that
  commit. This is why the 19.6 ms GitHub-runner number reads as over budget when
  it is comfortably inside one.
- **It says the CI perf smoke "reads the latest table for its thresholds".** It
  does not. `engine/app/src/bench.cpp:44` reads `budgetMs` out of each
  scenario's own JSON, and the table is read by nobody. A file that misdescribes
  where its own numbers are enforced sends the next person to change the wrong
  one.

Checked and NOT a finding, recorded so nobody re-checks it: `perf_budget` does
run in CI. The workflow's `ctest -LE gpu-golden` excludes only that label, and
this test carries `perf`.

**One thing decision 3 also turned up.** The M6 row records
`churn10k` at **19.6 ms on a GitHub `windows-latest` runner against a 16 ms
budget**, with the note that the runner is 2.7x slower than this machine "which
is why the budget is a detector and this file is the instrument". A budget the
CI machine cannot meet is a gate that either fails on every push or is being
scaled somewhere the table does not say. S7 checks which, because those are very
different things and only one of them is a working gate.

---

## Stages

Legend: `[ ]` not started · `[~]` in progress · `[x]` done · `[-]` dropped, with
the reason on the line.

### S1 — The tree is coherent, and the work is published

Everything else is downstream: no review, no CI, no backup, and a state file
that sends the next session to the wrong place.

- [x] **S1.1** Land the documentation site: `api/generator/gen_site.luau`,
      `api/generator/site/`, `scripts/docs.{ps1,sh}`, and the two gate edits
      that already invoke them — in one commit, or `main` goes red on every
      machine that is not this one.
- [x] **S1.2** Land `examples/11-ocean/` and the `10-open-world` island rework,
      with `lanternpost.stamp.json` in the same commit as the scene that needs
      it.
- [x] **S1.3** Land the branding set; leave `art/` pending decision 11.
- [x] **S1.4** Sweep the scratch: `tempdiag.txt`,
      `examples/05-streaming/content/match.scene.json` and the misspelled
      `schenes/`.
- [x] **S1.5** Green, and it is nine stages now rather than six. The seven that
      run by default — docs, luau, format, windows, linux, shipping, and the two
      opt-in placeholders — pass end to end, with 57 tests on Windows and 56 on
      Linux, 1,168 conformance cases on each tier and the packaging test.

      The three opt-in stages are green too, each run on its own: `asan`
      (**199.7 s, the first fully clean AddressSanitizer + UndefinedBehaviour
      run this repository has had** — S7.5 recorded that as still owed),
      `winprofiles` (the three profiles a Windows release ships, on MSVC —
      S7.4), and `lavapipe` (three real-image goldens compared exactly on Mesa's
      software rasterizer — S7.6).
- [x] **S1.6** Push. 164 commits and three tags exist on one machine.
- [~] **S1.7** Create the five missing milestone tags. `milestone/e2` and
      `milestone/e3` created and pushed 2026-08-26 -- both were recorded
      COMPLETE and signed off. `e5`, `e7` and `e8` wait on S2's sign-off
      pass, because a tag for a milestone still awaiting review would be
      a durable record of something that has not happened.

### S2 — The record says what is true

- [x] **S2.1** `../PROGRESS.md` — it contradicts itself in the sentence that
      names the next action, and claims the ImGui shell cannot be driven fifty
      lines after disproving it.
- [x] **S2.2** `roadmap.md` phase-1 table — stale on four rows, and E3 is named
      wrong.
- [x] **S2.3** `roadmap.md` M4.5 checklist — fifteen unticked boxes of which the
      register closes thirteen.
- [x] **S2.4** `api-design.md` §5 — declares as unbuilt an output that shipped
      and is freshness-gated.
- [x] **S2.5** E3 gets a roadmap detail section, the only E-milestone without
      one. Its gate keeps one honest PENDING row: whether a badge reads at
      16 px is a picture, and the geometry test is not that picture.
- [x] **S2.6** `briefs/e9-kickoff.md` and E9's roadmap section — E9 is being
      deferred against by name and has no gate to close against.
- [x] **S2.7** The decision record for the material reversal, and for the Part B
      deviation justified only in commit messages.
- [x] **S2.8** The two dangling `D-` placeholders in `defects.md`, and the
      `docs-lint` blind spot that let them through a green gate.
- [x] **S2.9** `../CLAUDE.md` and `ci.yml` disagree about whether macOS blocks a
      code push. They are exact opposites, tag behaviour included.

### S3 — The defect tail

- [x] **S3.1** An instance-valued property set inside a placed stamp is dropped
      silently on save. **Reproduced** with three cases, two failing; fixed by
      comparing instance-valued properties by PATH under each subtree's own root
      rather than by id, which is what makes a re-pointed or outside reference an
      override while an untouched internal one stays not-an-override. The loader
      needed nothing: it has resolved instance properties through a deferred pass
      since the format existed, and only the writer skipped.
- [x] **S3.2** Every loose texture is encoded as sRGB, and the Material design
      names loose textures — the regression a past fix already closed once.
- [x] **S3.3** The toolbar's New button wipes an unsaved scene with no prompt.
      Four other doors ask; this is the fifth.
- [x] **S3.4** A stamp whose root is a `Model` is placed at the world origin.
- [x] **S3.5** `dev.err.not_implemented` has no i18n entry, and the gate built
      for exactly that is structurally blind to it.
- [x] **S3.6** Three comments that are live doc/code lies.
- [x] **S3.7** The Console's log is a fixed-height child in a resizable panel.
- [x] **S3.8** D129 — a sound's first play decodes on the calling thread.
      **Settled: a prefetch with a synchronous floor.** The read is async and
      the decode is a job, both started from the frame and never from the
      tick; if the tick asks for a clip that has not landed it decodes it
      itself, so the answer is identical either way and the world hash cannot
      learn the disk's speed. **The earlier session's reason for leaving it
      open was wrong** — it said the repository has no audio file to measure
      with, and `audio_tests.cpp` has carried a WAV writer since M7.
      `Sound.Loaded` still fires immediately, now as a written decision.
- [x] **S3.9** D066 — a quarantined instrument whose successor is named and
      unbuilt. **Built**, and the run corrected the design: it is a place
      visited twice rather than a return to the start, because the flagship's
      path never comes back — 232 m was its nearest approach across 835 m.
      `examples/05-streaming` flies a circular orbit and carries it: 2.62 m
      apart, 14,401 frames apart, 1,685 instances both times. The quarantined
      check stays beside it, still measuring and still not gating.
- [x] **S3.10** ~~D092~~ — **already built, and the handover had the number
      wrong.** D092 is a sound defect and is fixed; the one meant is **D096**,
      "opening a project in the editor RUNS every entry script's file scope",
      and it is fixed too. Both halves of ADR 0058 are in the tree: boot mounts
      and does not start (`engine.cpp`'s `.startScripts = !options.editor`), and
      stop tears the VM down and rebuilds it (`restartRuntime`, after the
      restore and after the debugger detaches, in that order and for stated
      reasons). Verified by reading the code rather than by trusting the report.
- [x] **S3.11** ~~`content/schenes/` is misspelled in `templates/starter`~~ —
      **it is not.** The template has `content/scenes/` and its `luaug.toml`
      names `scenes/main.scene.json`. The only misspelled copies were the
      scratch under `examples/05-streaming` (deleted in S1.4) and one in a
      project outside this repository, which is the owner's to rename.

### S4 — E9 closed

- [x] **S4.1** Decide the split-piece URN form. **Settled: a fragment** -- see
      decision 7. Owes a decision record, which lands with E9's own.
- [x] **S4.2** Step 12 — the editor compiles on import. **Built, all five
      pieces**: `assetc::importOne` (the same call, not a second one), the
      object-store writer, `luaug_assetc_lib` linked under the editor flag, the
      store mounted between the source tree and the pack, and
      `splitByPrimitive` reaching both the pack and a world — a model dropped
      in becomes a `Model` with one named `MeshPart` per primitive. Opening a
      project compiles it; importing compiles what it brought.
      **The owner's `.fbx` loads because of it**, and every existing scene
      still resolves because a piece is emitted BESIDE the whole model rather
      than instead of it. Step 14 removes the whole-model row and is last.
- [x] **S4.3** Step 11's unbuilt half — parallel texture encode. **Built**:
      one texture per worker with basis's own threading still off, merged in
      source order. **20.2 s to 8.1 s** on five textures, and the determinism
      gate builds a third, serial leg so the claim is checked rather than
      asserted. `assetc = 7` in the layer table, which was also owed.
- [x] **S4.4** Step 9's unbuilt half — built. `View > Skeletons` draws every
      skinned mesh's rig through the same `DebugDraw` the physics wireframe
      uses, and `Bone.JointName` has a filtered, depth-indented picker off the
      rig above it. Lines rather than instances, which is assumption 2 made
      visible: 677 joints would be 677 Explorer rows and 677 contributions to
      the world hash bought for a picture. Four cases drive the drawing over an
      invented rig; the one that measures a bone's endpoints for a part ten
      metres out and turned a quarter turn fails when the composition is
      dropped.
- [ ] **S4.5** Step 14 — the cut-over. Last, because it is the only
      irreversible one.
- [~] **S4.6** Step 15 — **benches and baselines done**, decision records and
      the roadmap left. `tests/bench/ragdoll10` is built and
      `docs/perf-baselines.md` carries an E9 block: `sockets200`, `ragdoll10`,
      and `physics1k`/`churn10k` re-measured to show the constraint family's
      three new per-tick passes cost a jointless scene nothing (2.00 against
      M5's 2.02; 6.98 against M6's 7.32).
- [~] **S4.7** E9's declared verification — **four of six built**.
      `tests/determinism/ragdoll` (four bodies, sixty joints, three thousand
      ticks, reproduced) and `tests/bench/ragdoll10` are in; so is
      `physics/ragdoll.spec.luau`, and so is decision 6's own
      `Ragdoll:Build`/`Blend` with eleven C++ cases over an invented rig.
      **Both new scenes earned their keep on the first run**: the determinism
      one found D145's dangling pool pointer, and the bench found the joint
      frame the builder was giving a swing-twist — a cone measured across the
      bone instead of along it, so every shoulder started outside its own limit
      and the character launched. What is left is `import_matches_build` and
      the rendercapture leg, both of which belong to step 14.
- [x] **S4.8** E5's unfinished half — **the screenshot row is closed, and it
      is closed by moving the picture rather than by finding a way to
      photograph a panel.** The row asked for a screenshot of the chunk-state
      overlay; the overlay was ImGui, ImGui cannot render headlessly, and the
      row sat PENDING from the day the milestone closed. `drawChunkGrid` puts
      the grid in the WORLD through `DebugDraw` -- one cell outline per chunk
      coloured by state, a post at one corner, each focus's two rings -- which
      is both better for a person (streaming is a question about space) and
      capturable, because `--headless --screenshot` already reaches the
      ordinary renderer. `tests/screenshots/chunkgrid` is the differential.
      The other half, migrating `05-streaming` and `10-open-world` off the
      generator path, is the human's own ask and is not E5's gate.

### S5 — The editor's declared verbs

- [x] **S5.1** Anything that is not a `BasePart` can be picked and drawn —
      built. Picking walked the part pool and nothing else, so a `Camera`, a
      `PointLight`, an `Attachment` and a `Ragdoll` could be reached only
      through the Explorer, and the one you want to move is the one you can
      see. A marker is a point and a radius: an instance with no transform of
      its own is at its nearest ancestor that has one, which is the rule the
      RENDERER already follows for a light. A marker wins over geometry when
      the ray passes within its radius and it is not behind the solid hit --
      both halves are needed, since being smaller must not make it harder to
      click and being behind a wall must still make it unreachable. Drawn as a
      wire sphere at exactly the pick radius, so what is visible is what is
      clickable.
- [x] **S5.2** The manipulator moves a `Model`, a `Camera`, an `Attachment` —
      built, and each is transformed the way it is DEFINED. A part and a camera
      own a world `CFrame` and take one straight; an `Attachment.CFrame` is
      relative to the part it is on, so a world transform is divided back
      through the parent's frame captured at the drag's start; a `Model` has no
      transform at all and moves by moving every part under it, which is what
      `PivotTo` means. The gizmo sits at a model's PIVOT, because anywhere else
      the parts would move by a different amount than the handle travelled.
      Four cases, including the one that is silently wrong: a gizmo at an
      attachment's LOCAL frame sits at the origin for every bone on a character
      ten metres out, which reads as the gizmo not appearing.
- [x] **S5.3** Selection resolves to the meaningful ancestor, with drill-down —
      built, and S5.4 is what made it unavoidable: before Group, models were
      rare. A click selects the OUTERMOST `Model` at or above what it hit --
      outermost because a car in a convoy is still a car, and because
      nearest-first would make "one click, one level" a rule with memory in it.
      A `Folder` is not a stopping point: filing is not assembly, and resolving
      to one would make every part in a tidy project unselectable.
      Double-clicking drills in, a click outside comes back out, and Escape
      steps out one level before it lets go of the selection. Six cases.
- [x] **S5.4** Group / Ungroup — built, Ctrl+G and Ctrl+Shift+G, in the
      Explorer's context menu, one undo step each. The container is a `Model`
      when anything in the selection has a transform and a `Folder` when
      nothing does: a model has a pivot, extents and a scale, all meaningless
      around four scripts, and a folder around four parts throws away the one
      thing grouping parts is for. It lands under the shallowest COMMON parent,
      so grouping across two branches does not silently move things into one of
      them. Ungroup refuses something with no children rather than destroying
      it. Seven cases, and the live-list one is break-verified: walking
      `firstChild`/`nextSibling` while reparenting drops four of five children
      into a container the editor then destroys.
- [x] **S5.5** Tags — built. The manual names the tag path as the PRIMARY way a
      script finds what a streamed world brought in, and until now the only way
      to put one on anything was to write a line of Luau, in a world whose
      whole point is that it is authored. Chips under the Properties grid, add
      and remove over the whole selection, and a picker of the names this world
      already uses -- because a typo is a tag nothing will ever find, which
      looks exactly like a working one.
- [x] **S5.6** All three, and the model half is where the work was.

      ADR 0049 was reversed to inheritance-with-overrides — an instance
      inherits from its stamp, a change to one instance stays local, and a
      change to the stamp reaches every instance that has not overridden that
      property. **The SAVE has understood that since ADR 0051 and nothing else
      could ask it**, so a person editing a placed lamp post could not see which
      of its properties were their own and which came from the file, and
      therefore had nothing to revert and nothing to push back up.

      `scene::stampOverrides` is that question. It shares its comparison with
      the writer rather than restating it — `differsFromReference` is one
      predicate both ask — because two definitions of "differs" would disagree
      the first time either was touched, and the panel would offer to revert
      something the save had already decided was not an override. Seven cases,
      including the three that must answer EMPTY and are easy to get wrong the
      other way: a freshly placed instance, an instance in no stamp at all, and
      a stamp whose file cannot be read. One case asserts the query and the save
      count the same overrides.

      **Revert** puts the stamp's value back on one instance as one undo step,
      and refuses to record a step when the property already matches — the
      invariant D134 and D141 both record, because `UndoStack::record` clears
      the redo stack, so a step that undoes nothing has destroyed a real redo
      future before anybody presses ctrl-Z. **Apply** builds the stamp into a
      scratch world, writes the property there, writes the file, and restamps:
      the instance stops being overridden as a CONSEQUENCE rather than as a
      step, since once the file says what the instance says there is nothing
      left to differ. An instance that had overridden the same property with
      some other value keeps it, because `restamp` measures against the file's
      previous text — applying is not a way to overwrite other people's edits.
      Refused while that stamp is open on the stage, because two writers of one
      file means the one a person can see would lose. Seven more cases.

      The panel marks an overridden row and offers both verbs on it, disabled
      rather than hidden when there is nothing to do. **The cache lives in the
      panel and deliberately not in the editor**: answering reads the stamp
      file, and a model-side cache would be a wrong answer with no way to notice
      — the world can be changed by a gizmo drag, a script or a queued write,
      none of which the editor hears about. A unit test caught exactly that and
      is the reason it moved. The panel keys it on the selection and re-asks
      four times a second, so an edit made by something else appears without
      anybody clicking away and back, and D118's shape — a file read per frame
      on the frame thread — does not come back.
- [x] **S5.7** Project settings — built, and the writer it needed is a
      **surgical** one rather than a serialiser. Every `luaug.toml` in this
      repository opens with a paragraph explaining why its settings are what
      they are; a dialog that parsed the file and printed it back would delete
      all of that the first time somebody changed a window title. So
      `core::setTomlValue` finds the line, replaces what is right of the `=`,
      and leaves every other byte -- comments, ordering, blank lines, trailing
      notes -- exactly where it was. A key the file lacks is appended under its
      own header; a table it lacks is created at the end; those are the only
      cases where anything is added.

      The dialog writes on Apply rather than per keystroke, stops at the first
      refusal so a failure cannot leave the file half changed, and PARSES the
      result before writing it -- which is what stops a Settings box turning a
      working project into one the engine will not open. Seventeen cases, most
      of them asserting what the edit did NOT touch.
- [x] **S5.8** Camera control during play — built, as an eye button in the
      transport that appears only while something is running. Detaching takes
      the VIEW back without touching the simulation: the world keeps ticking
      and the game's camera keeps doing whatever it does. It is the only way to
      see a running game from anywhere other than where it puts you -- an enemy
      behind a wall, a chunk that failed to stream, a character stuck inside
      geometry the player camera is inside of too. It takes the pointer back
      too, which is not a second decision: the fly camera is a right-drag, and
      a game holding the pointer (D069) means the drag never arrives. Cleared
      by play and by stop, because it is a question about the current run.
- [x] **S5.9** The Streaming panel in the editor shell — built, inside Stats.
      `drawShell` has drawn those counters since M7 and `drawEditorShell` was
      never handed the host, so the one shell in which somebody is AUTHORING a
      streamed world was the one that could not see what streaming was doing.
- [x] **S5.10** The physics wireframe from edit mode — built, and it needed
      more than a menu item. `DebugService:ShowPanel("Physics")` is a SCRIPT
      call and edit mode runs no game script, so the one person who could not
      see the colliders they were placing was the author placing them. The
      switch is `View > Collision Shapes`; the half that was missing is
      `PhysicsSync::mirror()`, because a paused world never calls `step`, so
      the backend held no bodies at all and the wireframe of a world with no
      bodies is an empty picture that looks exactly like a working one.
      Deliberately not `step(0)`: that still runs the solver, the character
      controllers and the contact diff, so it would fire `Touched` in the
      editor. Three cases assert it creates, retires, and raises nothing.
- [x] **S5.11** A console error jumps to the line that raised it — built. A
      line that names a `chunk.luau:123` becomes a link; every other line is
      drawn as it was, because turning ordinary output into something that
      looks clickable and goes somewhere wrong is worse than not linking any of
      it. The FIRST location in a message, since Luau puts the raise site at
      the front and appends the traceback behind. The panel finds it and the
      frame loop acts on it -- the pane is drawn from a snapshot and cannot
      open a tab, because opening one reads the world. Eight cases, and one of
      them caught a real defect while being written: a digit cap that
      TRUNCATED turned an absurd run into a plausible line number.
- [~] **S5.12** The Stats panel shows what is already measured — **streaming
      is in**, beside the frame and memory readouts it already had. What is
      left is the render-side counters, which the F3 overlay shows and the
      editor's panel does not.
- [x] **S5.13** Snap step editable; a reference grid drawn — both built.
      `setSnapStep` had existed since the manipulator did and nothing could
      call it, so every scene in this editor was built on a quarter of a metre
      and fifteen degrees whether or not those were the numbers -- and a snap
      you cannot change is a snap you turn off. It is a right-click on the snap
      button rather than a Preferences page, because a step is a thing somebody
      changes while placing something.

      **The grid draws at the SNAP's step**, not at a round number of its own:
      lines you can see and a snap you cannot are two grids, and the one that
      catches is the invisible one. It thins by powers of ten as the camera
      rises, so every line on screen is still a line the snap would produce --
      a spacing computed as reach-over-line-count would be a rounder grid
      beside the one that catches. Six cases, and the alignment one is
      break-verified: a grid centred on the camera lines up with nothing and
      looks correct from every single position.
- [x] **S5.14** Properties search and categories — built, and the categories
      are DERIVED. Every other engine puts a `Category` string on each
      property; two hundred of those in the IDL is two hundred things to keep
      in step, and what they would mostly say is which class the property came
      from -- which the registry already knows for certain. So the headers are
      `BasePart` and `Part` rather than "Transform" and "Appearance", and the
      second is a fact rather than a judgement. `collectProperties` already
      emits root-first, so the rows arrive grouped and the panel only notices
      when the group changes. The filter turns the headers off: a filter is a
      flat answer to "where is that one" and headers between two matches are
      furniture around it.
- [x] **S5.15** Attributes — built, beside the tags and through the same queue.
      A property is declared by a class and an attribute is not, which is why
      it is a section rather than more rows: the panel cannot know what to show
      until it asks the instance. Edits `bool`, `number`, `string`, `Vector3`
      and `Color3`; names the rest rather than hiding them, because a `CFrame`
      attribute is legal and a matrix is not a one-line widget.

      **The queue is what made both of these small.** `PendingWrite` grew a
      `WriteKind`, so an attribute and a tag ride the same path a property
      does -- one undo step, one safe point, one coalescing key -- rather than
      each teaching those three about a new shape. Five cases assert it,
      including that tagging something already tagged is `Unchanged` and not a
      refusal: that is the normal case over a selection, and reporting it as a
      failure would fire the toast on the one gesture people use tags for.
- [ ] **S5.16** Thumbnails for meshes, scenes and stamps; a material swatch.
- [x] **S5.17** Pivot/centre choice for a multi-selection — built, as a toggle
      beside the axis-space one because they are the same kind of question: one
      is which way the handles point and this is where they are. Over one
      instance the two answers are identical and the control does nothing; over
      forty they are the difference between rotating a row of columns about the
      one you clicked last and rotating it about itself. The centre is the MEAN
      of the transforms rather than the middle of a bounding box: a box's
      centre moves when one part is scaled, so a gizmo on it would drift during
      a scale drag — and a handle that moves while you hold it is one that does
      not track the pointer. Four cases, persisted with the other tool
      preferences.
- [~] **S5.18** Sibling reordering. **The `World` verb is built** — a move
      that changes nothing reports `Unchanged`, the duplicate-name chain is
      rebuilt around it, and the world hash follows. The Explorer's
      drag-between-rows half is what is left.
- [x] **S5.19** Written, and the command two documents already named now exists.

      `.vscode/settings.json` has always aliased `@luaug/` to
      `.luaug/types/luaug/` and `@std/` to `.luaug/types/std/`, and **nothing
      created either**: `luaug new` wrote `engine.d.luau` and stopped. So every
      `require("@luaug/camera")` in a scaffolded project came up unresolved in
      the editor — no completion, no hover — while running perfectly. That is
      the worst shape for a beginner, because the tooling says the line is wrong
      and the engine says it is fine.

      **The modules themselves, not hand-written stubs.** They are `--!strict`
      Luau exporting their own types, so copying them copies the truth; a stub
      would be a second declaration of one surface and would drift the first
      time either moved. It is the argument `engine.d.luau` already makes,
      applied to the modules beside it.

      **`luaug setup` exists now**, and it had to for a reason narrower than the
      alias: `.luaug/` is gitignored, so only the machine that ran `luaug new`
      ever had types — anyone who CLONED the project had none. Two documents
      named the command before it was written: `settings.json` says it
      "regenerates it, and .luaug/types/, for whatever engine version is
      pinned", and the starter's own test says the editor resolves `@std` and
      `@luaug` "against the stubs `luaug setup` generates". `luaug new` calls
      the same function rather than carrying a second copy of the layout, and
      `api-design.md` — which said the command "does not exist" — says what
      it does instead.

      **The engine's `@std/` and deliberately not Lute's**, which is the one
      real decision here. A game script is sandboxed (R4) and can reach
      `@std/net` and nothing else of that family; pointing the editor at Lute's
      typedefs would offer `fs`, `process` and `io` to a VM that has none of
      them — the same mistake `settings.json` already refuses for the Roblox
      API dump. The ANALYZER resolves `@std` separately through `.luaurc`, which
      is what keeps a project's Lute-run tests working, and the starter's test
      says so in its own header.

      Verified end to end: `new` produces all five stubs, `setup` rebuilds them
      after `.luaug/` is deleted the way a clone would have it, and it refuses
      with a keyed message outside a project rather than writing into whatever
      directory somebody was standing in.

### S6 — Inert surfaces resolved

- [x] **S6.1** `PointLight.Shadows` / `SpotLight.Shadows` — honour or remove.
      **Honoured.** A sixteen-tile atlas of their own, one tile per spot and
      six per point, with a stable budget and a differential gate whose claim
      is that the property must CHANGE the picture. They were the last two
      `Inert` properties in the engine; the marker now has no members.
      Running the proof found D144, which was not ours.
- [x] **S6.2** `Enum.CollisionFidelity.Precise` — reviewed, and it is not
      silent: the enum item's own doc says this release collides against a hull
      and reads back `Precise`, and a triangle-mesh collider is a different
      shape class with different rules (it cannot be dynamic) that belongs to
      the asset pipeline. What reading `shapeOf` DID find is D146, which is
      much worse than the thing the item names: the mirror short-circuited on
      item ZERO, so every `MeshPart` nobody touched collided as its bounding
      box while the enum promised a hull, and the one that asked for `Box` got
      a hull. Two opposite faults from one constant, and a test titled
      "CollisionFidelity Box is honoured exactly" was asserting the defect
      because it used the same wrong number. Five cases now pin each fidelity
      to the shape it asks for.
- [x] **S6.3** Nothing runs in them, and **two documents said otherwise in the
      present tense.** `ParallelWindowA` and `ParallelWindowB` sit in the `Phase`
      enum at the seams `architecture.md` §3 names. The tick fires neither.

      §3 claimed "v1 executes `ConnectParallel` handlers serially inside those
      windows on the game VM — but the thread-safety checker (§4) **already
      enforces** Unsafe/ReadParallel/LocalSafe/Safe as if they were parallel".
      Both halves are false. There is no `ConnectParallel` —
      `datatypes.api.luau` calls it "a reserved name, absent in v1" — and there
      is no checker: the `ThreadSafety` annotations reach `ClassRegistry` and
      the api-dump and are read by nothing anywhere. `phase.h` said the same
      thing more briefly, that the windows exist "so that the thread-safety
      checker has something real to name", which names a thing never built.

      **Building either is out of scope and that is not what was wrong.** R15
      closes v1 to parallel scripting, and `services.api.luau` is already honest
      about the annotations — "nothing observes these yet", with the
      forward-compatibility argument for annotating conservatively anyway. What
      was wrong is that two other documents described the mechanism as working.

      So the record says what is true, and what the windows ARE is stated
      instead: a reserved **order**. The seams are where a later milestone would
      run actor handlers, and fixing their position now means a phase added
      between them cannot silently move one. `phase_tests.cpp` holds those
      positions with arithmetic — window B is PreRender plus one, window A is
      PreSimulation plus one — plus the whole tick order, so a reordering has
      to change a test that says what the order is FOR rather than a number
      nobody reads. A comment was what the old claim was, and it was wrong for
      two milestones.
- [x] **S6.4** One built, one still reserved with the reason written down.
      **ADR 0062**, and the split is the finding: the M3 brief gave two reasons
      and they were not the same kind of reason.

      `asset-changed` was deferred because "no asset pipeline exists before
      M4/M7". **Both shipped, so that reason expired** — and a reserved verb
      whose stated blocker no longer exists is one nobody revisits, because the
      note explaining it still reads as current.

      **The implementation is a removal**, and it is that small because the
      loaders already load everything MISSING: `syncTextures` reads every map it
      cannot find, so making an entry missing IS the reload. `MeshLoader::forget`
      takes the texture out, destroys its handle, removes the mesh and releases
      its buffers. It works because loose content is not cached —
      `ContentMounts::resolve` answers a loose URN with a PATH and reads nothing
      (D039 took the read out of it), so the next load opens the current file. A
      packed or compiled URN reloads the same bytes, which is correct: those are
      artifacts, and changing one means recompiling it.

      The watcher answers a **different verb for content than for source**,
      which is what makes it worth having: a `.luau` under `src/` reloads the
      world and a `.png` under `content/` reloads itself, where one verb for
      both would restart the game every time somebody saved a texture. Content
      is snapshotted by size and modification time rather than by contents,
      because reading megabytes on every filesystem event to decide whether to
      reload would cost more than the reload.

      `eval` stays reserved, and the record says what it is waiting for —
      including the half the brief did not name. R4 is the stated one: it is a
      second door into the VM, and the first thing anybody wants from a console
      is to reach what a script cannot. **R10 is the harder one.** The world hash
      is a pure function of the operation sequence; an `eval` that mutates the
      world inserts operations the recording does not contain, so a replay of a
      session somebody typed into cannot reproduce it, silently. Three ways out,
      all of them decisions — refuse while recording, record the source as an
      operation, or say plainly that a console session is not replayable. Taking
      one without the console being asked for would be inventing requirements.
- [x] **S6.5** The record exists and it decides the shape rather than deferring
      it again. **ADR 0063.**

      `https://` is refused with a named error, which is the right one of the
      three available behaviours — a silent downgrade would put a caller's
      credentials in the clear on a line that reads as secure — and three
      documents already said so. What was missing was the OTHER side. The
      client's header said "R5 forbids adding [a TLS library] without a
      human-approved ADR", which is true and is not a plan: it names an obstacle
      without saying what the answer looks like if somebody clears it, so the
      next person starts from nothing and does the obvious thing.

      **The obvious thing is the wrong one, and that is the decision.** When TLS
      is built it comes from the PLATFORM — WinHTTP or Schannel,
      `NSURLSession`, the system OpenSSL — and not from a vendored library. A
      TLS stack is a **maintenance obligation** rather than a dependency: it is
      the one where a published CVE obliges a release on somebody else's
      schedule, and this project has no process for that and no reason to
      acquire one. Shipping a pinned BoringSSL means shipping a version that
      will be out of date in a binary a player runs, and it gives no trust roots
      — so somebody then curates a CA bundle, or reads the system store per
      platform anyway, which is most of the work of just using the system stack.

      **What it costs is stated because it is not free**: on Windows and macOS
      the TLS and the HTTP are one API, so `engine/net/http.h` — a hand-rolled
      HTTP/1.1 client over sockets — is what gets rewritten, into a seam with
      three implementations. The choice is "rewrite the client", not "add a
      library", and that is worth knowing before somebody starts by adding a
      library. The vendored route's costs are listed beside it so the comparison
      is available rather than reconstructed, and **vendoring stays a human
      decision** — the platform route needs no new dependency and therefore no
      approval under R5, which is what makes this one takeable here.

      `https://` stays refused for v1. R15 closes the scope, ADR 0012 makes
      `@std/net` primitives-only, and the documented shape — a backend on
      localhost or a LAN — does not need it.
- [ ] **S6.6** KTX2 HDR; glTF topologies; the second UV set half-importer.
- [x] **S6.7** A text caret — built. `TextInput`'s doc has promised "typed
      text, backspace and a caret" since the class existed, and the caret was
      the one it did not have: the editor appended and backspaced at the END,
      so a typo four characters back meant deleting everything after it. The
      field was here once with nothing moving it, `inertcheck` found it, and
      the comment that replaced it said a real caret would arrive with the code
      that moves it — which is what this is. Arrows, Home, End, backspace and
      forward delete; the caret steps whole code points so it never lands
      inside a UTF-8 sequence; a script assigning `Text` puts it at the end,
      which is the only position always in range. Three keys were added to the
      platform and to `Enum.KeyCode`, INSIDE the keyboard block — the resolver
      reaches a code by subtraction over a contiguous block — which renumbered
      the mouse and gamepad items and is safe because a scene stores an enum
      item by name. Seven cases. Clicking to place it is left: that needs the
      glyph advances the layout produced, and the layout is a different pass.
- [x] **S6.8** An animation clip addressable on its own — built, and it was
      nearly free. `LoadAnimation` refused a path that was not the player's own
      mesh, so a clip existed only inside the glTF its skeleton came from and a
      project with twelve characters carried twelve copies of every animation.
      **The retargeting was already there**: `jointMapFor` has mapped joints by
      NAME since one player had to drive a body and a shirt with different
      rigs. What was missing was any way to SAY which file the clip was in, so
      the change is one argument through `createTrack` and the removal of a
      refusal. A joint the target rig lacks is skipped rather than guessed, so
      a horse's clip on a person moves what they share and nothing else. Four
      cases, break-verified — three fail without it.
- [x] **S6.9** i18n plural rules beyond English — built, as a CLDR SUBSET that
      says which. A catalog knew its own text and did not know its own
      language, so every locale was pluralised by English's rule: right for
      about half the languages anybody translates a game into and wrong for the
      rest in a way a translator cannot work around. The file's own name is now
      the locale, so `i18n/pl.json` is Polish with nothing to keep in step.
      Seven rule SHAPES rather than a few hundred locales, because that is what
      the languages games ship in fall into, and anything unnamed falls back to
      English — so nothing got worse and the named ones got right. Nine cases,
      checked at the counts where each rule turns over rather than at 1 and 2,
      which is where a hand-written rule is always right.
- [x] **S6.10** Done, and **the open question it carried is answered by running
      it.** ADR 0064.

      Jolt has stepped on `JobSystemSingleThreaded` since M5. The roadmap said
      why and said what would change it: "single-threaded first; Jolt's job
      system wired to the engine job system when M7 lands it — note the seam",
      and it named the risk out loud: "whether recorded hashes survive that is a
      question". M7 landed and nobody asked.

      Same machine, same build, `win-msvc-dev`:

      | Bench | single-threaded | four threads |
      |---|---|---|
      | `physics1k` step | 1.760 ms | **0.651 ms** |
      | `churn10k` step | 3.725 ms | **1.853 ms** |
      | `churn10k` worst tick | 174.17 ms | **40.03 ms** |
      | `ragdoll10` step | 0.264 ms | **0.131 ms** |

      **The hashes survived.** `tests/determinism/churn` — ten thousand ticks,
      and the one whose parts Jolt actually simulates — reproduced its
      committed hash `d3dd9b68722aa0fa` on both tiers, unchanged. Nothing was
      re-recorded. So ADR 0025's level-B guarantee is intact with the solver
      threaded, which is the thing the M5 note was worried about.

      **It is Jolt's own pool and NOT the engine job system**, which is where
      this departs from the roadmap's wording, and the reason is the guarantee
      rather than convenience: `jobs::init()` sizes itself from
      `hardware_concurrency`, and Jolt's determinism is per thread COUNT — so
      solving on the engine pool would make a trace recorded here unreproducible
      on a machine with a different core count, and the same platform's
      committed trace would stop being a fact about the platform. A fixed-size
      sub-pool of the engine's is the same four threads with more code between
      them.

      `kPhysicsThreads` is a named constant with that sentence beside it,
      because it is part of the world hash the way gravity is: change it and
      every trace has to be re-recorded. The worst-tick number is the one worth
      reading twice — 174 ms to 40 ms is the difference between a visible
      stall and a dropped frame. The A/B is in `perf-baselines.md` rather than
      only the new number, because the delta is the fact and it would be
      unrecoverable from a table of absolutes.
- [ ] **S6.11** The four outstanding items of the rendering reference decision.
- [x] **S6.12** All three instruments, and the decision record that states the
      price. **ADR 0061.**

      `workspace.Baseplate` raises in this engine and always has. The divergence
      note explained why to somebody who already agreed; what it never did was
      state the **price of reversing it**, which is the thing a future reader
      needs. It also undersold the cost: reaching a child with a dot needs a
      string indexer on `Instance`, and an indexer does not merely type the
      child access — it makes every unknown key on every instance resolve to
      `Instance?` instead of erroring, so `part.Positon = ...` stops being a
      type error and becomes a silent nil write. The cost is **typo detection
      across the whole language**, in a repository where R2 makes every file
      strict.

      **The half left standing was the completion**, and it was the sharp end:
      it offered children under a dot, so the editor was proposing a line the
      runtime raises on. A completion the language will not accept is worse than
      an empty one — somebody accepts it, runs it, and learns that the editor
      and the engine disagree. Children are offered inside `WaitForChild("` and
      `FindFirstChild("` now, where they can be typed.

      **At run time** the message names the child and the accessor instead of
      answering "has no member named", which is true and useless when the thing
      IS there and visible in the Explorer. A name that is nothing at all still
      gets the plain message: one message for both would recommend
      `FindFirstChild` for `part.Positon`. Six conformance cases, break-verified.

      **At edit time** `lintInstanceAccess` underlines it while somebody types.
      It resolves the path first, so every report is a fact — that instance
      exists, it has that child, its class has no such member. It lives with the
      completion rather than in the AST lint pass because it needs the TREE:
      without one, `t.Baseplate` on a plain table cannot be told from
      `workspace.Baseplate`, and a lint with false positives is a lint people
      turn off. Four of its six cases assert **silence**.

      `FindFirstChild` is what all three recommend and `WaitForChild` is not:
      scripts start when play starts and the tree is already built, so
      recommending the yielding one would teach exactly the load-order habit the
      divergence exists to kill.

      Found while writing the record: **the ADR index stopped at 0045** while
      sixty-one records existed, so a third of this project's decisions were
      unreachable from their own map. Sixteen rows added, and CLAUDE.md's
      "ADRs 0001-0040" corrected with them.

### S7 — The gates prove what they claim

- [x] **S7.1** macOS runs the suite. It was **already building every test
      binary** — the dev profile has `LUAUG_BUILD_TESTS` ON and
      `CMakePresets.json` has carried a `macos-clang-dev` test preset all along
      — and then throwing the result away. So the compile was already paid for,
      at the 10× multiplier, and the marginal cost of running them is the test
      runtime rather than another build. That makes this the cheapest coverage
      available anywhere in `ci.yml`.

      **Non-blocking until it has been green once**, which is exactly the shape
      S7.5 used for the sanitizers and for the same reason: this is the first
      execution of any test in this repository on macOS, nothing on the
      developer's machine predicts what a different libc++ and a Metal SDL_GPU
      backend do, and a red `main` on a platform nobody can reproduce locally
      teaches people to ignore red. The job's own summary says so, and the
      `continue-on-error` comes off in the commit that reports the first clean
      run.

      `-LE gpu-golden` as every other tier does, and the skip assertion from
      S7.2 runs here too with the same expected list — a macOS runner has no
      Vulkan and may have no window server session, so those are expected, and
      anything else skipping is the regression the check is for. The job's
      timeout rationale is corrected with it: 168 s was the compile-only wall
      time, and the comment claiming 7× headroom now says ~230 s and 5×.
- [x] **S7.2** Six, not two, and the Linux stage was as blind as CI. CTest
      counts a skip as a pass, so `editor_seam`, `editor_shell` and the four
      differentials have reported green on every push without executing —
      they want a graphics device or a display and a hosted runner has neither.
      S7.3 made `localgate.ps1` fail on an unexpected skip; it turned out to do
      that on the **Windows stage only**, which is how `editor_shell` came to
      print `***Skipped` inside a run that said `ok linux`.

      `scripts/gates/assert-no-skips.sh` is the one implementation all three
      callers share, which is where CLAUDE.md says gate logic goes. It takes a
      ctest log and a list of names that are allowed to skip: **a ceiling, not a
      prediction** — a runner that gains a device runs them, passes, and needs no
      edit here, and what fails is anything ELSE skipping. That is the
      regression worth catching, because a test that quietly stops finding its
      fixture skips exactly like a test that cannot find a GPU, and until now
      both were green.

      `linux-build.sh` allows nothing at all: that tier has a device through
      lavapipe and a screen through Xvfb, so a skip there means one of the two
      stopped being found. `ci.yml` names the six on Windows and Linux, and says
      why the three `screenshot_gate*` are absent — `-LE gpu-golden` excludes
      those outright rather than skipping them.

      Break-verified in both directions: with the shell's window hidden again the
      Linux stage fails naming `editor_shell` while ctest prints "100% tests
      passed", and the script's parsing is unit-checked over a log with a skip,
      an allowed skip, a failure and a clean run.
- [x] **S7.3** A skip that nothing asserts is a skip nobody notices — six of
      them. `localgate.ps1` now FAILS on a skipped test and names which,
      with `-AllowSkips` as the deliberate escape for a machine with no GPU.
      `--no-tests=error` beside it, because an empty suite is not a passing
      one either. **The CI half is still owed** and belongs with S7.2.
- [x] **S7.4** `scripts/localgate.ps1 -Only winprofiles` builds them, on the
      compiler that ships them. The `shipping` stage already builds `shipping`,
      `player` and `editor` — on Tier-2, deliberately, because Clang with
      warnings-as-errors is the stricter reader of the `#if`s only those
      profiles take. What Clang cannot read is MSVC, and MSVC is what a Windows
      release is compiled with.

      `package.ps1` does build `editor` and `player`, which is why this looked
      covered and is not: it builds them **as part of producing the artifact**,
      so a break is found at release time by the person doing the release. No
      gate built them, ever.

      What hides here is specific. These profiles differ from `dev` only by what
      `LUAUG_LUAU_COMPILER` and `LUAUG_DEBUG_UI` gate, so what rots in them is
      code inside an `#if` — and a preprocessor branch one compiler takes and
      another does not is exactly where two compilers disagree. D056 is this
      repository's own instance: an unknown number of commits where the shipping
      profile did not compile at all.

      It runs the same `shipping-build.sh` the Tier-2 stage does, with the three
      preset names passed in — the script already took them that way, so there is
      one statement of which profiles and which target. The `.cmd` around it
      exists only to supply the Developer Shell the Ninja presets require.

      Opt-in, like `asan`, and for the same reason: 97 s warm against a standing
      gate that is 90 s for everything else. Break-verified with an `#error`
      inside `#if !LUAUG_DEBUG_UI` — which fails the stage in 3.5 s and also
      proves these builds really do have the overlay compiled out.
- [x] **S7.5** `linux-clang-asan` is fully wired and run by nothing. **Now
      run**: a `-Only asan` stage locally and a non-blocking nightly job,
      both through the same script. Two blockers found and fixed, neither in
      our code — the image had no sanitizer runtime, and UBSan's `vptr` check
      aborted the shader compiler on a SPIRV-Cross downcast at build time.
      **Its first full green run has now happened**: 199.7 s, the whole ctest
      suite plus the 1,168 conformance cases and the hot-reload gate, clean
      under both sanitizers (2026-08-27, recorded under S1.5). The earlier
      attempt reached 1,170 of 1,214 objects and stopped on another session's
      mid-edit file, which was never a finding about this engine. The nightly
      job's note says it becomes blocking once it has been green once — and it
      now has been, locally; making the CI job blocking waits on Actions running
      at all (see **Blocked**).
- [x] **S7.6** Built, and sharper than the gate it sits beside. `architecture.md`
      §9 and `roadmap.md` both promise "a small real-image golden suite
      (lavapipe on Linux, WARP/D3D12 on Windows) runs nightly, non-blocking",
      and neither had it: what existed was one recorded PNG,
      `meshes-lavapipe.png`, whose own README said nothing compared it and that
      promoting it to a comparison was a later milestone's decision, which was
      never taken.

      **The reason it was never taken turns out not to apply.** The argument was
      that a golden spanning a discrete GPU and a software rasterizer has to
      widen its tolerance until it can no longer see a real change — true, and
      it is why `screenshot_gate` carries `gpu-golden`. But a lavapipe golden
      compared only on lavapipe spans nothing: measured first, the same scene
      renders to the same bytes twice, **zero differing pixels at tolerance
      zero**. So these compare EXACTLY, where the dev-machine gate must allow 2
      per channel — a sharper instrument, not a weaker one.

      Two scenes. `examples/02-meshes` carries the whole pass list at once —
      shadow map, sky, forward PBR, the blended pass, tonemap — and the
      instanced specular scene is there for D043, which is the case nothing else
      can catch: that path shipped drawing **nothing at all** while its draw
      count fell, its command stream matched its capture and its frame got eight
      times faster, because the geometry was being transformed off-screen.

      Non-blocking as promised, and structurally so: `LUAUG_LAVAPIPE_GOLDENS` is
      OFF, so the tests do not exist unless asked for — a Mesa upgrade moves
      every pixel of both and must not be able to redden a pre-push gate. The
      nightly job runs the same `scripts/gates/lavapipe-goldens.sh` that
      `scripts/localgate.ps1 -Only lavapipe` does, prints the driver it found
      before comparing, and uploads the images whether or not they matched.
      `-Record` re-records, as a flag and never as something a comparison run
      can do on its own. Break-verified by rendering one frame short: 196,196
      differing pixels.
- [x] **S7.7** Examples that no gate boots — fixed, and the count was FOUR
      rather than five: `01-instances` was already driven by
      `tests/determinism/example01` and `04-obby` by `tests/replay/obby`, both
      of which point straight at the example directory. The four nobody ran
      were `02-meshes`, `03-physics-playground`, `06-scene` and `11-ocean`.

      One gate each, and the assertion is `FAIL_REGULAR_EXPRESSION` on the
      log's own `[error]` and `[warn]` prefixes rather than a per-example
      string — because that is the check that catches decay. A missing mesh, a
      refused write, a script that raised on tick one and left the world empty
      are all a log line, and every one of them ends with the host exiting zero
      and reporting its frames: "it ran" is exactly the claim that was already
      true of a broken example. Break-verified with a `warn` in one of them.
- [x] **S7.8** The shell is entered on both tiers now, and drawn on both. One
      test enters it — `on a real device, F3 flips the panel`, which builds a
      real SDL_GPU device with validation on, loads the icon atlas and renders
      the explorer and the properties table over every widget the fixture has.
      It ran on Windows. It could not run on Linux for a reason that had nothing
      to do with the shell: the Tier-2 image has a software Vulkan device through
      lavapipe and **no screen at all**, so the case returned at its first line.

      The image gets `xvfb` and `linux-build.sh` runs `ctest` under `xvfb-run`.
      Both are a superset of what the hosted runner installs, which the
      Dockerfile now states rather than claiming a parity it has not had since
      lavapipe went in: the image may test MORE than CI, never less.

      It crashed the moment it could run — **D147**, a segfault inside SDL3's own
      `VULKAN_DestroyDevice`. The case called `platform::shutdown()` by hand
      while its window and device were locals declared after it, so both were
      destroyed after SDL had gone. `run`, `runLauncher` and `runTwoWorlds` all
      carry a `PlatformScope` declared before the window for exactly this; the
      test had none. All three cases in the file have one now, the two on the
      Null backend included, where the shape rather than the fault is what
      matters.

      And the window is created VISIBLE, which is the difference between the
      shell drawing on Linux and not: a hidden window gets a backbuffer on
      Windows and gets none under lavapipe, so `.visible = false` — there to
      stop a window flashing — was silently costing the tier every assertion
      past the atlas. It is 320×200 for a tenth of a second, against the whole
      ImGui recording path getting a second driver and a second compiler.
- [x] **S7.9** The test that builds the shell can no longer return green without
      running. It is registered with CTest a second time on its own, as
      `editor_shell`, with `SKIP_REGULAR_EXPRESSION` matching the
      `LUAUG_TEST_SKIP` token this repository already uses — so with only that
      case in it, "did not run" and "the test skipped" become the same
      statement, and the summary says `Skipped` where it used to say nothing.
      Inside the 457-case `app` suite it could not: 456 other cases really did
      run, so the run was green and honest and the shell's absence from it was
      invisible.

      The other door that token cannot cover is an early return INSIDE the
      device block, which would leave a case that passes having drawn nothing.
      So the drawing is counted and `CHECK(shellFrames >= 4)` asserts it — the
      claim stated as an assertion instead of as a comment.
- [x] **S7.10** Four named, and checking each rather than believing the list
      found two genuinely uncovered and two already covered.

      **`tools/iconpatch` had nothing at all.** It is what puts a game's own
      icon into its packaged executable, and its own header says why that
      matters: nothing fails when an icon is wrong — the program runs, the window
      opens, and it wears the wrong face. The part where a mistake is silent is
      not the Win32 call, it is the directory arithmetic, and its own comment
      calls the failure "an executable with an icon-shaped hole in it". That is
      a parser, so it is a library now — `luaug_iconpatch_ico`, built on **every**
      platform even though the tool is Windows-only, so the Tier-2 gate reads it
      too. Nine cases: the 256-pixel entry stored as zero that every naive
      reader gets wrong; a CURSOR, which is the same shape with a hotspot where
      an icon has planes and a bit count; truncation in the directory and in the
      payload, both refused whole rather than half-read; an offset that would
      wrap a 32-bit sum; and the four bytes the whole tool turns on — a file
      entry's 32-bit offset becoming a group entry's 16-bit id, with a byte
      count that must survive as 32 bits or every icon over 64 KiB breaks and
      only the 256-square one shows it. Break-verified by removing the type
      check.

      **The SDL3 GPU backend was tested on a machine with a GPU and on no
      other.** Its three cases return before asserting when there is no device,
      and inside the `rhi` entry that is a pass — the API and null-backend cases
      really did run, so the entry was green and honest and the three that found
      no device were invisible in it. They are a `TEST_SUITE` now, registered
      again as `rhi_sdlgpu` with `SKIP_REGULAR_EXPRESSION`, which is the
      `editor_shell` shape from S7.9. It passes on both tiers — Tier-2 has
      lavapipe — and CI names it in the list that is allowed to skip.

      **The other two were already covered and the list was stale.** The exotic
      importer has an OBJ round-trip in `compiler_tests.cpp`, and
      `LUAUG_ASSETC_ASSIMP` defaults ON so it runs rather than compiling out.
      The forward renderer has five capture gates — clear, meshes, skinned and
      the UI at two resolutions — which assert the exact RHI command stream it
      emits, need no GPU, and run on both tiers. That is a stronger test of a
      renderer than a unit test would be, and `architecture.md` §9 already says
      so: the capture gate and not the image comparison is the blocking render
      gate.
- [x] **S7.11** **Found the trace after writing this entry, and it changes what
      the item was about.** `docs/briefs/e8-kickoff.md` Finding 5 records it:
      E1 concluded "SDL does not accept injected input" and five milestones
      repeated it, which is true of ImGui-level injection and **false of real
      Win32 input** — `SetCursorPos` + `mouse_event` + `SendKeys` reaches SDL
      exactly as a person's does, and three script-editor defects were found by
      driving the window that way.

      So the harness was a **discovery** instrument, not a regression one, and
      the three defects it found already carry unit tests: the IME's
      `WantTextInput`, the byte-column-versus-codepoint defect asserted over a
      two-byte and a four-byte character, and AltGr read as a Ctrl chord. What
      was lost is the technique, and the technique is written down in that brief
      where somebody looking for it will find it.

      **Rebuilding it as a gate is declined, with the reason recorded so it is
      not re-litigated**: a Win32 driver needs a focused window on a real
      desktop session, and a gate that fails when somebody alt-tabs is the exact
      class of thing this whole stage is about — a check nobody trusts is a
      check nobody runs. It is worth having as an exploratory tool and it is
      worth nothing as a blocking one.

      What is committed instead is durable coverage for the OTHER thing nothing
      could reach, and the right place turned out not to be the input module at
      all. `input_tests`
      has thirty-one cases over actions, bindings, contexts and the virtual
      seam. What has none is **who owns the mouse**, and four defects came out
      of that one question: D049 (the pointer lock did nothing, because the
      property was stored and read by nothing), D059 (the editor opened on a
      project that locks its pointer and had no cursor to click a panel with),
      D063 (a right-drag hid the cursor without holding it, so it walked out of
      the window) and D069 (the invisible cursor still hovered and clicked
      Explorer rows nobody could see).

      **Every one of those was reported by a person**, and the reason is the
      same reason each time: the rule was arithmetic inline in the frame loop,
      so nothing could call it. `decidePointer` is that rule as a function —
      what is wanted in, what should be true out, no SDL and no world — and
      `engine.cpp` now asks it instead of working it out. It is the move
      `debug_overlay_tests.cpp` already argues for at the top of its own file:
      a decision made with arithmetic rather than with pixels is a function, and
      the cases run on any machine at all.

      Nine cases, each named after the defect it would have caught, plus the two
      that would go unnoticed in the other direction — play handing the pointer
      back, and a `lookActive` that must not reach the answer in a player build
      where there is no editor camera to turn. Break-verified by dropping
      `cameraDetached` from the ownership test, which fails four assertions.
- [x] **S7.12** A release-time check that the tag and the project version agree
      — built as `tools/repo/versioncheck.luau`, run by the Luau gate locally
      when HEAD carries a tag and by CI on every `v*` push. A release names
      itself twice: in the tag somebody pushes and in `project(LuauG VERSION
      ...)`, which ADR 0031 makes authoritative and which everything a release
      stamps comes from — the archive's name, `luaug --version`, the version in
      every packaged artifact. `v1.1.0` against a tree declaring `1.0.0` would
      publish a release page saying one number and a binary saying another, and
      the first bug report would arrive against the wrong version. D086 records
      the near-miss from the other direction. A milestone tag is skipped and
      says so, because a check that goes quiet is indistinguishable from one
      that is not running.
- [x] **S7.13** `inertcheck`'s stated blind spot — closed, and it was hiding two
      real ones. The tool swept by field NAME, so eleven names in `components.h`
      that more than one component declares (`enabled` by nine) shared a single
      verdict: a reader of any one cleared all of them. Its own comment offered
      `InputBindingComponent::image` against `ImageLabelComponent::image` as the
      hypothetical, and said the fix was a C++ parser.

      It is not a parser. **A file only clears the owners it names**, by the pool
      accessor — read out of `world.h` rather than listed, so a component added
      tomorrow is discriminated for free, and taking `EngineState& engineState()`
      as well as `ComponentPool<T>&` because the state sweep's readers say
      `engineState()` and never say `EngineState`. A file that holds the name and
      names no owner is not evidence about any of them and is skipped — which has
      to be that way round: clearing everyone instead is the by-name sweep again,
      and on the first run thirteen of these fell straight through it. `tests/`
      joined `generated/` and `native_accessors.cpp` on the excluded list, for
      their reason: a test that sets a field and reads it back demonstrates the
      accessor, which is exactly what `Inert` already means. That dropped 106 of
      383 files and surfaced nothing engine code genuinely reads.

      Two findings, both hand-confirmed. `InputBindingComponent::image` — the
      comment's own hypothetical — is genuinely unread, and is the same datum as
      `DisplayName` in a picture instead of a word: data a game reads back to
      draw its own prompt. It now carries that argument in its Doc and sits on
      the `StorageOnly` list beside its sibling. And `UIObjectComponent::rotation`
      was a documented property that **nothing drew for the whole of v1**,
      cleared every run by `CFrame::rotation` in the editor's camera code.

      `GuiObject.Rotation` now works rather than being marked `Inert`, because
      `Inert` means naming the milestone that will act on it and there is none —
      v1 is closed. The quad carries a full affine `p -> R*p + t` and not an
      angle and a pivot: a turned element inside a turned ancestor spins about
      two different points, which composes to a rotation plus a translation that
      angle-and-pivot cannot express. It is applied to POSITION only, so the
      vertex's own frame stays upright and a rounded corner stays round instead
      of becoming an ellipse; and it is stamped over the range of quads an
      element emitted rather than at each push site, because one element draws a
      background, two scroll bars, a picture and a glyph per character through
      four functions. Layout, the hit test and `ClipsDescendants` do not see it,
      which the property's Doc now says.

      The helpers are called `turn` and not the obvious thing — a field here
      wearing that name would itself have counted as a reader of the component
      field it draws, which is the same collision one level down. Break-verified:
      neutering the read makes the lint name `UIObjectComponent::rotation`, and
      restoring it passes. What the pass still cannot do is separate two owners
      read in ONE file — `render_world.cpp` reads a point light's `shadows` and a
      spot light's four lines apart — and the tool's comment says so instead of
      claiming otherwise.
- [x] **S7.14** The window icon test now runs against the hand-built `.ico`, and
      against all seven of its sizes. What existed read an icon back out of the
      test binary — which proves an icon is THERE, and three things beyond that
      it cannot say. `applicationIconBytes` returns only the largest entry,
      because that is what a window wants, so the six smaller ones were never
      looked at; nothing tied the bytes in the binary to the file in the tree, so
      a rebuilt `.ico` that never reached the resource compiler passed; and on
      anything but Windows the case asserts emptiness and checks nothing at all.
      The sizes it could not see are the ones a person actually sees — 16 in the
      title bar, 32 in the taskbar, 48 and 256 in Explorer.

      Two cases replace that. One parses `branding/icon/luaug.ico` itself, on
      every platform: the `ICONDIR` header, then every entry square, in range,
      and **PNG-compressed** — a BMP entry needs a DIB reader nothing in this
      engine has, and it is the small sizes a rebuild is most likely to emit
      that way — and one entry for every `luaug-<size>.png` beside it, because a
      size added to the folder and forgotten in the `.ico` is the ordinary way
      this decays. The other compares the largest entry of that file against
      what the executable hands back, byte for byte, which is the assertion that
      makes the other two mean something together.

      Break-verified by pointing the definition at `tests/identity/game.ico`:
      the shape case fails on the missing 24×24 and the identity case fails on
      the bytes, and both pass again on the real one.

### S8 — Small debt, then the release

- [x] **S8.1** Archived, and the file is back under its cap with this session's
      entry in it. It stood at 293 lines against §11's ~300, which means the
      next entry written would have broken the rule rather than the one after
      — a cap you are already at is a cap you have already lost.

      Session 26's entry moved whole to
      `docs/progress-archive/2026-08.md`, not summarised: an archive that
      paraphrases is a second version of the record to keep in step. The pointer
      in `## Session Log` names what went and when.

      Two stale claims went with it, which is the part that matters more than
      the line count. `## Now / Next` said "the full six-stage gate has not been
      run since the untracked tree landed" — that is S1.5 and it is closed, and
      the gate is nine stages now. It also named one quarantined instrument and
      not the second: macOS has no local instrument at all, so the only thing
      that can answer for Tier 3 is Actions, which is not running. **That is
      exactly the failure session 26 recorded about this file** — a section
      appended to and never re-read is a pile of facts nobody compares — and
      it had already grown two more.
- [x] **S8.2** The starter template's unnecessary incantation — gone, and the
      fix was not where the item pointed. `StreamingMode = "Persistent"` was
      CORRECT usage: a partitioned record carries no parent path, by design,
      because a path into `Workspace` is sometimes nil in a world that is not
      all present (ADR 0053, rule 5). What was wrong is that a fifteen-part
      project was partitioned at all. Streaming now needs a scene to reach four
      cells before it is worth anything: below that the whole grid is inside
      any sensible load radius, so nothing is ever evicted and the only thing
      the partition buys is the tree-identity trap. The starter boots whole.
      **Two engine changes came out of chasing it**: the floor, and the rule
      that a world with no focus registered wants everything — "no focus" is
      not "a focus that wants nothing", and a world nobody can see with nothing
      on screen saying why is the worst shape a default has.
- [x] **S8.3** `localgate.ps1` and `shipping-build.sh` disagree about how many
      profiles ship — corrected. The gate builds THREE (`shipping`, `player`,
      `editor`) and `shipping-build.sh`'s own header has said so since `editor`
      was added; `localgate.ps1`'s comment and its error message both still
      said two. A stage that names fewer profiles than it builds sends somebody
      looking in two places when the fault is in a third.
- [x] **S8.4** `tests/screenshots/ui-1280x720.png` — checked in at M6 as the gate
      record's evidence, read by no test since, and therefore never re-recorded.
      By the time anybody looked it differed from the renderer by **1,585
      pixels**.

      **Worth reading rather than deleting**, and D026 is the reason: `upload`
      records a buffer's SIZE and not its contents, so the two capture goldens
      of this same scene are structurally blind to the QUADS. They prove the
      scissors, the viewport and the draw count; `luaug_ui_tests` proves the
      rectangles exactly; only pixels can say the layout put anything where it
      belongs. This is the third leg and it was lying on the floor.

      Re-recorded after checking what had drifted rather than assuming: the
      difference is confined **entirely to the glyphs** — every panel, the badge,
      the clipped strip and the overhang are pixel-identical — so it is font
      drift since M6 and not a layout regression. Registered as
      `screenshot_gate_ui`, `gpu-golden` for the reason the other two carry it:
      the frame is mostly text and glyph rasterization is the first thing to
      differ between GPUs.

      And a lavapipe twin, `lavapipe_golden_ui`, which is where an exact
      comparison earns most of all — a tolerance wide enough to survive two
      rasterizers is wide enough to hide a character drawn in the wrong place.
- [x] **S8.5** Four named. Checking each rather than trusting the list found
      **three already closed and one real**, which is the same ratio S7.10 had.

      **D131's residual was never the leak, it was the silence.** The leak was
      fixed at four call sites; what remained is that `readFileAsync` returns an
      invalid request when the pool is full and every caller falls through to a
      synchronous read — right, and exactly why nothing broke. So a pool taken
      to 512 by any FUTURE leak would again stop every asynchronous path in the
      process at once, in silence: thumbnails, material textures and world
      streaming, all of them, with nothing said. It logs now, once per episode
      — not per call, because a full pool is asked hundreds of times a second,
      and not once per process, because a pool that fills, drains and fills
      again has told you two different things — and outside the mutex, so a
      formatter that allocates cannot put the pump thread behind a logger. A
      case asserts the ceiling: 512 allocate, the 513th is refused rather than
      hung, one release restores capacity, and the pool comes back WHOLE.
      Break-verified by removing the release from `cancelIo`, which reports "the
      pool came back short: slot 4".

      **D003's rotational half is D050**, fixed 2026-08-22, and this row said so
      nowhere: the snap rounded an absolute position in a light space that turns
      with the sun, and rounding against last frame's box fixed it — 201 then
      1,312 pixels between consecutive frames before, which is a discrete jump,
      against 224 / 570 / 1,029 / 1,571 after, which is what sliding looks like.
      D003's row now points at it instead of reading like an open residual.

      **D047 and D044 are closed with no residual at all.** D047's caveat is
      written into its own row and is a rule rather than a defect — a camera
      driven on `PreRender` is blended slightly towards the last tick, which is
      why every example drives it on `Heartbeat`. D044 records its own cost,
      0.54 ms at 1080p, in `perf-baselines.md` rather than burying it.
- [x] **S8.8** `perf-baselines.md` states the wrong `churn10k` budget and
      misdescribes where thresholds are enforced — corrected. `churn10k`'s
      budget is 32 ms, not 16, and it was wrong in FOUR rows rather than three
      (E9 added one). Every other scene's row already matched its own
      `scenario.json`. The file now says where a budget actually lives — the
      scene's own `budgetMs`, enforced by the `perf_budget` CTest — rather than
      calling it "the CI threshold", which named a place instead of a
      mechanism, and named the wrong place: nothing in `.github/workflows`
      knows what a bench scene costs.
- [ ] **S8.6** The release: where the editor archive attaches, and what version
      it carries.
- [ ] **S8.7** Hand back to the owner with one act left: make it public.

---

## Found while working, not on the original list

- **A crash the owner hit was mine, and reading it was impossible.** The
  `std::get` in S3.1's fix decided by the property's DECLARED type; every
  accessor answers `Value{}` for a missing component, and `std::get` on that
  throws. Fixed with `get_if`. **But the real finding is that the `.dmp` could
  not be read at all** — no debugger on this machine, and the handler wrote
  nothing else. It writes a readable note now: exception, plain name, faulting
  address, and a symbolised stack with file and line. Owes a register row.
- **A reported second defect that turned out not to exist, recorded so nobody
  re-reports it.** The owner's `.luaug/editor.json` names
  `scenes/testing.scene.json`, which is gone, and an editor run appeared to open
  the project empty -- "Loaded a scene of 0 instance(s)". The fallback is in
  fact already there and correct (`engine/app/src/engine.cpp`: a remembered
  scene that does not exist falls through to `luaug.toml`'s), and re-running it
  loads all six instances with the stale `editor.json` in place and without it.
  **The empty read came from a stale binary**, built before other sessions'
  committed work had been linked in. The lesson is the campaign's own standing
  rule pointing the other way: check what you are running before believing what
  it says.
- **The Linux half of the crash gate is unverified.** On MSVC the runtime's
  own `__try` around `main` means `std::terminate` never runs, so the note
  carries the stack rather than the message; POSIX should reach `terminate`
  and therefore the message. The gate asserts each accordingly and only the
  Windows half has been run.

## Blocked

- **Nothing is blocked on the owner.** The one act reserved for them -- making the
  repository public -- is sequenced last and blocks nothing before it.
- **GitHub Actions has been dark since 2026-08-22**, every run completing in about
  four seconds having executed zero steps. The annotation names it exactly:
  *"The job was not started because recent account payments have failed or your
  spending limit needs to be increased."* Not one job starts -- including
  `changes`, which does nothing but read a path filter -- so a red run means the
  account, not the commit.

  Two ways out and both are the owner's: raise the spending limit, or make the
  repository public, which is the last act on this list anyway and takes Actions
  off the metered quota entirely. Nothing here can do either.

  Local gates are the instrument until then, and they are a good one -- seven
  stages, both tiers, the sanitizers and the lavapipe goldens. **macOS is the
  hole**: Tier 3, unbuildable here, and no instrument at all in the meantime.

  **Three items closed in S7 wrote CI-side code that has therefore never
  executed**: the skip assertion (S7.2) on the Windows and Linux jobs, the macOS
  test step (S7.1), and the nightly lavapipe job (S7.6). Each was reasoned
  against a real ctest log and the shared parts -- `assert-no-skips.sh`,
  `lavapipe-goldens.sh` -- are exercised locally, break-verified, on every run.
  The workflow YAML around them is not, and the first push after Actions comes
  back is where that gets found out. The macOS step is `continue-on-error` for
  its own stated reason, which happens to also cover this one.

## What went wrong with delegating, and the rule it produced

**Four parallel agent workflows died silently after roughly ten minutes of
work each**, having written no result and left the tree half-edited. One had
written a whole test file for an implementation it never wrote, which broke the
build for every other agent that tried to compile; another had left
`if (false) return MoveResult::Unchanged;` under a complete comment explaining
why the predicate had to be there.

What was salvageable was salvaged and finished by hand, and it was most of it.
The rule for the rest of this campaign: **delegate narrow, verifiable pieces,
and never a piece whose half-done state breaks the tree for everybody else.** A
long agent task is a bet that it finishes; four of them at once is four bets,
and the tree pays for every one that loses.

## Log

- **2026-08-26** — Campaign opened. Peers notified; all three replied, disclaimed
  the orphaned untracked tree, and stood down. Ownership table above built from
  their answers rather than from inference.
- **2026-08-26** — **164 commits and the three existing tags reached `origin`.**
  The repository had no backup and no review of eight milestones' work; it has
  both now. `milestone/e2` and `milestone/e3` created and pushed.
- **2026-08-26** — **The sanitizers passed for the first time ever**: 41 ctest
  entries, 1,158 conformance cases and the hot-reload gate under ASan and
  UBSan. Getting there found a heap-use-after-free in `World::clone` that had
  been there since clone existed, plus three things that were not defects in
  this engine at all. The nightly job is blocking now, which is the promise it
  made while it had never passed.
- **2026-08-26** — The untracked tree landed in four commits: the documentation
  site with the two gate edits that already invoked it, the branding set, the
  two examples, and the icon masters. Scratch swept.
