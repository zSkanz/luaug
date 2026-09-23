# 16-particles

Sparks, smoke, fire and water (F2): four `ParticleEmitter`s and a burst.

```
run.bat
```

- **The campfire burns all over its logs** because a part is a volume and
  particles are born anywhere in it; its flames are additive (`LightEmission`
  1), so they brighten what is behind them and never darken it.
- **The smoke** comes from the same logs, blended, slowing (`Drag`) and
  swelling (`SizeEnd`) as it rises.
- **The sparks** leave from an `Attachment`, which is a point and a direction —
  the usual way to aim a jet — and are bright enough to bloom.
- **The fountain** is a narrow cone of discs brought back down by gravity.
- **The blast** is `Emit(120)` every two seconds from an emitter that does not
  stream.

Particles are a picture, not the world: they collide with nothing, a script
cannot find one, and they are simulated on the frame. A particle crossing a
surface shows a hard edge there — soft particles need a change to the RHI that
F2 recorded rather than took.
