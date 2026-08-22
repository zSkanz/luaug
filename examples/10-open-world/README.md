# `examples/10-open-world` — the v1 flagship

A third-person character walking a world larger than the engine keeps resident,
under a sun that moves, with a HUD, ambient sound, physics, and hot reload that
puts you back where you were standing.

Every earlier example proves one system against itself. This is the first scene
where all of them have to be true at once — the kernel, the renderer, Jolt, the
input actions, the UI, the audio timeline, the asset pipeline, the streaming
grid, the floating origin, and the graphics settings it runs under.

Run it:

```
examples\10-open-world\run.bat                    # windowed
examples\10-open-world\run.bat --quality=low      # the same world on a weaker machine
examples\10-open-world\run.bat --headless --frames=600 --exit --screenshot=out.png
```

**WASD** to move, **Shift** to sprint, **Space** to jump, **the mouse** to look,
**Left Alt** to free the cursor and take it back. Arrow keys and a gamepad's
right stick turn the camera too.

The pointer is locked at boot, because a camera that needs a click before it
responds feels broken for the first second of every session. The mouse and the
stick are **separate actions** on purpose: a stick is a rate and a mouse is a
displacement, and multiplying a mouse by a frame time makes it turn twice as far
on a slow machine.

## What to watch for

- **The world arrives before you reach it.** `MinRadius` is the ring that must
  be resident before the focus may advance into it; `LoadRadius` is the
  best-effort ring beyond. The gap between them is hysteresis — a single radius
  makes a character standing on a boundary load and evict the same chunk every
  frame, and the symptom is not a wrong world, it is a stutter nobody can find.
- **The sun moves and everything follows it.** One property — `ClockTime` —
  drives the sky, the shadow direction, the ambient, the reflection in the tower
  faces and the exposure. A full day takes four minutes.
- **Hot reload keeps your place.** Edit `src/scripts/init.luau` and save: the
  world is rebuilt from scratch and the character is standing where it was.
  That is `HotReloadService:SaveState` and nothing else.
- **The shadows are drawn on a grid, and `luaug.toml` chooses how fine.** A
  cascaded shadow map picks its cascade from where a shadow LANDS, not from
  where its caster stands, so what sets the resolution of a tree's shadow at
  fifty metres is the *far* cascade — and that cascade's box is the frustum's
  own cross-section at the shadow distance. This project asks for 140 m on a
  2048 tile, which is 0.20 m per texel out there; its first answer was 180 m on
  the preset's 1024 tile, which was 0.52 m and looked it (D052). The table is in
  `docs/perf-baselines.md`, and it is the first thing to read before making a
  world's shadows reach further.
- **The towers are navigation.** One every half kilometre, because a procedural
  world without landmarks is a treadmill: correct, streamed, and impossible to
  find your way across.

## The world is generated, not committed

`tools/generate_world.luau` writes the chunk SOURCES and `luaug build-assets`
compiles them into `.lchunk` payloads. `run.bat` does both when the output is
missing, so a fresh clone works without reading this first.

What is committed is the generator. 289 chunks of terrain is 4.8 MB of JSON,
and the roadmap's own deliverable line asks for "no giant binary assets in the
repo" — generated JSON is the same thing wearing a text extension.

The generator is deterministic: every value comes from a hash of the cell's own
coordinates, so the world is identical on every machine and a chunk looks the
same however many times it streams in and out.

## Two things this example is authored AGAINST, and both are worth stealing

**The ground is terraced finely enough to walk up.** A chunk's floor is 8 by 8
flat tiles of 32 m, and the height field's amplitudes are chosen so the step
between neighbouring tiles never exceeds the character's `AutoStepHeight`. A
fly-cam does not care what the slope is; a character walks into a ledge and
stops. The two numbers — the amplitude in the generator and `AutoStepHeight` in
the script — are one decision written in two files, and both say so.

**Colours come from a small palette.** A part's colour is part of its material,
and the instanced path groups by mesh AND material (M7.5), so two boxes that
differ only in tint are two draw calls. Six ground shades and four prop tints
let a resident ring of a thousand parts collapse into a few dozen draws. The
example reports both numbers side by side: with the overlay open, `DrawCalls`
against `InstanceCount` is the whole story.

**And the albedos are physical.** Grass reflects 10 to 20 per cent of what lands
on it. The first version of this world used 0.30 to 0.44 and came out as a pale
mint plain — but so did the version with the albedos halved, because automatic
exposure normalises whatever it is shown. What actually fixed it is
`Lighting.ExposureCompensation`, which is the artist control that exists for
this: a frame that is half sky meters far brighter than the ground in it.

## The autopilot

With no window, this demo drives itself: `PreRender` never fires in a headless
run, so a script that has not seen one knows nobody is watching. It then walks a
circuit for twenty-five seconds and flies a wider one for twenty-five, forever,
as a pure function of `SimTime`.

That is what the M8 soak gate runs, and it is deliberately the same file a
person plays rather than a test harness beside it — a soak over a scene nobody
plays proves the scene, not the game.
