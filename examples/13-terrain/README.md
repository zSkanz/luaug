# 13 — Terrain

Ground you dig into rather than a floor made of parts.

```
luaug run examples/13-terrain
```

**There is nothing special in this project**, and that is what it exists to
show. A `Terrain` is an ordinary instance, its verbs are ordinary method calls,
and the ground they produce is drawn, collided and hashed by the same code as
everything else in the engine. What a sculpting brush does in the editor is
exactly what `src/scripts/init.luau` does, one call at a time.

It has **no scene file**, which every other example with a world has. The world
is sculpted in the script because the sculpting is the subject.

## What to look at

- **The cave under the hill.** A ball removed from inside solid ground leaves
  air with ground above *and* below it, which a height map cannot express at
  all — it is why the terrain is a volume, and it is the cost the whole hybrid
  design in [ADR 0067](../../docs/decisions/0067-terrain-is-one-field-with-two-encodings.md)
  exists to keep affordable.
- **`MinHeight` and `MaxHeight`, set before anything is sculpted.** A collider's
  height precision is spread across that range when it is built and cannot be
  widened afterwards, so digging past it does not deepen the world — it stops.
- **The crate falls onto the hill and stays there.** The collider is one height
  field per 32×32 tile, edited in place when the ground under it changes.

## What is not here yet

The cave is **diggable and not collidable**. A cave's surface needs a triangle
mesh body, which is twelve milliseconds to build for a cell's worth of
triangles — most of a frame — so it needs a budgeted, off-frame rebuild that
F1 has not built. Walk into the hillside and you will pass through the wall of
the cave rather than into it.
