# M3 Kickoff — Tooling Loop: CLI, Hot Reload, Types, Tests

- Started: 2026-08-20
- Roadmap section: [`../roadmap.md`](../roadmap.md) § M3
- Previous milestone: [`m2-kickoff.md`](m2-kickoff.md) — read its **Findings** first.

## Goal (restated)

M2 made a script reach the engine. M3 makes the loop between writing that script
and seeing it run short enough to be worth using — and then makes every later
milestone pay for itself by being developed inside that loop. Concretely: a
`luaug` command exists, it is written in Luau on the pinned Lute, and it can
scaffold a project, check it, test it, and run it with a watcher attached so a
saved file becomes a new world in under half a second.

The dogfooding claim is the actual goal, and it is measurable rather than
rhetorical: from the end of this milestone, M4's meshes and M5's physics are
developed by editing a `.luau` file and watching, not by rebuilding the engine.
Anything in M3 that does not serve that loop is scope creep.

The second half is quieter and just as load-bearing. The IDL already generates
the type definitions the engine compiles against; M3 makes the same IDL generate
what an *editor* consumes — the docs file, the `@std`/`@luaug` stubs, the
`.vscode` settings a scaffolded project opens with — so that a person who runs
`luaug new` and opens VS Code gets completion, hover documentation and strict
analysis with nothing else installed. Zero-config onboarding is a feature of
this milestone, not of the docs site.

## Scope checklist (from roadmap)

- [ ] `luaug` CLI implemented as Lute 1.0.0 scripts (unmodified Lute, pinned via
      rokit)
- [ ] `luaug dev` — launch engine + `fs.watch` + WebSocket control channel →
      **fast world restart** per ADR 0024: state bag + `PreserveOnReload`,
      engine-side content survives, < 500 ms
- [ ] `luaug test` — headless engine executing spec files, TAP/JUnit output
- [ ] `luaug check` — luau-analyze + StyLua + i18n lint
- [ ] `luaug new` — template project
- [ ] `luaug fmt` — StyLua (CLAUDE.md lists it as active from M3)
- [ ] Typedef generation: engine API emitted as `declare extern type` defs
      consumed by luau-lsp 1.69 custom-platform mode
- [ ] VS Code workspace settings in the template

Two of these are partly built. `gen_dts.luau` has emitted
`runtime/types/engine.d.luau` since M2 and is freshness-gated; what M3 adds is
the *documentation* file, the stub modules, and the project-local `.luaug/types`
layout. `luau-check.sh` already runs analyze + StyLua + the i18n lint; what M3
adds is that `luaug check` becomes the one implementation and the gate script
calls it (Decision 7).

## The decisions this brief makes

Each names the alternative it rejects, because the alternative is what a future
session will otherwise re-derive.

### 1. The engine speaks the control channel over **stdio**; the WebSocket lives in the dev server

`api-design.md` §3.2 says the dev server "pushes `{script-changed |
asset-changed | eval}` messages to the runtime on a localhost port." Taken
literally that puts an RFC 6455 implementation inside the engine: a TCP
listener, the SHA-1 + base64 handshake, frame parsing, a background thread, and
a socket on the developer's machine that anything else can connect to. The
engine has no `net` module today and `net_api` is M7 scope.

The decision: `luaug dev` launches `luaug-host` as a child process and speaks a
line-delimited JSON protocol over its stdin/stdout. The dev server keeps the
`@std/net` WebSocket and serves it to *clients* — the E2E gate test, and later
the overlay console and any editor integration — relaying in both directions.
Every message type §3.2 names still exists and still arrives; only the last hop
changes.

Rejected: a hand-written WebSocket server in the engine. It buys exactly one
capability — attaching to a running engine that `luaug dev` did not start —
which is editor territory, and ADR 0017/R15 put the editor after v1. It costs a
network listener in every dev build (a Windows Defender prompt on first run, a
port to allocate and collide on, and a parse surface reachable from off-box if
the bind is ever wrong). A pipe the parent process owns has none of that, dies
with the process, and is identical on all three tiers.

Written up as ADR 0035; `api-design.md` §3.2's transport bullet is corrected in
the same commit (MASTER_PROMPT §5).

### 2. `luaug` is Lute **scripts run by the pinned `lute`**, not a `lute compile`d binary

`api-design.md` §4 describes the CLI as "a Lute app compiled with `lute
compile`". That is the shipping form. Inside this repository the gate, CI and
the dev loop invoke `lute tools/cli/main.luau <args>`, wrapped by
`scripts/luaug.ps1` / `scripts/luaug.sh` so the documented command line exists
and `luaug check` means one thing everywhere.

Rejected: compiling at M3. `lute compile` is host-only as far as anyone has
documented (research `lute-2026.md` §12 item 7 — cross-compilation unverified),
so a compiled CLI would have to be produced per tier before any gate could run,
and a stale binary would silently gate a fresh tree. Packaging the CLI is an M8
release-artifact task, and the entry point is written so that it stays one file
away from `lute compile`.

### 3. Reload = destroy and rebuild `WorldHost`; `Engine` survives

The reload seam is exactly the ownership boundary M2 already drew. The frame
loop holds the world host; a reload destroys it and constructs a new one at the
FrameStart safe point. The window, the RHI device, the renderer, the shader
cache and the debug-draw state are owned above it and are untouched — which is
ADR 0024's "engine-side content survives" restated as a C++ lifetime rather than
as a promise.

Rejected: resetting the VM in place inside `ScriptRuntime`. It leaves the ECS,
both registries, the signal queue and the task scheduler half-alive across the
boundary, and every one of those is a stale-state class ADR 0024 exists to make
impossible. Destroying the object is how you prove nothing survived.

### 4. `HotReloadService` is a real IDL service, and the state bag lives in C++

`BeforeReload`, `AfterReload`, `SaveState(key, value)`, `LoadState(key)`,
`IsReload` — declared in `api/defs/services.api.luau` with the `DevOnly` tag,
generated like every other service.

The bag cannot live in Luau: at the moment it matters, the VM that held it is
being destroyed. `SaveState` therefore converts its argument into an
engine-side value (JSON-able scalars, arrays and maps, plus `buffer` as bytes)
at call time, and `LoadState` converts back into the *new* VM. A value that
cannot cross raises rather than being silently dropped — a reload that quietly
loses state is worse than one that fails loudly (M2 Finding 19).

Rejected: preserving a Luau table by registry ref across the restart. There is
no "across": `lua_close` takes the table with it.

### 5. Preserved instances are re-materialized **before** entry scripts are deferred

`PreserveOnReload`-tagged instances have the same problem as the bag — the
`scene::World` they live in is destroyed too. They are captured as a description
(class, name, ancestry path, properties, attributes, tags), and re-created in the
fresh world *before* the new entry scripts get their first resumption, so a
script that looks for what it left behind finds it. `IsReload` is how a script
knows to look instead of re-creating, and that is the documented pattern
(api-design §3.2: "the character just stays put").

`Workspace.CurrentCamera.CFrame` is named in the same spec bullet as an
auto-preserved value. `Camera` is an M4 class; the auto-preserve lands with it,
and the mechanism is built now so that landing it is a table entry rather than a
design.

This ordering is a spec-level ruling and goes into `api-design.md` §3.2 in the
commit that implements it.

### 6. `luaug test` reads a **structured report file**, never the console

The headless runner gains `--test-report=PATH` and writes one JSON record per
case; the CLI turns that into TAP on stdout, or JUnit XML with `--junit=PATH`.

Rejected: parsing the engine's console output. Every line the engine prints is a
catalog-resolved message (R3) — parsing it would make the test runner break the
first time a locale is added, which is precisely the coupling ADR 0019 exists to
prevent. The report file is machine output and carries keys, not prose.

### 7. `luaug check` is the implementation; the gate script calls it

`scripts/gates/luau-check.sh` becomes a thin wrapper over `lute
tools/cli/main.luau check`, and the checks themselves move into `tools/cli/`.
CLAUDE.md's standing rule ("if you add a check, add it there, not in the
workflow") gets its final form: one implementation, three callers — the gate,
CI, and a user typing `luaug check` in a scaffolded project.

The roadmap says "luau-analyze default solver". R2 and ADR 0018 require the new
solver, and the M2 gate is stated in those terms; the pinned luau-lsp enables it
through `--no-flags-enabled`'s inverse, which is what the existing gate already
does. "Default" is read as "the solver this toolchain defaults to", not as an
instruction to turn the new one off. No gate is weakened.

### 8. The i18n lint's second half is a **sink lint**, not a literal lint

R3's remaining half — "zero hardcoded user-facing strings in C++ and CLI Luau" —
is enforced by naming the sinks a user-facing string can reach and rejecting a
raw literal as their message argument: `core::log`/`logText`, `core::makeError`,
and anything that raises to Luau on the C++ side; the console writers and
`error()` on the CLI side. The single documented exception is
`main.cpp::reportCatalogFailure`, which by definition runs when there is no
catalog, and it is allowlisted by name.

Rejected: a general "no string literals" rule. Every key, path, format
specifier and JSON field is a literal; a lint that flagged them all would be
suppressed everywhere within a week, which is a gate that passes while doing
nothing (M2 Finding 19).

### 9. `luaug.toml` is parsed by the **CLI only** in M3

`luaug new` writes one and `luaug dev` reads `[dev] port` and `[project] name`
from it, using a small TOML-subset reader in `tools/cli/`. The engine does not
parse TOML in M3 — the CLI passes what the host needs as flags, which is what
every existing harness already does.

`api-design.md` §3 lists "load `luaug.toml`" in the engine's boot lifecycle. That
becomes true in the milestone that first needs engine-side configuration (M7's
asset manifest, or M8's shipping profile). Writing a second TOML reader in C++
now would be speculative abstraction with a test burden and no caller —
MASTER_PROMPT §5's senior-review bar rejects it. Recorded here so the divergence
is deliberate and dated rather than discovered.

### 10. The < 500 ms budget is measured **engine-side**, from safe point to `AfterReload`

The engine timestamps the reload span it actually controls — FrameStart safe
point through `AfterReload` returning — and reports it in the reload-complete
message. That number is what `docs/perf-baselines.md` records and what the gate
asserts.

Rejected: timing the whole round trip from the test's file write. That measures
the filesystem notification latency, the debounce window and two process
schedulers, none of which ADR 0024 budgets and all of which vary by machine — a
gate that goes red because a CI runner was busy teaches nothing and gets muted.
The round trip is still *reported* by `luaug dev` for the developer, because
that is the number a developer feels; it is not the gated one.

This clock is host instrumentation and never enters the world hash. R10 forbids
wall-clock reads in simulation code, and this is not simulation code — stated
explicitly because the next reader will check.

### 11. The gate's "behavior change" assertion is that **reload equals restart**

After the test mutates the source and the reload confirms, the world hash at
tick N is compared against the hash of a *cold boot* of the same mutated source
run to the same tick. Equal means the reloaded world is indistinguishable from
one that never reloaded — which is ADR 0024's whole claim, turned into an
assertion. A weaker test ("the hash changed") would pass against a reload that
half-applied the edit.

The tick counter and instance ids restart with the world; the comparison is
therefore between two runs that both start counting at zero, which is the only
way it is well-defined.

### 12. The watcher coalesces; the engine reloads once per batch

`fs.watch` fires more than once per save — editors write to a temporary file and
rename, and some fire on both metadata and content. The dev server holds events
in a quiet-period window and sends one `script-changed` carrying the changed
paths. Rejected: one reload per event, which turns every save into three world
restarts and makes the 500 ms budget meaningless.

### 13. The CLI has its own catalog at `tools/cli/i18n/en.json`

Per api-design §6, and separate from the engine's `i18n/en.json` because they
ship separately and are translated separately. Keys are `cli.*`.

## NOT in scope

Named so that a later session does not read the absence as an oversight.

- **`luaug build`, `luaug asset`, `luaug add`, `luaug doctor`, `luaug setup` as a
  user-facing command.** Build and asset packaging are M7/M8; `add` wraps a
  package manager that has no dependencies to install yet. The type/stub
  regeneration `setup` performs happens inside `luaug new` in M3.
- **`lute compile` packaging of the CLI** — Decision 2; M8.
- **Module-level hot reload / `__hotreload`** — post-v1 by ADR 0024, and it is
  the single most tempting thing in this milestone.
- **Asset and shader hot swap.** The protocol reserves `asset-changed`; nothing
  consumes it, because no asset pipeline exists before M4/M7. Wiring a message
  no receiver acts on is not the same as implementing the feature, and the
  distinction is stated so the gate record does not claim it.
- **The `eval` dev console.** The message type is reserved in the protocol and
  rejected by the engine with a not-implemented key. Running arbitrary source in
  a live world touches R4 and deserves its own design.
- **Engine-side `luaug.toml`** — Decision 9.
- **`net_api`, sockets, `@std/net` in the game VM.** M7.
- **The `obby` and `openworld-demo` templates.** M6 and M8 per
  `templates/README.md`; M3 populates `starter` only.
- **A second example.** M3's deliverable is the *existing* `examples/01-instances`
  edited live. No `examples/02-*` is created here.
- **`Script.Enabled` gaining post-boot behaviour.** api-design §3 documents its
  v1 limit and names the world restart as the answer; M3 is the milestone that
  makes that answer real, not the one that changes the property.

## Subagent plan

M2's finding stands and is the planning input here: **delegating large C++ kernel
blocks did not work** — three stalled and were redone by hand — while tight,
well-bounded tasks with a frozen interface landed well. M3 is mostly Luau, which
is friendlier to fan-out, but the reload seam is kernel work by any definition.

**Orchestrator only** (single-threaded, by MASTER_PROMPT §7):

- The reload seam in `app`: the `WorldHost` lifetime, the FrameStart batch point,
  the child-process protocol loop.
- `HotReloadService`, the state bag, and the preserved-instance capture/restore.
- The dev server's protocol and process supervision.
- Every gate run and every merge.

**Fan out, interface frozen first** (≤ 3 in flight):

- The TOML-subset reader (Luau) — pure function, frozen signature, its own tests.
- The TAP and JUnit formatters — pure functions over the report schema, which is
  frozen before they start.
- `gen_docs.luau` (the luau-lsp documentation JSON) and the `@std`/`@luaug` stub
  emitters — the IDL and the existing `gen_dts.luau` are the frozen interface.
- The i18n sink lint (Decision 8) — the sink list is frozen input.
- The `starter` template's contents — written from api-design §4's tree.
- **The hot-reload specs, written from `docs/api-design.md` §3.2 alone**, by an
  author forbidden from reading `engine/` and `tools/cli/`. M2's Finding 17 is
  the reason this is not optional.

**Research verifier, first task of the milestone:** Lute 1.0.0's `fs.watch`,
`process.run`, and `@std/net` WebSocket server surfaces, read from the
*installed* binary's typedefs at the pin — not from the research report, which is
web-derived. Every claim that flows into the dev server gets a row in
`docs/research/UNCONFIRMED.md`.

## Gate checklist (verbatim from roadmap)

- [ ] automated E2E hot-reload test (dev server started headless, file mutated by
      the test, WebSocket confirms reload, behavior change asserted via world
      hash)
- [ ] `luaug test` green in CI on both tiers
- [ ] defs file lints clean and is regenerated + diff-checked in CI (drift
      between API and defs fails the build)
- [ ] i18n lint now enforces zero hardcoded user-facing strings in C++ and CLI
      Luau

And, carried by ADR 0024 rather than by the roadmap's gate list:

- [ ] the reload span is < 500 ms, recorded in `docs/perf-baselines.md`

## Entering risks

**1. The Tier-2 container has no Lute.** `scripts/docker/tier2.Dockerfile`
installs a C++ toolchain and nothing else; the Luau gates run on the Windows
host. The gate says "`luaug test` green in CI on both tiers", so the image needs
rokit and the already-pinned `lute@1.0.0`. This is installing an existing pin in
a second environment, not a new dependency — but it is a build-time network
fetch inside the image, and it is the kind of thing discovered at the end of a
milestone if it is not written down at the start.

**2. Everything known about Lute is web-derived.** `docs/research/lute-2026.md`
is explicit that it was captured from documentation and the GitHub API, and its
own §12 lists twenty things it could not verify. M2 spent a session learning that
reading the vendored VM contradicted the architecture in eight load-bearing ways.
Lute is *not* vendored — there is no source tree to read — so the equivalent
grounding is the installed binary's generated typedefs and small probe scripts.
That is the first task, and its output is UNCONFIRMED rows.

**3. The 500 ms budget has no bytecode cache behind it.** A reload re-compiles
every script in the project from source. `examples/01-instances` is small; a
project with a hundred modules may not be. ADR 0024 names the require graph as
the bytecode-cache invalidation input, and no cache exists. If the budget is
missed, the cache is the answer and it is in scope; if it is met without one,
the cache is not built on speculation.

**4. Spawning, supervising and killing a child process portably.** The dev
server owns an engine process across two tiers; a test owns a dev server. Orphan
processes on a CI runner are a class of flake that is invisible until the runner
runs out of something. Every layer gets an explicit teardown and the E2E test
asserts the child is gone.

**5. The state bag crosses a boundary in both directions.** Values are copied out
of a VM that is about to die and into one that does not exist yet. The failure
mode is silent partial preservation — a table that round-trips as empty. Fixture
tests assert the round trip per type, and an unconvertible value raises.

**6. A gate that can pass while doing nothing.** M2 built three of these in one
session and caught all three. M3's shapes are: a dev server that reports "reload
complete" without the engine having reloaded; a `luaug test` run that discovers
zero spec files and exits 0; a hot-reload test whose file mutation does not
change behaviour, so the assertion holds vacuously. Each of the three is
enumerated here so that each gets an emptiness check by construction.

## Attempted / abandoned

(append during the milestone; §12 of the master prompt)

## Findings

(append during the milestone — the things the docs assumed that reality
corrected)

## Gate Record

(filled at milestone end, before human review)
