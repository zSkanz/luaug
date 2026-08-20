# PROGRESS — LuauG Build Ledger

Fixed format (MASTER_PROMPT.md §11). Hard cap ~300 lines: archive old session
log entries to `docs/progress-archive/YYYY-MM.md`.

## State

- Current milestone: **M3 — Tooling Loop: CLI, Hot Reload, Types, Tests**,
  opened 2026-08-20 (brief: [`docs/briefs/m3-kickoff.md`](docs/briefs/m3-kickoff.md)).
  Built so far: the brief with its fourteen decisions and the control protocol,
  and `engine/net` — the RFC 6455 client the engine dials the dev server with.
- **M2 — Kernel: Instances over ECS, Scheduler, Signals, `task` — COMPLETE and
  SIGNED OFF by the human on 2026-08-20**, tagged `milestone/m2` (brief:
  [`docs/briefs/m2-kickoff.md`](docs/briefs/m2-kickoff.md), which carries the
  full scope record, the Gate Record and twenty-two Findings). Its gate re-ran
  green on both tiers at M3 kickoff: `scripts/localgate.ps1`, 20/20 tests,
  Windows 9.8 s / Linux 15.2 s.
- **M1** signed off 2026-08-19 (`milestone/m1`); **M0** signed off 2026-08-19
  (`milestone/m0`).
- **`main` is 21 commits ahead of `origin/main`** — M2's work has never been
  pushed, so CI has not seen it and macOS (Tier-3, CI-only) has not compiled it
  since M1. Pushing is a human call because Actions minutes are metered on this
  private repo; see Now / Next.

### What exists, in one pass

- **The engine runs a world from a script.** `Instance.new("Part")` builds a
  real instance over the ECS, every datatype is bound, signals deliver deferred
  in one queue with the ordering `api-design.md` §3.1 specifies, `task` runs on
  the SimClock in integer ticks, `game`/`workspace` are globals and services are
  singleton-on-demand. `WorldHost` owns the world, the VM and the project;
  `WorldHost::tick` is the only place architecture §3's resumption order exists
  as code, and the frame loop drives it.
- **The API IDL generates the engine.** `api/defs/*.api.luau` (13 classes, 6
  datatypes, 4 enums) → `gen_dts.luau` → `runtime/types/engine.d.luau` and
  `gen_cpp.luau` → `engine/scene/generated/class_descriptors.gen.{h,cpp}`. Both
  outputs are checked in and freshness-gated, and both gates were proved by
  tampering. 43 methods declared, 43 bound, 0 unbacked, enforced at boot.
- **The picture.** `examples/00-clear` (pulsing clear + orbiting wire cubes) and
  `examples/01-instances` (500 scripted instances in five rings) render through
  the SDL3 GPU backend; F3 toggles an ImGui overlay in dev builds.
- **The harnesses.** 903 conformance cases in 46 files written from
  `docs/api-design.md` by authors forbidden to read `engine/`; `--replay=DIR`
  (determinism, 10,000 ticks, in-process and against a per-platform trace);
  `--bench=DIR` (sim ticks against a per-scenario budget); the capture-stream
  and screenshot render gates. 21 CTest entries, green on Windows and Linux.

### What M3 has to build

The CLI (`luaug dev|test|check|new|fmt`) as Lute scripts, the hot-reload world
restart under 500 ms with the state bag and `PreserveOnReload`, the editor-facing
half of the type pipeline (docs JSON, `@std`/`@luaug` stubs, `.vscode` settings),
the `starter` template, and R3's second half in the i18n lint. The brief's
fourteen numbered decisions are the design; its six entering risks are what to
watch. `engine/net` is done; the reload seam is next.

## Now / Next

- **Next: the reload seam.** Rebuild `WorldHost` at the FrameStart safe point,
  build-before-destroy (brief Decision 14, ADR 0024 addendum), with
  `HotReloadService` and the C++ state bag — and a headless C++ test that asserts
  **reload == cold restart** by world hash (Decision 11), with no network in it
  at all. Then the control loop, then the Lute dev server.
- **The control protocol is written** into the brief (message set, envelope,
  token handshake, the three emptiness checks), and the transport under it is
  built: `engine/net` (L2) has the RFC 6455 client, 28 cases / 214 assertions,
  green on both tiers.
- **`core::json` parses and does not serialize**, and its own header says a
  writer would be unused surface. M3 is what changes that: the engine sends JSON
  control messages and writes the `luaug test` report (Decision 6). The writer
  lands with the first of those, and ADR 0033 gets a line saying why.
- **Lute is now grounded** (U-55…U-59): `process.run` returns only at child exit
  with no stdin and no incremental read, there is no raw TCP, `process.run`
  yields rather than blocking, `fs.watch` names the top-level entry rather than
  the changed file and fires 2–6 times per save, and `time.since` returns a
  number where the typedef says `Duration`. The first two reversed the brief's
  Decision 1 before a line of code existed.
- **`fs.watch` on Linux is still unverified**, and its failure mode there is
  silence rather than an error. Verify it in the same task that puts Lute into
  the Tier-2 image.
- **Ask the human whether to push.** 21 commits sit unpushed. CI proves `main` is
  green and is the only thing that builds macOS; it also spends metered minutes
  on a private repo. The local gate is green on both tiers either way.
- **The Tier-2 container has no Lute** (entering risk 1). `luaug test` green "on
  both tiers" needs rokit + the already-pinned `lute@1.0.0` in
  `scripts/docker/tier2.Dockerfile`. Discover this now, not at the gate.
- **Read the M2 brief's Findings before writing M3 code.** The three that bind
  hardest here: a gate that can pass while doing nothing is not a gate (19); a
  spec that holds a claim across many ticks is worth more than three that hold it
  across one (20); the conformance rule — specs are written against `docs/`,
  never against the implementation (17).
- **Fan out narrow.** M2 proved that large C++ kernel blocks handed to subagents
  stall; tight, interface-frozen tasks land well. The reload seam is
  orchestrator-only work.
- **Run `scripts/localgate.ps1` before every push. Do not use CI as a test
  runner.** Both tiers, ~20 s warm.
- Carried forward, none blocking:
  - **The shipping profile does not configure.** `engine/script/src/modules.cpp`
    and `runtime.cpp` include `<luacode.h>` unconditionally while
    `LUAUG_LUAU_COMPILER` is forced off in shipping (ADR 0002). The guard is one
    line, but a shipping host also needs the bytecode-loading path — which M3's
    reload budget may want anyway (entering risk 3).
  - **`architecture.md` §9 lists a clang-format gate that does not exist.**
    Turning it on means reformatting the tree and pinning a toolchain version;
    the version-pinning half is M3's `luaug check` work.
  - **`Luau.Analysis` is still compiled and never linked** — carried from M0.
  - **DXIL produced on Linux is never verified as signed.** It is also never
    loaded there, so this only matters if a Linux job ever produces a shipping
    shader pack for Windows (M8).

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
  Next: **the reload seam** -- rebuild `WorldHost` at the FrameStart safe point,
  build-before-destroy, with a headless test asserting reload == cold restart by
  world hash. No network in that step at all.

<!-- Format for future entries:
- **YYYY-MM-DD (session N):** did X; learned Y; Next: <literal first action>.
-->
