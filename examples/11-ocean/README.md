# examples/11-ocean — a sea, a boat, and one function shared by both

Sail a boat over an open ocean, with cargo floating around it under real
buoyancy. There is exactly one wave function in the example: the water is drawn
from it, the boat is placed by it, and the crates are pushed by it, so nothing
on screen can disagree with anything else on screen.

```
examples\11-ocean\run.bat
```

**Controls** — W/S throttle · A/D steer · arrow keys or the right stick orbit the
camera · Space drops a crate over the side.

## Where the budget actually goes

Every number below was measured on this example with `--frame-stats`, by taking
one thing out and leaving everything else in. Three of the four things that
looked expensive were not, and the one that was is not the one anybody guesses.

**The renderer is not the cost.** 676 water tiles draw in about 113 calls for the
whole frame, because identical geometry batches into instanced runs (ADR 0043).

**Transparency would have been the cost.** That batching explicitly refuses
transparent draws — *"their ORDER is their correctness"* — so **the water is
opaque on purpose**. A translucent sea is one draw call per tile. Deep water is
very nearly opaque anyway, so the look barely notices.

**A continuous colour ramp was the cost, and that was a surprise.** Tinting each
tile by its own height looked free: the model matrix is a per-instance vertex
attribute, so surely the colour is too. Measured:

| | draws | frame |
|---|---|---|
| a distinct colour per tile | **3629** | 14.2 ms |
| one colour for all tiles | **47** | 6.7 ms |

Colour is part of a material and a material is what a run is keyed on. So the
ramp is **quantised** to twelve steps: twelve materials, twelve batches, a cost
paid in draw calls rather than milliseconds. Twelve and not seven because seven
*bands* — the steps land on six-metre plates and the eye finds the rectangles.

**The real ceiling is the property write.** Moving one part from Luau costs about
**4.7 µs**, measured by removing the assignment and leaving the arithmetic in. A
thousand moving parts is five milliseconds before anything is drawn, and that —
not the renderer, not the wave maths — is what sets `TileSize` and `GridSide`.

**So the analytic derivative was not worth it.** Tilting each plate to lie along
the water needs the surface slope. Taking it analytically costs a `math.cos` per
wave per tile on top of the `math.sin`: five milliseconds a frame. Taking it as a
central difference between neighbours costs two array reads, because those
heights have already been paid for. The tilt itself is free either way — and it
matters, because a grid of *level* plates is a grid of little cliffs, and the
first version of this example looked like a tiled bathroom floor for exactly
that reason.

## The unbounded ocean

The grid is allocated once at startup and its origin is snapped to the tile size
every tick, so it follows the boat in whole-tile steps. Nothing is created or
destroyed after startup, the part count never changes, and the fog closes at
62 m — well inside the 78 m half-extent — so the edge is never something you can
see. Sail in one direction for as long as you like.

The snap is the part worth keeping: without it the whole surface slides under
itself and the sea looks like it is on a conveyor belt.

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

**This is not how a shipping engine draws water, and it is not pretending to
be.** A real ocean is one mesh whose vertices are displaced in a vertex shader —
Gerstner sums, or an FFT height field — which costs one draw call, no per-tile
property writes, and a wave count the CPU never sees. That needs a custom
material or shader seam reaching Luau, and **v1 has none**: a `BasePart` carries
`Color` and `Transparency` and nothing else, and `shaders/` is engine-side.

So this example is the shape of ocean the current API can express, and its
constants are set by a cost — 4.7 µs a part — that a GPU surface would not pay at
all. When that seam exists, the wave function in this file is exactly what the
shader would evaluate, and the boat and the cargo would keep sampling it here,
unchanged.

## Numbers worth turning

All at the top of [`src/scripts/init.luau`](src/scripts/init.luau):

| | |
|---|---|
| `TileSize`, `GridSide` | the ocean's resolution and extent — the whole performance budget. Past about 8 m the shortest wave aliases, and a wave that aliases does not look coarser, it looks like the sea is crawling |
| `WaveDefinitions` | the sea state. Wavelength, amplitude, direction. Speed is **not** in the table: deep-water waves travel at `sqrt(g·k)`, so a long swell outruns a short chop on its own |
| `WaterDrag`, `Current` | how quickly a floating thing settles, and how fast the sea carries it away |
| `Density` on a floater | 0.35 rides like balsa, 0.85 sits waterlogged, past 1.0 it sinks |
| `ExposureCompensation` | an ocean is a big even field of one brightness, which is the scene auto-exposure lifts until the water is pale. The value was found by measuring the water's pixels against a render with metering off — by eye, a third of the correction read as "about right" |

## Determinism

The wave clock is accumulated from the fixed tick, never read from a wall clock,
and the wave function is pure — the same position and time give the same height
on every machine. That is what keeps R10 intact while the sea drives a body the
input replay can see.
