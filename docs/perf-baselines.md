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

| Milestone | Scene | Preset | Metric | Value | Budget/Gate |
|---|---|---|---|---|---|
| M2 | `tests/bench/instances500` (500 parts in 10 models, one CFrame write each per tick) | `win-msvc-dev` | mean sim tick | **0.134 ms** | 4 ms — the roadmap's "500-instance scene ticks under budget" |
| M2 | `tests/bench/instances500` | `win-msvc-dev` | worst sim tick | 0.357 ms | — |
| M2 | `tests/bench/churn10k` (10,000 parts, 1,000 listeners, two thirds moving) | `win-msvc-dev` | mean sim tick | **2.02 ms** | 16 ms — the property-churn CI threshold |
| M2 | `tests/bench/churn10k` | `win-msvc-dev` | worst sim tick | 2.95 ms | — |
| M3 | `tests/hotreload` one-script project | `win-msvc-dev` | reload span | **0.9 ms** | 500 ms — ADR 0024's hard requirement |
| M3 | `tests/hotreload` 500-instance project (5 models × 100 parts, all moving) | `win-msvc-dev` | reload span, worst of 3 | **1.6 ms** | 500 ms |
| M3 | `tests/hotreload` 500-instance project | `linux-clang-dev` (container) | reload span, worst of 3 | 0.7 ms | 500 ms |

### M4 — the renderer

Captured with `luaug-host <project> --headless --width=1920 --height=1080
--frames=400 --exit --frame-stats`, median of the 390 frames after ten warm-up
frames, three runs. The spread across the three runs was under 5%.

**Headless, and it matters for reading these numbers.** A windowed run presents
through the swapchain and is pinned to the refresh rate, which would report
16.67 ms for any scene this side of impossible -- a measurement of the monitor.
Headless renders the identical pass list into an offscreen target and never
presents, so what is timed is the frame's own work. What it therefore excludes:
present, vsync, and swapchain acquisition.

**Draw calls and triangles are recorded beside frame time** because the roadmap
asks for the *why* next to the *what*: a frame that got slower with the same
draw count is a different problem from one that got slower because it drew more.

| Milestone | Scene | Preset | Metric | Value | Budget/Gate |
|---|---|---|---|---|---|
| M4 | `examples/02-meshes` (4 meshes, 4 materials, sun + 1 point light, shadow map, HDR + tonemap) | `win-msvc-dev` | median frame, 1080p | **0.46 ms** | 16.7 ms — a 60 fps frame |
| M4 | `examples/02-meshes` | `win-msvc-dev` | worst frame | 2.06 ms | — |
| M4 | `examples/02-meshes` | `win-msvc-dev` | draws / triangles | 8 / 48 | — |
| M4 | `examples/02-meshes`, **pinned to 2 cores** | `win-msvc-dev` | median frame, 1080p | 0.42 ms | — |
| M4 | `examples/02-meshes`, pinned to 2 cores | `win-msvc-dev` | worst frame | 6.65 ms | — |
| M4 | `tests/rendercapture/meshes` (the gate scene, 2 mesh parts) | `win-msvc-dev` | median frame, 1080p | 0.46 ms | — |

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
