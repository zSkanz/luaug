# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- Current milestone: **M2 — Kernel: Instances over ECS, Scheduler, Signals,
  task — SCOPE AND GATE COMPLETE, awaiting human sign-off** (brief:
  `docs/briefs/m2-kickoff.md`). The largest milestone in the project (18%); it
  spanned five sessions, as the brief's Entering Risks §5 expected.
- **M1 — Window, RHI, Frame Loop, Agent Eyes — COMPLETE and SIGNED OFF by the
  human on 2026-08-19**, tagged `milestone/m1` (brief:
  `docs/briefs/m1-kickoff.md`). Its gate was re-run green on both tiers at M2
  kickoff (`scripts/localgate.ps1`, 25.8 s, 15/15 tests).
- **M0 was signed off on 2026-08-19**, tagged `milestone/m0`.
- The engine renders: three wire cubes orbiting a world triad over a pulsing
  clear colour, driven from `examples/00-clear/init.luau` through a temporary
  Luau binding that M2 replaces. F3 toggles an ImGui overlay in dev builds.
- 20 CTest entries, green on Windows and Linux.

### M2: what exists

- **A script reaches the engine, and the engine answers back.**
  `Instance.new("Part")` builds a real instance, properties round-trip through
  their components, `a == b` holds for two handles to one instance, every
  datatype is bound, signals deliver deferred in one queue with the ordering
  §3.1 specifies, and `task` runs on the SimClock in integer ticks. 90 cases
  over the bindings, all asserting from inside the VM.
- **`core`** — `SlotMap`, `AtomTable`, `Pcg32`, `Phase`, `CFrameD`/`Mat3`/
  `Color3`, all six euler orders both ways, axis-angle, quaternions, slerp.
  Heavily tested and mutation-checked (102 cases).
- **The API IDL is real and generates the engine.** `api/schema.luau` with the
  §9 naming lints, `api/defs/*.api.luau` (13 classes, 6 datatypes, 4 enums),
  `gen_dts.luau` → `runtime/types/engine.d.luau`, `gen_cpp.luau` →
  `engine/scene/generated/class_descriptors.gen.{h,cpp}` — which now emits the
  **enum** tables and their ids as well. Both generated outputs are checked in
  and **freshness-gated**, and both gates were proved by tampering.
- **`scene` (L3)** — the ECS, the Instance facade, the duplicate-name index
  (ADR 0026), attributes, tags, the POD change queue, `WorldHash` over xxh3, and
  `EnumRegistry`. 41 cases / 328 assertions. `native_accessors.cpp` is the
  hand-written half of reflection: 17 properties, 35 functions, matching the
  generated declarations name for name.
- **`script` (L5)** — the VM boots in the one order that works, with the sandbox
  curation R4 actually requires; `VmContext` (reached through
  `lua_callbacks(L)->userdata`) carries the world, the atom bridge, the per-tag
  member tables, the signal system and the task scheduler.
  `instance_binding.cpp`, `datatypes.cpp`, `signals.cpp` and `tasks.cpp` are the
  surface itself. The boot-time method cross-check reports both directions, and
  a declared-but-unimplemented member raises `script.err.not_implemented` rather
  than reading as missing.

- **Every method the IDL declares is implemented**: `MethodCoverage` reports
  43 declared, 43 bound, 0 unbacked, and `instance_binding_tests.cpp` pins all
  three — so a method added to `api/defs` without a binding fails at boot rather
  than the first time a script calls it. `game` and `workspace` are globals,
  services are created on demand and singleton thereafter, the five phase
  signals fire from the scheduler, and `WaitForChild` parks on a tree state.

- **`app` (L6)** — `WorldHost` owns the world, the VM and the project;
  `WorldHostOptions{projectPath, seed, fixedTimestep, conformanceRoot}`. A
  directory mounts `src/scripts/**/*.luau` as entry `Script`s with `.luaurc`
  aliases resolved, a file mounts as one. `WorldHost::tick` is the only place
  architecture.md §3's resumption order exists as code, and the frame loop
  drives it. `render::extract`/`RenderWorld` (ADR 0027) carried forward from M1
  and closed.
- **Record/replay v1 and the determinism gate** — `--replay=DIR` runs each
  scenario twice in-process and against a per-platform recorded trace, sampling
  the world hash every N ticks. `--bench=DIR` measures sim ticks against a
  per-scenario budget. Neither opens a device.

### M2: the gate

**4 of 4.**

1. **Conformance suite** — 903 cases in 46 files, written against
   `docs/api-design.md` by authors forbidden from reading `engine/`. All pass.
   They found **twelve engine defects, five spec bugs and two documentation
   defects** (`tests/conformance/README.md`).
2. **Determinism** — `tests/determinism/churn`, 10,000 ticks, identical hash
   twice in-process and against the recorded trace, on Windows and Linux.
   Traces are per-platform; the reason and the measurement are in
   `tests/determinism/README.md`.
3. **500-instance scene under budget** — 0.134 ms/tick against a 4 ms budget,
   recorded in `docs/perf-baselines.md` along with the reference machine and the
   10k-parts/1k-listeners churn number (2.02 ms/tick against 16 ms).
4. **Zero `luau-analyze` errors** under the new solver across every example and
   spec — `scripts/gates/luau-check.sh`, 67 files.

### M2: what does NOT exist yet

Nothing from M2's own scope. The frame-budget instrumentation is
`DebugService:GetStat` plus the bench harness, not a per-phase profiler; a
profiler is M8's `luaug profile`. Physics, meshes, cameras, input and UI are M4
and later, by the roadmap.

## Now / Next

- **Next: stop for M2 human review** (MASTER_PROMPT §6). The scope is closed and
  all four gate items are green on both tiers. **Do not start M3 this session.**
- When M3 opens, its brief is written from `docs/roadmap.md` and from this
  milestone's Findings; `luaug dev` hot reload is the headline and ADR 0024 is
  the grounding.
- **The scene->script conversion is synchronous, not batched.** A fire captures
  its connection list when it is *raised*, so every mutating binding calls
  `flushSceneChanges` immediately and `ScriptRuntime::drain` flushes again only
  to catch what the engine itself raised. Batching at the drain would let a
  connection made after a mutation run for it, which §3.1 forbids in two places
  and the conformance specs test in both.
- **The conformance suite is the single most valuable thing M2 built.** 903
  cases written from the document by authors who could not read `engine/`, and
  they found twelve defects the C++ tests written beside the code had all
  missed. Where they contradicted the document, twice the document was wrong.
  Keep the rule: specs are written against `docs/`, never against the
  implementation.
- **A gate that can pass while doing nothing is not a gate.** Three separate
  instances of this were built and then fixed inside one session: the replay
  harness recorded a golden for a scenario whose script had died; a project
  directory that mounted no entry scripts booted, ran and reported success; and
  a determinism run with no trace for the current platform would have degraded
  to the weaker in-process half. Every harness now fails loudly on emptiness.
  This is worth checking for by construction in M3's `luaug test` and
  `luaug dev`.
- Kernel work (ECS, Instance facade, signal queue, `task`, bindings) stays
  single-threaded per MASTER_PROMPT §7. **Delegating large C++ kernel blocks to
  subagents did not work**: three in a row stalled with no output (class
  registry, scene tests, sandbox) and had to be killed and redone by hand.
  Tight, well-bounded tasks landed well — the two generators, the spec authors,
  the math. Fan out narrow, write the kernel yourself.
- **Read `docs/research/luau-c-api-2026.md` before writing any binding code.**
  It is the frozen, file-and-line-verified account of the Luau C API at the pin,
  and rows U-17…U-51 in `docs/research/UNCONFIRMED.md` record where it
  contradicts the architecture. Brief Decision 13 settles the biggest one.
- **Run `scripts/localgate.ps1` before every push. Do not use CI as a test
  runner.** Both tiers, ~20 seconds warm. The repository is private, so Actions
  minutes carry platform multipliers and the quota has been close to exhausted.
  CI proves `main` is green and builds macOS, which nothing local can.
- Carried forward from M1 (none blocked the gate):
  - **The shipping profile does not configure.** `engine/script/src/modules.cpp`
    and `runtime.cpp` include `<luacode.h>` unconditionally while
    `LUAUG_LUAU_COMPILER` is forced off in shipping (ADR 0002) -- the same defect
    `script_host.cpp` carried, moved with the code that had it. The guard is one
    line, but a shipping host also needs the bytecode-loading path, which is M3
    at the earliest. `require` is at least written rather than linked from
    `Luau.Require`, which would have dragged the compiler in as a propagated
    link requirement whatever the guard said (U-38).
  - **`architecture.md` §9 lists a clang-format gate that does not exist.**
    Turning it on means reformatting the tree and pinning a version; version
    pinning for the C++ toolchain is M3's `luaug check` work.
  - **`Luau.Analysis` is still compiled and never linked** — carried from M0.
    sccache now makes this cheap on a warm CI cache, so it is less urgent.
  - **The example's `luaug` global and its hand-written `.d.luau`** are M1
    scaffolding with a demolition date; M2 replaces both.
  - **DXIL produced on Linux is never verified as signed.** It is also never
    loaded there — D3D12 is Windows-only — so this only matters if a Linux job
    ever produces a shipping shader pack for Windows (M8).

## Blocked — needs human

- (none — M0 sign-off was given on 2026-08-19)

## Decisions pending ADR

- (none — ADR 0032 and ADR 0033 were written during M1)

## Session Log

Entries for the planning session and for M0 and M1 are in
[`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md), moved
there when this file passed its ~300-line cap.

- **2026-08-19 (session 4, Claude Opus):** M1 signed off; **opened M2**. Ran the
  §2 boot sequence — ledger and repo agreed, and the M1 gate re-ran green on
  both tiers locally (15/15, 25.8 s). Wrote `docs/briefs/m2-kickoff.md`: the
  scope checklist, an explicit NOT-in-scope list, the subagent split, the gate
  verbatim, five entering risks, and twelve numbered decisions each naming the
  alternative it rejects.
  Learned at planning time, before any code: the gate's phrase "*documented*
  ordering semantics" is not satisfied by anything currently written down, so
  the spec must precede the implementation; `api-design.md` §2.1 omits
  `ScriptService` even though §3 requires it; the roadmap's "RNG service" and
  api-design's `Random` datatype are the same thing under two names, and
  api-design wins per §4; and M2's own gate ("zero luau-analyze errors across
  all spec code") cannot be met without the `.d.luau` generator the roadmap
  scheduled for M3 — so `gen_dts` is pulled forward, forced by the gate rather
  than by preference.
  Next: write the ordering semantics into `docs/api-design.md`.

- **2026-08-19/20 (session 4, continued):** Built the M2 spine below Luau. The
  IDL now generates both the type definitions and the C++ reflection tables,
  both checked in and freshness-gated; `scene` has the ECS, the Instance facade
  and the world hash; `script` boots a VM with the sandbox R4 actually needs.
  Learned, each found by running rather than by reading: `ComponentPool` left a
  dead entity's component visible to `forEach` after a slot was recycled; the
  property subscription mask measured a pointer offset into the *declaring*
  class's array, which made every inherited property permanently unsubscribable
  and permanently noisy — the exact inverse of the quiet-write path the 10k
  benchmark rests on; `lua_newthread` pushes the thread above the loaded
  function, so load-then-`xmove` moves the wrong value and fails far from its
  cause; `luaL_sandbox` removes nothing at all; and Luau's base library defines
  neither `collectgarbage` (which api-design listed and now does not) nor
  `warn` (which the runtime now installs). Clang caught an unused private field
  MSVC would have kept forever, and caught three `-Wdouble-promotion` errors in
  a commit I pushed without running the Linux tier — the rule in CLAUDE.md
  exists for exactly that and I went around it.
  Next: `engine/script/src/instance_binding.cpp`.

- **2026-08-19 (session 4 continued):** Wrote api-design §3.1/§3.2 (the deferred
  ordering contract), settled the casing rule with the human (ADR 0034), and
  fanned out: five vendored-source verifiers, five conformance authors, a
  restyle pass, a research writer, an i18n pass, a doc-rulings pass and the
  `core` primitives. Landed `@luaug/testing`, `api/schema.luau` with the §9
  naming lints, `core`'s `SlotMap`/`AtomTable`/`Pcg32`/`Phase` (71 cases, six
  mutation tests, all caught), 14 error keys, and 125 rulings written into the
  documents.
  Learned: the conformance authors found two contradictions between our own
  documents and two places where independent authors guessed opposite answers —
  and the R8 removals turn out to be untestable from inside Luau, so they move
  to C++. Reading the vendored VM contradicted the architecture in eight
  load-bearing ways (`docs/research/luau-c-api-2026.md`, U-17…U-51): one
  metatable per tag makes the Instance binding shape as written impossible,
  cyclic require does not exist at this pin, `luaL_sandbox` removes nothing, and
  the 1 s runaway kill raises an error the script can catch. Two headers I had
  frozen had real defects, both found by the implementer: `AtomTable`'s
  `string_view` keys dangled on vector growth (SSO), and `SlotMap::erase` was
  `noexcept` while allocating.
  Next: write `api/defs/*.api.luau`, then `gen_dts`.

- **2026-08-20 (session 5, Claude Opus):** **A script reaches the engine.** The
  Instance binding, every datatype, signals and `task` — three commits, each
  green on both tiers, 233 C++ cases in total. `Instance.new("Part")` builds a
  real instance whose properties round-trip through their components; two handles
  to one instance are the same value; signals deliver deferred in one queue with
  every ordering rule §3.1 states; `task` runs on the SimClock in integer ticks.
  Also: `core::math` gained all six euler orders, axis-angle, quaternions and
  slerp; the IDL generator gained the enum tables and their ids, which retired
  `native_accessors.cpp`'s placeholder constant.
  Learned — three of these are things the VM refused and two are things the
  conformance authors caught before the code did: **`v.X` cannot raise**
  (`LOP_GETTABLEKS` answers a single-character index on a vector inline and
  case-insensitively, so no metatable is reachable for it — api-design §2.3
  corrected, U-52); **`typeof` never reads a table's `__type`**, so `Enum` and
  every enum object are tagged userdata rather than the frozen tables they read
  as (U-53); the `useratom` process-globals were never necessary, because the
  callback receives the `lua_State`; **the scene→script conversion cannot be
  batched at the drain**, because a fire captures its connection list when it is
  raised; and `lua_unref` before `lua_getref` reads the registry's free list
  rather than the value, which made the first `:Once` handler "attempt to call a
  number value". Clang caught nineteen `-Wdouble-promotion` errors MSVC did not.
  Then, in the same session: **the DataModel and every service.** `game` and
  `workspace` are globals, services are created on demand and singleton
  thereafter, the five phase signals fire from the scheduler, `WaitForChild`
  parks on a tree state, and the method cross-check closes at 43 declared / 43
  bound / 0 unbacked. `DebugService` was going to be deferred and deferring it
  would have been the worse choice -- `__index` on an event pushes a signal
  object unconditionally, so `MessageOut:Connect` would have succeeded and never
  fired, and no error key can cover an event because an event is a value rather
  than a call.
  Next: **`require` and the mounted-script lifecycle**, then move the resumption
  sequence out of `script_fixture.h::tick` into `app`'s `FrameScheduler`. That
  second half is a migration rather than an addition -- it replaces
  `engine/app/src/script_host.cpp` and `preview_api.cpp`, both M1 scaffolding
  with a demolition date, and `examples/00-clear` renders through the temporary
  `luaug` global those two provide. The M1 screenshot golden is what proves the
  migration kept the picture, so do it with the observation rule (§8) in hand.

- **2026-08-20 (session 5, Claude Opus): M2 closed.** Integrated the conformance
  suite and closed all four gate items. The suite went from 932 staged cases with
  ~1,900 analyzer diagnostics to **903 cases, 0 diagnostics, all passing**, and
  it earned its keep: **twelve engine defects** the C++ tests written beside the
  code had all missed (`clone` copied `Parent` as a value; `destroy` left
  descendants parented; renaming a child back to a duplicated name put it last in
  the chain, so `FindFirstChild` answered the wrong instance against ADR 0026),
  five spec bugs, and **two documentation defects** where the specs were right
  about the VM and api-design was not (U-52 `v.X` cannot raise; U-53 `typeof` on
  a table cannot be `"Enum"`). U-54 was added: the `Vector3` metatable members
  work at runtime and are not type-checkable, because a definitions file cannot
  augment a Luau builtin.
  Then the three remaining gates. **Record/replay v1** (`--replay=DIR`) runs each
  scenario twice in-process and against a per-platform recorded trace, sampling
  the hash every N ticks -- and the cross-process leg immediately found the world
  hash reading **four bytes of uninitialised padding out of `CFrameD`** and two
  out of `EnumValue`, which reproduced perfectly inside one process and differed
  in the next. `Hasher::pod` now static_asserts
  `has_unique_object_representations`, so the next struct to grow padding fails
  to compile. **The bench harness** (`--bench=DIR`) records 0.134 ms/tick for the
  500-instance gate scene and 2.02 ms/tick for 10k parts / 1k listeners, both
  well inside budget, in `docs/perf-baselines.md` with the reference machine.
  **`examples/01-instances`** is a real project directory, and building it found
  a genuine scheduler bug: `task.delay` parked its arguments in the deferred
  arena, which is reset at the end of every drain, so a delay longer than a tick
  lost them. They live on the timer's own coroutine now. Learned three times over
  that **a gate which can pass while doing nothing is not a gate** -- the replay
  recorded a golden for a dead script, an empty project booted and reported
  success, and a missing platform trace would have degraded silently. All three
  now fail loudly. Also learned that the cross-platform hash divergence is
  exactly the f64 state: `example01`'s traces are byte-identical across Windows
  and Linux because its results round through f32 `Part.Position`, while
  `churn`'s diverge from tick 500 because `CFrame` accumulates in f64.
  Next: **stop for M2 human review** (§6). Do not start M3 this session.

<!-- Format for future entries:
- **YYYY-MM-DD (session N):** did X; learned Y; Next: <literal first action>.
-->
