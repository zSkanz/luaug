# Determinism

"Deterministic" means nothing until it is scoped, so here is the scope, exactly:

> **Same engine build, same platform, same initial state and seed, same tick
> configuration, same ordered inputs ⇒ same observable simulation result.**

That is the guarantee. Same-run repeatability is implied by it. What is **not**
guaranteed is bit-identical results across platforms — a Windows run and a Linux
run of the same world may diverge, and the engine measures that as an aspiration
in a non-blocking job rather than promising it.

The result is verified by a **world hash**: the engine can hash its own
simulation state at a tick, replay a recorded input log, and compare. That
harness is not a demo — it gates merges.

## What the engine does to hold the line

Three rules govern every line of simulation code in the engine, and they are
worth knowing because the same three rules govern *your* simulation code:

1. **Simulation code never reads a wall clock.** This is why there is no
   `tick()` and no `os.time()` in a gameplay path. `RunService.SimTime` and the
   fixed `dt` your phase handler receives are the clock.
2. **Never an unseeded random.** `Random.new(seed)` is a stream; the mapping
   from a seed to that stream is itself part of the guarantee, because a replay
   records seeds and not draws. A stream that shifted between builds would turn
   every stored replay into a false failure.
3. **Never iterate an unordered container into observable order.**
   `Instance.GetChildren` is child order. `Instance.GetDescendants` is document
   order. Neither is an implementation detail you may not rely on — they are
   part of the contract.

Work that is not simulation-visible — rendering, asset loading, IO — is exempt,
which is why it can be parallel and out of order without touching the guarantee.

## What that asks of your code

### Use the fixed dt you were given

```luau
--!strict
local RunService = game:GetService("RunService")

RunService.Heartbeat:Connect(function(dt: number)
    fuel -= BurnRate * dt
end)
```

The `dt` a per-tick phase hands you is the fixed timestep, identically, every
tick. Measuring elapsed time yourself with anything other than `SimTime` opts
your game out of the guarantee.

### Seed your randomness

```luau
local rng = Random.new(WorldSeed)
local offset = rng:NextNumber(-2, 2)
```

`math.random` without a seed is per-VM state you did not choose. A `Random` with
a recorded seed replays; an ambient one does not.

### Do not iterate a hash table into the world

```luau
-- Wrong: pairs() order over a string-keyed table is not specified.
for name, spawn in SpawnPoints do
    createEnemyAt(spawn)
end

-- Right: iterate an array, or sort the keys first.
for _, spawn in OrderedSpawnPoints do
    createEnemyAt(spawn)
end
```

This is the rule people trip over, and it is invisible until the day two runs
disagree. If the *order* in which something happens can be seen in the world,
that order has to come from an array, a sort, or the tree.

### Keep render-rate work out of simulation state

`RunService.PreRender` runs at a rate that depends on the machine. Anything it
writes that the simulation later reads is a hole in the guarantee. Move the
camera there; do not move the player there.

## Why it is worth the discipline

Three things rest on it, and only the first is available today:

- **A recorded replay reproduces**, which turns "it happened once" into a test.
- **A world hash catches a class of bug nothing else does** — a divergence that
  produces no crash, no error and no visibly wrong frame.
- **Rollback netcode is possible later** rather than being a rewrite. The
  foundations are the fixed tick and this guarantee; multiplayer is a post-v1
  phase, and it does not have to relitigate either.

## Where to look next

- [The tick is fixed](manual:why/fixed-tick) — why the frame is shaped this way
- [The frame, phase by phase](manual:concepts/frame) — which phase runs on which
  clock
- [`Random`](api:Random) — the seeded stream
