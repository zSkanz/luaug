# Point and spot lights

Two classes, four shared properties, and no inheritance between them —
`SpotLight` is a sibling of `PointLight` rather than a subclass, because the two
share properties and no behaviour.

## Where a light is

A light has a `CFrame` of its own (ADR 0095), and where it shines from depends on
what holds it:

- **Nothing holds it** (it is in the Workspace, or in a folder): `CFrame` is
  where it is in the world. A light dropped straight into the scene works.
- **A `BasePart` or an `Attachment` holds it**: `CFrame` is relative to that
  holder, and the light moves with it. The default, the identity, is exactly at
  the holder, which is where every light used to be.

```luau
--!strict
-- A light standing on its own, in the world.
local fill = Instance.new("PointLight")
fill.CFrame = CFrame.new(0, 6, 0)
fill.Range = 20
fill.Parent = workspace

-- A light that travels with a lamp, half a metre above it.
local lamp = Instance.new("Part")
lamp.Size = vector.create(0.3, 0.3, 0.3)
lamp.CFrame = CFrame.new(-2.2, 3.2, 2.4)
lamp.Anchored = true
lamp.Parent = workspace

local light = Instance.new("PointLight")
light.Color = Color3.fromRGB(255, 176, 92)
light.Brightness = 14
light.Range = 9
light.CFrame = CFrame.new(0, 0.5, 0)
light.Parent = lamp
```

**A spot points along its `CFrame`'s `LookVector`** (-Z), after the holder's
own rotation. In the editor, a selected light shows its range and cone, and the
move and rotate handles work on it the way they do on an attachment.

A practical consequence worth knowing: a light inside a character sits at that
character's centre by default, which is a light every one of its surfaces faces
away from. Give it a `CFrame` in front of the character instead.

## The properties

| Property | Type | Default | Unit |
|---|---|---|---|
| `PointLight.Color` | `Color3` | white | Multiplied by `Brightness`. Not clamped. |
| `PointLight.Brightness` | `number` | 1 | Arbitrary radiance. 0 is off. |
| `PointLight.Range` | `number` | 16 | **Metres.** Contributes nothing past this. |
| `PointLight.CFrame` | `CFrame` | identity | In the world, or relative to its holder. |
| `PointLight.Enabled` | `boolean` | `true` | Off also frees its slot in the light budget. |
| `PointLight.Shadows` | `boolean` | `false` | Casts shadows. See below for what it costs. |

`SpotLight` adds one:

| Property | Type | Default | Unit |
|---|---|---|---|
| `SpotLight.Angle` | `number` | 45 | **The full width of the cone**, in degrees. |

`Angle` is the full width and not the half-angle, and it accepts more than 0 and
less than 180. The cone points along the light's own `LookVector`, which is
−Z. There is no inner-cone property: the soft edge is a fixed fraction of the
angle you set.

`Brightness` is not photometric. It is a radiance multiplier in the engine's own
units, tuned by eye against `Lighting.Brightness` — which is why a value of 14
above is unremarkable rather than enormous.

## Shadows from lights

`Shadows = true` makes a light cast shadows, from a shared atlas of sixteen
tiles. **A `SpotLight` takes one tile and a `PointLight` takes six**, one per
face of a cube around it. So two point lights and four spots fill the atlas,
and three point lights do not fit.

When more lights ask than there are tiles, the lights nearest and largest on
screen are served and the rest do not cast. A light that loses is not dimmed and
does not flicker: the order is by apparent size, with ties broken by creation
order, so the answer is the same on every frame and on every machine.

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
