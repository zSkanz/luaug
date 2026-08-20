# Performance Baselines

Milestone gates compare against the numbers recorded here (roadmap: no >10%
regression vs the previous milestone until M8; absolute targets bind at M8).

## Methodology

- **Reference machine:** recorded at M1 the first time a perf number is
  captured (CPU, GPU, RAM, OS build, driver version) — append it below and
  never change it silently; a hardware change is an ADR + full re-baseline.
- **Fixed scenes:** each baseline names its scene (an example + a scripted
  camera/input path). Scenes are deterministic (seeded) so numbers are
  comparable.
- **Capture:** headless where possible (`luaug-host --bench=tests/bench` for
  sim numbers; windowed scripted runs for frame times), release/`dev` preset
  stated per row, 3 runs, median reported. Frame-time histograms (not just
  averages) for anything gate-relevant; hitch = frame > 33 ms.
- **A quiet machine, and it matters more than it sounds.** `churn10k` measured
  3.50 ms/tick while the Docker tier of `scripts/localgate.ps1` was building in
  the background, and 2.02 ms/tick three runs in a row once it finished — a 73%
  error, larger than any regression worth recording. Numbers taken beside
  another build are not numbers.
- **Storage:** one table per milestone, appended — never rewrite history.
  The CI perf smoke reads the latest table for its thresholds.
- **A quiet machine should be enforced, not requested.** The paragraph above is
  discipline written down, and the error it describes (73%) is larger than any
  regression worth recording. Sampling machine load before and after a run and
  marking a row suspect when the two disagree turns that discipline into
  something a bad baseline cannot slip past — the same move this repository
  already made with generated-file freshness and declared-vs-bound coverage.
- **Record a reduced-CPU row beside the reference one.** Every number here comes
  from a fast desktop, and a factor of 30 to budget there can be a factor of 3
  on a modest target — which is the scenario R16 exists for, since iOS has no
  JIT. Re-running the same benchmark pinned to a subset of cores costs a flag
  and gives the absolute targets that bind at M8 somewhere to fail early.

## Reference machine

Recorded 2026-08-20 at M2, the first milestone with a simulation to measure.
Changing any of it is an ADR plus a full re-baseline (see Methodology).

| | |
|---|---|
| CPU | Intel Core i5-14600K, 14 cores / 20 threads |
| GPU | NVIDIA GeForce RTX 4070 Ti SUPER, driver 32.0.16.1047 |
| RAM | 64 GB |
| OS | Windows 11 Pro, 10.0.26200 build 26200 |
| Toolchain | MSVC 14.50.35717, `win-msvc-dev` (RelWithDebInfo, profile `dev`) |

## Baselines

### M2 — simulation kernel

Captured with `luaug-host --bench=tests/bench --bench-repeats=5`, median of
5 runs, three times over; the spread across those three was under 1.5%.

**What fifty characters cost, and whether there is a ceiling.** Asked during
M5's review, on the concern that character-against-character is O(n²). It is
not, and the measurement is why: a `CharacterBody` collides with another one
through the rigid body inside its capsule, which the broad phase indexes like
any other. A registration list would have been quadratic; a tree query is not.
Measured on the reference machine by scaling `crowd50`'s scene, five repeats
each:

| Characters | Mean sim tick | Per character |
|---|---|---|
| 10 | 0.026 ms | 2.6 µs |
| 50 | 0.209 ms | 4.2 µs |
| 100 | 0.475 ms | 4.7 µs |
| 200 | 0.939 ms | 4.7 µs |

Four times the crowd costs four and a half times the tick, and the per-character
cost stops climbing at around 4.7 µs — that is linear with a constant, which is
what a broad phase buys. **There is no ceiling in the engine**: nothing refuses
the fifty-first character, and the honest limit is the tick budget. Two hundred
of them, all touching, is 0.94 ms of a 16.7 ms frame. Only `crowd50` is
committed as a gate; the other three points were measured outside the repository
so that CI pays for one scene rather than four.

| Milestone | Scene | Preset | Metric | Value | Budget/Gate |
|---|---|---|---|---|---|
| M2 | `tests/bench/instances500` (500 parts in 10 models, one CFrame write each per tick) | `win-msvc-dev` | mean sim tick | **0.134 ms** | 4 ms — the roadmap's "500-instance scene ticks under budget" |
| M2 | `tests/bench/instances500` | `win-msvc-dev` | worst sim tick | 0.357 ms | — |
| M2 | `tests/bench/churn10k` (10,000 parts, 1,000 listeners, two thirds moving) | `win-msvc-dev` | mean sim tick | **2.02 ms** | 16 ms — the property-churn CI threshold |
| M2 | `tests/bench/churn10k` | `win-msvc-dev` | worst sim tick | 2.95 ms | — |
| M3 | `tests/hotreload` one-script project | `win-msvc-dev` | reload span | **0.9 ms** | 500 ms — ADR 0024's hard requirement |
| M3 | `tests/hotreload` 500-instance project (5 models × 100 parts, all moving) | `win-msvc-dev` | reload span, worst of 3 | **1.6 ms** | 500 ms |
| M3 | `tests/hotreload` 500-instance project | `linux-clang-dev` (container) | reload span, worst of 3 | 0.7 ms | 500 ms |

### M5 — the world gets mass

Captured with `luaug-host --bench=tests/bench --bench-repeats=5`, median of 5
runs, three times over; the spread across those three was under 3%.

**Every simulation number in this table is measured against a scene that now
contains rigid bodies, and the M2 rows are not comparable to it.** A `BasePart`
that is not `Anchored` is a Jolt body from this milestone on, so `instances500`
and `churn10k` did not get slower doing the same work -- they are doing more of
it, in a world that has a simulation in it. Anchoring their parts is what keeps
what they were written to measure measurable (see `churn10k`'s own comment); the
physics cost that remains is the mirror's per-tick sweep over ten thousand
static bodies plus Jolt's own broad-phase pass over them.

**The physics tick is recorded in three stages**, which is the roadmap's ask
("one number says a budget was missed and three say which stage missed it")
answered with the three stages that are separable at this seam: `apply` is the
scene's writes going down, `step` is the solver, `writeback` is the result
coming back. It is not broadphase / narrowphase / solver, and `UNCONFIRMED.md`
U-56 records why -- Jolt exposes that split only through a profiler that dumps
to a file and taxes every configuration to enable.

| Milestone | Scene | Preset | Metric | Value | Budget/Gate |
|---|---|---|---|---|---|
| **M5** | `tests/bench/physics1k` (1,000 active bodies: 25 towers of 40 crates, so the islands stay awake) | `win-msvc-dev` | mean sim tick | **2.02 ms** | 16 ms — the roadmap's "physics tick budget for 1,000 active bodies" |
| M5 | `tests/bench/physics1k` | `win-msvc-dev` | worst sim tick | 4.51 ms | — |
| M5 | `tests/bench/physics1k` | `win-msvc-dev` | physics: apply / step / writeback | 0.024 / 1.78 / 0.214 ms | — |
| M5 | `tests/bench/instances500` (500 parts, one CFrame write each per tick, now also 500 static bodies) | `win-msvc-dev` | mean sim tick | **0.62 ms** | 4 ms |
| M5 | `tests/bench/instances500` | `win-msvc-dev` | physics: apply / step / writeback | 0.081 / 0.313 / 0.078 ms | — |
| M5 | `tests/bench/churn10k` (10,000 anchored parts, 1,000 listeners, two thirds moving) | `win-msvc-dev` | mean sim tick | **4.96 ms** | 16 ms |
| M5 | `tests/bench/churn10k` | `win-msvc-dev` | worst sim tick | 9.13 ms | — |
| M5 | `tests/bench/churn10k` | `win-msvc-dev` | physics: apply / step / writeback | 1.60 / 1.23 / 0.026 ms | — |
| M5 | `tests/bench/crowd50` (50 `CharacterBody` shoulder to shoulder, all walking into a wall, so the crowd stays a crowd) | `win-msvc-dev` | mean sim tick | **0.22 ms** | 16 ms |
| M5 | `tests/bench/crowd50` | `win-msvc-dev` | worst sim tick | 0.68 ms | — |
| M5 | `tests/bench/crowd50` | `win-msvc-dev` | physics: apply / step / writeback | 0.191 / 0.019 / 0.004 ms | — |
| M5 | `examples/03-physics-playground` (the deliverable: 18 dynamic crates, a seesaw, ramps, a character, the Jolt wireframe on) | `win-msvc-dev` | median frame, 1080p | **1.11 ms** | 16.7 ms — a 60 fps frame |
| M5 | `examples/03-physics-playground` | `win-msvc-dev` | worst frame | 1.93 ms | — |
| M5 | `examples/03-physics-playground` | `win-msvc-dev` | draws / triangles | 0 / 0 — every part is a debug wireframe (D022) |

**The mirror costs about 160 ns per body per tick to decide that nothing
changed**, which is the 1.60 ms `apply` row above over ten thousand static
bodies. Two cheap wins were taken while measuring -- the in-world test is
memoised by parent, and the body records moved from a hash map to a
slot-indexed vector walked in the same ascending order the component pool is,
which together took `apply` from 2.27 ms to 1.60 ms and `writeback` from 0.127
to 0.026. What remains is a dirty-flag design: the mirror rebuilds a
`BodyDesc` per body per tick and compares it, where a scene that changes
nothing should touch nothing. That belongs with M7, which is the milestone that
puts tens of thousands of objects in a world and streams them.

### M4.5 — the renderer, re-measured against a scene it actually reads

Captured with `luaug-host <project> --headless --width=1920 --height=1080
--frames=400 --exit --frame-stats`, median of the 389 frames after ten warm-up
frames, three runs. The spread across the three runs was under 4% (0.4530,
0.4578 and 0.4682 ms).

**Headless, and it matters for reading these numbers.** A windowed run presents
through the swapchain and is pinned to the refresh rate, which would report
16.67 ms for any scene this side of impossible -- a measurement of the monitor.
Headless renders the identical pass list into an offscreen target and never
presents, so what is timed is the frame's own work. What it therefore excludes:
present, vsync, and swapchain acquisition.

**Draw calls and triangles are recorded beside frame time** because the roadmap
asks for the *why* next to the *what*: a frame that got slower with the same
draw count is a different problem from one that got slower because it drew more.

**The M4 rows below are superseded and kept.** Every one of them was measured
against a renderer that never read `Lighting`: the sun stood straight up, fog was
off, and the shadow map was built from a direction nothing in the scene had
asked for. A number recorded against a defect is not a baseline, and M5's "no
regression greater than 10%" clause would have been measured against it. They
stay visible rather than being edited in place, because a superseded measurement
that quietly becomes the current one is how a baseline stops meaning anything.

| Milestone | Scene | Preset | Metric | Value | Budget/Gate |
|---|---|---|---|---|---|
| **M4.5** | `examples/02-meshes` (4 meshes + a transparent pane, 5 materials, sun + 1 point light, shadow map, opaque and blended passes, HDR + tonemap) | `win-msvc-dev` | median frame, 1080p | **0.46 ms** | 16.7 ms — a 60 fps frame |
| M4.5 | `examples/02-meshes` | `win-msvc-dev` | worst frame | 1.79 ms | — |
| M4.5 | `examples/02-meshes` | `win-msvc-dev` | draws / triangles | 10 / 60 | — |
| M4.5 | `examples/02-meshes`, **pinned to 2 cores** | `win-msvc-dev` | median frame, 1080p | 0.42 ms | — |
| M4.5 | `examples/02-meshes`, pinned to 2 cores | `win-msvc-dev` | worst frame | 1.77 ms | — |
| M4.5 | `tests/rendercapture/meshes` (the gate scene) | `win-msvc-dev` | median frame, 1080p | 0.48 ms | — |
| ~~M4~~ | `examples/02-meshes` (4 meshes, 4 materials, sun + 1 point light, shadow map, HDR + tonemap) | `win-msvc-dev` | median frame, 1080p | 0.46 ms | superseded — measured with `Lighting` unreachable |
| ~~M4~~ | `examples/02-meshes` | `win-msvc-dev` | worst frame | 2.06 ms | superseded |
| ~~M4~~ | `examples/02-meshes` | `win-msvc-dev` | draws / triangles | 8 / 48 | superseded |
| ~~M4~~ | `examples/02-meshes`, pinned to 2 cores | `win-msvc-dev` | median frame, 1080p | 0.42 ms | superseded |
| ~~M4~~ | `examples/02-meshes`, pinned to 2 cores | `win-msvc-dev` | worst frame | 6.65 ms | superseded |
| ~~M4~~ | `tests/rendercapture/meshes` (the gate scene, 2 mesh parts) | `win-msvc-dev` | median frame, 1080p | 0.46 ms | superseded |

**What re-measuring actually changed, and it is worth reading before the next
one.** The median did not move: 0.46 ms then, 0.46 ms now, with a whole second
pass and two more draws added. What moved is the **worst frame, from 2.06 ms to
1.79 ms** — and on two cores from 6.65 ms to 1.77 ms, which is a factor of four.
Nothing in this milestone made a frame cheaper; what changed is that the shadow
map's texel grid no longer slides every frame, so the shadow pass's memory
traffic stopped varying with the camera. The lesson is the one the M4 row already
half-stated: at this scene size the median measures fixed cost and the tail
measures whether something is thrashing. **The tail was the number carrying the
defect, and the median never noticed.**

**What the numbers say, and what they do not.** 0.46 ms is 2.8% of a 60 Hz
frame for a scene with a shadow pass, a sky, a forward PBR pass and a tonemap.
That is not a claim that the renderer is fast: it is a claim that **this scene is
too small to measure a renderer with**. Eight draws and forty-eight triangles
exercise every pass and stress none of them, so what this row actually baselines
is the per-frame *fixed* cost -- pipeline binds, uniform uploads, four render
passes at 1080p -- which is the part that does not go away when a scene grows.
The scene that measures the renderer arrives at M7 with something to stream.

**The reduced-CPU row is the interesting one.** Pinned to two cores the median
does not move (0.42 vs 0.46 ms, inside the run-to-run spread) while the worst
frame triples. So this frame is not CPU-bound at all; what two cores cost is
scheduling tail, not throughput. R16's concern -- that a factor of 30 here can
be a factor of 3 on a device without a JIT -- is not yet visible in this scene,
and recording that it is not visible is the point of the row.

**What the numbers say.** The gate scene sits at 0.8% of a 60 Hz frame, with a
factor of 30 to the budget: at M2 the simulation kernel is not what will make a
frame late. The churn scene is the interesting one — 10,000 property writes and
1,000 subscribed signals per tick cost 2.02 ms, which is 12% of a frame, and it
is that low only because a write that does not change the value raises nothing
(M2 brief, Decision 6). That design choice is worth roughly the whole
measurement: a third of the writes in that scene are no-ops by construction.

**What the reload numbers say, and what they do not.** ADR 0024 set 500 ms as a
hard requirement and the measurement comes in at 1.6 ms — a factor of three
hundred. That is not a triumph; it is a statement about what is being measured.
The span is the FrameStart safe point through `PostReload` returning, on a
project whose scripts compile in under a millisecond, and the whole of the
budget's difficulty was always going to be somewhere else: the bytecode cache
ADR 0024 names and M3 did not need to build, and the assets and shaders a real
project reloads alongside its scripts. What these numbers establish is that the
*mechanism* — destroy a world, build another, carry the state bag and the
preserved instances across — costs nothing worth counting. The budget becomes
interesting again in M4, when a reload has meshes and pipelines behind it.

The Linux number being lower than the Windows one is not a portability finding
either: it is a container with no window, no device and a warm page cache.

**Why the budgets are so loose.** They are catastrophe detectors, not
instruments. A CI runner's speed varies by more than the regression anyone would
want to catch, so a budget tight enough to notice 10% would be red every other
week and would train everyone to ignore it. The threshold catches the change that
made a tick ten times slower; this table is where a 10% regression is actually
visible, and comparing against it is a human's job at each milestone gate.

Standing absolute targets (bind at M8, on the reference machine):
- `examples/10-open-world`: 60 fps at 1080p; zero streaming hitches > 33 ms
  in the scripted 5-minute fly-through; 10-minute soak with bounded memory
  delta and zero crashes.
- Hot reload (`luaug dev`): < 500 ms from file save to behavior change
  (ADR 0024), measured by the M3 E2E test.
- Sim: 500-instance scripted scene (M2 benchmark) and 1,000 active physics
  bodies (M5 benchmark) within their recorded budgets; the
  10k-parts/1k-listeners property-churn benchmark within its CI threshold.
- Script GC: ≤ 1 ms GC step at 60 fps under the M7 streaming scene load.
