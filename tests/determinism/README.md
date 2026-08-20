# `tests/determinism/` — record/replay scenarios

The M2 gate: **same script + same seed, twice, identical world hash** (ADR 0025,
architecture.md §9). Run by CTest as `determinism`, and by hand with:

```
luaug-host --replay=tests/determinism                  # compare against the goldens
luaug-host --replay=tests/determinism --record-replay  # rewrite them
```

A scenario is a directory holding `scenario.json`, the script it names, and one
recorded trace per platform:

```json
{ "name": "churn", "script": "init.luau", "seed": 20260820,
  "ticks": 10000, "checkpointEvery": 500 }
```

`trace.<platform>.txt` is one `<tick> <hash>` line per checkpoint, in hex — text
so that a change to it is reviewed as a diff, and 21 short lines so the diff says
*which* checkpoint moved rather than only that the end result did.

## What the run actually compares

Three ways, and each catches something the others cannot:

1. **Two runs in this process.** Catches state left in a global, in a static, or
   in an allocator whose addresses leaked into an iteration order. A fresh
   process would never show it.
2. **This run against the recorded trace.** The process is new every time CTest
   starts it, so this is the cross-process leg — and it is the one that caught
   the world hash reading four bytes of uninitialised padding out of `CFrameD`,
   which reproduced perfectly inside one process and differed in the next.
3. **Every 500 ticks, not just the end.** Two final hashes that differ tell you
   nothing; a divergence at tick 4,200 tells you where to look.

A missing trace for the current platform is an **error**, not a skip: a gate that
quietly degrades to "the two in-process runs agreed" reports success for the
weaker half of the check, and the weaker half is the one that missed the padding
bug.

## Why the traces are per-platform

Because the guarantee is per-platform, and architecture.md §9 says so: *same
build + same platform + same seed/inputs/tick-config ⇒ same WorldHash*, with
cross-platform comparison a tracked, non-blocking concern.

The reason is `sin`. `math.sin` reaches MSVC's CRT on Windows and glibc's on
Linux; they disagree in the last ULP, and one ULP compounded over 500 ticks of
accumulated transforms is a different world. Making that agree means shipping our
own transcendentals — a real option, and not M2's.

One file per platform is the honest shape: each tier gates against what it
actually recorded, so a Linux regression is caught on Linux and nobody has to
reconcile two libms to merge a patch.

**Where the divergence actually is, measured:** `churn`'s Windows and Linux
traces agree at tick 0 and differ from tick 500 on. `example01`'s are byte-for-
byte identical at every checkpoint. The difference between them is precision, not
luck: `example01` stores its results in `Part.Position`, which is f32 (R9), and
rounding to f32 discards the disagreement; `churn` accumulates into `CFrame`,
whose position is f64 engine-side, and f64 keeps it. So the cross-platform
divergence surface is exactly the f64 state — which is worth knowing before M5
puts physics in it.

## Re-recording

`--record-replay` rewrites the goldens for the platform it runs on, and it is the
wrong answer to a failure unless you can say which semantic change moved the hash
and why it was intentional. A hash that moves for a reason nobody can name is the
bug this directory exists to find.

Recording the Linux trace uses the same container the gate does:

```
docker run --rm -v "$PWD:/repo" -v luaug-tier2-build:/build -e LUAUG_BUILD_ROOT=/build \
  luaug-tier2:latest bash -lc \
  "cd /repo && cmake --build --preset linux-clang-dev && \
   /build/linux-clang-dev/engine/app/luaug-host --replay=/repo/tests/determinism --record-replay"
```

Editing a scenario's script counts as a semantic change, including edits that
look cosmetic. Running `stylua` over `churn/init.luau` once reordered two
`GetService` calls, which changed the order the services were created in, which
changed every instance id after them, which changed the hash — correctly.

## The scenarios

| Scenario | Ticks | What it is for |
|---|---|---|
| `churn` | 10,000 | The gate. Built backwards from R10's list: seeded RNG, slot reuse through destroy/recreate, reparenting within a sibling chain, tag iteration, deferred handlers that raise more deferred work, and CFrame composition accumulated tick over tick. |
| `example01` | 900 | `examples/01-instances`, so the milestone's own deliverable cannot rot silently. It is the cheapest gate that proves the example still builds its tree and evolves reproducibly, and unlike a pixel golden it needs no GPU and holds on every tier. |
