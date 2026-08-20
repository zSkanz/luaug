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

### 1. The engine is a WebSocket **client** of the dev server; only the dev server listens

**This decision was made twice.** The first version chose stdio — `luaug dev`
launching the engine as a child and speaking a line protocol over its
stdin/stdout, keeping the engine free of network code entirely. Then the
installed Lute typedefs were read, and they refuted the premise:

- `process.run` **returns after the child exits** and hands back `stdout` /
  `stderr` as whole strings. No child handle, no stdin, no incremental read
  (U-55). A pipe to a *running* engine is not writable in Lute 1.0.0.
- There is **no raw TCP**: the entire net surface is an HTTP client, a WebSocket
  client, an HTTP server and a WebSocket server (U-57).

So WebSocket is the only bidirectional push channel Lute can speak, and
`api-design.md` §3.2 was right for a reason it did not state.

What remained open is *which side listens*, and §3.2's wording ("pushes … to
the runtime on a localhost port") implies the wrong one. The decision:
`luaug dev` runs the `@std/net` WebSocket server on the `[dev] port` and
launches the engine in a spawned task (`process.run` yields — U-56); the engine
**dials out** to it. The engine opens no port in any profile. The same server
serves the gate test and, later, the overlay console.

Rejected: the engine as the WebSocket *server*. It puts a listener on the
developer's machine in every dev build — a port to allocate and collide on, a
firewall prompt on first run, an inbound parse surface, and the frame-unmasking
half of RFC 6455 — to buy attaching to an engine `luaug dev` did not start,
which is editor territory (ADR 0017, R15).

The cost is a small `net` seam in C++ that M3 must now build: TCP connect, the
HTTP upgrade handshake with the SHA-1 + base64 accept check, and RFC 6455 frame
read/write with client masking, tested against the published vectors in RFC 6455
§1.3 and §5.7. That is real work this brief did not originally budget, and it is
better learned now than at the gate.

ADR 0035; `api-design.md` §3.2's transport bullet is corrected with it.

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

### 12. The watcher watches every directory, debounces, and **rescans** — it does not trust the event

`fs.watch` was probed rather than assumed, and it reports less than the research
report implies (U-58). On Windows, editing `src/scripts/sub/deep.luau` under a
watch on `src/scripts` fires with `filename = "sub"` — the top-level entry, not
the file. One ordinary rewrite fires **two** events; a write-temp-then-rename
save fires **six** across two names; a creation fires **two**.

So: enumerate the project's source directories at startup and put a watch on
each; on any event mark dirty and start a quiet-period timer; when it expires,
**rescan** the script set and compare content hashes; reload only if something
actually changed, and report the real changed-file list from the rescan.

Rejected: trusting `filename` and reloading per event. It cannot name the file
that changed below one directory, and it turns every save into several world
restarts, which makes the 500 ms budget meaningless. A per-directory watch also
behaves the same on inotify, where a non-recursive watch on the parent may
report a subdirectory's contents not at all — which would be a silent failure,
the worst kind. **That Linux behaviour is still unverified** (entering risk 2).

### 13. The CLI has its own catalog at `tools/cli/i18n/en.json`

Per api-design §6, and separate from the engine's `i18n/en.json` because they
ship separately and are translated separately. Keys are `cli.*`.

### 14. The new world is built **before** the old one is destroyed

ADR 0024 writes the reload as a straight line: capture → janitor → destroy the
VM → fresh VM → re-run → restore. Taken as an instruction order, a project that
fails to mount — one syntax error in one entry script — leaves the process alive
with no world at all, and the failure reply has nothing to say about what the
developer is now looking at. In a loop whose whole purpose is that you save
often and sometimes save something broken, that is the common case, not the edge
one.

So: capture the state bag and the preserved instances from the old world, **boot
a second `WorldHost` alongside it**, and only when that boot succeeds restore
into it, swap the frame loop's pointer, and destroy the old. A failed boot
destroys the half-built new one and leaves the previous world running untouched;
the reply carries the error key and the session survives.

What ADR 0024 actually fixes is *what survives* a reload — window, GPU
resources, imported assets, streamed chunks — and that is unchanged. The cost is
that two VMs and two worlds coexist for the length of one safe point, which
doubles peak script memory during a reload and is bounded by the same 500 ms.
Recorded as a dated addendum on ADR 0024 rather than as a new ADR, following
what ADR 0013 did when M2 corrected it.

Rejected: destroy-first as literally written. It turns a typo into a dead
session, and it makes `ok: false` mean "your world is gone" — a reply the
protocol cannot act on.

## The dev control protocol

One JSON object per WebSocket **text** frame, no fragmentation on send. The
engine is the client (Decision 1); the dev server is the only listener and
relays between the engine and its other clients.

**Envelope.** Every message carries `type: string`. Every *request* carries
`id: number`, monotonic per sender, and every reply echoes it. An unrecognised
`type` is answered — never ignored — with
`{"type":"error", "id":N, "key":"dev.err.unknown_message"}`.

**Handshake.** The engine's first frame is
`{"type":"hello", "id":1, "protocol":1, "role":"engine", "token":"…",
"engine":"0.1.0", "pid":N}`. The dev server generates the token, passes it to the
engine on the command line, and **rejects a connection that does not carry it**
— a listener on loopback is still reachable by every process on the machine, and
the token is what keeps another one from driving your engine. It also tells the
engine connection apart from observers, who connect without a token and receive
a read-only relay. A `protocol` mismatch closes the connection with a stated
reason; it never degrades to a subset.

**Dev server → engine**

| `type` | Meaning |
|---|---|
| `reload` | `paths: {string}` — the rescan's real changed-file list. Perform the fast world restart. |
| `sample` | `afterTicks: number` — reply with the world hash once the sim has advanced that many ticks. This is what makes the gate's assertion race-free. |
| `ping` | Liveness. |
| `shutdown` | Exit cleanly, running `BindToClose`. The E2E test's teardown, and the answer to orphaned processes (entering risk 4). |
| `asset-changed`, `eval` | **Reserved.** Answered with `dev.err.not_implemented`. Not M3 scope, and named here so the protocol does not have to change to gain them. |

**Engine → dev server**

| `type` | Meaning |
|---|---|
| `hello` | The handshake above. |
| `reloaded` | `ok: boolean`. On success: `ms` (the span Decision 10 gates), `scripts` (how many entry scripts mounted), `tick`, `hash`. On failure: `key` and `detail`, and — per Decision 14 — the **previous world is still running**. |
| `sample` | `tick`, `hash`. |
| `pong`, `error` | As named. |
| `log` | **Reserved** for the overlay console. Not sent in M3; relaying the engine's log would double the console output for no reader that exists yet. |

**Emptiness, by construction** (M2 Finding 19 — three of these were built by
accident in one session):

- The dev server refuses to start if the project mounts **zero** entry scripts.
- `reloaded` with `ok: true` and `scripts == 0` is treated by the dev server as a
  failure, whatever the engine said.
- The E2E test asserts the post-reload hash **differs** from the pre-edit hash
  *and* **equals** a cold boot of the same edited source. Either half alone
  passes against a reload that did nothing.

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
- **`net_api`, sockets, `@std/net` in the game VM.** M7. The WebSocket client
  Decision 1 forces is a dev-profile host seam: it binds nothing to Luau, and no
  game script can reach it (R17).
- **The engine listening on any port, in any profile.** Decision 1.
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
  and the control-connection loop.
- The `net` seam Decision 1 forces: TCP connect, the WebSocket client handshake,
  frame read/write. Dev-profile only, and not the M7 `net_api` module — it binds
  nothing to Luau and no game script can reach it (R17).
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

**2. Everything known about Lute was web-derived — and the first thing done in
this milestone was to stop that.** `docs/research/lute-2026.md` says plainly that
it was captured from documentation and the GitHub API. Lute is not vendored, so
the grounding is the installed typedefs at the pin plus probe scripts. Done at
kickoff, before any code: five rows, U-55…U-59, four of them refutations. One
reversed Decision 1 outright.

What is still open is **`fs.watch` on Linux**. Every probe above ran on Windows;
inotify's non-recursive semantics differ, and the failure mode is silence rather
than an error. It is verified in the same task that puts Lute into the Tier-2
image (risk 1), and it must be verified before the dev server is called done.

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
