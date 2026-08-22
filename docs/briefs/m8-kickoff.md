# M8 Kickoff — Flagship, Hardening, Docs, v1.0

- Started: 2026-08-22
- Roadmap section: docs/roadmap.md#m8--flagship-hardening-docs-v10-m

## Goal (restated)

Assemble, polish, prove, ship. Every milestone so far built a capability and
proved it in a scene written to exercise that capability alone. This one builds
the scene that has to hold all of them at once — `examples/10-open-world`, a
third-person character walking a streamed world under a moving sun, with a HUD,
ambient sound, physics and hot reload — and then hardens the engine around the
things a person who is not the author will hit first: a quality setting for a
GPU that is not the reference machine, an icon that is theirs and not ours, a
`luaug build` that produces something they can send to somebody else, and a
migration guide that tells a developer arriving from the platform this engine
borrows its shape from what changed.

Two of the items are not about the demo at all, and both are promises made
several milestones ago coming due. **The editor seam** (ADR 0017) was declined
for v1 on the explicit condition that nothing hard-codes an assumption blocking
one, and nobody has checked in four milestones; the concrete check is two
`WorldHost`s alive at once, each with its own VM, rendered into two targets.
**The absolute performance targets** in [`../perf-baselines.md`](../perf-baselines.md)
say "bind at M8" — this is where they stop being aspirations and become numbers
with a scene behind them.

The v1 definition of done is in `MASTER_PROMPT.md` §13, and it ends with a human
playing the demo and saying ship. Everything below is evidence offered to that
decision.

## Scope checklist (from roadmap)

- [x] **`examples/10-open-world`** — third-person character exploring a large
      open world: streamed chunks (terrain + props via the M7 pipeline), Jolt
      physics, day/night cycle (sun animation + tuned tonemap), HUD, ambient
      audio, all hot-reloadable.
- [x] **Performance pass to absolute targets** — 60 fps at 1080p on the recorded
      reference machine; the standing targets at the end of
      [`../perf-baselines.md`](../perf-baselines.md) bind here.
- [x] **`luaug build` packaging** — distributable player + content (ADR 0045).
- [x] **Graphics settings, as a family rather than a number** — shadow
      resolution, cascade count and distance, render scale, light budget and
      post toggles stop being `constexpr`. Engine settings, not `Lighting`
      properties: a scene must not decide the player's GPU budget (ADR 0038,
      and ADR 0044 is the answer).
- [x] **Prove the editor seam is still open** — two `WorldHost`s alive at once,
      each with its own `ScriptRuntime`, rendered into two targets.
- [x] **Application identity** — `branding/` wired up; a game built with
      `luaug build` takes its icon from `[project] icon` in `luaug.toml`;
      embedded in the artifact, all sizes in one resource, taskbar identity on
      Windows, and **verified by reading the resource back out of the built
      artifact** rather than by looking at it.
- [ ] **Docs completion** — [`../coming-from-roblox.md`](../coming-from-roblox.md)
      written for real; API reference generated from the defs pipeline; README
      with screenshots.
- [x] **License/NOTICE audit** of every vendored dependency —
      `tools/repo/licensecheck.luau`, in the Luau gate.
- [ ] **CHANGELOG**; tag `v1.0.0`; GitHub release with Windows binaries + source
      instructions.

## NOT in scope

- Anything on R15's closed list — no editor, no multiplayer/replication, no 2D
  layer, no mobile port, no navmesh. The editor **seam** is proven; no editor is
  built.
- **Any new rendering technique.** M7.5 closed the renderer's feature list. What
  happens here is that its numbers become configurable and its cost becomes a
  budget. A gap found against a reference is a defect with a row, not new scope.
- **A settings persistence layer.** The engine ships no save/load pair for
  graphics settings, for the same reason and with the same wording it ships none
  for input bindings (api-design.md §2.4): persistence is the game's, and a
  settings screen serializes it like any other state.
- **`luaug build` for macOS and Linux targets.** The roadmap's release line says
  Windows binaries plus source instructions, and a packaging path no gate on
  this machine can execute is a packaging path that ships broken. The
  cross-platform halves of application identity — `.icns` plus `Info.plist`,
  `.desktop` plus hicolor — are *specified* here and built when a tier can run
  them.
- **A launcher, an installer, an updater, or a content-delivery story.** A
  folder that runs is what v1 owes.
- **Terrain, particles, decals, `SurfaceGui`.** The open world is streamed
  meshes and parts, which is what M7's pipeline produces. Post-v1 phase 2 owns
  the rest and says so.
- **Cross-platform determinism.** ADR 0025 level B stands; the `churn10k`
  judgement and Jolt's `CROSS_PLATFORM_DETERMINISTIC` switch remain human
  questions in the ledger.
- **An in-game quality slider.** The settings family ships without a Luau
  surface, which is not an omission: the roadmap's own sentence — a scene must
  not decide the player's GPU budget — forbids a script writing them, and a
  slider needs exactly that. ADR 0044 §Alternatives records what a post-v1
  version would have to solve.

## Subagent plan

**None.** Every item here is either a cross-cutting seam (the two-world proof
touches `app`, `script`, `render` and `scene` at once), a gate run, or a piece
of prose whose value is that one person held the whole milestone in their head
while writing it. `MASTER_PROMPT.md` §7 names all three as orchestrator-only
work, and this session's operating instruction forbids fan-out besides.

## Gate checklist (verbatim from roadmap)

- [ ] 10-minute scripted soak (walk + fly path) with zero crashes and bounded
      memory delta
- [ ] 60 fps at 1080p on the recorded reference machine
- [ ] every example launches and its automated run passes
- [ ] clean-machine CI job: fresh clone → bootstrap → build → `luaug new`
      template project runs
- [ ] determinism replay green
- [ ] `luaug check` clean repo-wide
- [ ] docs-lint clean
- [ ] **a human plays the demo and signs off** — the one gate that is
      deliberately not automatable

## The decisions this brief makes

### 1. Graphics settings are host settings, and a script may not write them

`Lighting` describes the world; a quality setting describes the machine. The two
must not be the same object, because a scene that shipped with a 4096-texel
shadow map would be deciding how a stranger's laptop spends its frame — which is
the whole of ADR 0038 §3's argument for putting this here rather than in
`Lighting`.

So the settings live in one `render::GraphicsSettings`, sourced in three layers,
each overriding the one before:

1. A **quality preset** — `low`, `medium`, `high`, `ultra` — a named set of
   every field. **`high` is exactly what M7.5 shipped, to the value**, which is
   a requirement rather than a coincidence: every pixel golden in this
   repository was recorded against those constants.
2. **`luaug.toml` `[graphics]`**, the game author's default for their content.
3. **`luaug-host` flags**, which is what a gate, a perf sweep and a person
   debugging use.

**A running script is not a fourth layer**, and that is the decision inside the
decision. The roadmap's own sentence forbids it — a script *is* the scene — so
what v1 ships is a family a player or a packager configures, and what it does
not ship is an in-game quality slider. ADR 0044 records that, what it costs, and
what would have to be true to change it.

**They are structurally outside the simulation, and a machine says so.**
`GraphicsSettings` lives in `render` (L4); `scene` is L3 and may not include it,
and `tools/repo/checklayers.luau` derives the layering from real `#include`
edges. A settings value reaching the world hash is a red gate rather than a
discovery. The replay harness makes the same point from the other side: it
creates no device and no renderer at all, so there is nothing for a quality
level to change.

**The family is gated by a differential rather than a golden.** `low` and
`ultra` must render the same scene to *different* images. A preset that is
parsed and then reaches nothing renders exactly what it did before, and no
golden can see that.

### 2. The editor-seam proof is a differential, not a boot

Two `WorldHost`s that both construct without crashing prove nothing: a global
that both of them scribble into would still construct. What the phase-2 editor
needs — and what post-v1 phase 4's loopback multiplayer needs, which is why this
has two callers — is that the two worlds are *independent*. So the test renders
both, and asserts:

- each world's image is **byte-identical to the same world rendered alone**, and
- the two images **differ from each other**.

The first half is what catches a shared static: if world B's contents leak into
world A's frame, A stops matching its solo render. The second half is what
catches a test that proved nothing because both worlds were empty. D043 is the
reason both halves are written down — three green instruments agreed about a
frame that was not drawing anything.

### 3. `luaug build` produces a folder, and the folder is the product

Not an installer, not a self-extracting executable, not a single file. A folder
containing the renamed host binary, the engine's `content/` (catalogs, compiled
shaders), the project's compiled bytecode and its built asset pack. It is what
every gate on this machine can execute end to end, and it is what "send it to
somebody" means without a distribution story v1 does not have.

The binary is the host, **copied and then edited**: on Windows its icon resource
group is replaced with the game's, so the artifact carries the game's identity
rather than the engine's. That is a Win32 `UpdateResource` call on a copy of an
already-linked PE — no relink, no per-game build of the engine.

### 4. Application identity is asserted by reading the artifact back

An icon is the single most regression-prone thing in a build, because nothing
fails when it is wrong: the program runs, the window opens, and it wears the
wrong face. So the check is not a screenshot. It opens the built `.exe`, walks
its PE resource directory, finds the icon group, and fails if it is absent or if
its bytes are still the engine's default. `docs/roadmap.md` asks for exactly
this and it is worth restating why: an icon nobody can assert is an icon that
silently regresses.

### 5. The flagship's world is generated, and its soak path is scripted

`examples/05-streaming` established both and for reasons that hold here: a
committed world is a binary asset the roadmap's own deliverable line forbids,
and a hand-flown path is not a gate. The world comes from a deterministic
generator keyed on cell coordinates; the soak path is a recorded route the
character walks and then flies, on the simulation clock, so that the same frame
of the same run is the same picture on every machine.

**The character is the streaming focus**, which is the thing `05-streaming`
deliberately did not do — it used the camera, and noted that a game would hand
it the character instead. This is that game.

## Build order

1. **The editor seam** — first, because its value decays with every line added
   before it and because whatever it finds is cheapest now.
2. **Graphics settings** — before the flagship, so the flagship is authored
   against a real quality family rather than retrofitted with one.
3. **Application identity and `luaug build`** — together, because the icon
   rewrite is a step of the packaging and neither is testable without the other.
4. **`examples/10-open-world`** — the flagship, on top of all three.
5. **The performance pass** — against the flagship, which is the only scene that
   loads every system at once.
6. **Docs, licences, CHANGELOG** — last, because they describe what the first
   five turned out to be.
7. **The full gate**, then stop for the human.

## Attempted / abandoned

_(appended during the milestone)_

## Findings

11. **A defect report that carries a RELATION is worth more than one that
    carries a symptom** (D054). "The flicker gets worse the further the camera
    is" is not a complaint, it is half a derivation: the far cascade's texel is
    larger, so the same one-texel step is larger — except that measuring it
    showed the step is about the same size on screen at every distance, and what
    actually changes is the FREQUENCY. The near cascade's lattice drifts half a
    texel per frame near noon and its edge moves every other frame; the far one
    drifts a twentieth and holds still for twenty-three frames before jumping.
    **Frequent small steps read as motion and one step every four hundred
    milliseconds reads as a jump**, which is why the near field always looked
    fine.

    That reframing is what chose the fix. The step cannot be removed without a
    temporal filter this renderer does not have, so the penumbra has to be wide
    enough to hide it — and the only thing that sets the penumbra in a far
    cascade is the texel band's floor, because out there the authored five
    centimetres is a fraction of one texel. Six texels rather than two, paid for
    by rotating the kernel per pixel so that the banding a wide fixed grid
    produces becomes fine noise instead.

    **And the same human handed over the instrument**: point the camera straight
    down. A shadow seen from above is a shape whose motion between two frames is
    unmistakable, where the same shadow seen from a standing eye is an edge at a
    glancing angle.

10. **"It starts at eleven in the morning" is a diagnosis, and it separated two
    defects nobody could have told apart from the symptom** (D053). A human
    reported the world going temporally unstable partway through the day cycle:
    the ground pulsing, shadow edges trembling, everything still. The detail
    that mattered was that it began at an HOUR rather than after a duration,
    which rules out anything that accumulates and points at something whose rate
    depends on where the sun is.

    Two things did. **The larger one was not a shadow at all**: the diffuse
    ambient rode on the specular chain's rebuild threshold, so the light every
    matte surface receives held still for a hundred frames and then stepped by a
    per cent — 68% of the ground changing on one frame while the sky beside it
    did not. **The smaller one is the shadow edge**, and it is inherent: a
    cascade's lattice lives in the light's own rotating frame, so a fixed world
    point drifts across it at 0.02 texels per frame at dawn and 0.50 near noon.
    The first is fixed; the second is mitigated with a wider penumbra and
    smaller texels, and named honestly.

    **The general shape: a threshold shared by two things with different prices
    is set by the expensive one.** Six texture bakes and a projection onto nine
    numbers were sharing `kEnvironmentRebuildCosine`, and the cheap half was
    paying the expensive half's rate.

9. **Reach in a shadow map is paid for in resolution, everywhere, at once** —
   and the flagship's own project file was the thing paying (D052). A human
   asked whether it was normal for a shadow to lose that much resolution at a
   moderate distance. Half the answer is yes: a cascade is chosen by where the
   shadow LANDS, so nothing about a distant caster coarsens its shadow on nearby
   ground. The other half was ours: `shadow_distance = 180` on a 1024 tile put
   everything past forty-four metres on a **half-metre grid**, and the file's own
   comment had called that "the trade a distant landmark is worth" without ever
   measuring what the trade cost.

   Measured by dumping each fitted box, the fix was obvious and free: 140 m on a
   2048 tile is 0.20 m per texel, at 2.81 ms against 2.89: a larger tile costs
   fill and a shorter distance hands it straight back in culled casters.

   **The same measurement convicted the preset above it.** `ultra` spent its
   four-times atlas on range rather than density — 0.32 m per texel against
   `high`'s 0.35, which is nine per cent for four times the memory. A preset
   nobody had measured looked, from thirty metres out, exactly like the one
   below it.

   And a third thing fell out while writing the test for it: `--quality=low`
   took the Low preset and then let the project file put its 4096-pixel atlas
   back on top. **A file's per-key entries refine the level that file names, so
   they go with it** — now written into ADR 0044, and covered by the first test
   `project_config.cpp` has ever had.

8. **A shadow parameter is not right or wrong on its own; it is right against a
   scene, and both of this milestone's last two defects were parameters that had
   never met one.** D050's texel snap rounded an ABSOLUTE position in a light
   space that turns — correct for a sun that stands still, and a lattice that
   sweeps out from under every shadow in the picture for one that does not; the
   fix is to round the box's MOVEMENT, which is the stronger statement anyway.
   D051's normal offset was a square kernel's diagonal and its depth bias two
   centimetres, both sized for shadow acne that this renderer's front-face cull
   already prevents, and what they actually did was lift every object off its own
   shadow.

   Both were found the same way: render the same scene twice with one number
   changed and subtract the images. **That is D043's differential method applied
   to a constant rather than to a code path**, and it is the only instrument in
   the repository that can see a defect a golden was recorded with.

7. **The reported artifact was three defects with one symptom, and the root was
   in the scene rather than in the renderer.** A human looking down in the
   flagship reported something dark on the ground that kept changing shape.
   Reproduced exactly as described, then reduced by holding the character still,
   freezing the sun and proving the scene identical between the two frames — 58
   draws, 12 visible objects, 1,320 triangles in both — which still left 10,658
   differing pixels of 518,400, tracing every shadow edge in the frame.

   The three, in the order they were removed: a day that took four minutes, so
   the sun crossed the sky ninety times faster than the real one and every shadow
   visibly swung; a cascade fit with no memory, refitting to its contents every
   frame and re-quantising every edge with it; and — the root — a character
   spawned exactly on the corner where four ground tiles of different heights
   meet, sliding off it at three centimetres a second, which moved the camera,
   which moved the whole shadow lattice.

   **10,658 pixels became 43, and 3 with the exposure frozen.** The forty that
   remain are automatic exposure still converging by a hair, which is the
   stabiliser working.

   The general shape is worth keeping: **the thing that moved was not in the
   frame.** Two of the three causes were invisible in any single screenshot —
   one was the clock and one was a position readout that showed `-0, 2, -0` for
   a character creeping in the ninth decimal place.

6. **A defect report is worth more than the gate it escapes.** Three of this
   milestone's defects came from a human running the flagship and describing
   what bothered them — the world vibrating (D047), the shadows crawling (D048),
   and both times every gate in the repository was green. The instruments that
   found them afterwards were built in minutes; what could not be manufactured
   was the observation.

5. **A sequence of GPU runs is not a sequence of measurements.** The per-feature
   sweep put the same baseline run first and last: 6.25 ms and 4.83 ms, a 23%
   difference with nothing changed between them. An earlier version of the same
   sweep, taken without a warm-up, reported a 2.9 ms baseline and then showed
   `--quality=low` as SLOWER than `high` — which is impossible, and is the shape
   a measurement takes when the variable is the order rather than the flag.

   It cost an hour of chasing a regression that was not there: the median had
   apparently doubled after the interpolation work, and forcing `alpha` to zero
   showed the same median. **The instrument was the thing that changed.** The
   baselines record the protocol now — warm up, repeat the control last, and do
   not read the middle rows to more precision than the two ends justify.

4. **A licence audit somebody performs is a fact about one afternoon.** Written
   as a check instead (`tools/repo/licensecheck.luau`), it immediately found more
   than the reading had: xxHash ships its *library* under BSD-2 and its entire
   `cli/` and `tests/` trees under GPL-2.0, about thirty files, and SDL's
   hidapi backend carries a triple licence in six headers rather than the three
   a first grep showed.

   None of it is compiled — `SDL_HIDAPI` is `OFF` and `engine/scene` uses xxHash
   as an include directory only — and each exemption now cites that evidence
   rather than asserting it. **The check is that the list does not grow**, which
   is the case a pin bump produces and a reading never catches.

3. **Two gates depended on a world nothing generated, and the way that surfaced
   is the finding.** M8's own soak gate was written, the flagship's generated
   world was deleted to prove the gate would rebuild it, and it did not —
   because M7's two gates over `examples/05-streaming` had never rebuilt
   anything either. They passed on this machine because a hand-run had left the
   world on disk, and neither has ever run in CI: the last all-green run across
   all three tiers predates both of them.

   **A gate whose input is produced by a step nobody automated is a gate that
   reports on whatever the last person left behind.** Fixed for all three by one
   included script, break-verified by deleting the world. Recorded as D046.

2. **`luaug new` had not worked since the CLI's commands moved into a
   subdirectory, and the way it was found is the finding.** M8 needed a
   scaffolded project to package, typed the first `luaug new` of the milestone,
   and it failed outright: `cliRoot` counted two directories up from its own
   source, which was right at `tools/cli/new.luau` and wrong at
   `tools/cli/commands/new.luau`.

   Two milestones of gates went green over it. The CLI's own suite covers
   `toml` and `version`; the scaffolder had no test at all, and the M3 gate that
   signed it off ran it by hand — before the move. **A count is a fact about a
   directory layout that no check holds still**, so the fix walks up looking for
   `templates/starter` instead of counting, and `tests/packaging` now scaffolds
   with the real `new.run` on every Windows gate run. Recorded as D045.

1. **A renderer is not stateless per view, and the seam proof is what said so.**
   The first version shared one `IRenderer` between the two worlds, on the
   reasoning that a renderer is pipelines and scratch buffers rather than scene
   state (ADR 0027). It went red immediately: world A drifted by 453,908 bytes
   and world B by 1,054,072 against their own solo renders.

   Neither number was a leak of scene DATA. A renderer carries scene HISTORY --
   the exposure it has adapted towards, and the environment chain it bakes one
   level per frame (M7.5, Decision 5) -- so the third frame the renderer had ever
   drawn was never going to match the first, whatever world was in front of it.
   Two worlds need two renderers, which `renderer.h` already implies for a
   different reason: "a caller that renders into two formats needs two
   renderers".

   **The general shape, and it is why the failure was useful rather than
   annoying: a differential over an instrument with memory is a differential over
   the instrument as well.** The fix is not tolerance, it is holding the frame
   COUNT equal and giving each world its own history. Both are in the harness and
   both are commented, because the next person to add a temporal pass will
   otherwise rediscover this by watching a green gate turn red.

## Gate Record

Filled 2026-08-22, before human review. Every command below was run on the
reference machine (`docs/perf-baselines.md`); the milestone is **not** complete
until a human plays the demo and says so (`MASTER_PROMPT.md` §6, §13).

### The roadmap's eight

| Gate item | Result |
|---|---|
| 10-minute scripted soak, zero crashes, bounded memory delta | **Pass.** 35,939 measured frames at 1080p: median 5.35 ms, p99 8.79 ms, worst 17.23 ms, **one frame over 16.7 ms**, zero streaming hitches, peak resident 168 MiB **equal to** final resident, instances 4,354 early against 4,285 late. `ok: true`, no failures. |
| 60 fps at 1080p on the reference machine | **Pass**, with three times the headroom at the median. The numbers above, and the per-feature sweep in `docs/perf-baselines.md`. |
| every example launches and its automated run passes | **Pass.** `00-clear`, `01-instances`, `02-meshes`, `03-physics-playground`, `04-obby`, `05-streaming` and `10-open-world` all run headless to a screenshot; the obby, the audio soak and the animation replays are three of the 44 ctest targets, and the two soaks (`streaming_soak`, `openworld_soak`) are two more. |
| clean-machine CI job: fresh clone → bootstrap → build → `luaug new` template project runs | **Pass locally, and it found two defects doing it.** `tests/packaging` runs `luaug new`, `luaug build`, the built folder executed **with no arguments**, and the icon read back out of the artifact — on every Windows gate run. It found D045 (`luaug new` could not find its own template) on its first execution. D046 is the other half: two M7 gates depended on a generated world that nothing generated, which is exactly what a fresh clone exposes. **Actions itself is dark** (see the ledger), so the CI job that would run this on a clean runner has not. |
| determinism replay green | **Pass.** `determinism` and `replay_gates` green on both tiers; 1,109 conformance cases. Render interpolation was built so it cannot touch this: `TransformHistory` lives in `render`, `scene`'s components are untouched, and every recorded trace still matches. |
| `luaug check` clean repo-wide | **Pass.** `luau-analyze` strict, StyLua, the i18n lint, the layering check, `inertcheck`, the vendor/narrowing check, and now `licensecheck` — 19 vendored dependencies, every licence present and permissive. |
| docs-lint clean | **Pass.** Relative links, version consistency, the R7 sweep, the ledger's shape, every example's launcher, and the defect register's numbering, states and citations. |
| **a human plays the demo and signs off** | **Outstanding, and it is the gate.** |

### The full local gate

`scripts/localgate.ps1`, all five stages green: docs, luau, format, windows,
linux. **44 ctest targets on Windows** and 41 in the Tier-2 container, 1,109
conformance cases on both, 335 files clang-formatted at the pinned 18.

macOS is unverified: CI has executed zero steps since 2026-08-21.

### Defects found and fixed during the milestone

Ten, and **eight of the ten came from a human running the thing** while every
gate in the repository was green.

| | Found by | What |
|---|---|---|
| D045 | the milestone's own work | `luaug new` could not find its template, and had not since the CLI's commands moved into `commands/` at M3 |
| D046 | the milestone's own work | `streaming_soak` and `asset_determinism` depended on a generated world that nothing generated |
| D047 | a human, walking | The world vibrated: the engine never interpolated between ticks, and `Frame::alpha` had been computed and read by nothing since M1 |
| D048 | a human, looking down | Shadows crawled. Three causes; the root was a character sliding off a tile corner at 3 cm/s, which moved the camera, which moved the shadow lattice |
| D049 | a human, asking for mouse look | `InputService.PointerLocked` was stored and read by nothing, and the camera turned the way you did not push |
| D050 | a human, watching a tree | Shadows flickered as the sun moved: the texel snap rounded an absolute position in a light space that turns |
| D051 | a human, with a close-up | Objects hovered over their own shadows: two biases paying for an acne the cull mode already prevents |
| D052 | a human, asking a question | Shadows coarse at a moderate distance: the flagship asked for 180 m of shadow on a 1024 tile, which is half a metre per texel past forty-four |
| D053 | a human, standing still | The world pulsed and shadow edges trembled after eleven in the morning: a diffuse ambient arriving in steps, and a shadow lattice that drifts twenty-five times faster near noon than at dawn |
| D054 | a human, moving the camera back | The remaining flicker scales with camera distance: the far cascade's edge holds still for twenty-three frames and then jumps, where the near one moves every other frame |

### What this milestone does not have

- **`luaug build` produces a Windows folder and refuses every other target.** A
  packaging path no tier here can execute is one that ships broken.
- **The packaged game ships Luau source rather than bytecode** (ADR 0045), which
  amends `api-design.md` §4 and says what it would take to change.
- **No in-game quality slider.** The settings family is configured by a player or
  a packager, never by a scene (ADR 0044).
- **`@luaug/signal` and `@luaug/imgui` are not shipped**, and api-design.md §1.1
  now records why rather than leaving two names to be discovered missing.
- **`inertcheck` still cannot see `EngineState`**, which is how D049 sat there
  since M6. Named in the ledger; not fixed here.
- **macOS is unverified** for this milestone, as for the two before it.
