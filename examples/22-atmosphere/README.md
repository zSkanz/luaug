# 22-atmosphere

The look of a world, as instances (ADR 0096): a small valley under a `Sky` of
six pictures with clouds drifting across it, in an `Atmosphere`, with every
post effect -- and a day that runs on `Lighting.ClockTime`.

```
run.bat
```

- **The day** runs from seven in the morning, six minutes from dawn to dawn:
  the sun and every shadow follow the clock, whatever the pictures show, and
  at night the moon and the stars come out.
- **Each effect is a key**, and a list at the top left says what is on:

  | Key | What |
  |---|---|
  | 1 | The `Atmosphere` -- in and out of `Lighting` |
  | 2 | The `Sky`'s pictures -- without them it keeps its clouds, sun and stars over the engine's own gradient |
  | 3 | `BloomEffect` |
  | 4 | `ColorCorrectionEffect`, a gentle warm grade |
  | 5 | `SunRaysEffect` |
  | 6 | `DepthOfFieldEffect`, sharp on the cabin |
  | 7 | `BlurEffect` on the camera -- a pause menu's, which nobody else in a networked game would see |
  | T | The day at ten times the speed |

- **The pictures are `content/sky/*.png`**, drawn by
  `tools/repo/draw_skybox.py`: no sun painted in, because the engine draws the
  sun where the clock puts it.
- A **`PointLight`** in the cabin comes into its own at night.

See [Atmosphere, sky and clouds](../../docs/manual/rendering/atmosphere-and-sky.md)
and [Post effects](../../docs/manual/rendering/post.md).
