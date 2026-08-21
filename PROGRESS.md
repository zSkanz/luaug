# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **M6 — Playing the World — BUILT, awaiting human sign-off.** The brief is
  [`docs/briefs/m6-kickoff.md`](docs/briefs/m6-kickoff.md), with its fifteen
  decisions, its Findings and a filled Gate Record. **Every scope item and every
  gate item is done**: the five systems, solid `Part` rendering, `InputService`'s
  raw event surface (ADR 0041), the non-device input seam, `examples/04-obby`,
  D017, and the six gates. Nothing is tagged: `milestone/m6` waits for the human,
  which is §6's rule and not a formality.
  **Three ADRs**: 0039 (a context declares its dispatch rate), 0040 (a `UDim2`
  placement is arithmetic, so v1 does not call Clay — and Clay is un-vendored),
  0041 (`InputService` gains raw events, fed from the IAS's own pipeline).
  **Ten defects closed** — D017, D021, D022, D027, D029, D030, D031, D032, and
  half of D028 — of which six were found by a person playing the deliverable.
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

### M6: what a game can do that it could not

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
- **The UI lays out, draws and answers a pointer.** Two passes over each dirty
  `ScreenGui`, a 2D pipeline over the finished frame, and a hit test that fires
  `Activated` only when both ends of a press land on one element. An idle frame
  runs no solver at all, and the test asserts that as a COUNTER.
- **A `Sound`'s timeline is the simulation's**, so `Ended` lands on the same
  tick in a replay, in a headless run, and on a machine with a different audio
  buffer. The underrun counter the roadmap's gate names does not exist in
  miniaudio; this engine defines one and defines what it counts.
- **A skinned character animates and casts a shadow that walks.** glTF skins
  and clips load (parents-first, with the vertex stream remapped — neither of
  which the file gives you), `AnimationPlayer`/`AnimationTrack` play and blend
  them on the SimClock, and there is a skinned pipeline for the forward pass AND
  the shadow one. Without the second, a character's shadow stands still while it
  walks: a bug nobody can photograph, because the image is correct everywhere
  except on the ground.
- **`Instance.new("Part")` is visible.** Five generated solids, one per
  `Enum.PartShape`, through `MeshCache` like any imported mesh — and the renderer
  changed not one line, which is what M4's "engine-generated geometry must reach
  the renderer" constraint was written to get. `Part.Shape` is honoured rather
  than marked `Inert`.
- **A person arriving from a familiar platform finds `InputBegan`** (ADR 0041),
  fed from the IAS's own dispatch and never from the OS: it sinks like an action,
  replays like an action, and carries whether the UI already took it. And a HUD
  button drives a real action through four virtual `KeyCode`s, which is the
  roadmap's non-device seam with its proving caller.
- **A character rides a moving platform**, which took two defects to reach:
  kinematic bodies are MOVED rather than teleported so they have a velocity at
  all (D027), and an anchored part something is writing becomes kinematic for
  twelve ticks (D031). Both were found by a person playing the obby.
- **A label in Portuguese says what is missing.** Text is decoded as UTF-8 and a
  codepoint the face cannot draw gets a visible box rather than mojibake, out of
  a glyph store keyed by face, size and codepoint — a cache and not a bake, so
  M7's user font is a widening rather than a rewrite.
- **`examples/04-obby` is a game**, playable start to finish, and it is the
  milestone's E2E gate: a recorded input stream drives it headless to the finish
  flag with the whole stack in the loop.
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
- **D018 — `luaug_net_tests` hung once** on Windows and passed on a re-run. §12
  quarantines on the second occurrence; this is the entry that makes a second
  one countable.
- **The build agreeing is not evidence that the build read your file.** A
  break-verification restored with `Copy-Item` kept the source's old timestamp,
  Ninja rebuilt nothing, and a passing fix was reported as failing against code
  that was no longer on disk. Restore with `cp` and `touch`. **M6 found the same
  shape from the other end**: a `DrawQuad` that grew by one field left a stale
  object behind and produced a segfault in a test that had nothing to do with the
  change. `--clean-first` on the module was the answer, and "a mysterious crash
  right after a struct changed size" is now a known first thing to check.
- **D028's remaining half — `Touched` for a character's SIDE contacts.** The
  ground half is fixed; a wall walked into still fires nothing, because the
  character's non-ground contacts are not on the `IPhysics3D` seam. That is a
  seam widening rather than a fix.
- **D026 — the capture gate records an upload's SIZE and not its contents**, so
  the quads a frame draws are invisible to the blocking render gate. What holds
  the line meanwhile is in the row.
- **M7's first step, written out so it is not re-derived**: the asset pipeline is
  what four `Inert` markers now name. `TextLabel.Font`, `ImageLabel.Image`,
  `ImageLabel.ScaleType`, `ImageLabel.SliceCenter`, `ScrollFrame.ScrollBarThickness`
  and `MeshPart.CollisionFidelity` are each stored, read back, and acted on by
  nothing — and `tools/repo/inertcheck.luau` is the gate that will not let a
  seventh join them quietly.
- **Three of M6's decisions went against a document, and all three are ADRs.**
  0039 (an `InputContext` declares its dispatch rate; the IAS's enums are total),
  0040 (a `UDim2` placement is arithmetic, so v1 does not call Clay), and 0041
  (`InputService` gains the raw event surface ADR 0029 had ruled out). 0040 is
  the one a reviewer should read for the process: the milestone declined to use a
  dependency it had vendored, did NOT remove it because that is not the agent's
  call, and the human answered on 2026-08-21 — so Clay is un-vendored now. 0041
  is the one to read for the judgement: the technical objection to raw events was
  never to events, it was to events read from the OS, and every part of it
  dissolved once they came out of the IAS's own pipeline. What changed the answer
  was a PRODUCT argument the agent had not weighed.
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

- **ANSWERED 2026-08-20 — the font is an asset, and the engine ships exactly
  one.** `TextLabel.Font` is already typed `Content`, so a game supplies its own
  face by URI the way a `MeshPart` supplies a mesh; that half of the design is
  done. What the human decided is the default: **Inter (OFL 1.1)**, chosen for
  legibility at UI sizes. Roboto (Apache-2.0) is the alternative if matching the
  repository's own licence family is worth more than the typeface, and it is a
  one-word change. Not several faces: one good default plus "bring your own"
  covers the ground, and three vendored faces are three things to licence,
  update and explain.

  **Three consequences the human named, and one of them binds M6 rather than
  M7.** They are recorded in the roadmap so they are not rediscovered:
  - **The glyph store must be a CACHE, not a boot-time bake** — keyed by face,
    size and codepoint, filled on demand. **Done at M6**: the key is all three,
    the bound is 2,048 entries with a clear-on-full policy and a log line, and
    sizes are quantized to a quarter pixel so a tweened `TextSize` does not mint
    an entry per frame.
  - **Unicode stops being optional.** **Done at M6**: text is decoded as UTF-8
    into codepoints and one the face cannot draw gets a visible replacement box —
    not nothing, not a question mark, and not the mojibake reading the bytes one
    at a time produced. `missingGlyphs` counts distinct characters the face could
    not draw, so a label full of boxes is a number before it is a bug report.
  - **`TextLabel.Font` stops being `Inert`** when the face lands, and the marker
    goes with it. Still M7's.

- **ANSWERED 2026-08-20 — the Clay row is removed.** ADR 0040 established that a
  `UDim2` placement is arithmetic and that Clay cannot express an unclamped scale
  or a fractional anchor point, so this is not "not used yet", it is "does not
  fit the model". A vendored dependency nobody calls still enters every build,
  every notices file and every future reader's half hour. The ADR stays in the
  tree if a genuinely flow-shaped feature (`UIGridLayout` was the example) ever
  wants it back. **Done 2026-08-21**: `third_party/clay/`, its manifest row and
  its notices row are gone, and ADR 0040 carries the answer so that re-vendoring
  is an ADR and a manifest row rather than a rediscovery.

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

- **2026-08-21 (session 11, Claude Opus): M6 built, awaiting sign-off.** Picked
  up mid-milestone with four systems in, and finished it: the animation runtime
  and the skinned pipeline, solid `Part` rendering, `InputService`'s raw event
  surface, the non-device input seam, the glyph cache, Clay's removal,
  `examples/04-obby` with its end-to-end replay gate, the audio soak, the
  animation determinism scene, D017's two DebugShell panes, and the Gate Record.
  Learned, and the one to keep: **the deliverable is the test that finds what the
  tests do not.** Six of the ten defects closed this milestone were found by a
  person playing the obby — a character that does not ride a platform, a
  classification that made the fix for it unreachable, a menu 16 px off centre, a
  `UICorner` that drew nothing. Every one of them had passing unit tests around
  it. The second lesson is inside the fourth: `UICorner` was born in exactly the
  state `Inert` was built at M4.5 to make visible, and nobody marked it, because
  the marker depended on somebody remembering. `tools/repo/inertcheck.luau` is
  the mechanical half, and run once against the whole tree it found five more.
  The third: **a gate can be wrong about the thing it gates.** The audio soak
  reported 1,348 underruns on a real device and every one of them was the mixer
  working correctly.
