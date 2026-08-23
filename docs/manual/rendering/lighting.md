# Lighting and the sky

`Lighting` is the service that describes the world's environment: the sun, the
sky, the fog and the exposure. It is a service rather than a set of engine
settings because it **travels with the scene** — it says what this place looks
like, not what the player's machine can afford.

```luau
--!strict
local Lighting = game:GetService("Lighting")

Lighting.ClockTime = 8
Lighting.GeographicLatitude = 35
Lighting.Brightness = 2.6
Lighting.Ambient = Color3.fromRGB(10, 11, 15)
```

## One property drives the sun

`Lighting.ClockTime` is the hour of the day, and with `Lighting.GeographicLatitude`
it is the **only** input to where the sun is. No wall clock, no accumulated
state — which is what makes a replay light the same way the live run did.

`Lighting.SunDirection` is read-only and is that answer.

```luau
--!strict
local RunService = game:GetService("RunService")

-- A ninety-second day.
RunService.Heartbeat:Connect(function(dt: number)
    Lighting.ClockTime += (24 / 90) * dt
end)
```

`ClockTime` wraps on its own: it is taken modulo 24, and a negative value folds
up rather than raising. `GeographicLatitude` is degrees from −90 to 90.

Through the sun's **elevation**, `ClockTime` also drives four things you do not
set directly:

- **The sun's colour** — warm within a few degrees of the horizon, white above.
- **The zenith colour** — deep blue by day, near black at night.
- **The horizon colour** — `Lighting.FogColor`, scaled by how much daylight
  there is.
- **A day factor** that multiplies the sun's brightness, so a sun below the
  horizon lights nothing rather than lighting every upward-facing surface from
  underneath.

That is why `Lighting.Brightness` is the sun's *strength* alone. The sun has a
colour of its own now, and it is not this property.

## The sky is analytic

There is no skybox, no HDRI and no `Sky` instance. The sky is a horizon-to-zenith
gradient plus a sun disc, computed from the properties above and drawn before any
geometry, in linear HDR so that the disc can be brighter than white without
clipping.

**The sky is also the reflection environment.** A metal surface reflects the sky
at the hour the script set — prefiltered per roughness, with diffuse irradiance
projected from the same sky. One consequence is worth stating plainly: there is
one environment for the whole world, so a polished floor **indoors** reflects the
sky outside. It is right outdoors and wrong in a cave.

## The properties

| Property | Type | Default | Meaning |
|---|---|---|---|
| `Lighting.ClockTime` | `number` | 12 | Hour of the day. Wraps mod 24. |
| `Lighting.GeographicLatitude` | `number` | 0 | Degrees, −90 to 90. |
| `Lighting.Brightness` | `number` | 2.0 | The sun's strength. |
| `Lighting.Ambient` | `Color3` | a dim blue-grey | A stand-in for bounced light there is still none of. |
| `Lighting.FogColor` | `Color3` | a pale blue | Also the horizon's colour. |
| `Lighting.FogStart` | `number` | 200 | Metres at which fog begins. |
| `Lighting.FogEnd` | `number` | 0 | Metres at which it is total. |
| `Lighting.ExposureCompensation` | `number` | 0 | EV stops on top of the measured exposure. |
| `Lighting.SunDirection` | `vector`, read-only | — | Derived. |

**Fog is off by default**, because `FogEnd` starts at 0 and an end at or below
the start means no fog at all. Turning it on is two writes:

```luau
Lighting.FogColor = Color3.fromRGB(150, 178, 214)
Lighting.FogStart = 40
Lighting.FogEnd = 220
```

`Ambient` is added to the image-based diffuse light, on the diffuse lobe only.
It is not clamped: a channel above 1 is legal and means what it says.

## Exposure

`Lighting.ExposureCompensation` is the one artist control over the post chain —
EV stops on top of the exposure the renderer measures from the frame itself. `+1`
is twice the light, `−1` is half.

It is the right knob for a scene that meters wrong. A world that is half sky
meters far brighter than its ground alone, so the aperture closes and the ground
goes dark; a stop of positive compensation is the fix. Changing albedos is
**not** the fix, because metering normalises whatever it is shown.

## Where to look next

- [Shadows](manual:rendering/shadows) — what `ClockTime` does to them
- [The post chain](manual:rendering/post) — where the exposure is measured
- [`Lighting`](api:Lighting)
