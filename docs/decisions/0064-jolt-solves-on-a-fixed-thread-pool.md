# 0064 — Jolt solves on a fixed thread pool, and the count is part of the hash

- Status: accepted
- Date: 2026-08-27
- Extends: [0025](0025-determinism-guarantee.md) (the determinism guarantee)
- Answers: the M5 roadmap note — "single-threaded first; Jolt's job system wired
  to the engine job system when M7 lands it — note the seam"

## Context

Jolt has stepped on `JobSystemSingleThreaded` since M5. The roadmap said why and
said what would change it:

> Jolt 5.6 on the fixed tick (**single-threaded first**; Jolt's job system wired
> to the engine job system when M7 lands it — note the seam) […] the determinism
> gate becomes *blocking* here while Jolt is single-threaded, but M7 wires it to
> the job system: **whether recorded hashes survive that is a question** for the
> grounding pass.

M7 landed. The question was never asked, and it is the kind that is answered by
running it rather than by reasoning about it — so it was run.

## The measurement

Same machine, same build, `win-msvc-dev`, one repeat:

| | single-threaded | four threads |
|---|---|---|
| `physics1k` mean tick | 2.013 ms | **0.904 ms** |
| `physics1k` physics step | 1.760 ms | **0.651 ms** |
| `churn10k` mean tick | 7.107 ms | **5.224 ms** |
| `churn10k` physics step | 3.725 ms | **1.853 ms** |
| `churn10k` worst tick | 174.2 ms | **40.0 ms** |
| `ragdoll10` physics step | 0.264 ms | **0.131 ms** |

**And the hashes survived.** `tests/determinism/churn` — ten thousand ticks, and
the one whose parts Jolt actually simulates — reproduced its committed hash
`d3dd9b68722aa0fa` on both tiers, unchanged. No trace was re-recorded for this.

The worst-tick number is the one worth reading twice. `churn10k`'s worst tick
fell from 174 ms to 40 ms, which is the difference between a visible stall and a
dropped frame.

## Decision

**Jolt steps on `JPH::JobSystemThreadPool` with a fixed four threads.**

**The count is a named constant and it is part of the world hash.** Jolt is
deterministic across runs provided the thread count is the same, so
`kPhysicsThreads` is a physics constant in the way gravity is: change it and
every recorded trace has to be re-recorded. It is written as a constant with
that sentence next to it rather than a literal at the call site, so the next
person to want eight threads knows what they are also signing up for.

**It is Jolt's own pool and NOT the engine job system**, which is where this
departs from the roadmap's note. The reason is the determinism guarantee rather
than convenience: `jobs::init()` sizes itself from `hardware_concurrency`, and
Jolt's determinism is per thread *count* — so solving on the engine pool would
make a trace recorded on one machine unreproducible on a machine with a
different core count. The same platform's committed trace would stop being a
fact about the platform. A fixed-size sub-pool of the engine's would be the same
four threads with more code between them and the same fixed number, so it buys
nothing the roadmap's note was actually after.

**Four rather than eight** because the gain is in the solver's own parallelism
and the tail flattens, and because a fixed count that oversubscribes a small
machine costs more than the threads it adds. It is revisitable with a
measurement and a trace re-record.

## Consequences

- Physics is roughly two and a half times faster at the step, and `churn10k`'s
  worst tick is four times better, on a change that is one type and one
  constant.
- The determinism gate is unchanged and still blocking. That was the open
  question and the answer is that it survives — which also means ADR 0025's
  level-B guarantee is intact with the solver threaded.
- `docs/perf-baselines.md` gains the A/B rather than only the new number,
  because the interesting fact is the delta and it would be unrecoverable from a
  table of absolutes.
- A future change to `kPhysicsThreads` is a trace re-record, and the constant
  says so where somebody will read it.
