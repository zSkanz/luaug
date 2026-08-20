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

- [ ] Jolt 5.6 on the fixed tick, single-threaded first; the job-system seam
      named but not wired (M7 wires it)
- [ ] rigidbody/collider state as `BasePart` properties per `api-design.md`
- [ ] collision events as deferred signals (`Touched` / `TouchEnded`)
- [ ] raycast / shapecast API (`Workspace:Raycast`, `:Spherecast`,
      `:GetBodiesInBox`, `RaycastParams`, `RaycastResult`)
- [ ] Jolt debug-draw bridge
- [ ] `CharacterBody` on Jolt `CharacterVirtual`: capsule, slopes, steps, jump
- [ ] third-person follow camera
- [ ] minimal direct keyboard polling — deliberately replaced in M6 by the
      Input Action System (that migration is an M6 gate item)
- [ ] **`Weld` and `WeldConstraint`** — added to the roadmap 2026-08-20 by human
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
- [ ] `PhysicsService.FixedTimestep` becomes writable; collision groups
      (`RegisterCollisionGroup`, `CollisionGroupSetCollidable`,
      `GetRegisteredCollisionGroups`)
- [ ] the tick budget is recorded **broken down** — broadphase, narrowphase,
      solver — because one number says a budget was missed and three say which
      stage missed it
- [ ] the grounding pass that vendors Jolt answers, in `UNCONFIRMED.md`,
      whether recorded hashes survive Jolt's job system being wired at M7 —
      **before** the gate hardens, not after it breaks
- [ ] `examples/03-physics-playground`

### Carried debts this milestone pays

- [ ] **D016 — `BindToClose` has no capped grace period.** Scheduled here by the
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

- [ ] **determinism becomes blocking**: recorded 60 s input replay → identical
      final world hash across 3 runs in CI
- [ ] physics tick budget for 1,000 active bodies recorded and regression-gated
- [ ] a scripted bot replay drives the character up ramps and steps (functional
      gate)
- [ ] collision-event conformance specs green

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

(append during the milestone)

## Findings

(append during the milestone — the things the docs assumed that reality
corrected)

## Gate Record

(filled at milestone end, before human review)
