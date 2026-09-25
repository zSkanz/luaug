# Atmosphere and post effects: captures for the owner's judgement

ADR 0096's before-and-after pictures, one folder per stage. **Every picture in
a stage is the same camera at the same `ClockTime`**; only the effects in the
world differ. The goldens for these effects are recorded after the owner
accepts the look, never before.

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
