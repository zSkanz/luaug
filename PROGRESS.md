# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **M6 — Playing the World — IN PROGRESS.** The brief is
  [`docs/briefs/m6-kickoff.md`](docs/briefs/m6-kickoff.md), with its fifteen
  decisions and its Findings. Three of the five systems are in: the **Input
  Action System** (ADR 0039), **TweenService**, and the **UI layout** (ADR 0040).
  Audio and skeletal animation are not started; `examples/04-obby` is not
  started; no gate item is recorded yet.
- **M5 — Feeling the World: Jolt Physics + Character — COMPLETE, signed off
  2026-08-20**, tagged `milestone/m5`. Signed after a review round that found
  something: two `CharacterBody` were reported as passing through each other,
  which did not reproduce — and the investigation found the real defect
  underneath, a character that ignored `CollisionGroup` (D025). The Gate Record
  is in [`docs/briefs/m5-kickoff.md`](docs/briefs/m5-kickoff.md), with seventeen
  Findings and the section written for a reviewer.
- **M4.5 — Correcting the World — COMPLETE, signed off 2026-08-20**, tagged
  `milestone/m4.5`. **M4 — Seeing the World — signed off 2026-08-20**
  (`milestone/m4`), its five gate items green against re-recorded artifacts.
  **M3** (`milestone/m3`), **M2** (`milestone/m2`), **M1**, **M0** — all signed
  off.
- **CI is green on `main`**, run 32429107275, all three tiers — including the
  first macOS build of Jolt. Two red runs preceded the first green one and both
  found something a local tier cannot see (D023, D024).

### M6 so far: what a game can do that it could not

- **Input is one model, and the scaffold is gone.** `InputContext` →
  `InputAction` → `InputBinding`, resolved in priority order with per-input
  sinking, on a clock the context declares (ADR 0039). `KeyboardService` is
  deleted from the IDL, the defs, the api-dump and the binary -- which is what
  M5's `DevOnly` tag was for. The determinism scene migrated and `inputs.txt`
  did not change a line.
- **`Enum.KeyCode` is ninety-four items** spanning keyboard, mouse and the
  standard gamepad, and its item names ARE `platform`'s device-layer names --
  asserted against `platform::Key::Count` at compile time, so a key added to one
  list and not the other is a build failure rather than every gamepad code
  shifting by one.
- **Tweens write through the setter scripts write through**, and step on the
  SimClock. `TweenService:GetValue` is checked against 297 numbers computed from
  the published easing formulas by an implementation written separately from the
  engine's.
- **The UI lays out**, at any resolution, with a dirty flag that makes an idle
  frame run no solver at all. It does not draw yet: there is no ui2d pass.
- **D021 is fixed**: a range refusal names its range.
- **Four value types** -- `Vector2`, `UDim`, `UDim2`, `Rect` -- with the
  attribute domain §2.2 widened by them, and a world-hash case that requires
  every `Value` alternative to hash its payload rather than its tag.

### M5: what the world can do that it could not

- **A `BasePart` that is not `Anchored` is a Jolt body.** Gravity, contacts,
  impulses, friction, restitution, density, collision groups, and `Touched` /
  `TouchEnded` as deferred signals. `Workspace.Gravity` is real and signed.
- **The mirror lives in `scene`**, where architecture.md §2 always said it
  would: "physics sync via an injected `IPhysics3D*`". The tree is the authority
  and the body mirrors it; `scene` never learns a body's identity beyond an
  opaque handle and `physics` never learns what an Instance is.
- **`CharacterBody` walks**, on a Jolt `CharacterVirtual` rather than a rigid
  body — it climbs steps under `AutoStepHeight`, is stopped by anything above
  it, jumps only when grounded, and reports what it landed on.
- **A script can ask the world what is there**: `Workspace:Raycast`,
  `:Spherecast`, `:GetBodiesInBox`, over `RaycastParams` and `RaycastResult`.
- **Two `CharacterBody` block each other, and `CollisionGroup` decides it.**
  Raised in review as "they pass through"; they do not, because each character
  carries a rigid body inside its capsule. What was real is that the character
  was the one thing in the world outside the collidability matrix — D025, fixed,
  with the decision (block, never push) written into the class doc.
- **`Weld` and `WeldConstraint`** (added to M5 by human decision on 2026-08-20):
  a transform weld, resolved in dependency order after the step, with cycles
  refused at the write that would create one.
- **The determinism gate is blocking and replays INPUT.** A scenario carries a
  recorded `inputs.txt` and the keyboard snapshot comes from it, so what is
  replayed is a keystroke's whole path to the character rather than a bot
  calling `Move`. The physics scenario is `sameBuildOnly` — run three times,
  compared against itself, with no committed trace — because ADR 0025's
  guarantee is same-BUILD and CI's compiler is not this machine's (D024).
- **`WorldHash` covers physics state**, including the four things no script can
  read: a queued impulse, a character's command, its vertical velocity, and
  which bodies the solver has put to sleep.
- **The C++ formatting gate exists**, five milestones after architecture.md §9
  first listed it, at a pinned clang-format 18 — and it now sees files that are
  not yet staged, which it did not when it was turned on.
- **D016 is fixed**: a `BindToClose` handler that yields is waited for, up to a
  capped grace period.

### M5: what does NOT exist yet

The brief's fifteen NOT-in-scope items, narrowed by one when the weld came into
scope. The ones most likely to be mistaken for bugs:

- **Every `Part` renders as a wireframe box** — D022, scheduled with M7.5. Only
  a `MeshPart` reaches the solid renderer, and that has been true since M2. The
  boxes are where the bodies are.
- **`BasePart.Material` is not shipped**, and neither is `RaycastResult.Material`.
- **`Enum.CollisionFidelity` round-trips and every value collides as a box**;
  a hull needs mesh geometry the mirror cannot see until M7.
- No joints or solver constraints beyond the transform weld; no sleeping policy
  exposed; no `saveState`/`restoreState` (declared, refuses); Jolt runs
  single-threaded until M7 wires the job system.

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
  half, and the crash handler is in place for the next occurrence — the next
  report should carry `luaug-crash-<pid>.dmp` and `luaug.log` from beside
  whatever was being run.
- **D017 — the `DebugShell` has no memory-category table and no log/REPL pane**,
  both named by `architecture.md` §app. Scheduled with M6.
- **D018 — `luaug_net_tests` hung once** on Windows and passed on a re-run. §12
  quarantines on the second occurrence; this is the entry that makes a second
  one countable.
- **The build agreeing is not evidence that the build read your file.** A
  break-verification restored with `Copy-Item` kept the source's old timestamp,
  Ninja rebuilt nothing, and a passing fix was reported as failing against code
  that was no longer on disk. Restore with `cp` and `touch`.
- **D021 — a range refusal reports the key for a type.** `FixedTimestep = 1/10`
  raises "it takes a number" about a number. Every M5 property with a range is
  affected; the fix is a per-property error-key override in the IDL.
- **D022 — a `Part` never reaches the solid renderer.** Scheduled with M7.5.
- **M6's next step, written out so it is not re-derived**: the ui2d render pass.
  `engine/ui` produces a `DrawList` of quads and scissors and nothing consumes
  it — `render` needs a 2D pipeline, a shader pair, and a call from
  `renderer_default` after the world and before ImGui. The UI goldens at two
  resolutions are blocked on it and so is the obby. After that, in the brief's
  order: `engine/audio`, skeletal animation, D017, `examples/04-obby`, gates.
- **Two of M6's decisions went against a document, and both are ADRs.**
  ADR 0039 (an `InputContext` declares its dispatch rate; the IAS's enums are
  total) and ADR 0040 (a UDim2 placement is arithmetic, so v1 does not call
  Clay). The second is the one a reviewer should read: it is the milestone
  declining to use a dependency the same milestone vendored, and it does not
  remove it, because that is not the agent's call.
- **The renderer submits one draw call per visible object, and that is the
  engine's real ceiling for a crowd.** Measured 2026-08-20 against a
  survivors-like horde: two thousand enemies run at 11.1 ms a frame, of which
  1.8 ms is the whole simulation and 6.9 ms is submitting 2,092 forward draws —
  and the same scene costs the same 10.2 ms at 320×180 as at 4K, for twelve
  thousand triangles. Instanced draws are now named M7.5 scope with that number
  attached; the table is in `docs/perf-baselines.md`.
- **The physics mirror costs ~160 ns per body per tick to decide nothing
  changed.** Recorded in `docs/perf-baselines.md`. The remaining fix is a
  dirty-flag design, and it belongs with M7's streaming rather than with M6.
- **A check on a moving thing names a window, not a moment.** Three M5 test
  cases in a row failed by measuring after the thing they were testing: a
  character crosses a six-metre ledge in a second and a half, so a check taken at
  the end reads "never climbed" and means "climbed and kept going".
- **A picture of two things at once catches what neither test can.** The Jolt
  debug-draw bridge found, on the frame it first drew, that `CharacterBody` was
  the one `BasePart` whose `Position` meant its feet. Every test passed.
- **Physics arriving changed what every scene already in the repository meant.**
  An unanchored part is a rigid body, and every example, fixture and benchmark
  predates that. The proof the change is inert where it should be is that
  `capture_gate_meshes` passes against the unchanged M4.5 golden.
- **A gate can be stronger than the guarantee it rests on, and get away with it
  until the day it cannot.** Committing a determinism trace and checking it on CI
  is a cross-BUILD check; ADR 0025 promises same-build. Four milestones of
  integer and tree state paid nothing for the gap, and the first floating-point
  scenario spent it — on CI, at tick 600 of 3,600, with nothing about the gate
  having changed.
- **A gate that can pass while doing nothing keeps being built by accident.**
  Twelve instances in six milestones. M5's was a conformance run reporting "938
  passed, 0 failed" over a suite that had silently lost seventeen cases to a
  syntax error.
- **A golden cannot detect a defect that was present when it was recorded.** It
  asserts stability, not correctness, and it *feels* like the other one. The
  shape that catches this is a differential.
- **Test the step that resolves, not the step that computes.** M5 acted on this:
  the host test asserts that a part falls in a world no script touched, because a
  mirror that was never handed a `Workspace` produces a world where nothing does
  and every API-level test still passes.
- **The Linux tier is the only thing between this repository and a whole family
  of defects** MSVC does not mention — M5 added an ABI one to the family: Jolt
  compiled `-fno-rtti` emits no typeinfo, so a class deriving from one of its
  types fails to link on Clang and links fine on MSVC.
- Carried forward, none blocking:
  - **Two of the five generated artifacts api-design.md §5 lists do not exist**:
    the typed `@std`/`@luaug` stubs and `docs/reference/**`.
  - **The shipping profile does not configure**, and needs a bytecode-loading
    path that does not exist. Scheduled with `luaug build` at M8.
  - **DXIL produced on Linux is never verified as signed.** M8.
  - **The message catalog does not load inside the APK.** `Catalog::loadFromFile`
    uses `std::filesystem`; `loadFromJson` plus `platform::readTextFile` is the
    fix.

## Blocked — needs human

- **Which font does the engine ship?** M6 draws every label in `stb_easy_font`,
  the vector face already vendored inside `third_party/stb`: ASCII, one weight,
  no kerning. ADR 0011 names stb_truetype, and stb_truetype needs a TrueType
  FILE — an asset with a licence, which is an R6 decision rather than an agent's.
  `TextLabel.Font` is marked `Inert` with M7 named, so the gap is declared rather
  than discovered. The question is which permissively-licensed face to vendor
  (DejaVu, Inter, Noto Sans and Liberation are all candidates), and it can wait
  until M7's asset pipeline — the UI works without it.
- **Clay is vendored and v1 does not call it.** ADR 0040 explains why: a `UDim2`
  placement is arithmetic rather than a constraint, and Clay cannot express an
  unclamped scale or a fractional anchor point. The row and the pin are
  untouched, because removing a dependency is a human decision (R5, §10). Two
  answers are reasonable — drop the row, or keep it for a UI feature that is
  genuinely flow-shaped, which `UIGridLayout` would be.

- **The Android run of `examples/02-meshes` is due.** Deferred by the human
  until M4.5 closed, and M4.5 and M5 have both closed since. It is a device
  checkpoint rather than milestone work, and it is here so that it is asked for
  rather than forgotten.
- **A judgement I made that a reviewer may want to remake.** `churn10k` reads
  4.96 ms/tick where M2 recorded 2.02, and the Gate Record calls that a changed
  measurement rather than a regression: the benchmark's scene now holds ten
  thousand rigid bodies where it held none. It is under its 16 ms budget either
  way and the physics half is itemised in the baselines. If the answer is
  "that is a regression", §8 wants an ADR and the work is the mirror's
  dirty-flag design.
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

- **2026-08-20 (session 10, Claude Opus): M5 built, awaiting sign-off.** Ran the
  §2 boot sequence green, spent the ledger's named first action on the
  clang-format gate over a quiet tree (205 of 205 files moved, in a commit that
  did nothing else), then wrote `docs/briefs/m5-kickoff.md` and worked its build
  order: Jolt vendored at the tag the manifest has named since planning, the
  `physics_api` seam and a Jolt behind it, the Instance↔body mirror in `scene`
  where architecture.md always put it, `CharacterBody` on a `CharacterVirtual`,
  the query family, `Weld`/`WeldConstraint` (added to the milestone by the human
  mid-session, and appended to the brief rather than assumed to have been read),
  the keyboard scaffold and the recorded input stream the determinism gate
  replays, the Jolt debug-draw bridge, `examples/03-physics-playground`, and
  D016.
  Learned, and the one to keep: **physics arriving changed what every scene
  already in the repository meant.** An unanchored `BasePart` is a rigid body, so
  on the first green build the mesh example rained its own scenery and a
  property-churn benchmark quietly became a ten-thousand-body physics benchmark
  under a name that says otherwise. The fix is one line per scene; the discipline
  is that `capture_gate_meshes` then passes against the UNCHANGED M4.5 golden,
  which is what says the change is inert rather than re-recorded.
  Also learned: **a picture of two things at once catches what neither test
  can** — the debug-draw bridge, on the frame it first drew, showed the
  character's collider floating half a body above the character's own box, with
  every test passing. **A check on a moving thing names a window, not a moment**:
  three cases in a row failed by measuring after the thing they tested.
  **A sleeping contact is not an ended contact.** **A gate that can pass while
  doing nothing keeps being built by accident** — the twelfth, and the first
  caught in the session that wrote it: a conformance run reported 938 passed over
  a suite that had just lost seventeen cases to a syntax error. And **the Linux
  tier found an ABI defect, not just a warning**: Jolt compiled `-fno-rtti` emits
  no typeinfo, which MSVC hides by emitting RTTI per translation unit.
  **Signed off by the human 2026-08-20** ("tá aprovado"), after a review round
  that found something and after the answer changed what the milestone shipped:
  two `CharacterBody` were reported as passing through each other, on a reading
  of Jolt correct in every particular. It did not reproduce -- this backend gives
  every character an inner rigid body, and that is what the other one's sweep
  finds -- and the investigation found the real defect underneath, a character
  whose sweep was handed a filter that accepts every layer, so `CharacterBody`
  was the one thing in the world outside the collidability matrix (D025).
  The general shape, which is the one to keep: **a report whose reasoning is
  sound can still have the wrong conclusion, and the way to find out is to run
  it rather than to argue with it.** Two probes and a break-verification cost
  twenty minutes and turned a wrong fix into a right one. The near-miss is that
  registering every character in `CharacterVsCharacterCollisionSimple`, which is
  what was asked for and would have "worked", would have reintroduced the same
  defect by a different door: its list has no filter at all.
  Also learned: **the build agreeing is not evidence that the build read your
  file** -- a restore with `Copy-Item` kept the old timestamp, Ninja rebuilt
  nothing, and a working fix reported itself as broken. And **the bench harness
  was the thirteenth gate found able to pass while doing nothing**, caught by
  writing a scene that failed its own assertions and watching it report a
  respectable number anyway.
  Closed with one thing measured that nobody asked for as work: **the renderer
  submits one draw call per visible object**, which is the engine's real ceiling
  for a crowd -- two thousand enemies at 11.1 ms, of which 1.8 ms is the entire
  simulation, and the same frame costs the same at 320x180 as at 4K. Instanced
  draws are now named M7.5 scope with that number attached.
  Next: **open M6 in a new session** (§6). Its first action is in Now/Next.

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
  **Signed off by the human the same day** ("aprovado para finalizar"), which is
  what closed it; tagged `milestone/m4.5`, and `milestone/m4` stands with it.
  Next: **open M5 in a new session** (§6) and make its first act the clang-format
  gate, on a quiet tree.

<!-- Format for future entries:
- **YYYY-MM-DD (session N):** did X; learned Y; Next: <literal first action>.
-->
