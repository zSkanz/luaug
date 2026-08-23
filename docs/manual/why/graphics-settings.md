# Graphics settings belong to the player

`Lighting` is a service a script writes. Shadow resolution, render scale, bloom
and the light budget are **not**, and a script cannot reach them at all.

The line between the two is one sentence:

> **`Lighting` describes the world. Graphics settings describe the machine it is
> being shown on.**

`Lighting.ClockTime` is a fact about the place — it is eight in the morning
there. `shadow_resolution = 2048` is a fact about a graphics card.

## Why a script must not decide

Because a scene must not decide a stranger's GPU budget.

A world author knows what their scene looks like. They do not know whether it is
being played on a laptop with integrated graphics, and they have no business
choosing four shadow cascades on that machine's behalf. The person who does know
is the person running it.

There is a second, less obvious reason: **it keeps the simulation honest.**
A world hashes identically at every quality level, because nothing in the
graphics family reaches the simulation. That is verified by running the
determinism replay twice at two different settings. If a script could read the
settings, that stops being structurally true — a scene could branch on quality,
and two players at different settings would be playing different games.

## What that means in practice

There is **no settings service**, no in-game quality slider API, and the name is
not reserved either. Declaring a class nothing implements is exactly what this
API's own rules forbid.

A game that wants to offer a quality menu writes the value where the settings
live — the project file, or the launcher's own command line — rather than
setting it live.

## The three layers

1. **A preset** — `low`, `medium`, `high`, `ultra`.
2. **`[graphics]` in `luaug.toml`** — the game author's default.
3. **The host's own flags** — what the person actually running it says.

Each is an **override** rather than a value, so that "nobody said anything" and
"somebody asked for the default" stay different answers.

And one rule that is not implied by "later wins": **a preset named on the
command line replaces the file's per-key entries as well as its level.** A
file's `shadow_resolution` refines the level *that file names*; `--quality=low`
says that level is not available here, so carrying the refinement across would
hand a weak machine the heaviest dial in the file while everything else was
turned down.

## Structurally, not by convention

The graphics settings live in the rendering module. The scene module sits below
it in the layering and may not include it — and the layering is derived from
real include edges by a check that runs in the gate.

So "a script cannot read the graphics settings" is not a rule somebody has to
remember. It is a build error.

## What a script can see

The frame those settings produced:

```luau
--!strict
local DebugService = game:GetService("DebugService")

if DebugService:GetStat("FrameTimeMs") > 20 then
    reduceEffectDensity()
end
```

That is the right shape for adaptive quality: a game responds to how it is
actually running, in terms the game owns — how many particles, how far to draw —
rather than by reaching into the renderer.

## Where to look next

- [Graphics quality settings](manual:rendering/quality) — every key and its range
- [Lighting and the sky](manual:rendering/lighting) — what a scene *does* own
- [The debug overlay](manual:guides/debug-overlay)
