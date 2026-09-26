# examples/23-surfaces — three surface shaders a project writes itself

A flag in the wind, a block that dissolves, and a pane of glass. Each is one
file in [`content/shaders/`](content/shaders/) written against
`luaug/surface.hlsli` (ADR 0091), worn through a material in
[`content/materials/`](content/materials/). The engine builds every pipeline the
surface needs from it, so the sun, its shadows, the lights, fog and the post
chain apply as they do to anything else.

```
examples\23-surfaces\run.bat
```

| | shader | what it shows |
|---|---|---|
| the flag | `flag.surface.hlsl` | **vertex displacement** on something that is not water: a flat `luaug://mesh/grid-64` on a pole, rippling away from it, its folds shaded from the same function |
| the dissolve | `dissolve.surface.hlsl` | a **Mask** surface cut by noise, with a glowing rim ahead of the cut. Each block wears its own `Material:Clone()`, and the script moves the clone's `Threshold` with `SetShaderParameter` |
| the glass | `glass.surface.hlsl` | a **blended** surface that reads the scene behind it (`readsSceneColor`) and bends it with `sceneColorAt` |

**Edit any of the three while the editor runs** and it recompiles on save; the
first compile of each takes under a second, and every later run reads the
cache.

**What it does not do yet.** A dissolving block's shadow stays whole: the
shadow pass draws where a surface is, not where it has cut itself. And a
clone's shader parameters are this machine's -- they do not replicate.
