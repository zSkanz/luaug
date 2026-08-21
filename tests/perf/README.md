# tests/perf — the scenes the perf table is measured on

Scripted projects that exist to be **measured by hand**, not by CTest.

A frame time is a property of the machine. A gate built on one goes red the day
CI allocates a slower runner, and a gate that goes red for a reason nobody
controls is a gate people learn to ignore — the same argument
`tests/screenshots/README.md` makes about pixel goldens, and the same conclusion:
the blocking gates are the ones that need no GPU, and the numbers live in
`docs/perf-baselines.md` where a human puts them.

`tests/bench/` is a different thing and stays where it is: those are SIMULATION
benchmarks, driven by `luaug-host --bench`, and `perf_budget` does run them in
CTest because a tick has a budget that holds on any machine.

## Running one

```
luaug-host tests/perf/horde --headless --frames=300 --exit --frame-stats \
  --width=1920 --height=1080
```

The last line is the measurement: median and worst frame time over the frames
after the warm-up, then **the count of draw calls beside the count of visible
objects**. Those two numbers were the same until M7.5; the roadmap's gate for
the instanced path is that they are not.

The reduced-CPU row `docs/perf-baselines.md` asks for is the same command with
the process pinned to a subset of cores:

```powershell
$p = Start-Process -FilePath luaug-host.exe -ArgumentList @(...) -PassThru -NoNewWindow
$p.ProcessorAffinity = 3   # two cores
$p.WaitForExit()
```

Measure on a quiet machine. `churn10k` once read 3.50 ms/tick while a Docker
build ran in the background and 2.02 ms three runs in a row once it finished — a
73% error, larger than any regression worth recording.

## The scenes

| Scene | What it is for |
|---|---|
| `horde` | Two thousand enemies sharing one mesh and one material, chasing a circling player, written from Luau every tick, under a shadow-casting sun. It prices **instanced draws**, which is M7.5's one scope item that is not about looking. Asked for on 2026-08-20 as "could I build a survivors-like on this engine"; answered at M2 by building one outside the repository, and committed here because M7.5 is where the answer became a number something defends. |
