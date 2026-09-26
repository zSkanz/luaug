# Surface shaders

A material describes a surface with numbers: a colour, some maps, how rough
and how metallic. A **surface shader** describes it with code. It is an HLSL
file a material names, and every part wearing that material is drawn with it
-- lit by the sun and the lights, shadowed, fogged and post-processed exactly
as any other part is.

Reach for one when the numbers cannot say it: water whose waves move every
vertex, a flag that ripples, a part that burns away, glass that bends what is
behind it. `examples/11-ocean` and `examples/23-surfaces` are all four.

## Writing one

Right-click in **Content** and choose **New Surface Shader...**. The file is
written under `content/shaders/`, opened in the script editor, and it compiles
as it stands: a tint and a wobble, with the rest of the contract explained in
its comments.

```hlsl
#include "luaug/surface.hlsli"

LUAUG_PARAM(float3, Tint, float3(1.0, 1.0, 1.0), colour)
LUAUG_PARAM(float, Wobble, 0.0, range(0, 1))

void surfaceVertex(inout SurfaceVertex vertex, SurfaceInputs inputs)
{
    vertex.Position += vertex.Normal * sin(inputs.Time * 3.0 + vertex.Position.y * 4.0) * Wobble * 0.05;
}

void surfaceFragment(SurfaceInputs inputs, inout SurfaceOutput surface)
{
    surface.BaseColor = Tint * inputs.VertexColor.rgb;
}
```

A surface shader is **two functions**:

- `surfaceVertex` may move a vertex, in the object's own space, and change its
  normal. A moved vertex casts a moved shadow.
- `surfaceFragment` decides what the surface **is** -- colour, alpha,
  metalness, roughness, normal, glow -- and never how it is lit.

Either may be left out: `#define LUAUG_NO_VERTEX` or `#define LUAUG_NO_FRAGMENT`
before the include says so.

A shader may `#include` a `.hlsli` of its own, beside it in `content/`. Saving
either recompiles every surface that uses it.

## What a shader reads and writes

`luaug/surface.hlsli` is the contract, and every member is documented there.
In short:

| `SurfaceVertex` | |
|---|---|
| `Position`, `Normal`, `Tangent` | The vertex, in the object's space. `Tangent.w` is the handedness |
| `Uv0`, `Uv1` | The mesh's two texture coordinate sets; `Uv1` is `Uv0` when there is one |
| `Color` | The vertex colour, white when the mesh has none |

| `SurfaceInputs` | |
|---|---|
| `Time` | Seconds of simulation, the clock a script reads as `RunService.SimTime` |
| `CameraPosition` | Where the camera is, in world coordinates |
| `ObjectToWorld` | The part's transform (vertex stage) |
| `WorldPosition`, `WorldNormal`, `WorldTangent` | In the world's own coordinates, so a pattern laid out in the world stays put |
| `Uv0`, `Uv1`, `VertexColor` | Interpolated from the vertices |
| `ScreenUv`, `ScreenPosition` | Where on screen, and `ScreenPosition.w` is the distance in front of the camera |
| `SceneDepth`, `SceneColor` | What is behind this pixel, in a blended surface -- see *Seeing what is behind* |

| `SurfaceOutput` | |
|---|---|
| `BaseColor`, `Alpha` | The colour, and how opaque |
| `Metallic`, `Roughness` | As a material's `Metalness` and `Roughness` |
| `Normal` | World space. Leave it for the mesh's own normal |
| `Emissive` | Light the surface gives off, in linear HDR |

`SurfaceOutput` starts as the engine's defaults -- white, opaque, not metallic,
roughness 0.7, the mesh's normal, no glow -- so a shader writes only what it
changes.

**What a shader may not do** is declare registers, samplers, constant buffers
or entry points of its own. The layout is the engine's, so the same file
compiles for Direct3D, Vulkan and Metal and reads the same parameters on all
three; a shader that declares its own is refused, and the message says which
line.

## Parameters and textures

A **parameter** is a field of the material. Declare it with a type, a name, a
default and, optionally, how the material panel shows it:

```hlsl
LUAUG_PARAM(float, FoamWidth, 0.35, range(0, 4))   // a slider
LUAUG_PARAM(float3, Deep, float3(0.0, 0.1, 0.2), colour)
LUAUG_PARAM(bool, Animated, true, toggle)
```

The types are `float` to `float4`, `int` and `bool`. The shader reads it by its
name, as if it were a global.

A **texture** is named the same way, with what it reads as when the material
sets none -- white unless you say `black` or `normal`. There may be five.

```hlsl
LUAUG_TEXTURE(Foam)
LUAUG_TEXTURE(Ripples, normal)

float3 foam = LUAUG_SAMPLE(Foam, inputs.Uv0).rgb;
if (LUAUG_TEXTURE_SET(Ripples))
    surface.Normal = surfaceNormalFromMap(LUAUG_SAMPLE(Ripples, inputs.Uv0).rgb, 1.0, inputs);
```

`LUAUG_TEXTURE_SET` says whether the material gave one: a normal map that is
not there is no normal map, not a flat one. `surfaceNormalFromMap` turns a
normal map's texel into a world normal the way the built-in surface does.

## Naming it from a material

In the material panel, **surface shader** lists the project's shaders. Choose
one and its parameters appear below it as fields -- a slider for a `range`, a
colour picker for a `colour` -- with a reset beside any the material sets. The
panel also says whether the shader compiled, and what the compiler said if it
did not.

In the file, it is the `shader` field, and the parameters sit in `properties`
beside the built-in ones:

```json
{
  "format": "luaug-material",
  "version": 1,
  "shader": "asset://shaders/ocean.surface.hlsl",
  "readsSceneColor": true,
  "properties": {
    "Transparency": 0.001,
    "Roughness": 0.14,
    "FoamWidth": 0.35
  }
}
```

A parameter the material does not set reads the shader's default. A variant
inherits its parent's shader and parameters and may change either.

## Changing parameters from a script

A loaded material is shared and read-only, as it is for the built-in fields.
Clone it, and set the parameter on the clone:

```luau
--!strict
local burning = Material.load("asset://materials/dissolve.material.json"):Clone()
burning:SetShaderParameter("Threshold", 0.5)
crate.Material = burning
print(burning:GetShaderParameter("Threshold")) --> 0.5
```

A value is a number, a boolean, a `Vector2`, a `vector`, a `Color3`, a table of
four numbers, or a texture's URN. **A clone's shader parameters are not
replicated**: set them on every machine that draws the part.

## One value per part

A material may let a part change a shader parameter, as it lets one change
`Color`: name it in `instanceParameters`.

```json
"instanceParameters": ["Threshold"]
```

Then every part wearing the material has a `Threshold` of its own, set the way
a part's colour is -- in the Properties panel, or from a script:

```luau
--!strict
crate:SetMaterialParameter("Threshold", 0.5)
print(crate:GetMaterialParameter("Threshold")) --> 0.5
crate:ClearMaterialParameter("Threshold") -- back to the material's value
```

A part's own values are saved with the scene and copied by `Clone`. They take
anything a shader parameter takes except a texture: a part with a texture of
its own is a material of its own, which is what `Material:Clone` is for. Like
a clone's, **a part's shader parameters are not replicated**.

A part with a value of its own is drawn apart from the parts without one, as a
part with a colour of its own is -- so a few hundred parts each with a
different value cost a few hundred draws.

## Moving vertices the game can feel

The GPU moves a vertex, and nothing can read where it went: the picture is
drawn and gone, a frame late and on another processor, and a simulation that
waited for it would no longer be deterministic. So when the game needs to know
-- a boat on a wave, a crate bobbing in the swell -- **write the function
twice**: once in the shader, and once in Luau, term for term.

Both copies agree because they read one clock. `inputs.Time` is
`RunService.SimTime`, interpolated to the frame, so this Luau and this HLSL
give the same height at the same place at the same moment:

```luau
local function seaHeight(x: number, z: number, t: number): number
    local k = 2 * math.pi / 67
    return 1.15 * math.sin(k * x - math.sqrt(9.81 * k) * t)
end
```

```hlsl
float seaHeight(float2 xz, float t)
{
    const float k = 2.0 * 3.14159265 / 67.0;
    return 1.15 * sin(k * xz.x - sqrt(9.81 * k) * t);
}
```

`examples/11-ocean` is this at full size: three waves, nine grids moved by the
shader, and a boat and its cargo floated by the Luau copy. Keep the function
something both languages can write the same way -- sines of positions and time
-- and keep what only looks nice, like a short chop in the normal, in the
shader alone.

A shader that moves vertices wants vertices to move: a flat `Part` has four.
`MeshPart.MeshContent` may name one of the engine's grids --
`luaug://mesh/grid-16`, `grid-64` or `grid-256` -- a one-metre square of that
many quads a side, scaled by the part's `Size`.

## Seeing what is behind

A **blended** surface -- `Transparency` above zero, or `AlphaMode` `Blend` --
can read the scene behind it:

- `sceneDepthAt(uv)` is the distance to whatever opaque surface is behind a
  point on screen. Less `inputs.ScreenPosition.w`, it is how much of this
  surface there is to see through: foam where water is thin against a hull,
  murk where it is deep.
- `sceneColorAt(uv)` is what was drawn there, before any blended surface. It
  needs `readsSceneColor` on the material, because it costs a copy of the frame
  on every frame something wearing it is on screen.

Anywhere else they read as a far distance and black, so a shader that uses
them still compiles on an opaque material.

## Compiling

In the editor, a shader compiles **in the background** the first time
something wearing it is drawn, and again every time it or an include is saved.
Until it is ready, what wears it draws with the built-in surface; no frame
waits. A compiled shader is cached by everything it was built from, so the
next session reads it back in a millisecond.

A shader that does not compile draws as a **magenta error surface**. Its errors
are in the console by file and line, marked on their lines in the script
editor, and listed in the material panel.

**Building a game compiles every surface shader for every backend** into its
content pack, and a shader that does not compile fails the build, by file and
line. The game itself carries no compiler and never compiles anything. A build
made on a machine with no shader compiler still succeeds and says so: its
surfaces go in as source alone, and the game draws them as the error surface.

## A shader can stop the GPU

A shader runs on the graphics card, with nothing between it and the hardware.
A loop that never ends -- or one that runs a million times a pixel -- stalls
the whole card, and after a couple of seconds the operating system resets the
driver -- the screen may go black for a moment.

The editor survives it but cannot keep drawing: the device it drew with is
gone. It says what happened and offers to **save and restart**. The surface
shaders that were on screen are **held back** in the restarted editor -- drawn
as the error surface, with *held back* beside them in the console and the
material panel -- until you change and save them, so the one that hung the
card does not hang it again the moment the editor opens. A game that loses its
device says so and closes.

Keep loops bounded by constants, and prefer `[unroll]` over a loop whose count
comes from a parameter.
