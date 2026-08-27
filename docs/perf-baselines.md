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

**What a horde costs, and where the ceiling actually is.** Asked on 2026-08-20
as "could I build a survivors-like on this engine, it has to be optimized", and
answered by building one and measuring it rather than by estimating. The scene:
N enemies as anchored `MeshPart`s whose `Position` is written from Luau **every
tick**, each chasing a `CharacterBody` player that circles so the horde never
settles; one sun with shadows; 1080p; the default SDL3 GPU backend; measured
with `--frame-stats`, 300 frames, first ten dropped as warm-up.

| Enemies | Simulation | Shadow pass | Forward pass | Visible draws | Frame |
|---|---|---|---|---|---|
| 200 | 0.69 ms | 0.70 ms | 1.23 ms | 400 | **2.63 ms** |
| 500 | 0.87 ms | 1.78 ms | 2.60 ms | 990 | **5.25 ms** |
| 1,000 | 1.18 ms | 2.59 ms | 4.07 ms | 1,560 | **7.84 ms** |
| 2,000 | 1.84 ms | 2.40 ms | 6.86 ms | 2,092 | **11.10 ms** |

The simulation column is the whole engine below the renderer — the Luau chase
loop over every enemy, the Instance writes, the scene walk, physics — and two
thousand enemies cost 1.84 ms of it. That is not the ceiling and this table is
the evidence.

**The ceiling is one draw call per visible object**, and the proof is that the
2,000-enemy scene costs the same at every resolution:

```
 320x180 : median 10.21 ms
1920x1080: median 10.22 ms
3840x2160: median 10.22 ms
```

Identical, for a scene with 12,552 triangles in it. The GPU is idle; the frame
is CPU-side submission at roughly **2.6–3.3 µs per visible draw**, one
`bindUniforms` and one `draw` each (`renderer_default.cpp:407`), with nothing
batching the two thousand objects that share a single mesh. Off-screen casters
are already culled against the shadow radius (`render_world.cpp:282`), so the
shadow column is bounded rather than growing with the horde — the leak that was
looked for and is not there.

**So a survivors-like with two thousand enemies runs at 60 Hz today**, with five
milliseconds to spare, and nearly all of the spent budget is the one thing
M7.5 now names as scope: instanced draws. Enemies that *collide* with each other
rather than merely being drawn cost 4.7 µs each on top (below), which is why the
scene above writes positions instead — the same architecture the genre uses.

Not committed as a gate scene: it was measured outside the repository, because
M7.5 is where it becomes a number something defends.

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

**Where a budget in this table actually lives, because the answer is not
"CI".** Every one of them is the `budgetMs` in that scene's own
`tests/bench/<name>/scenario.json`, and the thing that enforces it is the
`perf_budget` CTest -- which runs in the LOCAL gate, on the machine these
numbers were measured on, and in CI as one of the same suite. A row that said
"the CI threshold" was describing a place rather than a mechanism, and it was
describing the wrong place: nothing in `.github/workflows` knows what a bench
scene costs.

**And the budgets are catastrophe detectors, not regression detectors.**
`bench.h` says so and this table is the reason it can: a runner's speed varies
by more than any regression worth catching, so the gate fails on something being
broken and the numbers below are where a change of a millisecond is noticed by a
person reading a diff. That is why `churn10k`'s budget is 32 ms against a
measured 7 -- more than four times the headroom, deliberately.

| Milestone | Scene | Preset | Metric | Value | Budget/Gate |
|---|---|---|---|---|---|
| M2 | `tests/bench/instances500` (500 parts in 10 models, one CFrame write each per tick) | `win-msvc-dev` | mean sim tick | **0.134 ms** | 4 ms — the roadmap's "500-instance scene ticks under budget" |
| M2 | `tests/bench/instances500` | `win-msvc-dev` | worst sim tick | 0.357 ms | — |
| M2 | `tests/bench/churn10k` (10,000 parts, 1,000 listeners, two thirds moving) | `win-msvc-dev` | mean sim tick | **2.02 ms** | 32 ms — `churn10k`'s own `budgetMs`, checked by the `perf_budget` test |
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
| M5 | `tests/bench/churn10k` (10,000 anchored parts, 1,000 listeners, two thirds moving) | `win-msvc-dev` | mean sim tick | **4.96 ms** | 32 ms |
| M5 | `tests/bench/churn10k` | `win-msvc-dev` | worst sim tick | 9.13 ms | — |
| M5 | `tests/bench/churn10k` | `win-msvc-dev` | physics: apply / step / writeback | 1.60 / 1.23 / 0.026 ms | — |
| M5 | `tests/bench/crowd50` (50 `CharacterBody` shoulder to shoulder, all walking into a wall, so the crowd stays a crowd) | `win-msvc-dev` | mean sim tick | **0.22 ms** | 16 ms |
| M5 | `tests/bench/crowd50` | `win-msvc-dev` | worst sim tick | 0.68 ms | — |
| M5 | `tests/bench/crowd50` | `win-msvc-dev` | physics: apply / step / writeback | 0.191 / 0.019 / 0.004 ms | — |
| M5 | `examples/03-physics-playground` (the deliverable: 18 dynamic crates, a seesaw, ramps, a character, the Jolt wireframe on) | `win-msvc-dev` | median frame, 1080p | **1.11 ms** | 16.7 ms — a 60 fps frame |
| M5 | `examples/03-physics-playground` | `win-msvc-dev` | worst frame | 1.93 ms | — |
| M5 | `examples/03-physics-playground` | `win-msvc-dev` | draws / triangles | 0 / 0 — every part is a debug wireframe (D022) |
| **M6** | `tests/bench/platforms200` (200 platforms written every tick, 200 written every 15th so each transitions both ways 20 times, 600 anchored parts nobody writes) | `win-msvc-dev` | mean sim tick | **0.60 ms** | 16 ms |
| M6 | `tests/bench/platforms200` | `win-msvc-dev` | worst sim tick | 2.8 ms | — |
| M6 | `tests/bench/platforms200` | `win-msvc-dev` | physics: apply / step / writeback | 0.14 / 0.29 / 0.08 ms | — |
| M6 | `tests/bench/churn10k` **after D031** (the same scene: two thirds of its anchored parts are written every tick, so two thirds of them are now KINEMATIC) | `win-msvc-dev` | mean sim tick | **7.32 ms** | 32 ms |
| M6 | `tests/bench/churn10k` | `win-msvc-dev` | physics: apply / step / writeback | 0.43 / 3.89 / 0.78 ms | — |
| M6 | `tests/bench/churn10k` | GitHub `windows-latest` | mean sim tick | 19.6 ms | the runner is 2.7x this machine, which is why the budget is a detector and this file is the instrument |
| M6 | `examples/04-obby` (the deliverable: the course, two tweened platforms, a skinned rig, a `ScreenGui` with a list layout, five sounds) | `win-msvc-dev` | median frame, 1080p | **0.53 ms** | 16.7 ms — a 60 fps frame |
| M6 | `examples/04-obby` | `win-msvc-dev` | worst frame | 1.84 ms | — |
| M6 | `examples/04-obby` | `win-msvc-dev` | draws / triangles | 15 / 172 — solid parts now, where M5's playground was 0 / 0 |
| M6 | `luaug_ui_tests` (a laid-out tree, then two more frames touching nothing) | `win-msvc-dev` | `layoutStats().solverRuns` on an idle frame | **0** | 0 — asserted, not measured |
| **E9** | `tests/bench/sockets200` (200 anchored posts, each with a free arm on a `BallSocketConstraint`; half limited, half not) | `win-msvc-dev` | mean sim tick | **0.66 ms** | 16 ms |
| E9 | `tests/bench/sockets200` | `win-msvc-dev` | worst sim tick | 1.11 ms | — |
| E9 | `tests/bench/sockets200` | `win-msvc-dev` | physics: apply / step / writeback | 0.019 / 0.584 / 0.051 ms | — |
| **E9** | `tests/bench/ragdoll10` (10 humanoids, 160 bodies and 150 joints in **10 islands**, dropped and still moving at tick 300) | `win-msvc-dev` | mean sim tick | **0.31 ms** | 16 ms |
| E9 | `tests/bench/ragdoll10` | `win-msvc-dev` | worst sim tick | 0.75 ms | — |
| E9 | `tests/bench/ragdoll10` | `win-msvc-dev` | physics: apply / step / writeback | 0.012 / 0.262 / 0.034 ms | — |
| E9 | `tests/bench/physics1k` **after the constraint family** (the same scene, unchanged) | `win-msvc-dev` | mean sim tick | **2.00 ms** | 16 ms — unchanged from M5's 2.02, which is the claim |
| E9 | `tests/bench/churn10k` **after the constraint family** (the same scene, unchanged) | `win-msvc-dev` | mean sim tick | **6.98 ms** | 32 ms — 7.32 at M6, so the three new per-tick passes cost a scene with no joints in it nothing |

**A ragdoll is cheaper than two hundred sockets, and the ratio is the point.**
`sockets200` is 400 bodies in 200 two-body islands and costs 0.58 ms of solver;
`ragdoll10` is 160 bodies in 10 sixteen-body islands and costs 0.26 ms. Per BODY
the ragdoll is slightly dearer -- a sixteen-body island has to be solved together
every iteration, where two hundred two-body islands are two hundred independent
problems the solver splits apart -- and per SCENE it is far cheaper, because ten
characters is a hundred and sixty bodies and two hundred joints is four hundred.

The number worth carrying forward: **a ragdoll costs about 26 microseconds of
solver while it is moving**, so a fight with ten of them is a quarter of a
millisecond and thirty of them would still be under one. An island that has gone
to sleep costs nothing at all, which is why the bench drops them rather than
posing them -- ten settled heaps would have measured the sleep heuristic and
reported it as a ragdoll price.

**The constraint family's three new per-tick passes cost a jointless scene
nothing, and that was the gate.** Attachment resolution, the second `applyScene`
walk and constraint retirement are each a walk over a pool, and every one of
those pools is empty in `physics1k` and `churn10k` -- which is what their rows
above say: 2.00 against M5's 2.02, and 6.98 against M6's 7.32. Both are inside
run-to-run noise on this machine, and neither moved in the direction a new
per-tick walk would move them.


**UI cost is relayout and not draw, and the roadmap asked for the two to be
measured separately.** The draw half is in the obby row above: fifteen draws for
a course, a HUD and a menu, of which one is the whole UI -- the 2D pass is one
draw per scissor RUN, so a panel with a list layout and four labels is one.

The relayout half is a COUNTER rather than a duration, and deliberately: at this
scale a timing assertion measures the clock, and "about zero microseconds" is the
shape of gate that passes while doing nothing. `LayoutStats::solverRuns` counts
solver passes, a `ScreenGui` runs one only when something marked it dirty, and
`luaug_ui_tests` asserts that two further frames over an untouched tree run
**zero**. A static HUD costs nothing per frame, which is the claim, and a
regression that dirtied a tree on every property read would show as a number that
climbs rather than as a millisecond nobody would notice.

**What D031's motion switch costs, and it is `churn10k` that priced it rather
than the scene built for the purpose.** Four hundred platforms with four hundred
transitions over three hundred ticks do not show above the noise: 0.14 ms of
apply, and the six hundred still parts stay in the static layer and cost nothing.
That is the number the fix was designed to produce.

`churn10k` is the one that moved, from **4.96 ms a tick to 7.32**, and the
increase is honest: two thirds of its ten thousand anchored parts are written
every tick, so two thirds of them are now kinematic bodies in the broadphase
layer Jolt re-fits each tick. The step went from 1.23 ms to 3.89 and that is
Jolt doing work it was previously not asked to do -- for parts that were moving
all along and were being teleported. The budget is 16 ms and it is still met.

**Two costs found by measuring rather than by reasoning, both fixed before this
row was written**, and they are the reason this file exists:

  * Writeback went from 0.03 ms to **9.9 ms**. Jolt reports every kinematic body
    as active, always, so the mirror was copying six thousand solver transforms a
    tick back into components whose transforms the SCRIPT owns. A kinematic body
    is not written back now.
  * Apply went from 1.60 ms to **6.96 ms**. The pending-move list was
    deduplicated by scanning it, which is quadratic in the number of kinematic
    writes in a tick; six thousand of them cost four milliseconds. It appends and
    `step` applies in order, so the last write still wins.

The number that would have shown and does not is the one the narrow fix avoided.
`Anchored` meaning kinematic always would have put the six hundred still parts --
and every floor and wall of every real world -- into the layer that is re-fitted
every tick, and `churn10k`'s ten thousand are what that costs at scale. The
hysteresis is what bounds the transition count: inside twelve ticks a platform
transitions once and stays, and `platforms200`'s second population deliberately
writes outside it to price the pathological case.

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

### M7.5 — the renderer's second half, and the first scene it is CPU-bound on

Captured with `luaug-host <project> --headless --width=1920 --height=1080
--frames=300 --exit --frame-stats`, median of the 289 frames after ten warm-up
frames, three runs. The spread across the three runs of the horde scene was 1.5%
(3.826, 3.776 and 3.832 ms).

**These horde numbers are the RE-MEASURED ones, and the first set was taken on a
frame that was not drawing the scene (D043).** Both instanced shaders assembled
the per-instance model matrix transposed, so every instanced vertex left the
frustum: the field of two thousand enemies rendered as an empty floor with only
the player on it. The measurement said 3.72 ms at 22 draw calls, which is the
number a working instanced path also gives -- twenty-four thousand triangles at
1080p are not what this frame is spending its time on, so removing all of them
moved the median by 3%. **A performance number cannot tell you whether the frame
contained the scene**, and neither could the draw-call gate this milestone was
asked for, nor the command-stream goldens, which were all correct. The check that
can is `screenshot_gate_instanced`, and it is standing now.

**Two numbers per row now, and the second one is the point.** `DrawCalls` and
`VisibleObjects` were the same number until this milestone: a run of objects that
share a mesh and a material is one call now, so "how many objects are visible"
and "how many calls were issued" stopped being the same question. The roadmap's
gate for the instanced path is exactly the two of them side by side, and a row
where they are equal is a row where instancing did nothing.

**The horde, and the control that makes it a measurement.**
`tests/perf/horde` is two thousand enemies sharing one mesh and one material,
chasing a circling player, positions written from Luau every tick, under a
shadow-casting sun. It was measured outside the repository at M2 and is committed
now, because M7.5 is where the answer became a number something defends.

| Horde, 2,000 enemies | Frame | Draw calls | Visible objects |
|---|---|---|---|
| Instanced, as shipped | **3.83 ms** | **22** | 4,002 |
| Same build, instanced path disabled | 31.45 ms | 15,390 | 4,002 |
| Instanced, pinned to two cores | 4.04 ms | 22 | 4,002 |

**The control row is also the correctness check now.** With D043 fixed, the
instanced frame and the same frame with the instanced path disabled are
BYTE-IDENTICAL at 960x540 -- zero differing pixels. Before the fix, 87% of the
frame differed. That comparison costs one extra render and is the only instrument
that could have told these two rows apart, because everything else about them --
the command stream, the counters, the timings -- was consistent with both.

The control row is the same binary with one constant raised past any real run --
the same sort, the same passes, the same shaders. That is what makes it a control
rather than a comparison against history, which would have been comparing two
different renderers.

**The reduced-CPU row says this frame is nearly single-threaded.** Two cores cost
about a tenth of it rather than half, so what this workload wants is one fast
core rather than many. R16's concern is about the low end, and the answer this
scene gives is specific: a device with fewer cores does not suffer here, and a
device with a slower core suffers proportionally.

**Per-feature cost, each measured by disabling that pass alone and rebuilding.**
The whole M7.5 chain is about a millisecond at 1080p on the reference machine.

| Feature | Cost | Against |
|---|---|---|
| Three extra shadow cascades | 0.22 - 0.28 ms | One cascade, which is what M4 shipped |
| Depth prepass | 0.20 - 0.22 ms | No prepass; it is a second geometry submission and it is what makes depth samplable |
| FXAA | 0.19 ms | The tonemap resolving straight to the swapchain |
| Bloom | at the noise floor | Nine passes over five levels |
| Automatic exposure | 0.09 - 0.18 ms | Three passes, the last of them 1×1 |
| Ambient occlusion | at the noise floor | Sixteen taps at half resolution plus two blur passes |
| **Whole chain** | **about 0.9 ms** | |

**The measurement's own noise floor is about 0.08 ms**, and the ranges above are
the two independent sweeps rather than an average of them. The sweep was run
twice, on different builds of the same tree, and the three large rows agreed to
within 0.06 ms while the three small ones did not -- bloom came out at 0.13 ms
once and at MINUS 0.02 ms the other time, which is the measurement saying it
cannot resolve that pass rather than the pass being free. The two smallest rows are at the edge of what
this method can resolve, and they are reported as such rather than to three
decimals of false precision. A GPU timestamp query would resolve them properly
and the RHI has none; adding one is an ADR the milestone that needs it should
write.

| Milestone | Scene | Preset | Metric | Value | Budget/Gate |
|---|---|---|---|---|---|
| M7.5 | `tests/perf/horde` (2,000 enemies, one mesh, one material, 1080p) | `win-msvc-dev` | median frame | **3.83 ms** | re-measured after D043 |
| M7.5 | `tests/perf/horde` | `win-msvc-dev` | draw calls / visible objects | **22 / 4,002** | not equal, which is the gate |
| M7.5 | `tests/perf/horde`, instanced path disabled | `win-msvc-dev` | median frame | 31.45 ms | the control |
| M7.5 | `tests/perf/horde`, instanced vs. disabled | `win-msvc-dev` | differing pixels | **0** | the check the counters could not make |
| M7.5 | `tests/perf/horde`, two cores | `win-msvc-dev` | median frame | 4.04 ms | the reduced-CPU row |
| M7.5 | `examples/02-meshes` (1080p) | `win-msvc-dev` | median frame | **1.51 ms** | — |
| M7.5 | `examples/02-meshes` with a frozen sun | `win-msvc-dev` | median frame | 1.41 ms | isolates the environment prefilter |
| M7.5 | `examples/02-meshes` | `win-msvc-dev` | draw calls / visible objects | 61 / 11 | eleven objects across six passes |

**The shadow kernel went from nine taps to twenty-five after the milestone**, so
a shadow edge has a penumbra wide enough to hide the texel grid it was
rasterised on (D044). It costs 0.54 ms at 1080p in `examples/02-meshes` -- 1.51
to 2.01 -- which is a third of that frame and the largest single cost the
renderer has taken since M7.5 closed. **The tap count is the dial** if that is
judged too much: nine could not span the penumbra without banding, which is the
whole reason for the change, but sixteen might.

**`examples/02-meshes` went from 0.46 ms at M4.5 to 1.51 ms**, and that is not a
regression in the sense the 10% clause means. M4.5's frame was a shadow pass, a
sky, a forward pass and a tonemap; this one is a shadow ATLAS of four cascades, a
depth prepass, ambient occlusion and two blurs, the forward pass, three exposure
passes, nine bloom passes, a tonemap and an anti-aliasing resolve. Eleven objects
and seventy-two triangles do not move that number -- **the passes do**, and the
per-feature table above is what says which.

**It was 2.98 ms until the measurement was read properly**, and the story is
worth the paragraph because the diagnosis is the M2 horde's diagnosis exactly.
The frame cost 2.96 ms at 320x180 and 2.96 ms at 1920x1080 -- identical, which is
what says the cost is not fragments. It was the CPU environment prefilter, which
this milestone had priced at zero:

| | `examples/02-meshes`, 1080p |
|---|---|
| As first written | 2.98 ms |
| The same scene with its sun FROZEN | 1.41 ms |
| Shipped | **1.51 ms** |

The frozen-sun row is the control: it isolates the prefilter from everything else
the milestone added. Three things closed the gap and all three were found by that
one comparison. `evaluateSky` called `pow` twice per evaluation and a full
prefilter is a quarter of a million evaluations. The rebuild threshold was half a
degree, which in a ninety-second day meant the chain never got ahead of the sun.
And **the job pool M7 built had no caller at all** -- nothing in the engine ever
called `jobs::init`, so every `parallelFor` in it had been taking the documented
serial path.

What remains is about 0.10 ms a frame for an environment that follows a moving
sun, against 1.41 ms for a frozen one.

The comparison the 10% clause is actually for is the horde row, and there is no
M7 number to compare it against because the scene did not exist in the repository
then. The control row is what stands in for one: same build, same scene, one
constant.

### M8 — the flagship, and the absolute targets binding

`examples/10-open-world`: a character on streamed terrain, 289 chunks of which
about 4,300 instances are resident, a moving sun, a HUD, physics, and the M7.5
render chain. Measured with `--headless --width=1920 --height=1080` and the
demo's own autopilot, which walks a circuit for twenty-five seconds and flies a
wider one for twenty-five.

**The ten-minute soak, which is the gate.**

| | |
|---|---|
| Frames | 35,939 measured (36,000 run, 60 warm-up dropped) |
| Median | **5.35 ms** — 187 fps |
| p99 | **8.79 ms** — 114 fps |
| Worst | 17.23 ms, **one frame in 35,939** |
| Frames over 16.7 ms | **1** |
| Streaming hitches over 33 ms | **0** (worst streaming step 3.34 ms) |
| Peak resident | 168 MiB, and the final figure is the same number |
| Instances | 4,354 early, 4,285 late — flat, which is what a streamed world does |
| Draws / visible objects | **72 / 1,032** |

Sixty frames per second at 1080p is met with three times the headroom at the
median, and the tail is a single frame rather than a distribution with a shoulder
in it.

**Per-feature cost, from a sweep of the same scene at 6,000 frames.** Each row is
the same binary with one flag, so these are differences rather than separate
builds — which is what `--quality`, `--shadow-cascades` and the rest exist for
(ADR 0044).

| Variant | Median | p99 | Worst | Frames over 16.7 ms |
|---|---|---|---|---|
| Baseline (`high`) | 6.25 ms | 11.33 ms | 51.87 ms | 8 |
| `--shadow-cascades=0` | 4.79 ms | 8.86 ms | 35.72 ms | 3 |
| `--no-bloom --no-ambient-occlusion --no-anti-aliasing` | 4.60 ms | 7.90 ms | 17.16 ms | 1 |
| `--no-auto-exposure` | 4.59 ms | 8.16 ms | 10.65 ms | 0 |
| `--quality=low` | 4.33 ms | 7.83 ms | 10.17 ms | 0 |
| `--render-scale=0.5` | 4.55 ms | 8.07 ms | 12.14 ms | 0 |
| **Baseline again, last** | **4.83 ms** | **8.37 ms** | 13.04 ms | 0 |

**Read the first and last rows together before reading any of the ones between:
they are the same run, and they differ by 23%.** A sequence of GPU runs is not a
sequence of independent measurements — the first one gets a cool card at its
boost clock and everything after it does not. An earlier version of this sweep,
run without a warm-up, reported a 2.9 ms baseline and then showed `--quality=low`
as *slower* than `high`, which is the shape a measurement takes when the variable
is the order rather than the flag.

**So the per-feature numbers above are worth about half a millisecond each and
should be read as "roughly a millisecond and a half of shadows, a millisecond and
a half of post".** The way to get better ones is to interleave the variants and
repeat, which is what the next person who needs a real number should do rather
than trusting this table to more precision than it has.

**The one frame that misses 60 fps used to be sixty of them.** The demo's
autopilot originally began its flight by placing the character nine hundred
metres away, which replaces the entire resident set in one tick; the burst of
materialisation that followed was the only thing in a ten-minute run over 33 ms.
It spirals out over six seconds now. A player never teleports, and a soak that
did was measuring something no player would ever do.

**Shadow distance is paid for in resolution, everywhere, at once** (D052), and
this is the table to look at before choosing one. A cascade's texel is its box
divided by its tile, and the far cascade's box is set by the frustum's
cross-section at the shadow distance — so the last cascade is where the whole
choice shows up. Measured on the flagship's own camera (70 degrees, 16:9), by
dumping each fitted box:

| Tile | Distance | Far cascade starts | Far cascade texel |
|---|---|---|---|
| 1024 | 120 m (the `high` preset) | 31 m | 0.35 m |
| 1024 | 180 m (the flagship's first answer) | 44 m | **0.52 m** |
| 2048 | 220 m (`ultra`, before D052) | 44 m | 0.32 m |
| 2048 | 160 m (`ultra`, now) | 39 m | 0.23 m |
| 2048 | 140 m (the flagship, now) | 35 m | **0.20 m** |

Two things fall out of it. **Doubling the tile and shortening the distance is
free**: 2.81 ms median against 2.89 ms for the pair it replaced, over 2,400
frames at 1080p run twice each in alternating order — a larger tile costs fill
and a shorter distance hands it back in culled casters. And **`ultra` was
spending its four-times atlas on range rather than density**, which is how a
preset above `high` ended up drawing a fifty-metre shadow on a grid nine per
cent finer than the preset below it.

**The diffuse ambient costs 0.2 ms a frame and buys a world that does not
pulse** (D053). Re-projecting the sky onto nine coefficients four times as often
as the specular chain rebuilds, and walking the shader's copy towards the result
rather than handing it over whole, moved the flagship's median from 2.85 ms to
3.07 ms over four alternating 2,400-frame runs. What it bought is measured on a
still scene under a moving sun: the frame-to-frame change went from a mean of
42,577 with a 297,810 spike to a mean of 8,039 with a 10,250 one — peak over
mean 6.99 to 1.27. Baking it exactly, every frame, costs 0.55 ms instead of
0.2 and measures no better.

**A shadow edge steps by one texel whatever else is true, and what changes with
distance is how often** (D054). Measured on a still probe under the flagship's
clock: the near cascade's lattice drifts about half a texel per frame near noon,
so its edge moves every other frame and reads as motion; the far cascade drifts
a twentieth of a texel and holds still for twenty-three frames before jumping,
which reads as a jump. Widening the texel-band floor from two to six and
rotating the 5x5 kernel per pixel — measured with the casters at forty-five
metres, consecutive frames subtracted:

| | Pixels changing >2 levels | >4 levels | Worst single change |
|---|---|---|---|
| Floor 3 texels, fixed grid | 227 | 62 | 37 |
| Floor 4, rotated | 117 | 20 | 35 |
| **Floor 6, rotated** | **80** | **14** | **14** |

And then the taps themselves, because a filter returns a COUNT of them and the
smallest change it can express is one: with twenty-five, one texel of the map
flipping moved a shadow edge 3.58 pixels; with forty-nine over the same radius
it moves 0.75. **Neither change is measurable in a frame** — the flagship's
median was 2.98 ms and 2.99 ms over two 2,400-frame runs with the 7x7 kernel,
against 3.09 with the 5x5 one, which is inside the run-to-run spread this page
warns about. At 1080p this scene is not bound by shadow taps.

**Render interpolation costs nothing measurable** (D047). Forcing `alpha` to zero
on the same scene moves the median by less than the run-to-run spread, because
almost every part in an open world is static and the comparison in front of the
slerp is two `CFrameD` equality tests.

**Why the budgets are so loose.** They are catastrophe detectors, not
instruments. A CI runner's speed varies by more than the regression anyone would
want to catch, so a budget tight enough to notice 10% would be red every other
week and would train everyone to ignore it. The threshold catches the change that
made a tick ten times slower; this table is where a 10% regression is actually
visible, and comparing against it is a human's job at each milestone gate.

Standing absolute targets, **bound at M8** on the reference machine. Each is
followed by what it measured, so a later regression is a comparison rather than
a judgement:
- `examples/10-open-world`: 60 fps at 1080p; zero streaming hitches > 33 ms in
  the scripted fly-through; 10-minute soak with bounded memory delta and zero
  crashes. **Met**: median 5.35 ms, p99 8.79 ms, one frame of 35,939 over
  16.7 ms, zero hitches, peak resident equal to final resident at 168 MiB.
- Hot reload (`luaug dev`): < 500 ms from file save to behavior change
  (ADR 0024), measured by the M3 E2E test.
- Sim: 500-instance scripted scene (M2 benchmark) and 1,000 active physics
  bodies (M5 benchmark) within their recorded budgets; the
  10k-parts/1k-listeners property-churn benchmark within its CI threshold.
- Script GC: ≤ 1 ms GC step at 60 fps under the M7 streaming scene load.

### E4 — the editor's Explorer, measured as work rather than as a clock

**A number the machine cannot move.** Everything above this section is
milliseconds on the reference machine, and the methodology at the top of this
page spends four paragraphs on how easily a millisecond lies. This one does not
need them: what the Explorer costs per frame is the number of instances its walk
visits, and that number is a property of the algorithm — the same on a reference
desktop, on a busy laptop and in a container.

The panel drew a preorder over **every instance in the world** every frame, and
then walked that list a second time to drop everything under a closed node. The
*drawing* was already virtualised — `ImGuiListClipper` over the visible rows —
which is exactly why nothing ever showed this in a profile: the cost was in
deciding what to draw, not in drawing it.

Measured by `inspector_tests.cpp`, which is why the numbers are reproducible
rather than recorded:

| World | Rows on screen | Instances visited, before | After |
|---|---|---|---|
| 4 branches × 50 leaves (**205** instances) | 4 | 205 | **5** |
| 4 branches × 500 leaves (**2,005** instances) | 4 | 2,005 | **5** |
| Same, with one branch opened | 54 | 2,005 | 55 |

**The two worlds are an order of magnitude apart and the walk costs the same**,
which is the assertion the test makes — equality rather than a small bound, for
the reason E5's partition peak had to: a bound that is merely small passes while
the defect is still there. Break-verified by making the walk descend
unconditionally, which reports 205 and 2,005 again.

What this does not say is how many milliseconds it was, and that is deliberate:
the walk allocated nothing per node and the panel was never visibly slow, so a
frame time would have shown a regression only on a world large enough to make it
one. The cost was linear in a world an editor is expected to open, and now it is
not.

## Jolt on a fixed thread pool (S6.10, ADR 0064)

The M5 roadmap said "single-threaded first; Jolt's job system wired to the engine
job system when M7 lands it", and asked whether the recorded hashes would survive
it. M7 landed and the question was never asked. It is the kind that is answered
by running it.

Same machine, same build, `win-msvc-dev`, `--bench-repeats=1`. **The A/B rather
than only the new number**, because the interesting fact is the delta and it
would be unrecoverable from a table of absolutes:

| Bench | Measure | `JobSystemSingleThreaded` | `JobSystemThreadPool`, 4 threads |
|---|---|---|---|
| `physics1k` | mean sim tick | 2.013 ms | **0.904 ms** |
| `physics1k` | physics step | 1.760 ms | **0.651 ms** |
| `physics1k` | worst sim tick | 4.563 ms | **1.871 ms** |
| `churn10k` | mean sim tick | 7.107 ms | **5.224 ms** |
| `churn10k` | physics step | 3.725 ms | **1.853 ms** |
| `churn10k` | worst sim tick | 174.17 ms | **40.03 ms** |
| `ragdoll10` | physics step | 0.264 ms | **0.131 ms** |

**The worst tick is the number worth reading twice.** `churn10k`'s fell from 174
ms to 40 ms, which is the difference between a visible stall and a dropped frame.

**And the hashes survived**, which was the open question. `tests/determinism/churn`
— ten thousand ticks, and the one whose parts Jolt actually simulates —
reproduced its committed hash `d3dd9b68722aa0fa` on Tier 1 and Tier 2, unchanged.
No trace was re-recorded for this change.

**The thread count is part of the hash.** Jolt is deterministic across runs
provided the count is the same, so `kPhysicsThreads` is a physics constant in the
way gravity is: changing it means re-recording every trace. That is also why the
solver does not run on the engine job pool, which sizes itself from the machine —
a trace recorded here would stop being reproducible on a machine with a different
core count, which is the one property a committed trace cannot lose.
