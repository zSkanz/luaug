# Particles and decals

Fire, smoke, sparks, dust and magic are particles. A scorch mark, a footprint,
a bullet hole and a poster are decals. Both are part of the picture rather than
the world: nothing collides with them, and a script sets them up and lets them
run.

## Particles

A `ParticleEmitter` parented to a part or an attachment throws particles out
along its parent's up direction. From a part, they are born anywhere inside
it. Everything it does follows from its properties:

```luau
--!strict
local fire = Instance.new("ParticleEmitter")
fire.Rate = 160                         -- particles a second
fire.Lifetime = 0.8                     -- seconds each one lives
fire.Speed = 1.4                        -- metres a second, out along the parent's up
fire.SpreadAngle = 8                    -- degrees either side of it
fire.Acceleration = vector.create(0, 4, 0)
fire.Color = Color3.new(1, 0.6, 0.15)   -- at birth...
fire.ColorEnd = Color3.new(0.8, 0.1, 0.02) -- ...and at death
fire.Size = 0.7
fire.SizeEnd = 0.1
fire.LightEmission = 1                  -- added light rather than paint
fire.Brightness = 3
fire.Parent = logs
```

| Property | What it does |
|---|---|
| `Rate`, `Lifetime` | How many are born a second, and for how long each lives. |
| `Speed`, `SpreadAngle` | How fast they leave, and how widely around the parent's up. |
| `Acceleration`, `Drag` | What pulls on them -- gravity, wind, buoyant heat -- and how fast they slow. |
| `Color` → `ColorEnd`, `Size` → `SizeEnd`, `Transparency` → `TransparencyEnd` | How each one changes from birth to death. |
| `LightEmission` | 0 blends like smoke, 1 adds light like fire, and between is both. |
| `Brightness` | Multiplies the colour; above 1 glows and blooms. |
| `Shape` | `Soft`, a round falloff, or `Disc`, a crisp dot for sparks and water. |
| `Enabled` | Whether it keeps emitting. |

`Emit(count)` throws a burst at once, whatever `Rate` says: an explosion, a
splash, a puff of dust where a foot landed.

**Particles fade where they meet a surface**, instead of showing a hard line
along it, so smoke rolling over the ground looks like smoke. **They are cheap
because they are not the world.** They are simulated on the frame, not the
tick, a script cannot find one, and a fire of hundreds costs one draw.

## Decals

A `Decal` projects an image onto whatever lies inside its box: curved,
sculpted, animated, anything. It is not tied to a face of a part. It darkens
what it lands on and keeps that surface's own light and shadow, so a scorch
mark on a lit wall is a lit scorch mark.

**A raycast hit is a decal's placement:**

```luau
local result = workspace:Raycast(origin, direction * 100)
if result then
    local mark = Instance.new("Decal")
    mark.CFrame = CFrame.lookAt(result.Position, result.Position + result.Normal)
    mark.Size = vector.create(0.3, 0.3, 0.2)   -- width, height, and how deep it reaches
    mark.Texture = "asset://textures/bullet_hole.png"
    mark.Parent = workspace
end
```

| Property | What it does |
|---|---|
| `CFrame` | Where the box is, relative to its parent part when it has one, so a mark on a moving crate moves with it. |
| `Size` | Width, height and depth of the box, in metres. Only what is inside it is painted. |
| `Texture` | The image. Its alpha is how much of it lands. |
| `Color` | Multiplies the image. |
| `Transparency` | 0 paints the image as it is; 1 paints nothing. |

A decal can only darken: a white pixel is no change. A glowing sign is a
`SurfaceGui` with `Brightness` above 1 instead.

Both decals and particle emitters replicate: on every machine of a match, the
same emitters run and the same marks are painted.

## Where to look next

- [World-space UI](manual:ui/world-space)
- [Lighting and the sky](manual:rendering/lighting)
- `examples/16-particles`: a campfire and its smoke, a fountain, a shower of
  sparks, a burst every two seconds, and a scorch mark
