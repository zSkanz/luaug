# M2 Kickoff — Kernel: Instances over ECS, Scheduler, Signals, task

- Started: 2026-08-19
- Roadmap section: docs/roadmap.md#m2--kernel-instances-over-ecs-scheduler-signals-task-xl--the-most-consequential-milestone

## Goal (restated)

M0 proved a sandboxed VM can run. M1 proved pixels can come out and that the
agent can see them. M2 is the part that makes LuauG an engine rather than a
demo: a script writes `Instance.new("Part")`, sets `.Parent`, connects
`ChildAdded`, waits a tick, and every one of those words means something
precise, repeatable, and fast.

Concretely: a hand-rolled sparse-set ECS underneath, a Roblox-shaped Instance
facade on top, a deferred-only signal system with ordering that is *written
down before it is implemented*, a `task` library resumed by the fixed-tick
scheduler M1 already built, and a world-state hash that proves the whole thing
does the same thing twice.

Everything after M2 is a client of M2. That cuts both ways: the leverage is
enormous, and so is the cost of getting a semantic wrong and discovering it in
M6. So this brief spends its length on the seams and on what is deliberately
*not* here.

## Scope checklist (from roadmap)

- [x] ECS storage + component registry
- [x] Instance facade: `Instance.new`, properties, `Parent`/children,
      `FindFirstChild` with duplicate-name support (ADR 0026), attributes, tags
      — reachable from Luau; `WaitForChild` waits on `task`
- [ ] DataModel + services skeleton (`game:GetService`) — the next step, and
      what `WaitForChild` and the 19 unbound service methods are waiting on
- [x] Deferred-only signal implementation with **documented** ordering semantics
      — api-design §3.1 written first, then implemented against it
- [x] `task` library (`spawn`/`defer`/`delay`/`wait`/`cancel`) on the fixed-tick
      scheduler with documented resumption points — deadlines in integer ticks,
      one `(deadline, sequence)` FIFO
- [ ] Script host: lifecycle, `require` resolution per `.luaurc`, per-script
      sandboxing
- [x] Seeded deterministic RNG service — the `Random` datatype (Decision 4),
      with an omitted seed drawn from the world's own stream rather than a clock
- [ ] World-state hash function + record/replay harness v1
- [ ] Frame-budget instrumentation
- [ ] The 10k-parts / 1k-listeners property-churn benchmark with a CI threshold
- [ ] `examples/01-instances` — a Luau script builds 500 instances,
      parents/reparents, connects deferred signals, animates via Heartbeat, all
      visualized with debug draw

Two items are **added** to the roadmap list, and neither is new scope invented
here — both are things M2's own gate or M1's ledger already demand:

- [x] **The API definition IDL and its C++ + `.d.luau` generators.** See
      Decision 1 and Decision 12 below. Extended to emit the enum tables and
      their ids, because an item's numeric value is what a property write
      validates and what a snapshot stores.
- [ ] **`render::extract` / `RenderWorld` (ADR 0027)** — carried forward from
      M1, where it was marked `[~]` for exactly one reason: it is the POD
      snapshot extracted from `scene`, and `scene` did not exist. It does now.

## The decisions this brief makes

These are the calls that shape every line of M2. Each states the alternative it
rejected, because a decision without a rejected alternative is a preference.

### 1. The API IDL ships in M2, and generates C++ first

`architecture.md` §4 is unambiguous: "All descriptors are **generated** from the
API definition IDL." The roadmap schedules *typedef* generation in M3. Those are
compatible only if M2 builds the IDL and the C++ generator, and M3 adds the
editor-facing artifacts.

So M2 ships `api/schema.luau`, `api/defs/*.api.luau`, and
`api/generator/gen_cpp.luau` (a Lute app), emitting the `ClassDescriptor` /
`PropertyDesc` tables `scene::ClassRegistry` consumes.

**Generated C++ is checked in**, and a gate re-runs the generator and diffs —
the same "generated, checked in, CI verifies freshness" pattern
`architecture.md` line 48 already specifies for `runtime/types/`. This keeps
Lute out of the C++ build, which matters: the CI build job has no Lute, and
adding one would put a Luau interpreter on the critical path of every compile.

*Rejected:* hand-write the descriptors now and generate them in M3. That makes
the IDL a documented lie for a milestone, doubles the authoring cost of every
class, and guarantees the generator is eventually written to match whatever the
hand-written tables drifted into — the tail wagging the dog.

*Risk:* this is "build the compiler first" on the critical path. Mitigated by
scoping the generator to exactly what M2 needs, while the *schema* carries the
fields M3+ will want (`docKey`, `threadSafety`, `yields`) so the def files are
written once.

### 2. Where the modules sit

| Module | Layer | M2 responsibility |
|---|---|---|
| `core` | L0 | `SlotMap`, `NameAtom` interner, `Pcg32`, `InstanceId`, `Phase`, xxh3 |
| `scene` | L3 | ECS World, `ClassRegistry` + generated descriptors, Instance facade, hierarchy + child-name index, attributes, tags, deferred change queue, `WorldHash` |
| `script` | L5 | The VM, sandbox, bindings, datatypes, `task`, `Signal`/`Connection`, require, the signal drain |
| `app` | L6 | `FrameScheduler` resumption points, DataModel/service wiring, headless run modes, replay driver, `--run-tests` |

`engine/app/src/script_host.*` **moves into `engine/script/`**. It was in `app`
in M0/M1 because there was no `script` module; there is one now, and leaving it
would put Luau headers in L6 where every module can see them.

`jobs` (L1) is **not** built in M2. Nothing in scope has a parallel producer, and
the deterministic-commit pattern (R10) has nothing to commit yet. An empty
worker pool is not a foundation, it is an unused dependency.

### 3. The scene↔script seam: `scene` never holds a `lua_State`

This is the single most important seam in the milestone, so it is stated
explicitly rather than discovered during implementation.

Signal *arguments* can be arbitrary Luau values. Connections hold Luau
functions. If `scene` owned the signal queue, `scene` would hold Luau
references, and L3 would depend on the VM — breaking architecture §2 rule 2 and
making the ECS un-snapshottable.

So the queue is split by what it carries:

- **`scene` produces POD facts**: `{ChildAdded, parentId, childId}`,
  `{PropertyChanged, instanceId, propertyAtom}`, `{Destroying, instanceId}`.
  No Luau values, fully snapshottable, meaningful in a headless world with no VM.
- **`script` owns the drain**: it consumes those facts, plus its own
  `Signal.new()` fires (which *do* carry Luau values), resolves connections, and
  resumes each handler on its own coroutine.

`app`'s `FrameScheduler` calls the drain at each resumption point. One drain, two
producers, and `scene` stays a data structure.

### 4. "Seeded deterministic RNG service" means the `Random` datatype

The roadmap says *service*; `api-design.md` §2.1 lists the complete v1 service
set (14 + 1 dev-only) and there is no RNG service in it, while §2.3 specifies the
`Random` datatype with `Random.new(seed?)` and deterministic streams. §4 of the
master prompt makes `api-design.md` the authority for the Luau-facing surface.

M2 therefore ships `core::Pcg32`, the `Random` datatype in `script`, and a
World-owned default stream seeded from the world seed. **No new service.** This
is an interpretation of two documents that already agree on substance, not a
scope change — but it is written here so the next session does not re-litigate
it.

### 5. `ScriptService` is missing from `api-design.md` §2.1 — that is a doc bug

§3 requires `game:GetService("ScriptService")` as the mount point for every
entry Script. §2.1's service list does not contain it. Per master prompt §5
(docs follow reality), the row is added to §2.1 in the same commit that mounts
scripts. The service was already specified; only the list was stale.

### 6. `BasePart` ships its structural half only

M2 declares `CFrame`, `Position`, `Orientation`, `Size`, `Color`,
`Transparency`, and `Part.Shape`.

**Amended after the conformance authors ran (gap G58):** `PhysicsService` *is*
in scope, carrying `FixedTimestep` and nothing else, read-only for now. The
original text excluded the whole service, which was wrong — `FixedTimestep` is
the sim tick grid every timing guarantee in §3.2 rests on, and it is scheduler
state that happens to be spelled on a physics service. It becomes writable in
M5 with the rest.

M2 does **not** declare `Anchored`, `CanCollide`, `CanQuery`, `Material`,
`CollisionGroup`, `Friction`, `Restitution`, `Density`, `LinearVelocity`,
`AngularVelocity`, `ApplyImpulse`, `Touched`, or `TouchEnded`. Every one of those
is a promise about a simulation that does not exist until M5. A property that
accepts a write and silently changes nothing is precisely the green-but-broken
failure the roadmap's screenshot rule was written to prevent — and it is worse
here, because a *type-checked* no-op looks like a working API.

The line: properties that **describe the object** ship now; properties that only
mean something to a physics engine ship with the physics engine.

### 7. `CFrame` carries the f64 translation from the first commit

ADR 0014 makes `CFrame` the world-precision source of truth. Floating origin is
M7, and none of it is built here — but the *storage* is f64 from day one.
Widening a transform type after four milestones of code has consumed it is a
different and much worse project than starting wide.

### 8. Enums: the machinery in full, the values on demand

The `Enum` namespace, `EnumItem` (`Name`/`Value`/`EnumType`), `GetEnumItems`, and
the generator's enum emission are complete M2 scope. The *values* are only those
M2's own surface uses: `PartShape`, `RotationOrder`, `LogLevel`, plus
`RunContext` declared-and-reserved per api-design. Every other enum arrives with
the milestone that gives it meaning.

### 9. The host learns what a project is

M2's script lifecycle (api-design §3) mounts `src/scripts/**/*.luau` as `Script`
instances with subdirectories as `Folder`s. That needs a project root.

`luaug-host <path>`: a **directory** is a project root and gets the full mount;
a **file** is mounted as a single `Script`, which is what M0's and M1's tests
already do and will keep doing. `luaug.toml` parsing beyond what this needs is
M3, with the CLI.

### 10. Ordering semantics get specified before they get implemented

The gate says "deferred-only signal implementation with **documented** ordering
semantics". Today the documents say deferred-only (ADR 0015), drained at
resumption points with a re-entrancy cap of 10 (architecture §3) — and stop.
They do not say whether the drain is FIFO across all signals or per-signal,
whether connection order is guaranteed, where a signal fired *during* a drain
lands, what `:Once` does if the signal fires twice before the drain, or what
`Destroy` during iteration means for a queued fire.

Those answers are the contract the conformance specs are written against, so
they must exist in `api-design.md` **first**. Writing them is orchestrator work
and it blocks the spec authors. It is the first implementation task of the
milestone, ahead of any code.

### 11. `xxhash` gets vendored

`WorldHash` is specified as xxh3 (architecture §9). `third_party/manifest.json`
has carried an `xxhash` row since the planning session, at `TBD-AT-M0`, and M0's
roadmap scope explicitly permits vendoring lazily "as needed by later
milestones" provided the row exists.

So this is filling in an approved row's SHA, not adding a dependency, and not a
§10 escalation. It is flagged here anyway because it is the one action in this
brief a human might want to have seen — and it is a one-commit revert if so.

### 12. `gen_dts.luau` is pulled forward from M3 into M2

M2's gate requires "zero `luau-analyze` errors under the new type solver across
all example/spec code". Roughly a hundred spec files cannot type-check against an
engine API with no declarations, and M1's hand-written `preview.d.luau` stopgap
does not scale past four functions.

The IDL exists in M2 (Decision 1), and emitting `declare extern type` from it is
a second small generator over the same data. So M2 ships `gen_dts.luau`. M3 keeps
what it was actually about: the docs JSON, the api-dump, `luaug setup`, and the
luau-lsp plumbing in the project template.

This is a pull-forward forced by M2's own gate, not scope creep — M2 cannot pass
without it.

### 13. One `TAG_INSTANCE`, with the class resolved from the id

Forced by grounding finding U-17: architecture §5 asks for one instance tag
*and* per-class metatables, and Luau allows exactly one metatable per tag,
refusing reassignment outright. One of the two has to go.

**One shared tag wins**, and the class is resolved at dispatch time from the
`InstanceId` through the `ClassRegistry`. Three reasons, in ascending order of
how hard they are to argue with:

1. There are 128 host tags in total, shared with every datatype — `CFrame`,
   `Color3`, `Random`, `Signal`, `Connection`, `Vector2`, `UDim2`, `EnumItem`
   and the rest. Spending one per Instance class caps the class count somewhere
   around a hundred, which is not a budget an engine can live inside.
2. Identity already depends on it: the per-VM weak-valued cache that guarantees
   `a == b` for the same instance is keyed by `InstanceId`, not by class.
3. **The VM's own dispatch cache requires it.** `cachedslot` lives in the
   bytecode instruction, so it is per *callsite* and shared by every tag that
   flows through that callsite — upstream's header says so in capitals. A slot
   number that is a pure function of the atom is safe; a per-class numbering
   mis-dispatches the moment a callsite sees two classes. One tag with one
   global atom→slot table is not a compromise here, it is the shape the cache
   was designed for.

The cost is one indirection per property access — id → `ClassId` → descriptor —
before the atom switch, on a path that already touches the component store.

`architecture.md` §5 is corrected to match, and this is what the generated C++
targets.

## NOT in scope

Written explicitly so no later session has to guess whether an omission was a
decision or an oversight.

- **Physics of any kind.** No Jolt, no collision, no `Touched`, no raycasting,
  no `PhysicsService`. M5. (Decision 6.)
- **Rendering beyond debug draw.** No meshes, materials, lights, shadows, or
  `Camera` instance. The example is visualized with the M1 debug-draw path. M4.
- **UI, input, audio, tweens, animation.** M6.
- **Streaming, floating origin, chunks, the asset pipeline.** M7. `CFrame` is
  f64 (Decision 7); nothing rebases.
- **The `luaug` CLI, hot reload, `luaug test`, `luaug check`.** M3. M2 ships the
  *engine* side of the conformance runner (`--headless --run-tests`); the CLI
  that wraps it does not exist yet.
- **`jobs`.** (Decision 2.)
- **Actor VMs and parallel handler execution.** The `Phase` enum reserves the
  parallel windows and the thread-safety annotations are carried through the
  IDL, but v1 executes serially on the game VM (architecture §3).
- **`@std` modules.** ADR 0030's convergence surface is not M2 scope; `require`
  resolution is (roadmap), the library set it resolves *to* is not.
- **Scene serialization / save files.** Deliberately deferred for v1
  (api-design §10.6).
- **Cross-platform determinism.** ADR 0025 level C is explicitly not a v1
  guarantee; M2 builds the level-B harness only.
- **Anything on the R15 list.**

## Subagent plan

The master prompt §7 forbids fanning out on "scheduler/kernel work" and
"anything touching two modules' seams". M2 *is* the kernel — so the split below
is deliberate about what can genuinely be parallel and what only looks like it.

**Fans out** (interfaces frozen by the orchestrator before any agent starts):

1. **Vendored-source grounding (5 agents, complete).** Luau's userdata/atom
   binding API, coroutine/sandbox/error API, the require vtable and `.luaurc`
   reality, allocator/GC/compile options/vectors, and this repository's own
   module/test/i18n conventions — each quoting file and line at the 0.734 pin,
   per master prompt §9. Findings below.
2. **`core` primitives.** `SlotMap`, the `NameAtom` interner, `Pcg32`, the xxh3
   wrapper, `Phase`. Self-contained, doctest-covered, no seams.
3. **The IDL, schema and generators.** After the C++ descriptor structs are
   frozen: `api/schema.luau`, the def files, `gen_cpp.luau`, `gen_dts.luau`, and
   the naming lints from api-design §9.
4. **Conformance spec authors (5 agents).** Sliced by area — Instance tree,
   signals and ordering, `task`, attributes/tags/services, datatypes. Per §7
   these agents are **forbidden from reading `engine/`**: they write from
   `api-design.md` alone, which is the only thing that keeps the spec a contract
   rather than a transcript of the implementation.
5. **`@luaug/testing`.** The `describe`/`it`/`expect` library, strict Luau.
6. **The determinism/replay harness and the churn benchmark**, once `WorldHash`
   exists.
7. **Adversarial reviewers**, on every substantial kernel diff, briefed to attack
   R10 determinism, R4 sandbox, R3 i18n leaks, R17 backend leaks, and drift from
   `api-design.md`, citing rule numbers.

**Orchestrator-only, single-threaded:** the ECS `World`, the Instance facade
semantics, the signal queue and its drain, `task` and the scheduler resumption
points, the script bindings and per-script sandbox, the scene↔script seam, every
merge, and every gate run.

## Gate checklist (verbatim from roadmap)

- [ ] conformance suite (~100+ Luau specs) covering signal deferral ordering,
      task semantics, tree mutation edge cases (destroy during iteration,
      reentrancy limit 10, duplicate-name FindFirstChild) — specs are written
      against `docs/api-design.md`, NOT against the implementation
- [ ] determinism: same script+seed twice → identical hash after 10,000 ticks
- [ ] 500-instance scene ticks under budget (baseline recorded)
- [ ] zero `luau-analyze` errors under the new type solver across all
      example/spec code

## Entering risks

1. **The generator is on the critical path.** Nothing that needs a class
   descriptor can be written until it emits one. Mitigation: the C++ structs are
   frozen by hand first, the generator is written against them, and a single
   trivial class round-trips end to end before the def files are filled in.
2. **`require` per `.luaurc` may not be free at this pin.** The vendored
   `Require` library provides the navigator vtable; whether a runtime-usable
   `.luaurc` reader ships with it is a question the grounding agent was sent to
   answer. If it does not, M2 either implements the config reader over
   `core::json` (which exists, ADR 0033) or the scope bullet narrows — the latter
   is a §10 escalation and will be raised as one.
3. **A hundred specs written from the doc will find doc holes.** That is the
   design, not a failure mode; but it means `api-design.md` edits are expected
   throughout the milestone and each needs the §5 treatment (ADR when it is a
   decision, doc edit in the same commit either way).
4. **Instance-facade overhead** is architecture risk #1. The quiet-write +
   subscription-bitmask path is not an optimization to add later; the benchmark
   exists in this milestone precisely so it cannot be deferred into one.
5. **Scope pressure.** M2 is 18% of the project and this brief has already added
   two items to it. If the milestone has to be split across sessions — it will —
   the split points are: (a) after `core` + the IDL round-trip, (b) after the ECS
   and Instance facade with C++ tests only, (c) after signals + `task`, (d) after
   the specs, (e) the example, harness and benchmark.

## Attempted / abandoned

(append during the milestone; §12 of the master prompt)

## Findings

*(the things the docs assumed that reality corrected)*

**1. The gate's word "documented" was doing more work than anyone had done.**
"Deferred-only signal implementation with documented ordering semantics" looked
satisfied by ADR 0015 plus architecture §3. It was not: nothing answered one
queue or many, whether a connection made after a fire runs for it, whether
`Disconnect` is reliable mid-drain, what `:Once` does when a signal fires twice
before a drain, or what `Destroy` does to fires already queued. Those are the
first five questions a conformance spec asks. Writing api-design §3.1 had to
come before any code, and it blocked the spec authors until it existed.

**2. Five authors writing from the document alone found 125 holes in it.**
That is the mechanism working exactly as MASTER_PROMPT §7 intends — they were
forbidden from reading `engine/`, so every ambiguity became a question instead
of a silent copy of whatever the implementation would have done. Two were
outright contradictions between documents:

- **The precision claim was arithmetically false.** api-design said f32 is
  "exact to ~±131 km" and ADR 0013 said "millimeter-exact within ±131 km". An
  f32 ULP at 131072 m is 1/64 m ≈ 15.6 mm; millimetre exactness ends around
  8 km. Corrected in both, with a dated addendum on the ADR.
- **`Enum.RunContext` was declared in two directions**, present in §2.3's v1
  enum list and absent from §2.1's reserved-names list.

**3. Two independent authors guessed opposite answers to the same question**,
which is the clearest possible signal that the document was silent: does writing
a property the value it already holds enqueue a change? Ruled equality-filtered
— `GetPropertyChangedSignal` is a past-tense fact about a *change*, and an
unconditional enqueue makes the 10k-parts benchmark pathological for no semantic
gain.

**4. The re-entrancy cap has to cover `task.defer`, or it is not a cap.** The
task author read §3.1 as applying the depth rule to fires only and wrote a spec
asserting a twelve-generation defer chain completes. If that reading were right,
a self-deferring callback would be an unbounded drain — a hang, not a wrong
number. The cap covers everything on the queue.

**5. The R8 removals cannot be tested from inside Luau.** Naming an undeclared
global is a strict-mode analyzer error, so a spec cannot legally reference
`wait` to prove it is absent, and casting through `:: any` to get around that
tests the cast rather than the VM. Absence is a VM-level property and belongs in
the C++ test that already inspects the sandboxed global table — which M0 wrote
for `getfenv`/`setfenv`/`newproxy` and which now covers the whole §1.1 list.

**6. Reading the vendored Luau contradicted the architecture in ways that
change how M2 gets built.** Recorded in full as `docs/research/luau-c-api-2026.md`
with file and line for every claim, and as rows U-17…U-50 in the UNCONFIRMED
registry. The load-bearing ones:

- **The Instance binding shape as written is impossible.** architecture §5 wants
  one `TAG_INSTANCE` *and* per-class metatables through
  `lua_newuserdatataggedwithmetatable`. Luau has exactly one metatable per
  *tag*, and reassignment is refused outright. It is one tag per class — capped
  at ~126, shared with every value type — or one shared tag with the class
  resolved from the `InstanceId`. Not both.
- **Binding registration has a hard boot deadline.** The fast dispatch opcodes
  are chosen once per `Proto` at `luau_load`, deopt is permanent and one-way,
  and `lua_registeruserdatadirectaccess` *snapshots* the metamethods. Everything
  must be registered before the first script loads.
- **Cyclic `require` does not exist at this pin.** api-design §3 promises
  "cyclic requires per spec"; the vendored implementation needs two default-off
  FastFlags, the `export` keyword, and an embedder-supplied placeholder hook,
  and plain `return {}` cycles are unsupported under any flag combination —
  with no guard, so a cycle recurses until the C stack dies.
- **`require` failures are never cached**, where api-design says they are.
- **The 1-second runaway-script kill cannot work as specified**: the interrupt
  fires at only a few opcodes, a long builtin is unpreemptible, and the error it
  raises is catchable by the script's own `pcall`. There is no uncatchable error
  in this VM.
- **`luaL_sandbox` removes nothing.** It freezes one level deep and freezes only
  the string metatable, despite its own comment. Every library curation R4
  implies is ours to do — M0 found half of this the hard way.
- **A registered module is matched before `is_require_allowed` runs**, so a
  chunk denied require access can still reach `@luaug/…`.
- **The GC pacing formula has a 1024× unit bug waiting**: `LUA_GCSTEP` takes
  kilobytes, `lua_allocationrate` returns bytes per second — and reads the wall
  clock, which R10 forbids in sim-visible code.
- **256 memory categories is an ABI ceiling**, so the "32–255 per Script"
  scheme in architecture §5 caps at 224 concurrent scripts.
- **ADR 0013 is wrong about `vectorType`**: it reaches one line and only tags a
  type annotation. Constant folding and fastcalls come from `vectorLib` plus
  `vectorCtor` alone.

**7. Two documented answers turned out to be things the VM does not permit**,
and both were found by writing the binding rather than by reading the headers a
third time. Recorded as U-52 and U-53 with an addendum on
`docs/research/luau-c-api-2026.md`.

- **`v.X` cannot raise.** api-design.md §2.3 said reading the uppercase
  component is an error. `LOP_GETTABLEKS` answers a single-character index on a
  vector *inline and case-insensitively*, before any metatable is consulted
  (`lvmexecute.cpp:619-635`). There is no metatable the engine can install that
  is reachable for x/y/z in either case. The document now records the exception;
  lowercase is still the only declared spelling, so `v.X` remains a **type**
  error and only the runtime guard is lost. Luau's own `vector_index` lowercases
  the same way, which is separately why our metatable *replaces* the stock one:
  without the replacement, `v.Magnitude` would raise instead of answering.
- **`typeof` never consults a table's `__type`.** §2.3 requires
  `typeof(Enum.PartShape) == "Enum"` and `typeof(Enum) == "Enums"`.
  `luaT_objtypenamestr` reads `__type` for userdata and for `global->mt[type]`,
  and its own comment at `ltm.cpp:167` excludes tables. So the `Enum` global and
  every enum object are **tagged userdata** (tags 8 and 9) rather than the frozen
  tables they read as. Two tags out of 128 to keep a documented answer true; the
  alternative was to weaken the contract to keep a table, and the budget has 118
  left while the contract has none to spare.

**8. The `useratom` process-globals were never necessary.** `runtime.cpp` said
the callback "has no user pointer", and it does: it receives the `lua_State`
(`lua.h:607`), so it can reach `lua_callbacks(L)->userdata` exactly like every
other binding. The two file-scope pointers and the "v1 has exactly one game VM"
limitation written around them are both gone, replaced by a per-VM `VmContext`
that also carries the world, the member tables and the identity cache.

**9. Luau stores a C closure's debug name by raw pointer** unless
`LuauManagedDebugNames` is on, and it defaults off (`lapi.cpp:792-795`). The
obvious source for a method's name — the `const char*` `lua_tostringatom` just
handed back — points into an interned Luau string that the collector may free
while the closure outlives it. The name now comes from the engine's `AtomTable`,
which is append-only and outlives the VM. Found by reading, not by crashing,
which is the only way this one gets found.

**10. Clang caught nineteen `-Wdouble-promotion` errors MSVC did not**, all of
them `lua_pushnumber(L, someF32)` — the API takes a double and every scalar in
the datatype bindings is an f32. That is the second time this milestone the
Linux tier has been the only thing standing between an implicit widening and
`main`.

**11. A test that asserts on an error key needs the catalog loaded.** An error is
identified by the `[key]` prefix `core::makeError` writes, and the prefix is the
*catalog's* name for the key. Without `LUAUG_TEST_CATALOG` every raise reports
`[i18n:missing:xxxxxxxx]` and every `raises(...)` assertion silently matched
nothing — fourteen tests failed at once for one reason, which is at least a
legible failure mode.

**12. The scene→script conversion cannot be batched at the drain, and the
conformance authors are what proved it.** The obvious design is `drain()` calling
`enqueueSceneChanges(world.changes().take())` once per resumption point: `scene`
accumulates POD facts, `script` turns them into fires. It is wrong, and §3.1
already said so twice — "a connection made **after** the fire does not run for
it", and "`Destroy` enqueues `Destroying`, **then** closes the instance's other
signals". A fire captures its connection list at the moment it is *raised*, so a
batch converted at drain time would hand every fire the connection list as it
stood at the end of the frame. `destroy_signals.spec.luau` tests both directions
of that, written from the document by an author who had never seen the code.

So every mutating binding calls `flushSceneChanges` the instant it returns from
`World`, and `ScriptRuntime::drain` flushes only to catch what the *engine*
raised outside a script. `Instance:Destroy` additionally closes the subtree's
signals — every one except `Destroying`, whose own fire has just been queued and
whose handlers must still run.

**13. `lua_unref` before `lua_getref` reads the free list, not the value.** The
first `:Once` handler to run came back as "attempt to call a number value". The
cause is in the research report and I wrote the code anyway: freed registry slots
hold the next free index *as a number* (`lapi.cpp:1814`), so disconnecting a
one-shot before fetching its handler makes `lua_getref` return an integer. The
fix is ordering — fetch onto the coroutine, then disconnect — and the same
ordering saves the `:Wait()` path, where the parked thread would otherwise be
unrooted across its own resume.

**14. A test that cannot see a contained error passes for everything after the
first `task.wait`.** §3.1 contains handler and task errors by design: they go to
the log, not to a return value. A `runSource` that returns `nullopt` therefore
says nothing about what the chunk did after it yielded — which is most of what a
signal test asserts. The fixture installs a `LogSink` and every timing test ends
with `CHECK(fixture.errors() == "")`. Without it the whole signal suite would
have been green and meaningless.

**15. A comment of mine broke R7 within an hour of the rule being re-read.** The
legal sweep caught "Roblox" in `engine/core/include/luaug/core/phase.h` — in a
sentence explaining a divergence, which is exactly the well-intentioned use the
rule exists to stop from spreading outside the docs set.

## Gate Record

(filled at milestone end, before human review)
