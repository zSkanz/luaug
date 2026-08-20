# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **M4.5 — Correcting the World: the Environment the Renderer Never Read —
  AWAITING HUMAN REVIEW** since 2026-08-20. The gate is green and that is
  evidence, not a decision: the roadmap and MASTER_PROMPT §6 both say this
  milestone may be marked complete only by explicit human approval. **No
  `milestone/m4.5` tag exists and none will before the human says so in words.**
  Brief, with the Gate Record, the §app audit and seventeen Findings:
  [`docs/briefs/m4.5-kickoff.md`](docs/briefs/m4.5-kickoff.md).
- **M4 — Seeing the World — NOT complete**, tagged `milestone/m4` on 2026-08-20
  over the defect M4.5 exists to fix. Whether that tag stands is the human's
  call. Its build order, module work, RHI freeze and Android checkpoint all
  stand; what did not was the claim that what it drew was what its scene
  described. Every image it recorded has now been re-recorded.
- **M3 — Tooling Loop — signed off 2026-08-20**, tagged `milestone/m3`;
  **M2 — Kernel — signed off 2026-08-20** (`milestone/m2`); **M1** signed off
  2026-08-19 (`milestone/m1`); **M0** signed off 2026-08-19 (`milestone/m0`).
- **CI is green on `main`.** macOS is blocking on every code push.

### M4.5: what changed

- **`Lighting` exists from boot**, beside `Workspace` and `ScriptService`. It was
  created by its first `GetService`, which is after `WorldHost::start` cached its
  id, so `extract` answered every frame with `RenderEnvironment`'s defaults and
  no scene's environment ever reached a pixel. api-design.md §1.2 corrected in
  the same commit.
- **Four checks now catch that**, and all four were verified by reintroducing it:
  the host resolves the service on a world no script touched; the snapshot's
  environment equals what the world holds, field by field; `determinism` moves;
  and `clock_differential` renders one scene at two clock times and requires the
  frames to differ. The last is deliberately not a golden — a golden compares a
  recording against a recording, which is what certified this defect six times.
- **The capture backend records uniform CONTENTS**, by a digest of the block's
  floats quantized onto the same four-decimal grid the rest of the stream uses.
  It recorded the byte count before, so the blocking render gate could see the
  shape of a frame and nothing in it. Byte-identical across MSVC and Clang.
- **`Transparency` fades**, through a sorted back-to-front blended pass:
  `GpuObjectUniforms` carries the per-draw alpha, the order is `extract`'s, and
  no RHI call was added — ADR 0037's freeze is untouched.
- **The shadow grid moves in whole texels**, so an orbiting camera no longer
  slides it 0.42 of a texel per frame. The rotational half is named and out of
  scope.
- **`PVInstance` is a real class** carrying `PivotOffset`, `GetPivot` and
  `PivotTo` for `BasePart`, `Model` and `Camera`. Without the offset,
  `Model:PivotTo(cf)` was `PrimaryPart.CFrame = cf` — the deprecated call under
  the new name, passing its tests and unable to hinge a door.
- **A crash handler and a log file**, four milestones after `architecture.md`
  §app promised them. The handler is tested by a process that faults on purpose.
- **`Property.Inert`** in the IDL and "(stored)" in the inspector, so a property
  that is backed and unread says so.
- **`docs/defects.md`**, append-only and gate-enforced, so a milestone-close
  rewrite cannot quietly empty the open list again.
- **Every M4 artifact re-recorded**: both capture goldens, the lavapipe
  screenshot, the determinism traces on both tiers, and the 1080p baseline —
  whose median did not move and whose worst frame improved by a factor of four
  on two cores.

### M4.5: what does NOT exist yet

The brief's fifteen NOT-in-scope items. The ones most likely to be mistaken for
bugs: no order-independent transparency (two transparent surfaces that intersect
sort per draw, not per pixel); a half-transparent part still casts a full
shadow; the shadow flicker caused by a *rotating* sun is unfixed and visible only
while `ClockTime` moves; still no IBL, one shadow cascade, eight lights per draw
and no `Sky`.

## Now / Next

- **Every open defect is in [`docs/defects.md`](docs/defects.md)**, which is
  append-only and checked by the docs gate for gaps, states and dangling
  citations. That file exists because three human-reported defects were removed
  from this one while it was being rewritten to close M4. **A close rewrites this
  file wholesale; it can no longer take the open list with it.**
- **D004 — the inspector crash while dragging `Size`/`CFrame` — is still open and
  still not reproduced.** Two halves are ruled out: the write path driven through
  zero, negative, 1e30 and infinity with a render extraction every frame, and 25
  minimize/restore cycles over 900 windowed frames. What remains is the ImGui
  half, and the crash handler is now in place for the next occurrence — the next
  report should carry `luaug-crash-<pid>.dmp` and `luaug.log` from beside
  whatever was being run.
- **D016 — `BindToClose` has no capped grace period.** A close handler that
  yields is cut off at the next drain rather than waited for. Scheduled with M5,
  where shutdown ordering has physics state to care about.
- **D017 — the `DebugShell` has no memory-category table and no log/REPL pane**,
  both named by `architecture.md` §app. Scheduled with M6.
- **D018 — `luaug_net_tests` hung once** on Windows and passed on a re-run. §12
  quarantines on the second occurrence; this is the entry that makes a second
  one countable.
- **M5 does not open until a human approves M4.5.** Not when the gate is green —
  when the human says so. And not in this session either way (§6).
- **When M5 opens, its first act is the clang-format gate**, on a quiet tree.
- **When M5 wires physics: `PivotOffset` is not a centre of mass.** Jolt has its
  own notion of where a body turns about, and joining them would make hinging a
  door change how it falls. Said in `components.h` where the field is.
- **A golden cannot detect a defect that was present when it was recorded.** It
  asserts stability, not correctness, and it *feels* like the other one. The
  shape that catches this is a differential: change one input the output must
  depend on, and require the output to change.
- **Test the step that resolves, not the step that computes.** M4's environment
  assertions all passed against a `Lighting` id the test made itself, so the one
  broken step was the one nothing covered — because covering it needs a host and
  constructing a component does not.
- **A plausible default hides a defect for a milestone.** An unresolved
  `Lighting` looked like a lit scene rather than a black one, so nothing ever
  asked.
- **The tail carries what the median cannot see.** Re-measuring at 1080p moved
  the median not at all and the worst frame by a factor of four on two cores.
- **An audit scoped to one module is not an audit.** The sweep that "found"
  `Model.PrimaryPart` unread searched `engine/render` for a value consumed in
  `engine/script`.
- **A build directory is evidence of what was built once, never of what would be
  built now** — and a failed link is worse: LNK1168 moves the executable's
  timestamp before failing, so Ninja considers it current and CTest runs the old
  binary.
- **A gate that can pass while doing nothing keeps being built by accident.** Ten
  instances in six milestones, and the tenth certified a whole milestone.
- **The Linux tier is the only thing between this repository and a whole family
  of defects** MSVC does not mention.
- **`fs.watch` cannot be trusted on either platform, and Linux is the worse
  one** — a change below the watched directory produces no event at all there.
- Carried forward, none blocking:
  - **Two of the five generated artifacts api-design.md §5 lists do not exist**:
    the typed `@std`/`@luaug` stubs and `docs/reference/**`.
  - **The shipping profile does not configure**, and needs a bytecode-loading
    path that does not exist. Scheduled with `luaug build` at M8.
  - **DXIL produced on Linux is never verified as signed.** M8.
  - **The message catalog does not load inside the APK.** `Catalog::loadFromFile`
    uses `std::filesystem`; `loadFromJson` plus `platform::readTextFile` is the
    fix.
  - **`architecture.md` §9 lists a clang-format gate that does not exist** —
    scheduled at the start of M5, above.

## Blocked — needs human

- **M4.5 sign-off.** The gate is green on both tiers, the Gate Record is in the
  brief, and the three additions the roadmap asked for were each verified by
  reintroducing the defect they exist to catch. **Nothing here is marked
  complete and nothing is tagged**, per the roadmap's own instruction and
  MASTER_PROMPT §6. The deliverable to look at is
  [`docs/images/daystrip.png`](docs/images/daystrip.png): one day, fixed camera,
  the sun crossing and the pane fading.
- **M4 sign-off, and whether `milestone/m4` stands.** Its five gate items are now
  green against re-recorded artifacts rather than against the defective ones.

## Decisions pending ADR

- (none — ADR 0035 was written at M3 kickoff)

## Session Log

Entries for the planning session and for M0, M1, M2 and M3 are in
[`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md), moved
there when this file passed its ~300-line cap.

- **2026-08-20 (session 9, Claude Opus): M4.5, awaiting sign-off.** Ran the §2
  boot sequence, found the repo green, wrote `docs/briefs/m4.5-kickoff.md`, and
  worked its build order. `Lighting` became a boot service; the capture backend
  learned to record what a uniform block CONTAINS rather than how big it is;
  `clock_differential` renders one scene at two clock times and requires the
  frames to differ; the shadow grid moves in whole texels; `Transparency` fades
  through a sorted blended pass; `PVInstance` arrived with a pivot that can
  hinge; the crash handler and log file `architecture.md` §app promised at M0
  exist and are tested by a process that faults on purpose; `Property.Inert`
  makes a stored-and-unread property say so; `docs/defects.md` is append-only and
  gate-enforced. Every M4 artifact re-recorded. The human amended the scope twice
  mid-session (the `PrimaryPart` retraction, then `PVInstance`); both are in the
  brief, the second appended with the note that it arrived after the brief was
  written.
  Learned, and the one to keep: **the blocking render gate recorded the SIZE of
  every uniform block and never its contents.** Every matrix, light and material
  colour a frame carries goes through that one call, so six goldens across three
  camera angles and two lighting states were green for a whole milestone while
  the sun stood still. Behind it sits the more general thing: **a golden asserts
  stability, not correctness, and it feels like the other one.** Re-record it
  against a defect and it certifies the defect. What catches that is a
  differential -- change one input the output must depend on, require the output
  to change -- and it costs one CTest.
  Also learned: **the test was at the level that was easy to write.** M4's
  environment assertions all passed against a `Lighting` id the test built
  itself, so the resolving step -- the broken one -- was the only step nothing
  covered. **A plausible default hides a defect for a milestone**: an unresolved
  `Lighting` renders a lit scene rather than a black one. **The tail carries what
  the median cannot see** -- re-measuring moved the median not at all and the
  worst frame by a factor of four on two cores. **The debug path had been drawing
  in the wrong space since M4**, found by looking at a screenshot rather than by
  any test, because the boxes were present and a test that counts them passes.
  And **an audit scoped to one module is not an audit**: the sweep that "found"
  `Model.PrimaryPart` unread searched `engine/render` for a value consumed in
  `engine/script`.
  Next: **stop for M4.5 human review** (§6, and the roadmap says it again for
  this milestone). Do not tag, do not write COMPLETE, do not open M5.

- **2026-08-20 (session 8, Claude Opus): M4 complete, awaiting sign-off.** The
  renderer, from a content URN to a lit pixel: `engine/asset` with a glTF
  importer, `MeshCache` and `MeshLoader`, `RenderWorld` v2 with a camera, an
  environment, lights and pre-sorted draws, `renderer_default` behind the
  `IRenderer` contract, five new classes with real backing, the `DebugShell`,
  the triangle sample and its Android package, and three carried debts paid.
  Five subagents did the glTF importer, the shader set, the DebugShell, the
  Android packaging and the multi-module generator; the seams, the integration
  and every gate run stayed here (§7).
  Learned, and the one worth keeping above all: **the first screenshot of the
  deliverable was a black floor, and it was my scene rather than the shader.**
  The ground had a metallic top face, this renderer has no IBL, and a metal has
  no diffuse lobe -- so the physically correct image is black. Every test in the
  milestone passed while it rendered. Nothing except §8's observation rule would
  have caught it, and behind it was a real defect: ambient reaching only the
  diffuse lobe renders every metal in every scene black.
  Also learned, in descending order of how much they cost: **a failed link
  leaves a stale executable Ninja considers current**, so the gate measures
  yesterday's binary -- four cycles and one wrong measurement, and the
  verified-fresh answer was 0 forward draws where the stale one said 64. **The
  M4 capture golden could not fail when first recorded**, because one MeshPart
  culled against one bound gives the same draw count either way; reading its
  per-frame counts then found that the shadow pass was culled against the camera
  frustum, so everything behind the camera lost its shadow. **I broke `main`**
  by committing a file two workstreams were editing, capturing an include
  without the file it names -- a green working tree is not a green commit.
  **Installing lavapipe turned skips into failures** and exposed that
  `-LE gpu-golden`, written into the CMakeLists as what must happen to pixel
  goldens, had never been passed on the Linux tier; the absence of a device had
  been doing that job by accident for three milestones. And **six Clang-only
  build failures**, all in the same two warning families.
  Next: **stop for M4 human review** (§6). Do not start M5 this session.

- **2026-08-20 (session 7, Claude Opus): opened M4.** Ran the §2 boot sequence —
  the repo had moved two commits past the ledger and carried an uncommitted
  api-design correction from the previous session, so the repo won and that
  landed on its own first. Re-ran the M3 gate green on both tiers (903
  conformance, 3 hot-reload, reload 0.3 ms against 500 ms). Deep-read
  architecture §2, §3, §7 and §9, api-design §2.1–§2.3, ADRs 0005, 0006, 0010
  and 0027, and the ecosystem report's asset section, then wrote
  `docs/briefs/m4-kickoff.md`.
  Then build-order step 1, which produced three findings before any engine code
  existed — all from reading vendored `CMakeLists.txt` files rather than
  documentation. **fastgltf has a mandatory dependency no document here
  mentions**, and with no `simdjson::simdjson` target present it downloads
  simdjson's amalgamated pair, unpinned and unhashed, into
  `third_party/fastgltf/deps/` at configure time: R5, R13, R14 and ADR 0032's
  fetch rule in one upstream default. Escalated; the human approved vendoring
  simdjson (ADR 0036), pinned at the 3.12.3 fastgltf itself targets, with a
  patch turning the download branch into a `FATAL_ERROR`.
  Which exercised **the patch mechanism R13 rests on for the first time in four
  milestones, and it reported success while doing nothing**: `git apply`
  resolves paths against the repository root even from a subdirectory, and
  *skips* — exit 0 — anything outside the current directory. Patches now apply
  from the root with `--directory=`, a `Skipped patch` is an error, and each
  patch is verified with `--check --reverse` immediately after.
  And then the patch still would not apply, which found the sharpest one:
  **no vendored tree in this repository has ever been byte-identical to its
  pinned commit.** `vendor.luau` checks out through its own git dir, where
  `.gitattributes` cannot reach it, so `core.autocrlf=true` mangled every file
  on Windows — and `third_party/** -text`, the rule written to guarantee
  byte-exactness, then committed the mangling faithfully. Every tree since M0 is
  affected. The checkout forces `core.autocrlf=false core.eol=lf` now and this
  milestone's three trees are correct; whether to re-vendor the historical ones
  is a ~20,000-file question left for the human.
  Next: implement `core::AABB` and `core::Frustum` per architecture §2's math
  list, with the plane/box tests, then specs for them in
  `engine/core/tests/math_tests.cpp`.

<!-- Format for future entries:
- **YYYY-MM-DD (session N):** did X; learned Y; Next: <literal first action>.
-->
