# M5 Kickoff — Feeling the World: Jolt Physics + Character

- Started: 2026-08-20
- Roadmap section: docs/roadmap.md#m5--feeling-the-world-jolt-physics--character-l

## Goal (restated)

Four milestones built a world you could describe, script, hot-reload and look
at. Nothing in it has ever fallen. This milestone gives the world mass: a
`BasePart` that is not `Anchored` is a Jolt rigid body, a contact between two of
them fires `Touched` at the next drain, a ray cast from a script hits something
real, and a `CharacterBody` walks up a ramp and over a step because a Jolt
`CharacterVirtual` says it can. The deliverable is
`examples/03-physics-playground`: stacks, ramps, a seesaw, and a third-person
capsule you steer through them.

Two things make it heavier than "add a library". The first is that **the
determinism gate becomes blocking here** (ADR 0025) — from this milestone on, a
change that makes the same inputs produce a different world is a red merge, not
a note. The second is that physics is the first system whose output writes back
into the scene every tick for thousands of instances, so it is the first real
customer of the quiet-write path M2 built and the 10k-parts benchmark measured.

The through-line, and the lesson M4.5 paid for: **test the step that resolves,
not the step that computes.** A physics test that constructs a body handle and
steps it will pass over a world in which no `BasePart` ever became a body. The
assertions in this milestone are written against a *world*, from a script, and
at least one of them is a differential.

## Scope checklist (from roadmap)

- [x] Jolt 5.6 on the fixed tick, single-threaded first; the job-system seam
      named but not wired (M7 wires it)
- [x] rigidbody/collider state as `BasePart` properties per `api-design.md`
- [x] collision events as deferred signals (`Touched` / `TouchEnded`)
- [x] raycast / shapecast API (`Workspace:Raycast`, `:Spherecast`,
      `:GetBodiesInBox`, `RaycastParams`, `RaycastResult`)
- [x] Jolt debug-draw bridge
- [x] `CharacterBody` on Jolt `CharacterVirtual`: capsule, slopes, steps, jump
- [x] third-person follow camera
- [x] minimal direct keyboard polling — deliberately replaced in M6 by the
      Input Action System (that migration is an M6 gate item)
- [x] **`Weld` and `WeldConstraint`** — added to the roadmap 2026-08-20 by human
      decision, **after this brief imported its scope**, so it is appended rather
      than assumed to have been read. A **transform** weld and not a Jolt
      constraint, and that is forced: `CharacterVirtual` is not a `Body`
      (Decision 8 below), a Jolt constraint connects bodies, so a constraint
      could never reach a character. The welded part is driven from its anchor
      and is not independently simulated; the solver is not involved. Both names
      or neither — `Weld` carries explicit `C0`/`C1`, `WeldConstraint` captures
      the relative transform when it becomes active, and shipping one is worse
      than shipping neither because the wrong one is silently wrong. R10: welds
      form a graph, so resolve in stable topological order at a defined point in
      the tick and **reject cycles**. It arrives here because nothing else in v1
      can keep a skinned `MeshPart` on a `CharacterBody`, and the alternative is
      a `Heartbeat` handler doing it per frame in every project.
- [x] `PhysicsService.FixedTimestep` becomes writable; collision groups
      (`RegisterCollisionGroup`, `CollisionGroupSetCollidable`,
      `GetRegisteredCollisionGroups`)
- [x] the tick budget is recorded **broken down** — broadphase, narrowphase,
      solver — because one number says a budget was missed and three say which
      stage missed it
- [x] the grounding pass that vendors Jolt answers, in `UNCONFIRMED.md`,
      whether recorded hashes survive Jolt's job system being wired at M7 —
      **before** the gate hardens, not after it breaks
- [x] `examples/03-physics-playground`

### Carried debts this milestone pays

- [x] **D016 — `BindToClose` has no capped grace period.** Scheduled here by the
      M4.5 close, because shutdown ordering now has physics state to care about.

## NOT in scope

Written before the work so that a gap found later is measured against a
decision rather than against a hope.

1. **No joints, motors or solver constraints** — narrowed 2026-08-20, when the
   human moved the rigid weld into scope; the rest of this entry stands and the
   weld is in the checklist above. No `HingeConstraint`,
   `SpringConstraint`, `Motor6D`. The seesaw in the deliverable is a body
   resting on a fulcrum, not a hinge — and that is deliberate: `PivotOffset`
   already exists and it is *not* a constraint anchor.
2. **No `PivotOffset`-to-Jolt coupling of any kind.** Jolt has its own notion of
   where a body turns about (its centre of mass). `components.h` says this where
   the field is: joining them would make hinging a door change how it falls.
3. **No continuous collision detection tuning.** Jolt's default motion quality
   for our bodies, chosen once and recorded; no per-part `MotionQuality`
   property.
4. **No sleeping policy exposed.** Bodies sleep on Jolt's defaults; there is no
   `Sleeping` property and no `Sleep`/`Wake` methods.
5. **No mesh colliders beyond a convex hull.** `Enum.CollisionFidelity` exists
   in the enum list; M5 backs `Box` and `Hull` and reports what it did for the
   other two rather than pretending. A triangle-mesh collider is asset-pipeline
   work (M7).
6. **No physics materials beyond friction/restitution/density.** `Enum.Material`
   stays a look, not a physical property table.
7. **No multi-world.** `IPhysics3D` is multi-world by interface (architecture
   §2) and the engine creates exactly one.
8. **No job-system parallelism.** Jolt runs single-threaded this milestone; the
   seam is named and M7 wires it.
9. **No `saveState`/`restoreState` implementation.** The rollback-oriented seam
   (ADR 0016) is declared in the interface and refuses honestly — v1 does not do
   rollback, and a half-written snapshot would be worse than a refusal.
10. **No character animation.** The capsule is a capsule. Skeletal animation is
    M6.
11. **No Input Action System.** The keyboard scaffold is a scaffold; see
    Decision 7.
12. **No `Humanoid`.** api-design §2.7 divergence 13 already settled this:
    `CharacterBody` is one instance and a direct controller, and there is no
    `HumanoidRootPart`.
13. **No swimming, ladders, or climbing.** Slopes, steps and jump.
14. **No network or replication of physics state** (R15).
15. **No cross-platform determinism claim.** ADR 0025 level B only; the
    win↔linux comparison stays the tracked non-blocking job it already is.

## Gate checklist (verbatim from roadmap)

- [x] **determinism becomes blocking**: recorded 60 s input replay → identical
      final world hash across 3 runs in CI
- [x] physics tick budget for 1,000 active bodies recorded and regression-gated
- [x] a scripted bot replay drives the character up ramps and steps (functional
      gate)
- [x] collision-event conformance specs green

And the standing items every gate here carries: both tiers green through
`scripts/localgate.ps1`, the docs gate, the formatting gate, a screenshot of the
deliverable looked at *against the scene it describes*, and the defect register
in `docs/defects.md` reconciled.

## Build order

1. **Grounding pass and vendor Jolt.** `resolve` the pin, `sync`, wire the
   CMake, and answer from vendored source — quoting file and line — the three
   questions the gate depends on: what `JPH::PhysicsSystem::Update` guarantees
   about determinism under a single-threaded job system, what changes when a
   multi-threaded one is substituted (the M7 question the roadmap wants answered
   *now*), and which of Jolt's build-time switches we set and why. Every
   web-derived claim gets a row in `UNCONFIRMED.md`.
2. **`engine/physics`**: `physics_api` public headers (`IPhysics3D` as
   architecture §2 declares it), `physics_jolt` under `src/` with no public
   header of its own — the `rhi` pattern, ADR 0023. Tests at this level are
   about the *interface*: a body falls, a raycast hits, a character steps.
3. **Scene storage and the IDL.** The `BasePart` physics properties,
   `CharacterBody`, the two signals, the enums. Generated descriptors
   regenerate; nothing hand-edited.
4. **The mirror.** The system that makes an Instance a body and keeps the two in
   step, in both directions, at the two points in the tick where each is legal.
   This is the milestone's real content — Decision 3.
5. **Contacts → deferred signals.**
6. **Queries and collision groups.**
7. **`CharacterBody`.**
8. **The keyboard scaffold, and the replay input stream that feeds it.**
9. **Debug draw bridge.**
10. **`examples/03-physics-playground`, and the bot replay that drives it.**
11. **Gates, budgets, docs, ledger.**

## The decisions this brief makes

### 1. Vendoring Jolt is a row fill, not a new dependency

`third_party/manifest.json` has carried a `jolt` row at version 5.6.0 since
planning, ADR 0007 accepted it in words, and the commit field says
`TBD-AT-M0` — the placeholder `vendor.luau` treats as "declared, not yet
pinned", which the roadmap allows for a dependency a later milestone needs. This
is the third time that path is walked (xxhash at M2, meshoptimizer and fastgltf
at M4) and the rule is the same: filling a commit into a row whose *version* a
human already approved is vendoring, not adding. Anything else — a different
major, an extra dependency Jolt turns out to need, a licence surprise — stops
and asks (§10). M4 is why that sentence is here: fastgltf turned out to have a
mandatory dependency no document mentioned, and it downloaded it unpinned.

### 2. `physics_api` is header-only and `physics_jolt` has no public header

Straight from architecture §2 rule 1 and ADR 0023, and it is the shape `rhi`
already has: one directory, one include root, two targets, and the backend lives
entirely under `src/` so no other module *can* reach it whatever a layer table
says. `scene` (L3) may include `physics_api` (L2) and nothing else; only `app`
links `physics_jolt`. R17 is the reason: no `JPH::` type may appear in a
signature another module sees, and none may appear anywhere near the bindings.

### 3. The Instance tree is the authority; the body is a mirror of it

A `BasePart` under `Workspace` gets a body; one taken out of the tree loses it.
`scene` never learns a body's identity and `physics` never learns what an
Instance is: the mirror is one map, `InstanceId ↔ BodyHandle`, owned by glue
that sits above both.

Writes cross in both directions and each direction has exactly one legal point
in the frame:

- **Script → physics** is collected when the property is written and applied at
  the start of the sim tick. A script that sets `CFrame` mid-tick must not
  teleport a body the solver is in the middle of.
- **Physics → script** happens after `step`, as a **quiet write** with a batched
  changed-set (architecture §4). That is the path the 10k-parts benchmark
  measures, and using any other would silently forfeit the equality filter worth
  about a third of it.

An `Anchored` part is a Jolt *static* body, not a dynamic one with infinite
mass, and toggling `Anchored` recreates it. That is Jolt's model, and pretending
otherwise buys a solver that has to be told a mass is infinite every step.

### 4. `Touched` is a deferred signal over a contact *buffer*, not over Jolt's callbacks

Jolt reports contacts through listener callbacks that run *inside* `Update`, and
at M7 they will run on job threads. Nothing there may touch the world, allocate
a script value, or decide an order. So the listener does one thing — append to a
per-tick buffer — and `drainContacts` hands that buffer over after the step,
where the glue sorts it into a stable order and enqueues deferred signals
through the ordinary change queue (R8, R10). The sort key is the instance-id
pair, never arrival order, because arrival order becomes a job-thread artifact
the moment M7 wires the pool.

`TouchEnded` needs the *previous* tick's contact set, so the glue keeps two sets
and diffs them: entering fires `Touched`, leaving fires `TouchEnded`, staying
fires nothing.

### 5. `WorldHash` grows a physics term, and that is what makes the gate real

`World::worldHash` walks components today. A body's position lands in the
`PartComponent` it writes back to, so the hash already *moves* when physics
runs — but velocity and sleep state do not, and two runs agreeing on every
position while disagreeing on velocity are one tick from disagreeing on
everything. Architecture §9 already says the hash covers "sim-relevant
components **and physics state**". This is where the second half starts meaning
something.

### 6. The determinism gate gets an input stream, because "input replay" has to replay input

`ReplayScenario` today is a script and a tick count: the script *is* the input.
That was honest through M4 and it is not enough for a gate whose words are
"recorded 60 s input replay" and "a scripted bot replay drives the character up
ramps and steps". A bot that calls `Move` directly proves the physics is
deterministic and proves nothing about the path a keystroke takes to get there.

So a scenario grows an optional tick-stamped input stream, and in replay mode
the keyboard snapshot comes from that stream instead of a device. The bot then
drives the character through the *same* path a player does — the only version of
that gate worth having, and the machinery M6's "input replay of a full obby run"
needs anyway.

### 7. The keyboard scaffold is a `DevOnly` service, so its removal is structural

The roadmap asks for "minimal direct keyboard polling", deliberately replaced at
M6. ADR 0029 says the *only* input model is the Input Action System and there is
no `UserInputService`. Both are true at once only if the scaffold is visibly a
scaffold.

So it is a service tagged `DevOnly` — the tag that already means "compiled out
of shipping builds" — with one method, `IsKeyDown(Enum.KeyCode)`. A `DevOnly`
scaffold cannot reach a shipped game by accident, which is a stronger guarantee
than a comment promising removal. api-design §2.1's list gains it with its
expiry in its own `Doc`, and M6's migration gate item is what deletes it.

`platform::Key` grows from "the fourteen keys the engine itself reacts to" to
the letters, arrows, space and modifiers a character needs. `Enum.KeyCode` is
already on api-design's v1 enum list, so this is that enum arriving, not a new
one being invented.

### 8. The character is a `CharacterVirtual`, and it is not a rigid body

`CharacterBody` extends `BasePart` in the API and does **not** become a rigid
body. Jolt's `CharacterVirtual` is not a body in the physics system: it sweeps
its own shape, resolves its own contacts, and moves at a velocity the game sets
rather than one the solver produces. The other common approach — a dynamic body
with rotation locked — gets you a capsule that tips on a step and a permanent
argument with the solver.

The consequence to state loudly: **things collide with the character; whether
the character pushes them back is a separate wiring decision**, and which way it
went is recorded in the Findings rather than left for a player to discover.

### 9. `FixedTimestep` becomes writable at a safe point, never mid-tick

api-design §2.1 already says it: read-only until M5, and from here a write takes
effect at the next `FrameStart` safe point. The accumulator, the timer wheel and
the physics step all read it, and changing it between two of those reads inside
one frame is a class of bug worth designing out rather than debugging.

### 10. The physics budget is three numbers, and they are measured rather than estimated

The roadmap is explicit that one number says a budget was missed and three say
which stage missed it. Broadphase, narrowphase and solver are the three, timed
around the phases themselves. The baseline goes in `docs/perf-baselines.md`
beside M4's frame times, under the same >10% regression rule.

## Subagent plan

Per §7, fan out only where the interface is frozen first and the seam is not the
thing being designed.

- **Orchestrator only** — the seams and everything touching two of them: the
  `IPhysics3D` interface, the Jolt vendoring and CMake, the Instance↔body
  mirror, the tick integration, `WorldHash`, the replay input stream, every gate
  run and every merge.
- **Candidate fan-out, each after its interface exists and compiles:**
  1. the Jolt grounding pass — a research verifier reading vendored source and
     quoting file and line, writing no engine code;
  2. the query family (`Raycast`/`Spherecast`/`GetBodiesInBox` and the two
     datatypes), once `IPhysics3D` is frozen;
  3. the debug-draw bridge, once `IPhysics3D` and the debug renderer both exist;
  4. the conformance specs, written from `api-design.md` alone and forbidden
     from reading the implementation (§7);
  5. the adversarial review of the whole diff, briefed to attack R10, R17, R4
     and R3 by number.

## Attempted / abandoned

- **A per-phase physics breakdown from Jolt's own profiler.** The roadmap asks
  for broadphase / narrowphase / solver, and Jolt has all three — inside a
  profiler whose aggregation is private and whose only output is a file
  (`Jolt/Core/Profiler.h:120-175`). The alternative, `JPH_USE_EXTERNAL_PROFILE`,
  replaces the measurement class in EVERY scope of the library in EVERY
  configuration, shipping included. Abandoned in favour of the three stages this
  seam can separate, with the roadmap note amended and the reason recorded as
  U-56 rather than left as an unmet "should".
- **A hash map from instance to body record.** Correct and 214 ns per body per
  tick, most of it missing cache on a key the pool walk already had. Replaced by
  a slot-indexed vector; `apply` went from 2.27 ms to 1.60 ms over ten thousand
  bodies and every determinism trace stayed byte-identical, which is the proof
  that it changed a data structure and not a simulation.

## Findings

Things the documents assumed that reality corrected, in the order they cost
time.

1. **Physics arriving changed what every scene already in the repository
   meant.** An unanchored `BasePart` is a rigid body, and every example, capture
   fixture and benchmark scene was written before that was true — so on the first
   green build, `examples/02-meshes` rained its scenery and `churn10k` became a
   ten-thousand-body physics benchmark under a name that says property churn.
   The fix is one line per scene, and the discipline is in which line: scenery
   says `Anchored = true` because it is scenery, and the proof that this is inert
   rather than a re-record is that `capture_gate_meshes` passes against the
   **unchanged** M4.5 golden.

2. **A gate that can pass while doing nothing, the twelfth in six milestones,
   and the first caught in the session that wrote it.** A syntax error in a new
   conformance spec logged one line and the runner reported "938 passed, 0
   failed" over a suite that had just lost seventeen cases. It was noticed only
   because the case count did not move when a file was added. The run now fails
   on a load failure, verified by breaking a spec on purpose.

3. **The debug-draw bridge found a defect on the frame it first drew.** The
   character's collider capsule floated half a body above the character's own
   box, because `CharacterBody` was the one `BasePart` whose `Position` meant its
   feet rather than the middle of its `Size`. Every test passed. The character
   walked, climbed and jumped. Nothing but a picture of the two things at once
   could have shown it — which is what the roadmap means by "the only picture
   that can disagree with the rendered one".

4. **The Linux tier found two defects MSVC does not have the diagnostics for**,
   and one of them was an ABI mismatch rather than a warning: Jolt compiled
   `-fno-rtti` emits no typeinfo, so a debug-draw bridge deriving from one of its
   classes fails to link — invisible on MSVC, which emits RTTI data per
   translation unit. The other was `-Wformat-nonliteral` on a trace callback.

5. **Three test cases in a row failed by measuring after the thing they were
   testing.** A character walking at 4 m/s crosses a six-metre ledge in a second
   and a half, so a check taken at the end reads "never climbed" and means
   "climbed and kept going". The same shape appeared in the seam test, the
   conformance spec and the gate scene. The rule that came out of it: **a check
   on a moving thing names a window, not a moment.**

6. **A sleeping contact is not an ended contact.** Jolt stops calling the
   contact listener for an island it has put to sleep, so a diff of this tick's
   pairs against last tick's fires `TouchEnded` for a crate that is still visibly
   resting on the floor — and `Touched` again the moment anything nudges it. The
   pair is carried forward when both bodies are inactive. Found by a test that
   waited four seconds instead of two.

7. **Jolt's determinism does not depend on thread count, and three other things
   do.** Upstream's own documentation (`Docs/Architecture.md:787-807`) lists two
   conditions for a deterministic simulation and thread count is not one of them
   — but contact-callback order, the active-body list's order and a query's hit
   order all are. That is the M7 answer the roadmap wanted before the gate
   hardened: recorded hashes survive the job system provided nothing observable
   is derived from those three orders, which is why they are sorted now, under a
   single-threaded job system where none of it matters yet.

8. **`Enum.X` inside a composite type reached the definitions verbatim**, where
   `Enum` is a value rather than a namespace of types — and luau-lsp answers that
   by refusing to read the whole file, so every global in the repository became
   unknown at once. One `RaycastParams.new` parameter did it. The generator now
   substitutes inside composite types rather than only matching whole ones.

9. **A `Part` still renders as a wireframe box**, because only a `MeshPart`
   reaches the solid renderer. True since M2, unchanged by M4, and found by
   looking at the first screenshot of a physics playground made entirely of
   primitives. Recorded as D022 and scheduled with M7.5 rather than fixed here:
   the fix moves every render golden, which makes it a milestone's work.

10. **A range refusal reports the key for a type.** `FixedTimestep = 1/10`
    raises "it takes a number" about a number, because a setter answers with a
    bool and `PropertyDesc` carries one key per value type. Recorded as D021;
    every M5 property with a range is affected.

11. **`-text` did not stop `eol` from rewriting a vendored tree.** Git documents
    `eol` as implying `text`, so the `*.bat text eol=crlf` rule kept applying
    inside `third_party/` with `text` unset — and Jolt ships twenty-three `.bat`
    files. The committed blobs were already correct; what was wrong was the
    working tree a fresh clone would produce. Same class as M4's `core.autocrlf`
    finding, found the same way: by reading a warning instead of ignoring it.

12. **A gate script with no execute bit passes locally and fails on CI.**
    `localgate.ps1` invokes every gate script as `bash <script>`, which does not
    care about the mode; `ci.yml` runs them as programs. The new formatting gate
    was committed 100644 while the three that predate it are 100755, and both
    local tiers were green. Recorded as D023, and it is the third defect in two
    milestones that only CI could see — the other two were a transitively
    included header and an unauthenticated API quota.

13. **One thing recorded and not explained.** The `character` scenario's tick-0
    hash moved once between two builds inside this milestone while `churn` and
    `example01`'s did not — and those two are byte-identical to their M4.5
    goldens today, which is what says the engine's boot path did not move. The
    scenario is new this milestone, its trace is recorded at the end of it, and
    three fresh processes plus two in-process runs agree. Written down rather
    than smoothed over: if it happens again, this is the precedent.

## Gate Record

Filled 2026-08-20, before human review. Every command below was run on the
reference machine recorded in `docs/perf-baselines.md`; the Linux rows come from
the Tier-2 container `scripts/localgate.ps1` builds.

### 1. Determinism becomes blocking — 60 s input replay, identical across 3 runs

```
$ luaug-host --replay=tests/determinism        (three fresh processes)
[info] Replay character: 3600 ticks, hash beefd65851f1f593, reproduced.
[info] Replay character: 3600 ticks, hash beefd65851f1f593, reproduced.
[info] Replay character: 3600 ticks, hash beefd65851f1f593, reproduced.
```

3,600 ticks is sixty seconds at the default 1/60. **The input is recorded** —
`tests/determinism/character/inputs.txt`, one line per key transition — and in
replay mode the keyboard snapshot comes from it rather than from a device, so
what is replayed is a keystroke's whole path to the character rather than a bot
calling `Move`. That is the difference the gate's own wording asks for.

"Three runs" maps onto architecture.md §9's design rather than onto three
identical invocations: each run compares the scenario against itself twice
in-process (which catches leaked global state) and against the recorded trace,
and the process is fresh every time CTest starts it (which is the cross-process
leg). The three lines above are three fresh processes on top of that.

`ctest -R determinism` is green on both tiers, against per-tier traces —
win↔linux hash equality is ADR 0025's level C and remains a tracked
non-blocking aspiration, not a gate.

### 2. Physics tick budget for 1,000 active bodies, recorded and regression-gated

```
$ luaug-host --bench=tests/bench --bench-repeats=5      (median of 5, three times)
[info] Bench physics1k: 1007 instances, 300 ticks, 2.0447 ms/tick mean, 4.7405 ms worst.
[info] Bench physics1k physics: 1001 bodies, 0.0240 ms apply, 1.7989 ms step, 0.2178 ms writeback (per tick).
```

**2.02 ms per tick for 1,001 bodies against a 16 ms budget**, recorded in
`docs/perf-baselines.md` under "M5 — the world gets mass" and gated by
`ctest -R perf_budget` on both tiers.

*Active* is the load-bearing word: the scene is twenty-five towers of forty
crates, because a thousand crates spread on a floor settle in under a second and
then measure a world where nothing happens.

The breakdown is **apply / step / writeback** rather than broadphase /
narrowphase / solver. The roadmap note is amended in place with the reason and
`UNCONFIRMED.md` U-56 records it: Jolt exposes that split only through a
profiler that dumps to a file and taxes every configuration to enable.

### 3. A scripted bot replay drives the character up ramps and steps

The same 3,600-tick recording, with seven assertions in the scene itself. They
are `error` calls, and the replay harness fails a run that logged one — so the
functional gate and the determinism gate are the same run, and a scenario that
climbed nothing cannot have its emptiness recorded as a golden.

| Tick | What the recording should have made happen |
|---|---|
| 130 | the character settles on the floor before the recording moves it |
| 220 | walking north climbs onto the kerb |
| 330 | and is stopped by the wall behind it |
| 372 | Space leaves the ground |
| 620 | walking south comes back down off the kerb |
| 940 | walking east climbs the ramp |
| 1200 | the crates have settled and gone to sleep |

All seven pass. Two of them earned their tick numbers the hard way — see
Finding 5.

### 4. Collision-event conformance specs green

```
$ luaug-host --run-tests=tests/conformance --rhi=null
[info] 966 passed, 0 failed, 966 total
```

966 cases, up from 903 at M4.5. The 63 new ones are six physics files:
`falling` (12), `touched` (6), `collision_groups` (8), `character` (9),
`queries` (17) and `welds` (11). `Touched`/`TouchEnded` are pinned at the edges
the document names: once per pair, once for each side, nothing while resting —
**including across the moment the simulation puts both bodies to sleep** — a
non-collidable part still reporting a touch, and a destroyed part firing no
`TouchEnded`.

Green on both tiers through `luaug test` as well as through `ctest`.

### 5. The standing items

```
$ scripts/localgate.ps1
  ok    docs (4.3 s)
  ok    luau (2.6 s)
  ok    format (7.5 s)
  ok    windows (26.0 s)
  ok    linux (32.4 s)
green (macOS is Tier-3 and only CI can build it)
```

- **27/27 CTest on Windows, 26/26 on Linux** (the pixel golden is
  `-LE gpu-golden` there).
- **`capture_gate_meshes` passes against the UNCHANGED M4.5 golden**, which is
  the evidence that physics arriving is inert for a scene whose scenery says it
  is scenery. Nothing was re-recorded to make a render gate pass.
- **Both determinism traces for `churn` and `example01` are byte-identical to
  their M4.5 recordings**, before and after the mirror's data-structure rewrite.
- **The formatting gate covers new files now.** It did not when this milestone
  started; it reported 205 files green while a fourteen-hundred-line new module
  sat beside them unchecked.
- **The defect register is reconciled**: D016 closed with the commit that fixed
  it, D021 and D022 opened with milestones attached.

### 6. The picture, looked at against the scene it describes

`docs/images/m5-playground.png` — `examples/03-physics-playground` at frame 400,
1280×720, with `DebugService:ShowPanel("Physics")` on so the solver's own
wireframe is over the top of the rendered world.

What it shows, checked against what the script asks for: three towers of six
crates standing where they were stacked; the seesaw's plank resting on its
fulcrum with the weight on one end; the two ramps; the character's blue box with
its green collider capsule **inside it** rather than floating half a body above
it, which is the defect this picture found (Finding 3); and the welded marker
held over the capsule's head by a `Weld` rather than by a per-frame handler.

**Everything is a wireframe**, and that is D022 rather than a physics failure: a
`Part` has no geometry of its own in this release, only a `MeshPart` reaches the
solid renderer, and the boxes are exactly where the bodies are. It is scheduled
with M7.5, which owns how this engine renders, and the example's README says so
where somebody running it will read it.

Frame time for the deliverable at 1080p: **median 1.11 ms, worst 1.93 ms**
(0 draws — see above), recorded in the baselines table.

### 7. What a reviewer should know before signing

- **`Enum.Material` and `BasePart.Material` are not in M5**, and neither is
  `RaycastResult.Material`. They are a surface look rather than rigidbody state,
  nothing reads them, and a type-checked no-op looks more like a working API
  than a missing member does. api-design.md §2.2 says so in the class tree.
- **`Enum.CollisionFidelity` round-trips and every value collides as a box.**
  `MeshPart` geometry lives in `render`, which sits above the mirror, so there is
  no hull to build from yet — the property reads back what was written and the
  gap is stated in `physics_sync.cpp` where the shape is chosen. M7's asset
  pipeline is where a hull comes from.
- **The physics mirror costs ~160 ns per body per tick to decide nothing
  changed.** Two cheap wins were taken; what remains is a dirty-flag design that
  belongs with M7's streaming. It is recorded in the baselines rather than left
  to be discovered.
- **`KeyboardService` is a scaffold with an expiry**, tagged `DevOnly` so it
  cannot reach a shipping build, and migrating the deliverable off it is an M6
  gate item.
- **`churn10k` reads 4.96 ms where M2 recorded 2.02, and I am calling that a
  changed measurement rather than a regression — which is a judgement worth a
  reviewer's word.** MASTER_PROMPT §8 blocks a gate on a >10% regression against
  the recorded baseline unless a human-approved ADR accepts it. The argument for
  "not a regression": the benchmark's scene now contains ten thousand rigid
  bodies where it contained none, so it is not the same measurement under the
  same name — the same reasoning M4.5 used when it marked M4's frame times
  superseded. The argument against: the name is the same, the budget is the same,
  and a number that trebles is a number that trebles. It is under its 16 ms
  budget either way, the physics half of it is itemised in the baselines table,
  and the remaining fix is named and scheduled. If the answer is "that is a
  regression", the ADR is a paragraph and the work is the dirty-flag design.
- **Nothing here is tagged.** Per MASTER_PROMPT §6 the milestone is complete when
  the human says so in words; `PROGRESS.md` records it as awaiting review and
  `milestone/m5` does not exist yet.
