# Post effects

What happens to the image after the world is drawn, in order:

```text
shadow → depth prepass → occlusion → forward (sky first)
       → atmosphere → blended surfaces and particles
       → depth of field → sun rays → blur
       → exposure → bloom → tonemap (with the colour grade) → anti-aliasing
```

The atmosphere, the three passes after the particles, the bloom and the grade
are **yours to shape with instances**: a world's look is effects you insert,
tune in Properties and toggle from a script. Each is drawn only while it
exists, so a world with none of them costs what it always did.

| Class | What it does |
|---|---|
| `BloomEffect` | The glow bright light spills onto what is around it |
| `ColorCorrectionEffect` | Brightness, contrast, saturation and a tint over the whole picture |
| `BlurEffect` | A soft, full-screen blur -- behind a pause menu, say |
| `DepthOfFieldEffect` | Focus by distance, as a camera lens has it |
| `SunRaysEffect` | Shafts of light from the sun past whatever stands in front of it |

The air and the sky are instances too, and they have
[their own page](manual:rendering/atmosphere-and-sky).

## Where an effect counts

**Where an effect sits says whose it is:**

- **Directly under `Lighting`**, it belongs to the world. It is saved with the
  scene, and in a networked game every player sees it.
- **Directly under `Workspace.CurrentCamera`**, it belongs to whoever looks
  through that camera. A pause-menu blur on one player's camera is nobody
  else's.
- **Anywhere else it does nothing**, and the editor says so: the instance's
  name is dimmed in the Explorer, and hovering it or selecting it gives the
  reason.

```luau
--!strict
local Lighting = game:GetService("Lighting")

-- The world's: every player sees it.
local grade = Instance.new("ColorCorrectionEffect")
grade.Saturation = -0.4
grade.Parent = Lighting

-- This player's: a pause menu's blur.
local blur = Instance.new("BlurEffect")
blur.Size = 16
blur.Parent = workspace.CurrentCamera
```

`Enabled` switches any of them off without taking it out, and every one of
them is off until it exists.

## When there are several of one kind

- **`ColorCorrectionEffect`**: every enabled one applies, one after the other
  -- `Lighting`'s children first, in order, then the camera's. Within one, the
  tint applies first, then the saturation, then the contrast, then the
  brightness.
- **`BlurEffect`**: the sizes combine as blurs do in optics, by their squares.
  Two blurs of 8 are one of about 11.3, not one of 16.
- **`BloomEffect`, `DepthOfFieldEffect`, `SunRaysEffect`**: the first enabled
  one wins, in that same order. The editor marks the others as not used.

## Bloom

A glow around what is brighter than its surroundings. **The engine blooms
without a `BloomEffect`**, exactly as one with its defaults does. Adding one
takes the glow over, and one with `Enabled` off turns it off -- which is what a
2D game whose white sprites should not glow wants.

| Property | What it does |
|---|---|
| `Intensity` | How strongly the glow is added, as a multiple of the engine's own. 0 is none. |
| `Size` | How far the glow reaches, 0 to 56. 24 is the engine's own; 48 spreads the same light about twice as far. |
| `Threshold` | How bright a pixel must be, after exposure, before it glows. The frame's average sits near 0.45 after exposure, so the default of 1.1 blooms what is a little over twice as bright as the average. |

Inside it is a dual-filter blur -- five halvings down with a thirteen-tap box
and back up with a tent -- whose soft-kneed threshold is applied **after**
exposure, so "bright enough to bloom" means the same at noon and at dusk.

## Colour correction

A grade over the picture: `Brightness`, `Contrast` and `Saturation` from -1 to
1, and a `TintColor` that multiplies it. It works on the light after exposure
and before the tone curve, so a highlight it brightens still rolls off rather
than clipping, and contrast pivots about the frame's average brightness.
However many there are, they cost the same: they fold into one sum before the
picture is drawn.

## Blur

`Size` is how far a pixel's light is spread, in pixels of a picture 1,080 lines
tall, so a blur looks the same at any window size; `0` is no blur and costs
nothing. **The interface is drawn after it and stays sharp.** It works on
light, before exposure: a lamp blurred stays a bright disc, as through a lens,
rather than a grey smudge.

## Depth of field

A sharp band of the world, and what is nearer or further softening away from
it. The sky counts as infinitely far.

| Property | What it does |
|---|---|
| `FocusDistance` | The distance from the camera, in metres, that is sharpest. |
| `InFocusRadius` | How many metres either side stay fully sharp. |
| `NearIntensity` | How soft what is nearer becomes, 0 to 1. |
| `FarIntensity` | How soft what is further becomes, 0 to 1. |

A blurred foreground spreads over what is behind it, as through a lens, and a
blurred background never bleeds over a sharp object in front of it. It is not
drawn under an orthographic camera.

## Sun rays

Shafts of light streaming from the sun past whatever stands in front of it --
trees, a fence, a window frame. **Only open sky shines**: anything nearer is a
gap in the light, which is where the dark between the shafts comes from.
`Intensity` (0 to 1) is how bright they are, and `Spread` (0 to 1) how far they
reach from the sun. They fade away as the sun leaves the view or sets.

## Automatic exposure

The renderer measures the frame's own average brightness, adapts towards it
over time, and opens or closes the aperture accordingly:

```text
exposure = (key / measuredLuminance) * 2 ^ ExposureCompensation
```

- **Adaptation is per frame, not per second**, and never from a wall clock -- a
  screenshot at frame 30 is the same picture on a fast machine and a slow one.
- **The measured brightness is clamped.** Without a floor, midnight would open
  the aperture until it matched noon; with it, night lands about a stop and a
  half below day.
- **Metering normalises whatever it is shown.** A scene that is half sky
  meters brighter than its ground, so the ground darkens.
  `Lighting.ExposureCompensation` is the fix, in EV stops: `+1` is twice the
  light, `-1` half.

`Lighting.AutoExposure = false` holds the exposure at its calibration value, so
a world lit darker looks darker. The machine's own setting can turn it off too;
either one does.

## Tonemapping

Khronos PBR Neutral, and it is not toggleable: the filmic curve rotates
saturated hues towards orange as they brighten, and Reinhard never really
reaches white. sRGB encoding happens here and nowhere else.

## Ambient occlusion and anti-aliasing

Ambient occlusion is screen-space, from the depth prepass, at half resolution,
and it darkens the image-based and ambient light only -- never the sun, which
has its shadow map. Anti-aliasing is FXAA, on the finished picture. There is no
temporal anti-aliasing, because there is no velocity buffer.

## What the machine decides

The graphics settings describe the machine, not the world
([Graphics quality settings](manual:rendering/quality)). `bloom`,
`depth_of_field` and `sun_rays` there **win over the world**: a scene with a
`DepthOfFieldEffect` on a machine set to `depth_of_field = false` draws sharp.
**`BlurEffect` and `ColorCorrectionEffect` have no machine switch**, because a
game uses them to say something -- a paused menu, a flash of damage -- and
turning them off would change what the picture means, not what it costs.

## What it costs

At 1080p on the reference machine, the engine's own chain is about **0.9 ms**.
Each effect's measured cost is in `docs/perf-baselines.md`, beside the budget
it was built to.

## Where to look next

- [Atmosphere, sky and clouds](manual:rendering/atmosphere-and-sky)
- [Lighting and the sky](manual:rendering/lighting)
- [Graphics quality settings](manual:rendering/quality)
