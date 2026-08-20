# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **M4 — Seeing the World: Meshes, Materials, Camera, Lighting — COMPLETE, all
  five gate items green plus the three non-gate obligations**, awaiting human
  sign-off. Brief:
  [`docs/briefs/m4-kickoff.md`](docs/briefs/m4-kickoff.md), which carries the
  Gate Record and twenty-five Findings. The Android device checkpoint passed:
  the human ran the APK on a real device and reports it functional, which is the
  question ADR 0005 left open and the reason the RHI could be frozen (ADR 0037).
- **M3 — Tooling Loop — signed off 2026-08-20**, tagged `milestone/m3`;
  **M2 — Kernel — signed off 2026-08-20** (`milestone/m2`); **M1** signed off
  2026-08-19 (`milestone/m1`); **M0** signed off 2026-08-19 (`milestone/m0`).
- **CI is green on `main`**, and the three steps M3 added to the workflow — the
  toolchain install, `luaug test` and the hot-reload suite — have run on hosted
  runners on both tiers. macOS is now blocking on every code push, which is M4's
  own gate item, and has not yet been observed under that trigger.

### M4: what exists

- **glTF in, lit PBR out.** `examples/02-meshes` renders a generated scene
  through a real pass list — sun shadow map, sky, forward PBR into an
  `Rgba16Float` target, tonemap — with four materials, a point light on a part,
  distance fog and a day/night slider that writes `Lighting.ClockTime`.
- **`engine/asset` (L2)**, born as a loader rather than a streaming system:
  image encode/decode moved in from `app` as its own header promised at M1, and
  a fastgltf importer whose tests were mutation-tested with twenty-two planted
  defects.
- **The render module grew a renderer.** `MeshCache` (static buffers and a
  per-frame ring), `MeshLoader`, `RenderWorld` v2 with a camera, an environment,
  lights and pre-sorted draws, and `renderer_default` behind the `IRenderer`
  contract.
- **Five new classes with real backing** — `Camera`, `MeshPart`, `PointLight`,
  `SpotLight` and the `Lighting` service — declared only as far as they are
  implemented, which cost `Sky`, `Camera.ViewportSize` and
  `MeshPart.CollisionFidelity` their place in this milestone.
- **The `DebugShell`**: a tree explorer and a properties panel that is one
  generic sweep over the generated descriptors, writing through
  `World::setProperty` and applying at the FrameStart safe point. ADR 0017's
  compensating control, four milestones after the ADR leaned on it.
- **The triangle sample and its Android package**, which is the artifact the
  device checkpoint was passed with.
- **The RHI is frozen** (ADR 0037): 32 calls, exercised by a real pass list, with
  the one addition the renderer wanted declined.
- **Three carried debts paid**: `Luau.Analysis` out of the build (53.9 s → 34.8 s
  cold), `api-dump.json` generated and freshness-gated, and `luaug --version`.

### M4: the gate

**5 of 5, plus the three obligations.** The Gate Record in the brief carries the
numbers, the commands and the screenshot.

### M4: what does NOT exist yet

The brief's twenty-two NOT-in-scope items, of which the ones most likely to be
mistaken for bugs: no IBL (so a metal is lit only by punctual lights and by the
flat ambient), one shadow cascade and shadows from the sun alone, no clustered
light culling with a bound of eight lights per draw, no skinning or animation,
and no `Sky` class. `PointLight.Shadows` is stored and read back faithfully
while nothing acts on it.

## Now / Next

- **`Lighting` is never read by the renderer. The whole environment has been the
  struct defaults since M4 shipped it** — found 2026-08-20 by the human saying
  the shadow did not seem to move, and confirmed by observation rather than by
  reading. **This is urgent: it changes every rendered image, and the M4 render
  goldens and lavapipe screenshots are being recorded against it.**

  **The proof, before the cause.** `examples/02-meshes` with `Lighting.Ambient`
  set to pure red renders **byte-identical** to the same frame with the ambient
  the example ships (md5 `170446b8…` both). A property the shader multiplies
  into every pixel cannot change and leave the image identical. Nothing from
  `Lighting` reaches the frame.

  **The cause.** `WorldHost::start` caches
  `findFirstChildOfClass(dataModel, Lighting)` into `m_lighting` before any
  script runs, and its comment says "Created by `registerServices` during the
  boot above, so this is a lookup rather than a creation". That is true of
  `Workspace` and false of `Lighting`: `services.cpp` states the rule one line
  from where it is broken — "`Workspace` and `ScriptService` exist from boot;
  every other service is created by its first `GetService`". So the lookup finds
  nothing, `m_lighting` stays invalid for the life of the world, and
  `extract`'s `world.lighting().find(lightingHost)` returns null every frame.
  The environment then keeps `RenderWorld`'s defaults: sun straight up
  (`Vec3{0, 1, 0}`), brightness 2.0, the default ambient, and fog off because
  the default `fogEnd <= fogStart`.

  **Which is exactly what the human saw, and explains three reports as one.**
  A sun pinned to straight up casts shadows that are the object's own footprint
  and never lengthen, whatever `ClockTime` says — confirmed at a 12-degree sun
  where a three-metre pillar's shadow should cross the floor and is instead a
  patch at its foot. The lit faces are the tops rather than the sides facing
  the sunrise. And the flicker reported separately is the only thing left that
  moves: the shadow box is centred on the camera (the snapshot is
  camera-relative and `sunViewProjection` looks at the origin), the camera orbits
  at 1.47 m/s, and one shadow texel is 0.059 m — so the grid slides 0.42 of a
  texel per frame and the edges crawl. The day/night slider ADR 0025 and the
  deliverable both advertise has never done anything.

  **The fix is a boot-order question, not a renderer one.** `Lighting` should
  exist from boot as `Workspace` does, and for the identical reason: `extract`
  reads it every frame whether or not a script ever asks for it, so "created on
  first `GetService`" cannot be true of it. That also makes the cached id correct
  by construction rather than by timing. Resolving lazily on each miss is the
  smaller change and leaves the same trap one refactor away.

  **And the sixth gate this milestone that passes while doing nothing.**
  `render_world_tests.cpp` creates a `Lighting` instance itself and hands its id
  straight to `extract`, so every environment assertion passes against a
  hand-made id the host never produces. The untested step is the one that
  resolves the id — which is the step that is broken. The test to add is the
  host's, not the extractor's.

- **Three human-reported defects were dropped from this file when it was
  rewritten to close M4, and are restored below.** Not archived -- removed. A
  milestone-close rewrite is the moment a ledger is least able to afford losing
  its open items, because the next reader is the human deciding whether to sign
  it off. They are listed after the entry above, unchanged except where reality
  corrected them.

- **`BasePart.Transparency` is decided: alpha cutout here, a blended pass at M6**
  (human decision, 2026-08-20, on the report from the same day). It is declared
  in the IDL and `render_world.cpp` extracts it, and every value still renders
  opaque.

  **The first diagnosis here said `renderer_default` "never reads it", and that
  is true but too kind.** The value cannot reach it. There are two draw paths
  and Transparency exists in only one:

  - `RenderPart` — a `BasePart` as a debug wire box — carries `transparency`,
    and `submitWorld` honours it (`engine.cpp`: `if (part.transparency >= 1.0f)
    continue;`). This is the path that works, and it is the one the human's
    observation exercised: setting the orbiting lamp `Part` to 1 made it vanish
    while the scene did not.
  - `DrawItem` — the real renderer's unit of work — **has no such field**. Not
    an unread value: an absent one.

  That observation is also what settled the scope call, because the scene did
  not change for a second reason worth writing down: `examples/02-meshes` is one
  `MeshPart`, so the boxes and floor are sections of a single glTF file and have
  no per-instance property to set at all.

  **The obvious place to put it is wrong.** `GpuMaterialUniforms` already
  carries an alpha cutoff, but its own comment states why it cannot hold this:
  "per material rather than per frame, because it changes with the bind set and
  the sort key already groups draws by material". Materials are deduplicated
  across the frame; a per-instance alpha written there splits one material into
  as many as there are distinct transparencies and fights the grouping the sort
  key was built for.

  **The shape that fits, and it needs nothing the freeze closed.**
  `GpuObjectUniforms` is already per draw (`b0 space1`, vertex stage). Widen it,
  carry the alpha through an interpolant, and `clip()` in the fragment shader
  against the material's existing cutoff. No new bind, no new RHI call — ADR
  0037 froze the calls, and this adds none — and `DrawItem` gains the field it
  is missing. The `static_assert` on the struct size moves with it, which is the
  layout check doing its job rather than an obstacle.

  Cutout is honest and partial, and the entry should say so where a user reads
  it: `Transparency` becomes a threshold, not a fade. **The blended half is M6
  scope** — sorted back-to-front, after the opaque pass — recorded in
  `roadmap.md` under that milestone, where UI and tweens make blending
  mandatory anyway rather than speculative.

- **There is no crash artifact, and the human has now reported four defects
  without one (2026-08-20).** `architecture.md` §app promises a "crash handler
  (minidump + log)"; it does not exist, and `core::log` has no file sink either
  — every line the engine prints dies with the window. A human running the
  engine by hand is this project's verification model, and it currently asks
  that human to report from memory.

  **Amended the same day, by evidence.** The human captured a crash to a file and
  it held two lines — the two an ordinary successful run prints. `core::log`
  already `fflush`es after every line, so nothing was lost to buffering: the
  process died silently, without reaching any C++ error path, which is the
  signature of an access violation. **A file sink would not have helped at all
  here.** The ordering below is therefore backwards for this class of crash: the
  handler is the piece that matters, because it is the only one that runs after
  the fault and before the process is gone.

  Two pieces:

  - **A file sink for `core::log`.** With a layering constraint: `core` is L0 and
    `platform::paths()` is L1, so the path is *injected by `app` at boot* rather
    than resolved downward. The console sink stays exactly as it is — every gate
    and the conformance runner read it — and the file is an addition, not a
    replacement. Print its path at startup, or the log nobody can find is the log
    nobody sends.
  - **The handler proper.** `SetUnhandledExceptionFilter` plus a minidump on
    Windows, a signal handler elsewhere. Platform work, and it belongs in
    `platform` for the same reason the SDL seam does.

  Fourth thing `architecture.md` §app named that no milestone had imported —
  after the `DebugShell`, the api-dump and the triangle sample. That list is
  worth reading against reality once, rather than one entry at a time as each is
  discovered missing.

- **The sun's shadow flickers in `examples/02-meshes`, reported by the human on
  2026-08-20.** Not anchoring: there is no physics before M5 and nothing in that
  scene moves itself. What moves is the sun, a pure function of `ClockTime` on
  the SimClock, and the 47-second camera orbit.

  `renderer_default` fixed the shadow extent deliberately, against the crawl a
  camera-fitted box produces. Nothing addresses the other half: a directional
  light that *rotates* turns its own texel grid every tick, so a world point
  lands on a different texel each frame, and `sampleSunShadow` resolves each tap
  with a binary `reference <= occluder`. A point sitting near the bias threshold
  therefore flips between lit and shadowed frame to frame. Texel snapping — the
  usual answer — fixes translation and not rotation, so it would not help here.

  **Check first, because it is one line if true:** whether the shadow pass and
  the forward pass read the *same* sun. The map is built from one direction and
  sampled with another if either takes the tick value while the other takes the
  interpolated one, and that flickers at exactly the tick rate.

  If the sun is consistent, the fix is a **normal-offset bias** — displacing the
  sample along the surface normal rather than only in depth is what survives a
  moving texel grid — with a hardware comparison sampler
  (`SampleCmpLevelZero`) so a tap degrades instead of switching. Resolution
  alone only moves the threshold.

- **The inspector crashes on "go" from `RunService.Parent`, reported by the human
  on 2026-08-20.** Following the reference from a service to the DataModel takes
  the whole host down. Reading the code did not settle which of the two paths
  does it, and both are worth checking with a debugger rather than by eye:

  - **The button's guard is on the reference, not on what follows.** `go` checks
    `reference.valid() && world.alive(reference)` and then selects. Everything
    after that -- `world.classOf(game)`, the ancestry walk in
    `collectProperties`, the per-property getter, `formatValue` -- runs against
    the DataModel, which is the one instance in the world that no test selects.
  - **The explorer is rooted at `root`, and the selection just went above it.**
    `drawExplorer` walks from the root it was handed; selecting an ancestor of
    that root leaves `selection()` outside every row the tree produces. Anything
    that assumes the selection is reachable from the root breaks exactly here.

  Whatever it turns out to be, the fix is not only the crash: **the DataModel
  deserves a case in `inspector_tests.cpp`**. 618 lines of tests passed while
  this shipped, which says the fixture builds a world the panel is then pointed
  at from below -- and the human clicked the one edge that walks the other way.

- **Next: stop for M4 human review** (MASTER_PROMPT §6). `milestone/m4` is
  tagged. Do not open M5 in the session that closed M4.
- **When M5 opens, its first act is the clang-format gate**, on a quiet tree.
  M4's brief moved it there deliberately: turning it on requires reformatting the
  whole C++ tree and pinning a toolchain version, and doing that while the
  renderer was being written would have bought a milestone of diff noise.
- **A build directory is evidence of what was built once, never of what would be
  built now** — and a failed link is worse: LNK1168 moves the executable's
  timestamp before failing, so Ninja considers it current and CTest runs the old
  binary. It cost four cycles and one wrong measurement in M4. `localgate.ps1`
  clears orphaned hosts now; the discipline that remains is to never measure
  without watching the build succeed first.
- **A gate that can pass while doing nothing keeps being built by accident.**
  Nine instances in five milestones, three of them in M4 and two of those mine.
  The reliable test is to break the thing the check names and watch it fail.
- **The Linux tier is the only thing between this repository and a whole family
  of defects.** Six Clang-only build failures in M4 —
  `-Wmissing-field-initializers` and `-Wdouble-promotion` — none of which MSVC
  mentions.
- **Read the vendored `CMakeLists.txt`, not the library's documentation.** M4's
  first three findings came from doing that at kickoff, and each would have cost
  a milestone at the gate instead of an hour at the start.
- **`fs.watch` cannot be trusted on either platform, and Linux is the worse
  one** — a change below the watched directory produces no event at all there.
  Anything that watches files in a later milestone watches every directory and
  rescans.
- Carried forward, none blocking:
  - **Two of the five generated artifacts api-design.md §5 lists do not exist**:
    the typed `@std`/`@luaug` stubs and `docs/reference/**`. Both are DX surface
    with no gate behind them.
  - **The shipping profile does not configure**, and also needs a
    bytecode-loading path that does not exist. Scheduled with `luaug build` at
    M8.
  - **DXIL produced on Linux is never verified as signed.** No consumer until a
    Linux job produces a Windows shader pack — M8.
  - **The message catalog does not load inside the APK.** `Catalog::loadFromFile`
    uses `std::filesystem`; on a phone an engine error prints as a key hash plus
    its `detail`, which for a device failure is the `SDL_GetError()` that answers
    the question. `loadFromJson` plus `platform::readTextFile` is the fix.
  - **`architecture.md` §9 lists a clang-format gate that does not exist** —
    scheduled at the start of M5, above.

## Blocked — needs human

- **M4 sign-off.** All five gate items are green plus the three non-gate
  obligations, the Gate Record is in the brief, and `milestone/m4` is tagged.
  The Android device checkpoint passed on 2026-08-20.

## Decisions pending ADR

- (none — ADR 0035 was written at M3 kickoff)

## Session Log

Entries for the planning session and for M0, M1, M2 and M3 are in
[`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md), moved
there when this file passed its ~300-line cap.

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
