# Point and spot lights

Two classes, four shared properties, and no inheritance between them —
`SpotLight` is a sibling of `PointLight` rather than a subclass, because the two
share properties and no behaviour.

## Parent it to a part

```luau
--!strict
local lamp = Instance.new("Part")
lamp.Size = vector.create(0.3, 0.3, 0.3)
lamp.CFrame = CFrame.new(-2.2, 3.2, 2.4)
lamp.Anchored = true
lamp.Parent = workspace

local light = Instance.new("PointLight")
light.Color = Color3.fromRGB(255, 176, 92)
light.Brightness = 14
light.Range = 9
light.Parent = lamp
```

**A light takes its position from the nearest `BasePart` above it.** A light
with no such ancestor lights nothing — it is not an error, it simply has nowhere
to be.

A practical consequence worth knowing early: a light parented directly to a
character sits at that character's own centre, which is a light every one of its
surfaces faces away from. Hang it on a small invisible welded part instead.

## The properties

| Property | Type | Default | Unit |
|---|---|---|---|
| `PointLight.Color` | `Color3` | white | Multiplied by `Brightness`. Not clamped. |
| `PointLight.Brightness` | `number` | 1 | Arbitrary radiance. 0 is off. |
| `PointLight.Range` | `number` | 16 | **Metres.** Contributes nothing past this. |
| `PointLight.Shadows` | `boolean` | `false` | Stored, not yet acted on. |

`SpotLight` adds one:

| Property | Type | Default | Unit |
|---|---|---|---|
| `SpotLight.Angle` | `number` | 45 | **The full width of the cone**, in degrees. |

`Angle` is the full width and not the half-angle, and it accepts more than 0 and
less than 180. The cone points along its anchor part's `LookVector`, which is
−Z. There is no inner-cone property: the soft edge is a fixed fraction of the
angle you set.

`Brightness` is not photometric. It is a radiance multiplier in the engine's own
units, tuned by eye against `Lighting.Brightness` — which is why a value of 14
above is unremarkable rather than enormous.

## Falloff

Inverse-square, windowed so that the light reaches exactly zero at `Range`
rather than merely getting small. Without the window the culler's bounding
volume would be a lie and the cost of a light would be unbounded.

Practically: `Range` is a real cutoff you can budget against, not a hint.

## How many lights

Lighting is **clustered**: the view is diced into a grid and each cell carries
the lights that reach it, so a fragment pays only for the lights near it rather
than for every light in the scene.

Two numbers, and they are different numbers:

- **256 lights per frame.** Settable downward with `[graphics] light_budget`
  (the presets use 32, 96, 256, 256).
- **64 lights per cluster**, which is a fixed constant — and this is the one
  that matters, because it is what a fragment actually pays for.

Past the frame budget, the lights nearest the front of the extraction order win.
That is deterministic rather than arbitrary, because extraction order is.

## Shadows

Neither class casts one in this release. `Shadows` on both is stored and read
back faithfully and changes nothing — see [Shadows](manual:rendering/shadows).

## Where to look next

- [Lighting and the sky](manual:rendering/lighting) — the sun, which is a
  different thing entirely
- [Graphics quality settings](manual:rendering/quality) — where `light_budget`
  lives
- [`PointLight`](api:PointLight) · [`SpotLight`](api:SpotLight)
