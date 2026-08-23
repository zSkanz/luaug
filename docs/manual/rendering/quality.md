# Graphics quality settings

Graphics settings are **engine settings, not `Lighting` properties**, and a
script cannot write them. `Lighting` describes the world and travels with the
scene; these describe the machine it is being shown on. A scene must not decide
a stranger's GPU budget.

There is no settings service and no in-game quality slider. The name is not
reserved either — declaring a class nothing implements is exactly what the API
definition forbids.

## The table

```toml
[graphics]
quality = "high"          # low | medium | high | ultra
render_scale = 1.0        # 0.5 to 1.0
shadow_resolution = 1024  # texels; one cascade's tile, atlas is 2x2 tiles
shadow_cascades = 4       # 0 through 4; 0 is "the sun casts no shadow"
shadow_distance = 120.0   # metres
light_budget = 256        # lights one frame may carry
bloom = true
ambient_occlusion = true
anti_aliasing = true
auto_exposure = true
```

Every key is optional. An absent one leaves whatever the preset chose.

## The presets

`high` is exactly what the engine ships with, to the value — a project that says
nothing gets it.

| Field | low | medium | **high** | ultra |
|---|---|---|---|---|
| `render_scale` | 0.75 | 1.0 | **1.0** | 1.0 |
| `shadow_resolution` | 512 | 1024 | **1024** | 2048 |
| `shadow_cascades` | 2 | 3 | **4** | 4 |
| `shadow_distance` | 70.0 | 100.0 | **120.0** | 160.0 |
| `light_budget` | 32 | 96 | **256** | 256 |
| `bloom` | false | true | **true** | true |
| `ambient_occlusion` | false | false | **true** | true |
| `anti_aliasing` | true | true | **true** | true |
| `auto_exposure` | true | true | **true** | true |

Two of those are worth a sentence each.

**Low turns down render scale first**, because it is the only dial that reduces
every per-pixel pass at once, and a machine that needs this preset is a machine
that is fragment-bound.

**Ultra is shadow density, not shadow range.** Its distance is 160 m rather than
something larger: at 220 m the far cascade measured barely better than high's,
so four times the atlas bought nine per cent, and a preset called Ultra was —
for anything past thirty metres — exactly as blocky as the one below it. At 160
it is genuinely sharper.

## What render scale touches

The **world** renders at that fraction and is upscaled into the target. The post
chain and the UI are unaffected: the 2D pass draws at full resolution on top,
which is the whole reason a render scale is worth having.

The floor is 0.5 because below that the world is upscaled by more than two and
the crisp UI drawn over it makes the difference impossible to ignore.

## Three layers

1. **The preset** — a named set of every field.
2. **`luaug.toml`'s `[graphics]`** — the game author's default.
3. **The host's own flags** — what a person debugging, a benchmark or a
   capture uses.

Each is expressed as an *override* rather than as a value, so that "nobody said
anything" and "somebody asked for the default" stay different answers. The
result is clamped once, at the end.

One rule is not implied by "each layer overrides the one before", and it is the
one that matters:

> **A preset the player names replaces the file's per-key entries as well as its
> level.**

A file's `shadow_resolution` is a refinement *of the level that file names*.
`--quality=low` says that level is not available on this machine, so carrying
its refinements across would hand a weak machine the single heaviest dial in the
file while every other one was turned down. The host's own per-key flags always
apply, because they were typed by the same person as the preset.

## The host flags

```text
--quality=low|medium|high|ultra
--render-scale=F
--shadow-resolution=N
--shadow-cascades=N
--shadow-distance=F
--light-budget=N
--bloom            / --no-bloom
--ambient-occlusion / --no-ambient-occlusion
--anti-aliasing    / --no-anti-aliasing
--auto-exposure    / --no-auto-exposure
```

Parsing is strict: `--render-scale=0.75x` is a usage error rather than 0.75.

## Clamping

Values are clamped rather than refused, once, at the last door:
`render_scale` to 0.5–1.0; `shadow_resolution` to 256–2048 and then **down** to
a power of two; `shadow_cascades` to at most 4; `shadow_distance` to 10–1000;
`light_budget` to at most 256.

## What a script can see

Not the settings — but the frame they produced:

```luau
--!strict
local DebugService = game:GetService("DebugService")

print(DebugService:GetStat("FPS"), DebugService:GetStat("FrameTimeMs"))
print(DebugService:GetStat("DrawCalls"), DebugService:GetStat("VisibleObjects"))
```

## Where to look next

- [The post chain](manual:rendering/post) — what each toggle switches
- [Shadows](manual:rendering/shadows) — what the three shadow dials do
- [The debug overlay](manual:guides/debug-overlay) — reading those numbers live
