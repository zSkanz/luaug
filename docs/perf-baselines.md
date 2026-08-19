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
- **Capture:** headless where possible (`--headless --ticks N` for sim
  numbers; windowed scripted runs for frame times), release/`dev` preset
  stated per row, 3 runs, median reported. Frame-time histograms (not just
  averages) for anything gate-relevant; hitch = frame > 33 ms.
- **Storage:** one table per milestone, appended — never rewrite history.
  The CI perf smoke reads the latest table for its thresholds.

## Reference machine

_(recorded at M1 — do not fill by hand before then)_

## Baselines

| Milestone | Scene | Preset | Metric | Value | Budget/Gate |
|---|---|---|---|---|---|
| _(M1+)_ | | | | | |

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
