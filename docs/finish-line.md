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
- [ ] **S1.5** Full `scripts/localgate.ps1`, six stages, green.
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
- [ ] **S5.6** A stamp override is visible, revertable and appliable.
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
- [ ] **S5.19** The typed stubs every scaffolded project's settings already
      point at.

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
- [ ] **S6.3** The parallel scheduler phases that nothing runs in.
- [ ] **S6.4** The dev protocol's two reserved verbs.
- [ ] **S6.5** TLS — needs an approved decision record before it needs code.
- [ ] **S6.6** KTX2 HDR; glTF topologies; the second UV set half-importer.
- [ ] **S6.7** A text caret.
- [ ] **S6.8** An animation clip addressable on its own.
- [ ] **S6.9** i18n plural rules beyond English.
- [ ] **S6.10** Jolt on a real job pool.
- [ ] **S6.11** The four outstanding items of the rendering reference decision.
- [ ] **S6.12** Decision 10 above, and whichever half of the dot-access
      inconsistency it leaves standing.

### S7 — The gates prove what they claim

- [ ] **S7.1** macOS runs tests.
- [ ] **S7.2** The two gates that skip on every CI push.
- [x] **S7.3** A skip that nothing asserts is a skip nobody notices — six of
      them. `localgate.ps1` now FAILS on a skipped test and names which,
      with `-AllowSkips` as the deliberate escape for a machine with no GPU.
      `--no-tests=error` beside it, because an empty suite is not a passing
      one either. **The CI half is still owed** and belongs with S7.2.
- [ ] **S7.4** No gate builds the three Windows profiles the release ships.
- [x] **S7.5** `linux-clang-asan` is fully wired and run by nothing. **Now
      run**: a `-Only asan` stage locally and a non-blocking nightly job,
      both through the same script. Two blockers found and fixed, neither in
      our code — the image had no sanitizer runtime, and UBSan's `vptr` check
      aborted the shader compiler on a SPIRV-Cross downcast at build time.
      **Its first full green run is still owed** — it reached 1,170 of 1,214
      objects and stopped on another session's mid-edit file. It becomes
      blocking once it has been green once, which the job says out loud.
- [ ] **S7.6** The nightly real-image golden job that two documents promise.
- [ ] **S7.7** Five of nine examples are never booted.
- [ ] **S7.8** The shell is entered by no test on any machine.
- [ ] **S7.9** The one test that builds the shell returns green without running.
- [ ] **S7.10** `tools/iconpatch`, the exotic importer, the SDL3 GPU backend and
      the forward renderer.
- [ ] **S7.11** The input harness that found three defects and was never
      committed.
- [ ] **S7.12** A release-time check that the tag and the project version agree.
- [ ] **S7.13** `inertcheck`'s stated blind spot, live today.
- [ ] **S7.14** The window icon test has never run against the hand-built `.ico`
      now in the tree.

### S8 — Small debt, then the release

- [ ] **S8.1** `../PROGRESS.md` archiving, now that one writer makes it safe.
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
- [ ] **S8.4** A checked-in golden nothing reads.
- [ ] **S8.5** D131's residual, D047, D044, D003's rotational half.
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
  four seconds having executed zero steps, which is the quota signature. So CI
  cannot confirm anything until the repository is public. Local gates are the
  instrument until then, and macOS -- Tier 3, unbuildable here -- has no
  instrument at all in the meantime.

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
