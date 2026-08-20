# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- **M3 — Tooling Loop: CLI, Hot Reload, Types, Tests — COMPLETE and SIGNED OFF
  by the human on 2026-08-20**, tagged `milestone/m3` (brief:
  [`docs/briefs/m3-kickoff.md`](docs/briefs/m3-kickoff.md), which carries the
  Gate Record and seventeen Findings). All four gate items green on both tiers,
  plus ADR 0024's own <500 ms requirement.
- **M4 — Seeing the World: Meshes, Materials, Camera, Lighting — OPEN** since
  2026-08-20 (brief: [`docs/briefs/m4-kickoff.md`](docs/briefs/m4-kickoff.md) —
  thirteen decisions, fifteen NOT-in-scope items, six entering risks, and a
  nine-step build order). The M3 gate was re-run green on both tiers before it
  opened. Two things in it need the human and are named there: the **Android
  device checkpoint**, which blocks the RHI freeze at the end of the milestone,
  and **which glTF sample asset the repository carries**, which would be its
  first binary content.
- **M2 — Kernel — signed off 2026-08-20**, tagged `milestone/m2`; **M1** signed
  off 2026-08-19 (`milestone/m1`); **M0** signed off 2026-08-19
  (`milestone/m0`).
- **CI has not run the three steps this milestone added to it** — the toolchain
  install, `luaug test`, and the hot-reload suite. They are exercised locally on
  both tiers; macOS (Tier-3, CI-only) has not compiled since M1. First thing to
  look at after the push.

### M3: what exists

- **The loop the engine sells.** `luaug dev` watches a project, and a saved file
  becomes a new world in **1.7 ms** on the M2 example — against a 500 ms budget.
  The engine dials out to the dev server as a WebSocket client (ADR 0035) and
  opens no port in any profile; commands are applied at the FrameStart safe
  point, never mid-tick, which is what keeps within-run determinism true with a
  watcher attached.
- **The reload is a restart, and that is testable.** `app::reloadWorld` destroys
  and rebuilds `WorldHost` and nothing above it, building the new world before
  destroying the old so a syntax error leaves the running one alone. The
  assertion that matters, in C++ and again end to end: a reloaded world hashes
  the same as a cold boot of the edited source at the same tick.
- **`HotReloadService`** (`SaveState` / `LoadState` / `IsReload()` / `PreReload`
  / `PostReload`), a state bag that lives in C++ because the VM that held the
  value is what a reload destroys, and `PreserveOnReload` instances captured as
  descriptions and re-materialised before the new entry scripts are deferred.
- **`engine/net` (L2)** — the RFC 6455 client, tested against the RFC's own
  published vectors; and **`core::JsonWriter`**, which `json.h` said would have
  no caller until this milestone gave it two.
- **`tools/cli`** — `dev`, `test`, `check`, `fmt`, `new` on the pinned Lute, a
  TOML-subset reader for `luaug.toml`, the dev server, and the `starter`
  template with the `.vscode` settings that make `luaug new` plus opening the
  folder a complete setup.
- **The gate got teeth.** `luau-check.sh` calls `luaug check`; the i18n lint
  enforces R3's second half over 203 sink calls; the CLI's own tests and the
  end-to-end hot-reload suite both run in the gate, on both tiers.

### M3: the gate

**4 of 4, plus ADR 0024's own.** The Gate Record in the brief carries the
numbers, the commands and the deliverable's transcript.

### M3: what does NOT exist yet

Nothing from M3's own scope. The brief's NOT-in-scope list names nine things
deliberately left: `luaug build`, `asset`, `add`, `doctor`, `lute compile`
packaging, module-level `__hotreload`, asset and shader hot swap, the `eval` dev
console, engine-side `luaug.toml`, and the `obby`/`openworld-demo` templates.
The bytecode cache ADR 0024 names is also not built — the budget did not need
it, and building it on speculation is what §5 rejects.

## Now / Next

- **Next: build-order step 1 of the M4 brief — vendor `fastgltf` and
  `meshoptimizer`**, fill their manifest rows with real commit SHAs and resolved
  versions, regenerate `THIRD_PARTY_NOTICES.md`, and wire both into the build as
  SYSTEM includes, with the gate green before anything else is written. Both
  rows already exist carrying `TBD-AT-M0`, so this is the lazy vendoring M0's
  scope allows and not a new dependency (brief, Decision 3).
- **Two M4 items need the human, neither blocking yet.** The **Android device
  checkpoint** (roadmap: before the RHI freeze at the end of M4 — the agent does
  not hold phones) and **which glTF sample asset the repository carries**, which
  would be its first binary content. Both are written up in the brief; ask
  before the freeze and before committing any binary, respectively.
- **The dogfooding claim binds from here and is weaker than it sounds.** What
  `luaug dev` reloads is the *scene* — mesh, camera, sun, materials — while the
  renderer is C++ and needs a rebuild. The brief's Decision 13 commits to
  recording the real reload-vs-rebuild count in the Gate Record, so the claim
  ends the milestone either evidenced or as a finding.
- **CI is green on `main` at `2794263`**, and the two `build-test` tiers ran the
  three steps this milestone added: the toolchain install, `luaug test`, and the
  hot-reload suite. Gate item 2 is therefore green *in CI* and not only locally,
  which is what it actually asks for.
- **CI went red once on the M3 tag and was fixed in the same session.**
  `scripts/luaug.sh` was committed without its executable bit — a Git working
  tree on Windows does not track file modes, and a local run invokes the script
  through PowerShell, so nothing here could notice. Every caller goes through
  `bash` explicitly now and the bit is set in the index. The same push built
  **macOS green for the first time since M1**, which is the tier nothing local
  can check.
- **Read the installed artifact, not the report about it.** Six of this
  milestone's fifteen findings are Lute behaving differently from its own
  typedefs or documentation, and every one was settled by a ten-line probe:
  `process.run` cannot hold a pipe, there is no raw TCP, `process.args` carries
  the script path, a module and a directory cannot share a name, an HTTP handler
  refuses the WebSocket upgrade, `time.now()` is a userdata. The research report
  said it was web-derived; treating it as source was the mistake.
- **`fs.watch` cannot be trusted on either platform, and Linux is the worse
  one** — a change below the watched directory produces no event at all there.
  Anything that watches files in a later milestone watches every directory and
  rescans.
- **A gate that can pass while doing nothing keeps being built by accident.**
  This milestone caught three more: the i18n lint looking for a call spelling
  nobody uses, a `luaug test` that would have reported success on a stale report
  file, and a dev server that noticed saves and did nothing because the caller
  supplied no callback. All three now fail loudly.
- **Run `scripts/localgate.ps1` before every push.** Both tiers, ~50 s warm now
  that it carries the conformance suite and the hot-reload gate.
- Carried forward, none blocking:
  - **Three of the five generated artifacts api-design.md §5 lists do not
    exist.** `.luaug/types/std/**` and `.luaug/types/luaug/**` (typed stubs for
    `@std`/`@luaug`), `api-dump.json`, and `docs/reference/**`. The stubs were
    named in M3's own goal and planned as fan-out; the api-dump was assigned to
    M3 by M2's Decision 12 and never picked up by M3's brief, which is how it
    fell between the two. None blocked M3's gate — the gate asks for defs that
    lint clean and are diff-checked, and those are (1). Recorded here because a
    list that reads as present tense is how an artifact stays missing without
    anyone deciding that it should be.
  - **The shipping profile does not configure.** `engine/script/src/modules.cpp`
    and `runtime.cpp` include `<luacode.h>` unconditionally while
    `LUAUG_LUAU_COMPILER` is forced off in shipping (ADR 0002).
  - **`architecture.md` §9 lists a clang-format gate that does not exist.**
  - **`Luau.Analysis` is still compiled and never linked** — carried from M0.
  - **DXIL produced on Linux is never verified as signed.**

## Blocked — needs human

- (none)

## Decisions pending ADR

- (none — ADR 0035 was written at M3 kickoff)

## Session Log

Entries for the planning session and for M0, M1 and M2 are in
[`docs/progress-archive/2026-08.md`](docs/progress-archive/2026-08.md), moved
there when this file passed its ~300-line cap.

- **2026-08-20 (session 6, Claude Opus): M2 signed off; opened M3.** Ran the §2
  boot sequence — the ledger and the repo agreed, and the M2 gate re-ran green on
  both tiers locally (20/20; Windows 9.8 s, Linux 15.2 s). Deep-read the roadmap's
  M3 section, ADR 0024 and ADR 0003, `docs/research/lute-2026.md`, api-design §3,
  §4, §5 and §6, and architecture §3, §5 and §8. Wrote
  `docs/briefs/m3-kickoff.md`: the scope checklist, an explicit NOT-in-scope list
  naming nine things this milestone will not do, the subagent split, the gate
  verbatim, six entering risks, and thirteen numbered decisions each naming the
  alternative it rejects.
  Learned at planning time, before any code: **the transport api-design §3.2
  specifies would put an RFC 6455 server inside the engine** — a TCP listener, a
  handshake and a parse surface in every dev build — to serve one tool that
  already sits on the other end of a pipe, so the control channel is stdio to the
  engine and the WebSocket stays in the dev server where ADR 0003 put Lute (ADR
  0035, §3.2 corrected in the same commit). **The state bag and the preserved
  instances cannot be preserved *in* Luau or *in* the world**, because a reload
  destroys both — they are captured into C++ and restored before the new entry
  scripts are deferred, which is a spec ruling §3.2 did not carry. **`luaug test`
  must read a report file rather than the console**, because every line the engine
  prints is catalog-resolved and parsing it would break the first time a locale is
  added. And the Tier-2 container has no Lute at all, which the gate's phrase "on
  both tiers" quietly requires.
  Then did exactly that, and it **reversed Decision 1 before any code existed**.
  `process.run` returns only when the child exits and hands back whole strings —
  no child handle, no stdin, no incremental read (U-55) — and Lute has no raw TCP
  at all (U-57), so a pipe protocol is not writable and WebSocket is the only
  bidirectional push channel it can speak. api-design §3.2 was right and my
  argument against it was not. What was still open is which side listens, and the
  answer is the opposite of what §3.2 implies: the dev server listens, the engine
  dials out as a WebSocket client, and the engine opens no port in any profile.
  ADR 0035 rewritten, §3.2 corrected, and the first draft kept in the ADR under
  "What this ADR nearly said" because the next reader will have the same idea for
  the same good reasons.
  Also learned by probing rather than reading: `process.run` **yields** the
  calling coroutine, so one task can run the engine while another serves (U-56);
  `fs.watch` names the top-level entry rather than the changed file — a nested
  edit reports the *directory* — and one save fires between two and six events
  (U-58), so the watcher watches every directory and rescans instead of trusting
  the payload; and `time.since` returns a number where the installed typedef says
  `Duration` (U-59).
  Then wrote the protocol into the brief and built the transport under it.
  `engine/net` is L2, the first slice of the `net_api` the architecture has
  always reserved: the pure half (framing, handshake, accept) tested against RFC
  6455's own published examples -- the worked handshake in §1.3 and the seven
  frames in §5.7 -- and the socket half tested against a loopback server that
  sends the frames a real server never would. 28 cases, 214 assertions.
  Learned in the doing: **a test that asserted a digest against a value I had
  written from memory failed, and the code was right** -- FIPS publishes no
  55-byte vector, so the expectation now carries what Windows CNG computes and
  says so. The handshake-failure test asserted on message text without loading
  the catalog, which is M2 Finding 11 arriving on schedule. The ping test waited
  for a Close the client had no reason to send and **hung the suite instead of
  failing it**; the helper takes a read deadline now, because a hang costs a CI
  runner its whole timeout to say nothing. And Clang caught a `socklen_t` MSVC
  did not -- the address length is `int` on Winsock and unsigned on POSIX -- the
  third time in this family of milestones that the Linux tier was the only thing
  between an implicit conversion and `main`.
  Then built the reload seam. `app::reloadWorld` destroys and rebuilds
  `WorldHost` and nothing above it, so ADR 0024's "engine-side content survives"
  is a C++ lifetime rather than a promise; the frame loop holds the host by
  pointer for exactly that reason. The assertion that matters is one line --
  `afterReload == hashOfColdBoot(project, kTicks)` -- paired with one that the
  hash actually moved, because either alone passes against a reload that did
  nothing.
  Learned by running it: **a syntax error does not fail `boot`**. `startScripts`
  logs it, skips that script and carries on, which is right for a boot and wrong
  for a reload -- the first version reported a successful reload of a world in
  which the edit had never run. A reload is now stricter than a boot, because
  unlike a boot it has the world that was already running to fall back on, and
  the asymmetry is written into `reload.h`.
  Then `HotReloadService` and the bag. It is in the IDL with the `DevOnly` tag,
  the bag lives in C++ because `lua_close` takes a Luau table with it, and a
  value that cannot cross -- a function, an Instance, a cyclic table, a table
  that is half an array -- raises instead of being dropped, because a reload that
  quietly loses state is discovered a save later and blamed on the reload.
  Learned before any C++ existed: **`api-design.md` §3.2 named three members the
  API definition's own §9 lints reject.** `BeforeReload`/`AfterReload` are
  neither past-tense facts nor `Pre*`/`Post*` phases, and `IsReload: boolean` is
  a boolean property carrying the prefix §9 reserves for boolean methods.
  `apicheck` said so the moment the service was declared. The rules were right;
  the surface is `PreReload` / `PostReload` / `IsReload()`, and making the third
  a method deleted the service's only property and the component behind it -- the
  implementation got *smaller* by obeying the naming rule.
  Then `PreserveOnReload`: tagged instances are captured as descriptions --
  class, name, ancestry, writable properties, attributes, tags, subtree -- and
  re-created before the new entry scripts are deferred, with an ancestor the new
  world lacks *replayed* with the class it had rather than invented as a Folder.
  Skips are counted, never silent, and child order is pinned by a case because
  it is observable through the world hash.
  Learned, and it was about the test rather than the feature: the first version
  set `Anchored` on a `Part`, which no class declares -- BasePart's physics half
  lands with Jolt in M5 -- so the entry script died before setting `Parent` and
  the failure surfaced three assertions later as a missing child. **M2's Finding
  14 arriving in a new file.** The `Captured` fixture now keeps error lines apart
  and the session requires there were none, so the next version of that mistake
  fails on the line that caused it.
  Next: **the control loop in `app`** -- the `--dev-control` flag, the WebSocket
  client on its own thread, and the reload applied at the FrameStart safe point.

- **2026-08-20 (session 6, continued): M3 closed.** The control channel, the
  CLI, the dev server, the starter template, R3's second half in the i18n lint,
  and the end-to-end hot-reload gate. `luaug dev` on the M2 example reloads a
  saved file in **1.7 ms** against ADR 0024's 500 ms, and the gate asserts the
  strong claim rather than the easy one: a reloaded world hashes the same as a
  cold boot of the edited source at the same tick.
  Learned, and six of these are Lute rather than us: `process.run` cannot hold a
  pipe to a running child and there is no raw TCP (U-55, U-57), which reversed
  the brief's Decision 1 before any code existed; `process.args` carries the
  script path and the `--` separator (U-60); a module and a sibling directory
  cannot share a name (U-61); `net.server.serve` refuses the WebSocket upgrade
  whenever an HTTP handler is present, whatever it returns (U-62); `time.now()`
  is a userdata `string.format` will not take (U-63); and **`fs.watch` on Linux
  reports nothing at all for a change below the watched directory** where
  Windows at least named it (U-58) — so watching every directory individually is
  the only design that works on both, and trusting the event payload would have
  left the Linux dev loop silently broken.
  Ours, and the sharpest one: **the engine's `hello` was an event where it had to
  be state.** The dev server relayed it to whoever was connected at that instant,
  so a client that finished its own handshake milliseconds later waited forever
  for an engine that was already attached -- a flake one run in four whose
  symptom named the thing that was working. Found by running the suite eight
  times instead of once. Also **a headless dev session had nobody to stop it** and ran forever once its
  control connection dropped, which is entering risk 4 arriving from a direction
  the risk did not describe; **the i18n lint was looking for `makeError(` in an
  engine that only writes `core::makeError(`** and reported a clean tree because
  it matched nothing; and **the synthetic headless clock is wrong for a dev
  session**, which ran 37,000 ticks before the first request for tick 40 finished
  crossing the socket.
  Next: **stop for M3 human review** (§6). Do not start M4 this session.


<!-- Format for future entries:
- **YYYY-MM-DD (session N):** did X; learned Y; Next: <literal first action>.
-->
