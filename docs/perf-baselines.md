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

**What the numbers say.** The gate scene sits at 0.8% of a 60 Hz frame, with a
factor of 30 to the budget: at M2 the simulation kernel is not what will make a
frame late. The churn scene is the interesting one — 10,000 property writes and
1,000 subscribed signals per tick cost 2.02 ms, which is 12% of a frame, and it
is that low only because a write that does not change the value raises nothing
(M2 brief, Decision 6). That design choice is worth roughly the whole
measurement: a third of the writes in that scene are no-ops by construction.

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
