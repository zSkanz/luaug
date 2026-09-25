# Atmosphere, sky and clouds

The air a world is seen through and the sky above it are **instances you put
under `Lighting`**: an `Atmosphere` for the air and a `Sky` for everything
overhead. With neither, a world draws with the engine's own sky -- a gradient
driven by the time of day -- and the linear fog of `Lighting.FogStart`,
`FogEnd` and `FogColor`.

```luau
--!strict
local Lighting = game:GetService("Lighting")

local air = Instance.new("Atmosphere")
air.Density = 0.3
air.Glare = 1
air.Parent = Lighting

local sky = Instance.new("Sky")
sky.CloudCover = 0.5
sky.Parent = Lighting
```

**Both count only directly under `Lighting`, and only the first of each.** A
second `Sky`, or an `Atmosphere` in a folder, does nothing, and the editor says
so on the instance: its name is dimmed in the Explorer, and hovering it or
selecting it gives the reason.

## `Atmosphere`

The air between the camera and everything it sees. Distance fades into it,
it thins with height, and the sky turns towards its colour at the horizon, so
the far ground and the sky meet.

| Property | Range | What it does |
|---|---|---|
| `Density` | 0 to 1 | How thick the air is. At 0.35, half of what stands 280 metres away still shows; at 1, half is gone within 35 metres. |
| `Offset` | metres | The height where the air has exactly its `Density`. Thicker below, thinner above. |
| `Color` | colour | The air's own colour, lit by the time of day: pale at noon, warm at dusk, dark at night. |
| `Decay` | 0 to 1 | How fast the air thins with height. 0 is the same thickness everywhere; 0.1 halves every 100 metres; 1 every 10. |
| `Glare` | 0 to 10 | A bright lobe of the sun's light around it, in the sky and in the distance seen towards it. |
| `Haze` | 0 to 10 | Extra thickness towards the horizon. |

**With an `Atmosphere`, the linear fog is not used.** `FogStart`, `FogEnd` and
`FogColor` keep their values -- take the air away and they apply again -- and
the Properties panel says so on `Lighting`.

Two things about how it is drawn explain what you will see:

- **The air is laid over the finished opaque world and the sky in one pass**,
  from the depth of every pixel. Glass and particles are drawn after it, so
  they are fogged by a simpler, linear version of the same air rather than by
  the exact one.
- **Over the open sky the air counts for less** than over the ground, because
  the sky's gradient already is the air above. The horizon, where the air is
  thickest, is covered either way.

## `Sky`

What the sky shows, above the air.

### Six pictures

`SkyboxBack`, `SkyboxDown`, `SkyboxFront`, `SkyboxLeft`, `SkyboxRight` and
`SkyboxUp` name six square images around the world, as seen from inside it,
upright:

| Face | Looks towards |
|---|---|
| `SkyboxFront` | -Z, where a camera with no rotation looks |
| `SkyboxBack` | +Z |
| `SkyboxRight` | +X |
| `SkyboxLeft` | -X |
| `SkyboxUp` | straight up; its bottom edge meets the front's top edge |
| `SkyboxDown` | straight down; its top edge meets the front's bottom edge |

`SkyboxOrientation` turns the set, in degrees about X, then Y, then Z -- a
mountain range painted into the pictures is moved to where the world wants it.

In the editor, **drop a folder of six pictures on a `Sky`** in the Explorer and
its faces are filled from the names: the last word of each name says which face
it is -- `back` or `bk`, `down`, `dn` or `bottom`, `front` or `ft`, `left` or
`lf`, `right` or `rt`, `up` or `top`, or the axis names `px`, `nx`, `py`, `ny`,
`pz` and `nz` (where +Z is the back).

What happens to the pictures:

- **They are resampled once, off the frame**, into one picture the sky is drawn
  from. Until that is done, the sky that was there keeps drawing.
- **They are what the world reflects and is lit by.** A metal surface shows the
  pictures, and a shadow is filled with their light.
- **They are shown at their own brightness at every hour.** A daytime sky stays
  a daytime sky at midnight; choose pictures for the hour a scene is set at, or
  use `Lighting.EnvironmentDiffuseScale` and `EnvironmentSpecularScale` to say
  how much they light the world.
- They are colour images, like a part's colour map.

### The sun stays on the clock

**The sun's direction is `Lighting.ClockTime` and `GeographicLatitude`'s, and
nothing a picture shows changes it.** Shadows, sun rays and the drawn sun disc
all agree, and a replay lights the same way. A picture with a sun painted into
it will disagree with the real one: choose pictures without one, or set
`CelestialBodiesShown` to false.

### The sun, the moon and the stars

| Property | What it does |
|---|---|
| `SunTexture` | A picture drawn as the sun's disc, in the sun's colour. None draws a round, soft-edged disc. |
| `SunAngularSize` | How wide the sun looks, in degrees across. The real sun is about half a degree; the default is 2.3, so a sunset reads in a short day. |
| `MoonTexture` | A picture drawn as the moon at night. None draws a pale disc. |
| `MoonAngularSize` | How wide the moon looks, in degrees. |
| `StarCount` | How many stars cover the whole sky at night, 0 to 10,000. They are the same stars in the same places every night. |
| `CelestialBodiesShown` | Whether the sun, the moon and the stars are drawn at all. The light still comes from where the clock puts the sun. |

The moon stands opposite the sun, so it rises as the sun sets and is highest at
midnight. The moon and the stars come out as the day goes; neither is in the
reflections, where a point of light is too small to see.

A `Sky` with no pictures still governs these, over the engine's own gradient.

### Clouds

| Property | What it does |
|---|---|
| `CloudCover` | How much of the sky the clouds cover, from 0 (none) to 1 (overcast). |
| `CloudDensity` | How thick each cloud is: 0 is wisps the sky shows through, 1 is solid, with dark undersides. |
| `CloudColor` | Their colour where the sun lights them. |

The clouds are one layer high above the world, seen from below: large
overhead and smaller towards the horizon. **They drift on the game's own
clock** (`RunService.SimTime`): a paused game's clouds stand still, and a
replay's move the same way. They are in the reflections too.

## Where to look next

- [Lighting and the sky](manual:rendering/lighting) -- the time of day and the sun
- [Post effects](manual:rendering/post) -- bloom, colour, blur, depth of field and sun rays
- [`Atmosphere`](api:Atmosphere) and [`Sky`](api:Sky)
