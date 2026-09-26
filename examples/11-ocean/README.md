# examples/11-ocean — a sea, a boat, and one function shared by both

Sail a boat over an open ocean, with cargo floating around it under real
buoyancy. There is exactly one wave function in the example, written twice: in
[`content/shaders/ocean.surface.hlsl`](content/shaders/ocean.surface.hlsl), a
**surface shader** (ADR 0091) that draws the water on the GPU, and in
[`src/scripts/init.luau`](src/scripts/init.luau), which places the boat and
pushes the crates. Both evaluate it at one clock -- `RunService.SimTime` -- so
nothing on screen can disagree with anything else on screen.

```
examples\11-ocean\run.bat
```

**Controls** — W/S throttle · A/D steer · arrow keys or the right stick orbit the
camera · Space drops a crate over the side.

## Where the budget actually goes

Measured with `--frame-stats` on the same machine, before and after the water
moved to the GPU:

| | before (676 plates moved from Luau) | after (a surface shader) |
|---|---|---|
| draws | 47 to 113 | **39** |
| median frame | 6.7 ms | **0.67 ms** |
| property writes a tick for the water | 676, plus colour changes | **9** |
| triangles | about 8 000 | 786 000 |

**The ceiling was the property write**, and it is gone. Moving one part from
Luau costs about 4.7 µs, so 676 plates were three milliseconds before anything
was drawn, and they set how fine the sea could be. Now nine grids of 256 x 256
quads are placed once a tick, in whole-quad steps, and the GPU moves every one
of their 66 000 vertices -- a hundred times the triangles for a tenth of the
frame.

**The water is translucent now**, which the plate version could not afford: a
transparent draw is never batched, and 676 of them would have been 676 draws.
Nine are nine. Being translucent is what lets the shader read the scene behind
it -- foam where the water is thin against a hull or a crate, and what is under
the surface refracted through it.

**Compiling it**: the first time the editor or `luaug-host` meets the shader it
compiles all ten stages in the background -- 882 ms here, while the water draws
with the built-in surface -- and every run after that reads them from the cache
in about 1 ms.
## The unbounded ocean

Nine grids, the middle one under the boat, are placed every tick with their
origin snapped to the **quad spacing** (96 m / 256 quads, about 0.37 m). They
follow the boat, but every vertex lands where a vertex was, so the surface
does not slide under the waves it carries -- without the snap the sea looks like
it is on a conveyor belt. Nothing is created or destroyed after startup, and
the fog closes well inside the 144 m the grids reach, so the edge is never
something you can see.
## What is simulated and what is driven

The difference is a real limit of v1, not a shortcut, and the example is built
around it rather than hiding it.

**The cargo floats for real.** Archimedes through `ApplyImpulse`: the upward
force is the weight of the water the crate pushes out of the way, and where that
balances gravity is where it settles. Nothing was tuned to make things float — a
crate ends up with `Density / WaterDensity` of itself under water because that is
what the arithmetic says. They are seeded from 0.35 to 0.84 so the difference is
visible from the deck, and a drag term proportional to submersion is what makes
one stop bobbing instead of ringing forever.

**The boat is driven.** `ApplyImpulse` applies at the centre of mass, and v1 has
no impulse-at-a-point and no angular impulse, so buoyancy cannot produce torque:
a simulated hull would heave and never roll. So the hull is welded to a
script-driven helm and its frame is read off the wave surface four times a tick —
fore, aft, port and starboard — which gives heave from the average and pitch and
roll from the two slopes. A nine-metre hull spanning four samples also ignores
waves shorter than itself, the way a real one does, instead of following every
ripple like a cork.

The weld is doing real work and not just holding pieces together: a welded part
is *driven*, and a driven body is **kinematic** rather than static, so the hull
still pushes what it collides with.

## What this is not

**Not an FFT ocean.** Three directional sine waves on deep water are what the
Luau side can afford to evaluate for every floater, and the shader must say
exactly what Luau says or the boat floats beside the water rather than on it.
The shader could add detail the boat never feels -- a fourth, short chop in the
normal alone -- and that is the natural next step.

**Not Gerstner waves**, deliberately: a Gerstner crest moves sideways as well as
up, and then the height under a given (x, z) has no closed form -- the boat
would sit on an approximation of the surface drawn under it.
## Numbers worth turning

At the top of [`src/scripts/init.luau`](src/scripts/init.luau), and in the
material [`content/materials/sea.material.json`](content/materials/sea.material.json):

| | |
|---|---|
| `WaveDefinitions` | the sea state -- wavelength, amplitude, direction -- **in both files**. Speed is not in the table: deep-water waves travel at `sqrt(g·k)`, so a long swell outruns a short chop on its own |
| `GridSpan` | how far the water reaches and how coarse it is: 96 m over 256 quads |
| `Clarity`, `FoamWidth`, `Refraction` | the look, as material parameters: how deep before the water is its deep colour, how wide the foam line is, how far what is underneath bends |
| `WaterDrag`, `Current` | how quickly a floating thing settles, and how fast the sea carries it away |
| `Density` on a floater | 0.35 rides like balsa, 0.85 sits waterlogged, past 1.0 it sinks |
## Determinism

The wave clock is accumulated from the fixed tick, never read from a wall clock,
and the wave function is pure — the same position and time give the same height
on every machine. That is what keeps R10 intact while the sea drives a body the
input replay can see.
