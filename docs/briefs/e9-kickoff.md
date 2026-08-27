# E9 — Compiled Assets and a Skeleton You Can Touch

**Written in the middle of the milestone rather than at its start, and that is
the first thing to know about it.** E9 was specified in a plan that lived
outside this repository, built two thirds of the way through, and then left
standing. Three of its steps are deferred against **by number** in
[`PROGRESS.md`](../../PROGRESS.md) and in [`defects.md`](../defects.md) — by a
numbering nothing inside the repository defined. That is the hole this file
fills: the scope, what is actually in `main`, what a reversal replaced, and a
gate somebody can close the milestone against.

- Opened 2026-08-25 at `96421a5e`. **Open.**
- Roadmap: the summary row is in [`docs/roadmap.md`](../roadmap.md); the detail
  section is this brief's companion and is owed by the campaign's S2.6
  ([`finish-line.md`](../finish-line.md)).
- Decision records: **none.** Every decision below is in a commit message, and
  one of them reverses a design that had already shipped. S2.7 owes those.

## Goal (restated)

Importing a downloaded horse exposed the whole seam at once. The file was
refused outright, then loaded lying on its back, then turned out to carry 677
joints against a 64-matrix palette. Three fixes landed (`96421a5e`, `16d81571`)
and what they uncovered is that the shape underneath is wrong in four connected
ways:

- **The runtime parses vendor formats.** A loose `.gltf` is re-parsed on every
  launch — 3 MB of JSON and 14 MB of buffer for that model — with no LODs, no
  meshlets, and textures uploaded as raw RGBA8 with no mips. The compiled
  pipeline that does all of that (`assetc`) exists and the editor does not use
  it.
- **A model arrives as one opaque `MeshPart`.** Five materials become five
  submeshes of one part: nothing to select, nothing to give a material to, and
  nothing for a `Model.Scale` to scale.
- **There is no material.** The definition is embedded in the mesh, so it cannot
  be edited, shared, or pointed at by two parts. And because nothing named which
  slot a texture filled, *every* normal and ORM map in this engine was encoded
  as sRGB.
- **The skeleton is invisible and untouchable.** Joints are a flat array in a
  render-side library keyed by URN, so nothing in `scene` or `physics` can read
  a joint transform: no socket to weld to, and no path for physics to drive a
  pose.

The outcome the milestone is for: **you drag a model in, it becomes a `Model`
you can open, with named parts you can select and materials you can edit; you
scale it with one number; you weld a sword to a hand; and you turn a character
into a ragdoll.** Compiled, incremental, and fast enough that none of it is a
wait.

The human's constraints, as given: loose `.gltf` stops working; materials
reference the source textures and the game compiles later; parts are named after
the primitive in the file; **ragdoll is for now**; and it is one milestone in a
single flow rather than tasks broken up by stage.

## The three assumptions the plan took, and what became of them

1. **A skinned file produces one `MeshPart`, not one per primitive.** *Held.*
   `splitByPrimitive` returns a skinned model as a single piece wearing the
   file's own name, and its header says why: a skeleton belongs to one mesh, and
   a 677-joint rig duplicated into every piece is the rig several times over in
   memory and several palettes uploaded per frame for one answer.
2. **`Bone` is created on demand, plus a skeleton overlay in the viewport and a
   joint list in the property picker.** *Half.* Bones are on demand and the
   class documentation argues the case. **The overlay and the picker were never
   built**, so `JointName` is free text typed from memory against a rig nobody
   can see — which is the whole of what assumption 2 was buying.
3. **Opening a project compiles what has no compiled form.** *Untested.* Step 12
   is unbuilt, so nothing compiles on import or on open, and the assumption has
   never been exercised.

## Decisions E9 took, none of which has a record

Each is load-bearing, each is currently only in a commit message, and the first
one reverses a design that had already shipped.

- **A `Material` is an instance, and a material file is a stamp of one**
  (`8ea7eee7`). This replaced `.material.json` as a bespoke asset format — see
  the section below.
- **`BasePart.Color` multiplies its material rather than replacing it**
  (`6b5d7717`). White on either side is the identity, so nothing that already
  existed changed; the fourth channel had multiplied since M4 and leaving RGB
  alone was an inconsistency rather than a decision.
- **`Model.Scale` is absolute and baked into descendants** (`19b28a27`). Setting
  2 twice leaves the model twice its built size. Descendants added afterwards
  are not scaled, which is the honest cost of a baked fan-out and is stated in
  the property's own Doc.
- **A ragdoll is parts and constraints, not `JPH::Ragdoll`** (`c948097c`,
  `8a04e28a`). Jolt's class owns its bodies; ours are created by mark-and-sweep
  from the tree, and two owners of one body is the mirror rule broken.
- **A rig past the palette budget is baked to a static mesh at import**
  (`16d81571`). 677 joints against 64 does not pose badly, it indexes off the
  end and scatters. Past the budget the import skins every vertex once and says
  what it did with both numbers in it.
- **A joint frame is given in each body's own local space** (`c948097c`), so a
  floating-origin rebase costs nothing: the bodies move and the frames do not.

## Scope — the fifteen steps, and where each one actually is

The plan ordered the work by compile order rather than by approval points. This
table keeps the plan's own numbering, because that is what `PROGRESS.md` and
D125 already defer against, and it is checked against the tree rather than
against the plan.

| # | What the step is | Where it is | Commit |
|---|---|---|---|
| 1 | `core::cframeFromMatrix`; the pivot helpers lifted out of the VM binding into `engine/scene/src/pivot.cpp` | **Built** | `25d899f0` |
| 2 | `ContentMounts::mountObjects` — a third mount kind — and the `blob()` widening | **Built, mounted by nobody.** Six cases in `content_tests.cpp` are its only callers | `956d9998` |
| 3 | `engine/asset/material.{h,cpp}`, `AssetKind::Material`, `ContentKind::Material` | **Built, then reversed** — see below | `fe979d04`, reversed by `8ea7eee7` and `038727b9` |
| 4 | `Model.Scale` and `MeshPart.MeshSize` | **Built** | `19b28a27` |
| 5 | The constraint seam in `IPhysics3D`, the Jolt backend, and its test suite | **Built** | `c948097c` |
| 6 | `SkeletonHost`'s read direction; `Pose::model`; the `rebuildPose` repairs | **Built** — one of the two planned repairs was real, one was not | `c7c9faff` |
| 7 | `splitByPrimitive`, `MeshInstance` names, `assetc::importOne` | **Built.** `importOne` is the one call both the command line and the editor make, and the split now has a caller | `cb54b3bb`, `25c7b7ad`, `555b617c` |
| 8 | Materials written and referenced; the material property; the sRGB fix | **Built.** The import writes a material file per primitive and points the part it places at it | `6b5d7717`, `df03f5b0`, `22649d53` |
| 9 | `Attachment`, `Bone`, `anchorFrame`, `resolveAttachments`, the skeleton overlay | **Built.** `View > Skeletons` draws every bone and a joint is something the picker offers | `3c02c4cb`, `566f49b5` |
| 10 | The constraint classes; the second `applyScene` walk; retirement before bodies | **Built** | `ac2acda7` |
| 11 | Incrementality and parallel texture encode in `assetc` | **Built.** One texture per worker over `StableCommit`, with the `--jobs=1` leg asserting the bytes did not move | `ca92c35c`, `f626f326` |
| 12 | The editor compiles on import and on project open; the object store; `Model` plus named parts placed | **Built**, all five pieces | `3ce31636`, `15607b18`, `22649d53` |
| 13 | `SkeletonHost`'s write direction; `Ragdoll`; `Bone.Transform` | **Built, minus two named verbs.** `Ragdoll:Build(profile)` and `Ragdoll.Blend` do not exist | `8a04e28a` |
| 14 | The cut-over: delete the loose feed, rewire `syncSkeletons`, migrate every project and gate | **Built.** Three loose feeds, not one -- `syncTextures` had no compiled branch at all (ADR 0065) | this session |
| 15 | Benches, baselines, decision records, roadmap | **Built.** `ragdoll10` and `sockets200`, the E9 block in `perf-baselines.md`, ADRs 0060 and 0065, both soak ceilings re-measured | this session |

**What step 12's absence actually looks like today**: an import from the
Explorer copies the file with its companions and creates **one** `MeshPart`
pointed at `asset://models/<file>.gltf` (`engine/app/src/engine.cpp`), which is
precisely the shape this milestone exists to replace. `mountObjects` and
`splitByPrimitive` are the two halves of the answer and neither has a caller.
`luaug_assetc_lib` is not linked into `luaug_app`, and `assetc = 7` was never
added to `tools/repo/checklayers.luau`.

Two things landed that the plan did not ask for, and both are keeping. **One
`AnimationPlayer` parented to a `Model` drives every skinned `MeshPart` under
it, matched by joint NAME** (`ce2e139d`) — a character is a body, a shirt and a
pair of trousers wearing one skeleton, and before this it took one player and
one copy of the clip per garment. And the editor's whole material workflow: New
Material, New Stamp, Duplicate, a preview sphere, and a picker that offers the
project rather than only the world (`8b2611c5`, `ce19e42d`, `06af38cc`,
`301c8d7c`, `e9f42c74`, `e1144693`).

## What replaced what: a material became an instance

**This is the reversal, and no decision record states it.** Step 3 shipped a
material as a bespoke asset: `.material.json` with its own reader, its own
writer, its own `ContentKind`, its own library keyed by URN, and its own
create-a-file verb in the browser. All of it was a second answer to questions
[ADR 0049](../decisions/0049-a-stamp-is-a-source-and-an-instance-carries-its-mark.md)
and
[ADR 0051](../decisions/0051-a-prefab-is-inherited-and-an-edit-is-an-override.md)
had already answered: one file, many instances, edit the file and every instance
follows, override one and only that one differs, see it in the Explorer, select
it, undo it.

So a `Material` is an instance (`8ea7eee7`), `BasePart.Material` is an instance
reference rather than a path, and what a project keeps in `content/` is a
**stamp** of one. What that deleted: `asset/material.{h,cpp}` and its tests,
`ContentKind::Material` and its six switch arms, and the browser's write-a-file
verb. What it kept and did not have to build: linking, overrides,
refresh-on-save, the badged icon, and `Duplicate`.

**It has residue, and the residue is stated here rather than left to be found.**
`AssetKind::Material = 6` is still in `engine/asset/include/luaug/asset/pack.h`,
still documented as "a compiled material: the parameter block plus the hashes of
the textures it samples" — a format nothing writes and nothing reads. The icon
id went at `038727b9`, a day later, because a theme may legally define an id
nothing draws and no gate caught it.

This is E3's shape a fourth time, and it was right for the same reason: the
question was asked, the answer shipped, and the person who asked corrected it
once it was in their hands. What made it cheap in E3 was that each reversal had
been **written down first**. This one was not, and the cost is visible — the
`churn` determinism trace was re-recorded twice on both tiers, once for the
design and once for its replacement.

## NOT in scope

A material graph. What `engine/asset/material.h` was and what a `Material`
instance is are both **parameter blocks**, and the documentation says so out
loud, because otherwise somebody asks for nodes. A second physics backend for
constraints — every Jolt constraint class was already compiled and linked, so
the whole of Part B's solver work is an interface and a backend rather than a
dependency. `SixDOF`, which stays unexposed because `SwingTwist` expresses a
shoulder exactly and more cheaply. LODs and meshlets, which the compiled
pipeline already produces and which nothing here changes. And a variant of a
stamp, which E3 refused outright and E9 does not reopen.

## Subagent plan

None. This is one flow across six modules' seams, which `MASTER_PROMPT.md` §7
names as orchestrator-only work: the interfaces cannot be frozen before the
thing is built, because the milestone's content **is** the interfaces —
`SkeletonHost`, the constraint seam, the object mount and the split.

## Gate (definition of done)

- [x] **A model dragged in becomes a `Model` with named parts.** Not one opaque
      `MeshPart`. The parts carry the primitive names the file gave them.
      **The second clause is superseded** (ADR 0065): an old scene naming the
      whole file does NOT get an error, because the whole-model row stays. One
      piece is the whole model, so every skinned file and every static
      single-primitive file has that row as its only name -- removing it would
      break them to punish a scene that is not wrong.
- [x] **The editor's import and `assetc` produce the same bytes.**
      `import_matches_build`, per URN, hash, kind and stored size, in
      `engine/app/tests/content_import_tests.cpp`.
- [x] **A loose `.gltf` no longer feeds the runtime.** ADR 0065. There were
      THREE loose feeds, not one: `MeshLoader::sync`'s branch, `syncSkeletons`
      on the sim thread, and `syncTextures`, which had no compiled branch at all
      and so read the raw PNG beside a `.ktx2` the compiler was already
      producing. All three are done, every project compiles itself on open in
      every host mode, and exactly one golden moved -- `lavapipe/specular.png`,
      by one pixel at delta 4.
- [x] **Re-importing an unchanged tree does no work**, asserted as
      `texturesEncoded == 0 && meshesCompiled == 0` rather than as a duration.
- [x] **A parallel encode is byte-identical to a serial one.**
      `asset_determinism` gains a `--jobs=1` leg that must match.
- [x] **`Model.Scale` is absolute, about the pivot, and one undo takes it back.**
- [x] **A hinge never exceeds its limits and a swing-twist cone holds**, over
      hundreds of steps, and the four traps each have a case:
      `collideConnected=false` really excludes, `updateBody` on a constrained
      body does not dangle, `destroyBody` drops its constraints, and constraints
      retire **before** the bodies they hold.
- [x] **Two worlds built by the same call sequence are bit-identical after 300
      steps**, and `tests/determinism/ragdoll` — four ragdolls, 3000 ticks,
      checkpointed — says constraints did not break R10.
- [x] **A part welded to a bone follows the animation**, and a bone whose joint
      the rig does not have falls back to its parent part rather than to the
      world origin.
- [x] **A skeleton can be seen and a joint can be chosen from a list**, so
      `JointName` stops being free text typed from memory.
- [x] **A ragdoll can be built without hand-authoring every joint**
      (`Ragdoll:Build(profile)`), and blending back to animation says which of
      the two blends it is doing — the palette blend of a stumble, or the
      solver blend of a powered ragdoll.
- [x] **Part B costs nothing to a scene that does not use it.**
      `tests/bench/{physics1k, churn10k, instances500, crowd50, platforms200}`
      unchanged within noise; `tests/bench/{ragdoll10, sockets200}` added.
- [x] **Three conformance specs**: `physics/constraints`, `physics/ragdoll`,
      `animation/bones`. All three are in `tests/conformance/`.
- [x] **`streaming_soak`'s ceiling is re-measured.** 192 MiB to 96, against
      29 MiB on Windows and 25 on Linux. `openworld_soak` went 384 to 192, and
      that one turned up a stale record rather than a cheaper program: it
      claimed 169 MiB measured and the peak today is 47. The 169 predates ADR
      0053, so it is a number about a different program -- the ceiling is set
      above it anyway, because macOS is Tier-3 and nothing local can watch it.
- [x] **`scripts/localgate.ps1` green on every stage**, including the Linux one:
      this milestone touches six modules and Clang diagnoses what MSVC does not.
- [~] **The end to end, by hand, with the model that started it**: drag it in,
      see named parts and material files, scale it, weld a part to a bone, press
      play, ragdoll it, reopen the project and have it not recompile.
      **The only item here nothing automated can close**, because it needs a
      person, a window and that particular horse -- which is not in this
      repository and cannot be, being somebody else's file. Every leg of it has
      a gate standing in for it (`example_ragdoll_builds`, the capture gates,
      `content_import_tests`' compile-then-delete case, and the re-open costing
      0.03 s and saying nothing), and none of that is the same as looking at it.

## Findings

**Finding 1 — a refused file is not a file "imported without its materials".**
`KHR_materials_pbrSpecularGlossiness` was superseded and archived, and it is
still what a great many exported models declare — in `extensionsRequired`, which
is the half that matters: a parser that does not know an extension listed there
refuses the whole document rather than ignoring a block it does not understand.
Tolerating it and reading past it would have been worse, not better: all five of
that file's materials carry the extension and not one carries a
metallic-roughness block, so the geometry would have loaded and every texture
would have been lost. It is converted instead, by the archived extension's own
rules, and the specular colour is the stated loss.

**Finding 2 — glTF says a skinned node's transform must be ignored, and every
fixture in this repository hid it.** A skinned mesh was drawn with no palette
whenever nothing was animating it, and the mesh node's transform stood in for
the bind pose. That substitution is right only when the two happen to agree,
which they do in every file this repository authored and do not in a downloaded
one: the horse lay on its back, its export's axis swap applied twice. **The rest
palette has to be resolved from the node graph**, not from `Joint::parent` — the
joint list is not closed under parenthood, so a chain rebuilt from parents
returns half the joints as roots and loses everything above each break.

**Finding 3 — a stale binary writes the OLD hashes, and that looks exactly like
"nothing moved".** Re-recording the determinism traces for `BasePart.Material`
on Linux produced a clean, plausible, wrong result: the build had failed on a
missing `<cmath>` and the host ran the previous binary. The first recording was
thrown away. **A trace that did not move is the same picture as a trace that was
never recomputed**, so the thing to check is the build's exit code, not the
diff.

**Finding 4 — the plan said `churn` must not move, it moved, and that was
correct.** The plan's constraint on Part B was "add no field to an existing
component", which held. Part A's `BasePart.Material` is a field on
`PartComponent`, which every scene with a part in it hashes — so all four traces
moved, on both tiers, and `character` did not because it has no part the
property reaches. Verified before landing rather than after, which is what the
plan asked for and is the part worth keeping.

**Finding 5 — a recursive lock inside Jolt is not an error, an assert or a
slowdown.** `jointFrame` reads a transform through the locking body interface,
so computing one inside the write locks locked a body this thread already held.
The process stopped at zero CPU with no output. Jolt's own assertion had said so
fifty-six times, and nobody read past the `i18n missing` prefix to the message
underneath it.

**Finding 6 — three constraint tests passed against defects, and fixing the
tests was the finding.** A central impulse makes no torque, so nothing at that
seam can turn a joint except gravity — and gravity turns a horizontal hinge, not
a vertical one. And two boxes joined at their touching faces are in *contact*:
the contact solver holds the door rigid, the joint never moves, and the limit is
never reached. A test that cannot reach the thing it names is worse than no
test, because it reports coverage.

**Finding 7 — one of the two repairs the plan predicted was real and one was
not.** The real one: a mesh whose tracks stop contributing had its pose erased,
which snaps every unsimulated joint out of the animation on the exact frame a
character goes limp — a hand that springs open as the body drops. The first fix
merely stopped erasing and then fell through and rebuilt from zero-weight
accumulators, which *is* the rest pose; it had to leave the pose untouched. The
one that was not real: collecting override-only meshes in `sample` changed no
observable behaviour, because `commitOverrides` builds a pose from rest when it
finds none. **It is absent, and the comment says why** rather than leaving the
next reader to wonder whether it was forgotten.

**Finding 8 — a `Bone` resolves against the nearest skinned `MeshPart` above it,
not against the part it sits on.** A ragdoll's limb is a plain `Part` with no
rig, and the first rule asked it about a joint anyway — which worked only
because the real host is defensive enough to refuse, and that is not a contract
worth having. A bone on a character *is* the joint and moves with the animation;
a bone on a limb is a label saying which joint the limb stands for, and
following the joint would make it chase the pose it is itself about to write.
The test found it: a permissive fake put the limb two metres from where the
simulation had left it.

**Finding 9 — a feature can be complete, tested, and inert.** The material
picker, the Explorer drag and the content drag all landed and were all tested,
and a person still could not assign a material (D130). `editable()` returns
false for every instance-valued property, and has since M4 — deliberately, for a
brief in which reparenting from the panel was out of scope. The panel grew a
real reference editor and that one predicate was never told, so the button came
up disabled and the drop target was never installed. **Every test reached the
command behind the widget and none reached the predicate that decides whether
the widget is live.**

**Finding 10 — a reference written from outside a stamp's root is written as
null, and `restamp` then pushes the null everywhere** (D133). A stamp is
serialised from its root down; the material was placed beside it. The save
dropped the reference, the next open showed an untextured part, and every
instance in the world inherited it. Three separate changes were needed because
any one alone still loses something, and the last of them is that `saveStamp`
reports what it dropped instead of swallowing it.

**Finding 11 — measuring the milestone's own model is what found the frame
budget defects.** 2937 KiB of glTF, 60,688 vertices, 677 joints: 21 ms to read
and 191 ms to parse, with no budget anywhere (D125), and the skeleton pass
reading every PNG beside every model in the tick to throw them away (D126).
Neither is an editor problem — a `MeshPart` a script creates mid-play takes the
same path. E9's own direction retires the residual caveat rather than fixing it:
`importGltf` reads a glTF's external `.bin` and `.png` itself, so the job pool
is unavailable to it until the importer can be handed buffers somebody else
read.

## Attempted / abandoned

**A `Ragdoll` that owns its bodies.** `JPH::Ragdoll` is compiled and linked and
was not used: its class owns the bodies it creates, ours are created by
mark-and-sweep from the tree, and two owners of one body is the physics mirror's
rule broken. `ENABLE_OBJECT_STREAM OFF` also means its settings format is not
built.

**A weld settle inside `resolveAttachment`.** Removed rather than kept, because
it changed no observable behaviour — `resolveWelds()` already runs first and the
one early path settles the owner itself. The comment says so, so it does not
read like a fix somebody deleted by accident.

**Merging a material over the block a mesh file described.** Replace, not merge:
a merge needs per-field "is set" bits on a struct whose virtue is being flat,
and "which half of this material is mine" is not a question anybody wants to
answer while looking at a wrong-coloured wall.

## Gate Record

**NOT FILLED. E9 is open**, and this is the honest state of it on 2026-08-26
rather than a close. Rows read *Green*, *Half*, *Not built*, or **Declared and
absent** — the last being verification the plan committed to that no file in
this repository provides.

| Gate item | Result |
|---|---|
| A model that was refused now loads, and one that will not says why | **Green.** `96421a5e` and `16d81571`, both break-verified: with the extension off the fixture is refused and the case says so on the import rather than on a missing texture, and `skinned_under_export` hangs the rig under a unit conversion and an axis swap, which is the shape every FBX-derived export has. |
| An object store resolves like a pack | **Green, and mounted by nobody.** Six cases in `content_tests.cpp`, including a missing object not refusing the mount (unlike a pack) and an object store outranking a directory and a pack. Nothing in `engine.cpp` mounts one, because that is step 12. |
| One file becomes the pieces a person can select | **Green as a library, dead as a feature.** Nine cases in `model_split_tests.cpp` covering piece counts, the naming order, `_2` collisions in document order, an empty model and the skinned refusal. `splitByPrimitive` has no caller outside them. |
| `Model.Scale` is absolute and about the pivot | **Green.** Thirteen cases in `tests/conformance/instance/model_scale.spec.luau`; `Size` over `MeshSize` asserted on the renderer side (`render_world_tests.cpp`) and on the physics side (`physics_sync_tests.cpp`, two cases including the identity skip). |
| Constraints hold, and the four traps have cases | **Green.** Fourteen cases in `engine/physics/tests/constraint_tests.cpp`. Three of the four traps were confirmed by deleting the fix; deleting the first two also crashed the run at case eight with thirty-nine skipped, which is what a dangling joint does. The fourth found itself, as a deadlock at zero CPU. |
| Constraints reach the world in pool order and retire before bodies | **Green.** Eleven cases in `physics_sync_tests.cpp`, including the recorded call order, a limit change being an update rather than a rebuild, a disabled joint not being destroyed, and a constraint refusing a `CharacterBody` by name. |
| A skeleton is readable from below the renderer | **Green.** Nine cases in `animation_tests.cpp`: a joint answers in bind pose with nothing playing, an override moves the joints below it, an override survives a tick in which no clip contributes, an override on a joint the rig lacks is refused, and the pose keeps the model transforms it used to throw away. |
| A sword welded to a bone follows the hand | **Green.** Six cases in `physics_sync_tests.cpp`, including a sword welded to the hand of a character welded to a moving platform settling in the tick the platform moved — break-verified by deleting the anchor pull-forward — plus the one that asserts the pose is committed once per tick, after everything that could still move a joint. |
| A ragdoll writes the pose from the simulation | **Green for the mechanism.** Five cases: disabled drives nothing, an enabled one writes in the mesh's own space, an unresolved joint drives nothing, a ragdoll drives only the bones under it, and the drive runs before the commit. |
| Every map is encoded as what it is | **Green.** `texture_tests.cpp`, break-verified by construction: the same pixels encode to *different* bytes as colour and as data, so a flag reaching nothing would have come back equal. Committed pack bytes do not move — nothing regenerates them until step 14. |
| Re-importing an unchanged tree does no work | **Green, asserted rather than timed.** Five cases in `tools/assetc/tests/compiler_tests.cpp`: a second build compiles nothing and produces the *same* pack, changing one file recompiles one file, other options are a miss, a truncated cache is a miss rather than a crash, and no cache root is the behaviour the tool always had. |
| A material survives a round trip through a scene | **Green.** One end-to-end case (`f32b8149`): make a material as a stamp, point a part at it by dropping the file, save, reopen in a fresh world — three serialisers agreeing about one instance. |
| A model dragged in becomes a `Model` with named parts | **Not built.** An import creates one `MeshPart` pointed at the loose file. |
| A loose `.gltf` no longer feeds the runtime | **Not built.** `mesh_loader.cpp` and `WorldHost::syncSkeletons` both still call `importGltf` on the source file. |
| A skeleton can be seen; a joint can be chosen from a list | **Not built.** Nothing in `engine/app` draws a skeleton or offers `JointName` as anything but text. |
| A ragdoll can be built without hand-authoring every joint | **Not built.** `Ragdoll` carries `Enabled` and nothing else: no `Build(profile)`, no `Blend`. The solver-side blend exists at the seam — `MotorMode::Position` reaches Jolt — and no scene class exposes it. |
| A parallel encode is byte-identical to a serial one | **Declared and absent.** No parallel encode in `assetc`, and `asset_determinism` has no `--jobs=1` leg. |
| `import_matches_build` | **Declared and absent.** There is no `assetc::importOne` for it to compare against, and `luaug_assetc_lib` is not linked into `luaug_app`. |
| `tests/determinism/ragdoll` | **Declared and absent.** The replay driver scans the folder; the folder was never written. |
| `tests/bench/ragdoll10`, `tests/bench/sockets200` | **Declared and absent.** Same mechanism, same absence. Part B's cost against the five existing benches has not been measured either. |
| `tests/conformance/physics/constraints`, `physics/ragdoll`, `animation/bones` | **Declared and absent.** All three. The only Luau-level coverage E9 has is `instance/model_scale.spec.luau`. |
| `streaming_soak`'s ceiling re-measured | **Declared and absent.** Still `CEILING_MB=192`, and nothing has yet made editor content cheaper for it to measure. |
| `scripts/localgate.ps1` green on every stage | **Held throughout the built steps**, each of which landed behind a green six-stage run. Not a close: the milestone is open. |
| The end to end, by hand, with the model that started it | **Not reachable.** Its first four steps need step 12; its last two need step 14. |

**What closing E9 costs, in the campaign's own terms**: S4.1 through S4.7 in
[`finish-line.md`](../finish-line.md) are exactly the *Not built* and *Declared
and absent* rows above, and S4.1 — the split-piece URN form — blocks the two
under it.
