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
