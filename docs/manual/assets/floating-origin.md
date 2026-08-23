# Floating origin

A 32-bit float has about 24 bits of mantissa. At four kilometres from the origin
its quantum is a quarter of a millimetre; at ten thousand kilometres it is
metres. A world that is genuinely large cannot be simulated in f32 coordinates
without things beginning to shake.

LuauG solves that in two halves:

1. **Authoritative transforms are f64.** `BasePart.CFrame`'s translation is a
   double, and it is the source of truth.
2. **The world is periodically moved back under you.** Physics and rendering
   work in f32 relative to a *floating origin*, and when a focus wanders too far
   from it, everything is translated at once.

## It is invisible to a script

> **A `CFrame` read after a rebase is the same `CFrame` read before it.**

Scripts always see true world coordinates. A rebase is not an event you handle
and not a transform you have to compose — it happens between two ticks, and
nothing a script can observe changes.

## When it happens

When the **primary focus** — the first instance passed to
`StreamingService.AddFocus` — leaves a four-kilometre tolerance of the current
origin.

Four kilometres because that is where f32's quantum is still a quarter of a
millimetre and a contact resolves cleanly. The tolerance is a **box**, compared
per axis, so a focus travelling along one axis rebases at the same distance
whichever axis it is.

It happens at a frame safe point, in one pass, and resident bodies are teleported
**preserving velocity**. A rebase between two ticks is a translation of
everything at once; a rebase inside one would move the world under a solver
halfway through it.

## What it asks of your code

One rule, and it is the same rule the precision itself implies:

> **Past a few kilometres from the origin, keep gameplay maths on `CFrame`.**

```luau
--!strict
-- Fine anywhere: the arithmetic stays on the f64 transform.
local ahead = part.CFrame * CFrame.new(0, 0, -5)

-- Fine near the origin, lossy far from it: Position is the f32 rounding.
local alsoAhead = part.Position + part.CFrame.LookVector * 5
```

`BasePart.Position` is the f32 rounding of the transform's translation, and
every `vector` a script touches is f32 — the language's vector primitive is
32-bit, and that is not something the engine can change per world.

So the failure mode is not a wrong world. It is **jitter**: a position that
rounds to the same value two ticks running, or a difference between two large
numbers that has lost its small part.

## What it does not do

It does not move the origin per region, and there is one world. Multiple
simulation regions with independent origins are possible in the architecture and
are not something this release exposes.

It also does not help a script that stores its own positions as `vector`s and
does maths on them across kilometres. The engine keeps *its* numbers precise;
yours are yours.

## Where to look next

- [Streaming a large world](manual:assets/streaming) — where the focus comes
  from
- [Transforms, pivots and units](manual:world/transforms)
- [Determinism](manual:concepts/determinism)
