# The post chain

What happens to the image after the world is drawn, in order:

```text
shadow → depth prepass → occlusion → forward (sky first)
       → exposure → bloom → tonemap → anti-aliasing
```

Most of it is not scriptable, and that is the design: these are properties of
how the image is produced, and they belong to the machine showing it rather than
to the scene being shown. The one control a scene owns is
`Lighting.ExposureCompensation`.

## Automatic exposure

The renderer measures the frame's own average luminance, adapts it towards that
value over time, and opens or closes the aperture accordingly:

```text
exposure = (key / measuredLuminance) * 2 ^ ExposureCompensation
```

Three things about it that explain most of what you will see:

- **Adaptation is per frame, not per second**, and never from a wall clock — a
  screenshot at frame 30 has to be the same picture on a fast machine and a slow
  one. The side effect is that adaptation feels slightly quicker at a high frame
  rate.
- **The measured luminance is clamped.** Without a floor, midnight would open
  the aperture until it matched noon. With it, night lands about a stop and a
  half below day. A stabiliser with no limit is not a stabiliser, it is a
  flattener.
- **Metering normalises whatever it is shown.** A scene that is half sky meters
  far brighter than its ground, so the ground darkens. Changing albedos does not
  fix that; `Lighting.ExposureCompensation` does.

With `auto_exposure = false` the exposure holds at the calibration value; the
scene still tonemaps and `ExposureCompensation` still applies.

## Tonemapping

Khronos PBR Neutral, and it is not toggleable. It was chosen over the two
obvious alternatives for reasons you can see in a sunset: the filmic curve
rotates saturated hues towards orange as they brighten, and Reinhard never
really reaches white.

sRGB encoding happens here and nowhere else.

## Bloom

A dual-filter blur — six downsamples, five upsamples — with a soft-kneed
threshold applied **after** exposure, so "bright enough to bloom" means the same
thing at noon and at dusk. Toggled with `[graphics] bloom`.

## Ambient occlusion

Screen-space, from the depth prepass: sixteen taps on a spiral with a
depth-aware blur, at half resolution, with normals reconstructed from depth
rather than from a G-buffer.

**It multiplies the image-based and ambient light only, never direct light.** A
surface's occlusion of the sky says nothing about whether the sun reaches it,
and the sun already has a shadow map that answers that exactly.

Toggled with `[graphics] ambient_occlusion`.

## Anti-aliasing

FXAA — spatial, single-frame. Toggled with `[graphics] anti_aliasing`.

There is no temporal anti-aliasing, and the reason is worth knowing because it
decides what else is possible: **there is no velocity buffer.** Without motion
vectors there is no TAA, and no temporal upscaler either — no FSR, no XeSS, no
frame generation. That is exactly as far away as it was before this release.

## What it costs

At 1080p on the reference machine, the whole chain is about **0.9 ms**. The
largest items are the extra shadow cascades (0.22–0.28 ms), the depth prepass
(0.20 ms) and FXAA (0.19 ms). Bloom and ambient occlusion both measure at the
noise floor — which is the measurement saying it cannot resolve them, not that
they are free.

## The one scriptable knob

```luau
--!strict
local Lighting = game:GetService("Lighting")

-- EV stops on top of what the renderer measured. Finite, otherwise unbounded.
Lighting.ExposureCompensation = -1.1
```

## Where to look next

- [Graphics quality settings](manual:rendering/quality) — how each toggle is set
- [Lighting and the sky](manual:rendering/lighting)
