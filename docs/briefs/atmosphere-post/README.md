# Atmosphere and post effects: captures for the owner's judgement

ADR 0096's before-and-after pictures, one folder per stage. **Every picture in
a stage is the same camera at the same `ClockTime`**; only the effects in the
world differ. The goldens for these effects are recorded after the owner
accepts the look, never before.

The scene is `tests/look`: a valley at five in the afternoon on the equator,
looking west into a sun fifteen degrees up. It has a dark frame standing across
the sun, a row of pillars running into the distance, four coloured blocks and
a glowing lamp. `none.png` in each folder is the "before": the engine as it
draws with none of these instances. `tools/repo/look_captures.py` renders every
picture here and measures every cost in `docs/perf-baselines.md`:

```
python tools/repo/look_captures.py capture none bloom-off bloom-strong --out docs/briefs/atmosphere-post/stage2
python tools/repo/look_captures.py measure bloom-strong grade-warm --host <package>/luaug-host.exe
```

## Stage 2 -- `BloomEffect` and `ColorCorrectionEffect`

| File | What is in the world | What to look at |
|---|---|---|
| `none.png` | nothing | The engine's own bloom: a soft glow round the lamp (the bright block, left of the frame) and the sun |
| `bloom-off.png` | a `BloomEffect` with `Enabled = false` | The lamp and the sun lose their glow; nothing else changes |
| `bloom-strong.png` | `BloomEffect` `Intensity` 4, `Size` 48, `Threshold` 0.6 | A wide, strong glow; the pale pillars and the sky near the sun start to glow too, because the threshold is lower |
| `grade-warm.png` | `ColorCorrectionEffect` tinted warm, `Contrast` 0.2, `Saturation` 0.25 | Warmer and punchier; the blocks' colours are stronger and the shadows deeper |
| `grade-mono.png` | `ColorCorrectionEffect` `Saturation` -1, `Contrast` 0.3, `Brightness` -0.05 | Black and white, with more contrast |
| `platformer-none.png` | `examples/20-platformer` as it ships | M1's case: a faint halo under the brick platform and round the coins |
| `platformer-bloom-off.png` | the same, with a disabled `BloomEffect` under `Lighting` | The halo is gone: the sprites no longer glow |

**Also checked, not pictured:** a `BloomEffect` inserted with its defaults draws
exactly the engine's own bloom, and a world with no colour correction draws
through the unchanged tonemap. The command-stream goldens did not move.
