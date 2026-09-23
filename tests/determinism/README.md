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

`trace.txt` is one `<tick> <hash>` line per checkpoint, in hex — text
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

A missing trace is an **error**, not a skip: a gate that
quietly degrades to "the two in-process runs agreed" reports success for the
weaker half of the check, and the weaker half is the one that missed the padding
bug.

## One trace, every platform

**The guarantee is level C since 2026-09-23** (ADR 0083): the same seed and the
same operations give the same world hash on Windows, Linux and macOS, whatever
compiler built the engine. One `trace.txt` per scenario, and every tier gates
against it.

It used to be per platform, and the reason was `sin`. `math.sin` reached MSVC's C
runtime on Windows and glibc's on Linux, the two disagree in the last bit, and one
bit compounded over 500 ticks of accumulated transforms is a different world. What
changed:

- **The simulation's transcendentals are the engine's own** (`core::dmath`):
  argument reduction and fixed polynomials in plain IEEE arithmetic, the same
  bits everywhere. `core` maths, `CFrame`, easing and the seeded random use them.
- **Luau's `math` goes through them**, with the builtins disabled in the
  compiler so no fastcall reaches the C runtime, and **`^` does too**, through
  one patch to the vendored VM and compiler (`third_party/patches/luau`).
- **Jolt runs cross-platform deterministic** (ADR 0074).
- **No compiler fuses a multiply-add the source did not write**
  (`-ffp-contract=off`), which is what would otherwise part an ARM Mac from an
  x86 PC.

Measured when it landed: all five scenarios, `character`, `churn`, `example01`,
`ragdoll` and `terrain`, produced byte-identical traces under MSVC on Windows and
Clang with glibc on Linux, and `character`, `ragdoll` and `terrain` stopped being
`sameBuildOnly`. macOS is proved by the CI job that runs the same file.

**A tier that parts from the trace is a defect**, not a platform difference to
record: find the maths that reached a platform's runtime.

## Re-recording

`--record-replay` rewrites the goldens, on whichever platform it runs, and it is the
wrong answer to a failure unless you can say which semantic change moved the hash
and why it was intentional. A hash that moves for a reason nobody can name is the
bug this directory exists to find.

Any tier can record them, because every tier must produce the same file; the
Linux container is how to check a recording made on Windows before pushing:

```
docker run --rm -v "$PWD:/repo" -v luaug-tier2-build:/build -e LUAUG_BUILD_ROOT=/build \
  luaug-tier2:latest bash -lc \
  "cd /repo && cmake --build --preset linux-clang-dev && \
   /build/linux-clang-dev/engine/app/luaug-host --replay=/repo/tests/determinism"
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
