# Shadows

**The sun is the only shadow caster in this release.** Point lights and spot
lights illuminate; they do not occlude.

That is not a limit hidden in a footnote — `PointLight.Shadows` and
`SpotLight.Shadows` both carry the **stored, not yet acted on** badge in the
reference. Writing `light.Shadows = true` succeeds, reading it back gives `true`,
and the frame is identical. The property is there because its meaning is not in
doubt, only its implementation date; the badge is there because a property that
accepts a write and changes nothing is otherwise impossible to notice.

## What a script controls

Nothing, directly. There is no shadow property on `Lighting`, on `Camera`, or
anywhere else in the scripting API.

What a script controls is **where the sun is**, and that is `Lighting.ClockTime`
and `Lighting.GeographicLatitude`:

```luau
--!strict
local Lighting = game:GetService("Lighting")

-- Early morning: long shadows, raking across the ground.
Lighting.ClockTime = 7.5
Lighting.GeographicLatitude = 45
```

A low sun gives long shadows and a hard test of the shadow map; a sun overhead
gives short ones and hides most of what a cascade split does.

## What the project controls

Three dials, in `luaug.toml` under `[graphics]` and on the host's own command
line. They are engine settings rather than scene properties, because a scene
must not decide the player's GPU budget.

| Setting | Default | Range |
|---|---|---|
| `shadow_resolution` | 1024 | 256 to 2048, rounded **down** to a power of two |
| `shadow_cascades` | 4 | 0 to 4. **0 means the sun casts no shadow** |
| `shadow_distance` | 120.0 | 10 to 1000 metres |

```toml
[graphics]
shadow_resolution = 2048
shadow_cascades = 4
shadow_distance = 160.0
```

**Fewer cascades buy submission, not memory.** The atlas is two tiles by two
whatever the count is, so `shadow_resolution` is the dial that buys memory —
512 costs 4 MiB, 1024 costs 16 MiB, 2048 costs 64 MiB. What fewer cascades
remove is drawing: every caster is drawn once per cascade it touches.

A cascade nothing renders into is a cleared tile, and a cleared depth reads as
lit.

## How it works, briefly

Four cascades in one atlas, split by a blend of uniform and logarithmic
spacing. The filter radius is constant in **world** space across cascades, so
softness does not change when an object crosses a split, and the last part of
each cascade's range samples both neighbours and blends — so a split is not a
visible line.

The ortho box is snapped to texel increments in light space, which is what stops
the shadow crawling as the camera moves.

## Two honest gaps

- **A transparent part casts a full shadow.** The shadow pass draws everything;
  a half-transparent pane occludes completely.
- **Alpha-masked geometry writes depth where its own fragments would have been
  discarded**, so a cut-out leaf casts the shadow of its quad.

## Where to look next

- [Lighting and the sky](manual:rendering/lighting)
- [Graphics quality settings](manual:rendering/quality) — the three layers those
  dials go through
- [Point and spot lights](manual:rendering/lights)
