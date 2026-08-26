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
| 3 | `churn10k` 2.02 → 7.32 ms/tick | *pending* | |
| 4 | Jolt `CROSS_PLATFORM_DETERMINISTIC` | *pending* | |
| 5 | `PointLight.Shadows` / `SpotLight.Shadows` | *pending* | |
| 6 | `Ragdoll:Build` / `Blend` | *pending* | |
| 7 | The split-piece URN form | *pending* | |
| 8 | `examples/05-streaming` golden row | *pending* | |
| 9 | Where the editor archive attaches | *pending* | |
| 10 | Dot-access to children (`api-design.md` divergence #26) | *pending* | Raised at handover. The owner reported it as a bug **twice**. A live inconsistency rides on it: autocomplete offers children after a plain dot and the runtime refuses them. Either the divergence is reversed, or the completion stops offering what cannot work. |
| 11 | `art/` in git | **Masters in, working files out.** `art/` lands minus retired drafts, `temporary/` and rejected candidates -- 39.5 MB, 203 files. | `icons/bake.py` is TRACKED and reads `art/editor-icons/` to generate the COMMITTED `icons/default/**`. A generated artifact whose input is absent cannot be regenerated by anyone. (A peer's sweep for readers of `art/` missed this because it did not include `*.py`.) The rejected and retired piles are working files, and a public repository cannot remove them from its history afterwards. |

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

- [ ] **S2.1** `../PROGRESS.md` — it contradicts itself in the sentence that
      names the next action, and claims the ImGui shell cannot be driven fifty
      lines after disproving it.
- [ ] **S2.2** `roadmap.md` phase-1 table — stale on four rows, and E3 is named
      wrong.
- [ ] **S2.3** `roadmap.md` M4.5 checklist — fifteen unticked boxes of which the
      register closes thirteen.
- [ ] **S2.4** `api-design.md` §5 — declares as unbuilt an output that shipped
      and is freshness-gated.
- [ ] **S2.5** E3 gets a roadmap detail section, the only E-milestone without
      one. Its gate keeps one honest PENDING row: whether a badge reads at
      16 px is a picture, and the geometry test is not that picture.
- [ ] **S2.6** `briefs/e9-kickoff.md` and E9's roadmap section — E9 is being
      deferred against by name and has no gate to close against.
- [ ] **S2.7** The decision record for the material reversal, and for the Part B
      deviation justified only in commit messages.
- [ ] **S2.8** The two dangling `D-` placeholders in `defects.md`, and the
      `docs-lint` blind spot that let them through a green gate.
- [ ] **S2.9** `../CLAUDE.md` and `ci.yml` disagree about whether macOS blocks a
      code push. They are exact opposites, tag behaviour included.

### S3 — The defect tail

- [ ] **S3.1** An instance-valued property set inside a placed stamp is dropped
      silently on save. Reproduce first; the fix is a decision, not a patch.
- [ ] **S3.2** Every loose texture is encoded as sRGB, and the Material design
      names loose textures — the regression a past fix already closed once.
- [ ] **S3.3** The toolbar's New button wipes an unsaved scene with no prompt.
      Four other doors ask; this is the fifth.
- [ ] **S3.4** A stamp whose root is a `Model` is placed at the world origin.
- [ ] **S3.5** `dev.err.not_implemented` has no i18n entry, and the gate built
      for exactly that is structurally blind to it.
- [ ] **S3.6** Three comments that are live doc/code lies.
- [ ] **S3.7** The Console's log is a fixed-height child in a resizable panel.
- [ ] **S3.8** D129 — a sound's first play decodes on the calling thread. Open
      by decision; this campaign settles it rather than re-deferring it.
- [ ] **S3.9** D066 — a quarantined instrument whose successor is named and
      unbuilt.
- [ ] **S3.10** D092 — opening a project runs every entry script's file scope,
      so a world nobody authored appears before anything is played. The design
      is settled and recorded; the implementation is not, and
      `templates/starter/src/scripts/main.luau` has to change with it or a
      scaffolded project opens empty.
- [ ] **S3.11** `content/schenes/` is misspelled in `templates/starter` and in
      `testingproject`. Fix both or neither.

### S4 — E9 closed

- [ ] **S4.1** Decide the split-piece URN form. Blocks the two below.
- [ ] **S4.2** Step 12 — the editor compiles on import: `assetc::importOne`,
      the object-store writer, `luaug_assetc_lib` linked into the app, and
      `splitByPrimitive` reaching a world.
- [ ] **S4.3** Step 11's unbuilt half — parallel texture encode.
- [ ] **S4.4** Step 9's unbuilt half — the skeleton overlay and the joint
      picker, so `JointName` stops being free text typed from memory.
- [ ] **S4.5** Step 14 — the cut-over. Last, because it is the only
      irreversible one.
- [ ] **S4.6** Step 15 — benches, baselines, decision records, roadmap.
- [ ] **S4.7** E9's declared verification, none of which exists.
- [ ] **S4.8** E5's unfinished half.

### S5 — The editor's declared verbs

- [ ] **S5.1** Anything that is not a `BasePart` can be picked and drawn.
- [ ] **S5.2** The manipulator moves a `Model`, a `Camera`, an `Attachment`.
- [ ] **S5.3** Selection resolves to the meaningful ancestor, with drill-down.
- [ ] **S5.4** Group / Ungroup.
- [ ] **S5.5** Tags, so the documented primary addressing path is reachable.
- [ ] **S5.6** A stamp override is visible, revertable and appliable.
- [ ] **S5.7** Project settings, which needs a TOML writer the engine lacks.
- [ ] **S5.8** Camera control during play.
- [ ] **S5.9** The Streaming panel in the editor shell — blocks E5's last gate
      row.
- [ ] **S5.10** The physics wireframe from edit mode.
- [ ] **S5.11** A console error jumps to the line that raised it.
- [ ] **S5.12** The Stats panel shows what is already measured.
- [ ] **S5.13** Snap step editable; a reference grid drawn.
- [ ] **S5.14** Properties search and categories.
- [ ] **S5.15** Attributes.
- [ ] **S5.16** Thumbnails for meshes, scenes and stamps; a material swatch.
- [ ] **S5.17** Pivot/centre choice for a multi-selection.
- [ ] **S5.18** Sibling reordering.
- [ ] **S5.19** The typed stubs every scaffolded project's settings already
      point at.

### S6 — Inert surfaces resolved

- [ ] **S6.1** `PointLight.Shadows` / `SpotLight.Shadows` — honour or remove.
- [ ] **S6.2** `Enum.CollisionFidelity.Precise` — accepted and silently a hull.
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
- [ ] **S7.3** A skip that nothing asserts is a skip nobody notices — six of
      them.
- [ ] **S7.4** No gate builds the three Windows profiles the release ships.
- [ ] **S7.5** `linux-clang-asan` is fully wired and run by nothing.
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
- [ ] **S8.2** The starter template's unnecessary incantation.
- [ ] **S8.3** `localgate.ps1` and `shipping-build.sh` disagree about how many
      profiles ship.
- [ ] **S8.4** A checked-in golden nothing reads.
- [ ] **S8.5** D131's residual, D047, D044, D003's rotational half.
- [ ] **S8.6** The release: where the editor archive attaches, and what version
      it carries.
- [ ] **S8.7** Hand back to the owner with one act left: make it public.

---

## Blocked

Nothing yet.

## Log

- **2026-08-26** — Campaign opened. Peers notified; all three replied, disclaimed
  the orphaned untracked tree, and stood down. Ownership table above built from
  their answers rather than from inference.
- **2026-08-26** — **164 commits and the three existing tags reached `origin`.**
  The repository had no backup and no review of eight milestones' work; it has
  both now. `milestone/e2` and `milestone/e3` created and pushed.
- **2026-08-26** — The untracked tree landed in four commits: the documentation
  site with the two gate edits that already invoked it, the branding set, the
  two examples, and the icon masters. Scratch swept.
