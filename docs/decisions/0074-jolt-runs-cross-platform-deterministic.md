# 0074 — Jolt runs cross-platform deterministic; the engine's transcendentals are what is left

- Status: accepted
- Date: 2026-09-23
- Milestone: finish-line audit row 4, closed
- Decided by: the agent, under the owner's instruction of 2026-09-23 to settle
  the benchmark questions by what is normal in the industry and to apply it
  where it is.
- Amends: ADR 0025 (determinism levels), `third_party/CMakeLists.txt`'s Jolt
  options

## Context

ADR 0025 records level B: the same build, the same seed and the same operation
sequence give the same world hash. Level C, the same hash across compilers,
operating systems and architectures, was declined for v1 as research-grade.
Jolt's `CROSS_PLATFORM_DETERMINISTIC` switch was left off, with a comment
calling it "the one line that would change that answer" and pricing it, from
upstream's own documentation, at about 8% of the library's speed.

The finish-line audit (row 4) noticed that the evidence pointed somewhere
else. `tests/determinism/example01`'s Windows and Linux traces are
byte-identical, while `tests/determinism/churn`'s agree at tick 0 and part at
tick 500. It specified an experiment instead of an argument: turn the flag on,
regenerate both tiers' `churn` traces, measure the cost, and keep the flag only
if the traces converge.

**Run as written, that experiment could not have answered.** `churn` anchors
every part, so Jolt simulates none of them, and its traces did not move by a
byte with the flag either way. What `churn` does per tick is compose
`CFrame.fromEuler`, and that is where it parts.

## The experiment, run so it can answer

Both tiers (MSVC on Windows, Clang in the Tier-2 container), every scenario,
with the flag off and then on. `character` and `ragdoll`, the two scenarios
Jolt actually drives, were recorded on both platforms for the comparison and
reverted afterwards.

| scenario | what drives it | flag off: win vs linux | flag on: win vs linux |
|---|---|---|---|
| `character` | Jolt's character controller and contacts | **differ** from the first checkpoint | **identical**, every checkpoint |
| `ragdoll` | Jolt constraints plus the engine's pose maths | differ | differ |
| `churn` | the engine's `CFrame` maths, no simulated body | differ | differ, byte-for-byte the traces the flag-off build wrote |
| `example01` | the engine's kernel | identical | identical |

The flag changed the Windows `character` trace and left Linux's alone, and the
Windows trace moved onto the Linux one. MSVC and Clang were computing Jolt's
maths differently, and the switch is exactly what makes them agree.

**What still parts is ours.** `churn` and `ragdoll` go through `std::sin`,
`std::cos` and friends: `CFrame.fromEuler`, the pose's rotations, and, from a
script, Luau's `math` library, which calls the same C runtime. The Microsoft
runtime and glibc round those functions' last bit differently, and a
checkpoint every 150 ticks is plenty of time for one bit to reach the hash.

## The cost, measured

Same machine, same build, `win-msvc-dev`, `--bench-repeats=3`, two runs each
way, the mean of the two:

| bench | measure | flag off | flag on |
|---|---|---|---|
| `physics1k` | physics step | 0.645 ms | 0.649 ms |
| `churn10k` | physics step | 1.808 ms | 1.809 ms |
| `ragdoll10` | physics step | 0.136 ms | 0.134 ms |
| `platforms200` | physics step | 0.108 ms | 0.108 ms |

That is under 1% where it shows at all, inside the noise elsewhere. Upstream's
8% is a worst case measured on its own samples, and this engine's bodies spend
their time in the broad phase and the solver rather than in the maths the
switch slows down.

## Decision

1. **`CROSS_PLATFORM_DETERMINISTIC` is ON.** Jolt's part of a simulation now
   gives the same bytes on Windows and Linux, measured, and upstream promises
   the same across architectures, which only the macOS job can confirm. The
   cost is below what this machine can measure.
2. **The guarantee stays at level B.** One of three sources of divergence is
   gone; claiming level C while the other two remain would be the kind of
   promise R10 exists to refuse. Each tier keeps gating against its own traces,
   and `character` and `ragdoll` stay `sameBuildOnly`: turning either into a
   committed trace needs a macOS recording, which only CI can make.
3. **Level C is now a bounded piece of work, and it is written down rather
   than taken.** What is left is two things:
   - **the engine's own transcendentals**: `sin`, `cos`, `tan`, `atan2`,
     `acos`, `asin`, `exp`, `log` and `pow` on the simulation path, routed
     through one `core` implementation (range reduction and minimax
     polynomials, compiled with contraction off) instead of each platform's
     runtime;
   - **Luau's `math` library**: the same functions registered over the
     built-ins, with those built-ins named in the compiler's
     `disabledBuiltins` so a fast call cannot bypass them.
   Nothing in `third_party/` changes for either one.

## Why not take level C now

**Because nothing the engine does needs it yet, and the industry does not ship
it by default.** Server-authoritative multiplayer (ADR 0069) sends the world,
not the inputs, so a replica never re-simulates the server's hash. Client
prediction corrects itself from snapshots, so it needs a close answer, not an
identical one. The large engines' physics (PhysX, Havok, Chaos) promise determinism per
build at most, and a lockstep game on any of them owns its maths. What level C
would buy is a replay recorded on Windows and played back on a Mac, and a
lockstep or rollback netcode. Both are worth having, neither is on the roadmap,
and the two items above are the whole distance when one is.

## Consequences

- Flipping the flag re-records no committed trace: `churn` and `example01` are
  byte-identical to what the flag-off builds wrote on both tiers.
- Jolt's comment in `third_party/CMakeLists.txt` now describes the switch as
  on, and why.
- `docs/perf-baselines.md` records the A/B above.
