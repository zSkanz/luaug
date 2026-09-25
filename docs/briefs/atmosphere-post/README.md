# Atmosphere and post effects: captures for the owner's judgement

ADR 0096's before-and-after pictures, one folder per stage. **Every picture in
a stage is the same camera at the same `ClockTime`**; only the effects in the
world differ. **The owner accepted these on 2026-09-25**, and the goldens were
recorded from the same scene after that: `tests/rendercapture/look-everything-3frames.jsonl`
(every effect at once, the blocking command-stream gate) and
`tests/screenshots/lavapipe/look-*.png` (one exact image per look in
`tests/look/goldens.txt`, at 640x360, on the nightly suite).

The scene is `tests/look`: a valley at five in the afternoon on the equator,
looking west into a sun fifteen degrees up. It has a dark frame standing across
the sun, a row of pillars running into the distance, four coloured blocks and
a glowing lamp. `none.png` in each folder is the "before": the engine as it
draws with none of these instances. `tools/repo/look_captures.py` renders every
picture here and measures every cost in `docs/perf-baselines.md`:

```
python tools/repo/look_captures.py capture none bloom-off bloom-strong --out docs/briefs/atmosphere-post/stage2
python tools/repo/look_captures.py measure bloom-strong grade-warm --host <package>/luaug-host.exe
```

## Stage 2 -- `BloomEffect` and `ColorCorrectionEffect`

| File | What is in the world | What to look at |
|---|---|---|
| `none.png` | nothing | The engine's own bloom: a soft glow round the lamp (the bright block, left of the frame) and the sun |
| `bloom-off.png` | a `BloomEffect` with `Enabled = false` | The lamp and the sun lose their glow; nothing else changes |
| `bloom-strong.png` | `BloomEffect` `Intensity` 4, `Size` 48, `Threshold` 0.6 | A wide, strong glow; the pale pillars and the sky near the sun start to glow too, because the threshold is lower |
| `grade-warm.png` | `ColorCorrectionEffect` tinted warm, `Contrast` 0.2, `Saturation` 0.25 | Warmer and punchier; the blocks' colours are stronger and the shadows deeper |
| `grade-mono.png` | `ColorCorrectionEffect` `Saturation` -1, `Contrast` 0.3, `Brightness` -0.05 | Black and white, with more contrast |
| `platformer-none.png` | `examples/20-platformer` as it ships | M1's case: a faint halo under the brick platform and round the coins |
| `platformer-bloom-off.png` | the same, with a disabled `BloomEffect` under `Lighting` | The halo is gone: the sprites no longer glow |

**Also checked, not pictured:** a `BloomEffect` inserted with its defaults draws
exactly the engine's own bloom, and a world with no colour correction draws
through the unchanged tonemap. The command-stream goldens did not move.

## Stage 3 -- `BlurEffect`

| File | What is in the world | What to look at |
|---|---|---|
| `none.png` | nothing | The "before" |
| `blur-soft.png` | a `BlurEffect` of `Size` 4 under `Lighting` | A gentle softening: edges lose their bite, nothing smears |
| `blur-pair.png` | two of `Size` 8, one under `Lighting` and one on the camera | They combine by their squares: one blur of about 11.3, not 16 |
| `blur-menu.png` | a `BlurEffect` of `Size` 24 **on the camera**, with a "Paused" label | The pause-menu case: the world is soft and the label over it is sharp |
| `blur-wide.png` | a `BlurEffect` of `Size` 80 | The largest kind of blur. **Worth judging**: very bright lights -- the lamp, the sun -- stay bright round discs, because the blur works on the light before the tone curve, as a lens does. The alternative (blurring the finished picture) would turn them into grey smudges |

## Stage 4 -- `DepthOfFieldEffect`

| File | What is in the world | What to look at |
|---|---|---|
| `none.png` | nothing | The "before" |
| `focus-near.png` | focus at 20 m, 4 m either side sharp, `FarIntensity` 1 | The coloured blocks and the pillar beside them are sharp; the frame, the far pillars and the valley soften with distance, and the sky is as soft as the furthest thing |
| `focus-far.png` | focus at 150 m, 60 m either side sharp, `NearIntensity` 1, `FarIntensity` 0 | The far valley is sharp; the blocks and the near pillar soften, and **their blur spreads over the sharp background behind them** -- a near object out of focus covers a little of what it stands in front of, as through a lens |

The widest blur, at an intensity of 1, is 16 pixels of a 1080-line picture.
A machine whose graphics settings turn `depth_of_field` off -- the Low and
Medium presets do -- draws these pictures sharp.

## Stage 5 -- `SunRaysEffect`

From this stage on the scene has a slatted fence standing across the sun,
behind the dark frame: shafts of light need gaps to stream through, and the
frame alone was too thin to show any. `none.png` here is that scene.

| File | What is in the world | What to look at |
|---|---|---|
| `none.png` | nothing | The "before": the sun behind the fence |
| `rays.png` | a `SunRaysEffect` with its defaults (`Intensity` 0.25, `Spread` 0.5) | A soft glow around the sun, broken by the slats into faint shafts above and around the fence |
| `rays-strong.png` | `Intensity` 0.8, `Spread` 1 | Clear shafts fanning out from the sun through every gap, reaching well across the picture -- and a haze where the light is thickest |
| `rays-away.png` | the strong rays, with the camera turned away so the sun is off to the right of the picture | The shafts fade out as the sun leaves the view, rather than cutting off |

Only open sky shines: a slat, a post or the ground is a hole in the light, which
is where the dark between the shafts comes from. A machine whose graphics
settings turn `sun_rays` off -- the Low preset does -- draws without them.

## Stage 6 -- `Atmosphere`

Each hour is a pair: the scene as it draws with no `Atmosphere`, and the same
hour with one, so the air is the only thing that differs.

| File | What is in the world | What to look at |
|---|---|---|
| `none.png` / `air.png` | 17:00; an `Atmosphere` with its defaults | An afternoon haze: the far ridges and the valley's end fade into the air's colour, the blue stays overhead, and the horizon and the far ground meet in one colour |
| `air-thick.png` | 17:00; `Density` 0.6, `Glare` 3, `Haze` 2 | A misty evening: the far half of the valley is gone, the sky whitens towards the horizon, and a bright lobe of glare surrounds the sun |
| `noon.png` / `air-noon.png` | 12:00; `Density` 0.45 | Midday haze in the air's own colour, lit white |
| `dusk.png` / `air-dusk.png` | 18:12, the sun just down; `Density` 0.45, `Glare` 2 | The same air tinted by the hour: warm, and dimmer |
| `night.png` / `air-night.png` | 22:00; `Density` 0.45 | The air is dark at night -- the same colour, lit by nothing -- so the lamp glows through it rather than the air glowing |

With an `Atmosphere`, `Lighting.FogStart`, `FogEnd` and `FogColor` are kept and
not used, and the Properties panel says so on `Lighting`.

## Stage 7 -- `Lighting`'s five properties

Not a look to judge so much as a proof: with every one at its default the
picture is the engine's own to the bit (the command-stream captures and the
image goldens did not move). These show what each does when it is changed.

| File | What changed | What to look at |
|---|---|---|
| `none.png` | nothing | The "before" |
| `shadows-off.png` | `GlobalShadows = false` | No sun shadows anywhere; the lamp still glows |
| `shadows-soft.png` | `ShadowSoftness = 1` | The shadows' edges widen into a penumbra |
| `sky-light-off.png` | `EnvironmentDiffuseScale` and `EnvironmentSpecularScale` both 0 | The blue fill the sky gives the shadows is gone: they read darker and neutral, lit only by `Ambient` |
| `exposure-fixed.png` | `AutoExposure = false` | The exposure holds at the calibration value instead of following the frame |

## Stage 8 -- `Sky`

The pictures are `tests/look/content/sky/*.png`, drawn by
`tools/repo/draw_skybox.py`: a late-afternoon gradient, a ring of distant
mountains, streaks of high cloud and dark ground below, **with no sun painted
in** -- the engine draws the sun where the clock puts it.

| File | What is in the world | What to look at |
|---|---|---|
| `none.png` | nothing | The engine's own gradient sky |
| `sky-images.png` | a `Sky` with the six pictures | The pictured sky and its clouds behind the scene; the sun and its shadows exactly where they were -- the clock's, not the pictures'. The reflections and the sky's light on the shadows now come from the pictures |
| `sky-turned.png` | the same, `SkyboxOrientation` 0, 90, 0 | The pictures turned a quarter round the vertical: different clouds overhead, the same sun |
| `sky-big-sun.png` | a `Sky` with no pictures and `SunAngularSize` 8 | The engine's gradient kept, and a sun four times the size |
| `night-before.png` | 05:00, no `Sky` | The engine's own night: dark, no moon disc, no stars |
| `sky-night.png` | 05:00 with a `Sky` and its defaults | The stars and the moon a `Sky` adds -- the moon low in the west behind the fence, opposite the sun, lighting the scene -- in the same places every night |

## Stage 9 -- Clouds

| File | What is in the world | What to look at |
|---|---|---|
| `none.png` | nothing | The "before" |
| `clouds.png` | a `Sky` with `CloudCover` 0.5 | Scattered cumulus over the engine's own gradient, smaller and flatter towards the horizon as a sheet seen from below is |
| `clouds-overcast.png` | `CloudCover` 0.9, `CloudDensity` 0.9 | An overcast sky with darker undersides where the cloud is thick; the sun only just shows through |
| `clouds-dusk.png` | 17:48, `CloudCover` 0.6, a warm `CloudColor` | The same layer lit by a low sun: the clouds take the sunset's colour |

The clouds drift on the game's own clock (`RunService.SimTime`), never a wall
clock: a paused game's clouds stand still, and a replay's move the same way.
They are in the reflections too -- the environment rebuilds every few seconds
of drift.
