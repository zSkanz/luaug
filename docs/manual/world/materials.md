# Materials

A material is what a surface looks like: a colour, a set of maps, and how rough
and metallic it is. **A material is a file**, `<name>.material.json` under your
project's `content/` (by convention `content/materials/`). It is never an
instance: nothing parents one, and nothing finds one in a `Workspace`.

A part **wears** a material. It has no colour of its own.

```luau
--!strict
local wall = Instance.new("Part")
wall.Size = vector.create(8, 4, 1)
wall.Material = Material.load("asset://materials/brick.material.json")
wall.Anchored = true
wall.Parent = workspace
```

A part wearing nothing (`part.Material = nil`, the default) wears the **engine
default material**: white, dielectric, roughness 0.7. That is exactly what a
plain part has always looked like.

## The file

```json
{
  "format": "luaug-material",
  "version": 1,
  "parent": "",
  "instanceParameters": ["Color"],
  "properties": {
    "Color": [0.8, 0.3, 0.2],
    "Transparency": 0,
    "ColorMap": "asset://textures/brick.png",
    "NormalMap": "asset://textures/brick_n.png",
    "MetallicRoughnessMap": "",
    "Emissive": [0, 0, 0],
    "EmissiveMap": "",
    "Metalness": 0,
    "Roughness": 0.9,
    "NormalScale": 1,
    "AlphaMode": "Opaque",
    "AlphaCutoff": 0.5,
    "DoubleSided": false
  }
}
```

| Field | Meaning |
|---|---|
| `Color` | The base tint, multiplied into `ColorMap`. |
| `Transparency` | 0 opaque, 1 invisible. |
| `ColorMap`, `EmissiveMap` | Colour images (sampled through the sRGB curve). |
| `NormalMap`, `MetallicRoughnessMap` | Data images (read as numbers). `MetallicRoughnessMap` packs occlusion, roughness and metalness in R, G and B, as glTF does. |
| `Emissive` | Light the surface gives off on its own. It lights nothing else. |
| `Metalness`, `Roughness` | 0 to 1. |
| `NormalScale` | Scales the normal map. 1 is as authored. |
| `AlphaMode`, `AlphaCutoff` | `Opaque`, `Mask` (tested against `AlphaCutoff`) or `Blend`. |
| `DoubleSided` | Whether back faces are drawn. |

The editor writes the file for you: right-click in the content browser and
choose **New Material**, then edit it in the **Material** panel.

## Variants

A **variant** names another material as its `parent` and writes only what it
changes. Everything it does not write comes from the parent, so editing the
parent changes every variant that has not changed that field.

```json
{
  "format": "luaug-material",
  "version": 1,
  "parent": "asset://materials/brick.material.json",
  "instanceParameters": [],
  "properties": { "Color": [0.4, 0.5, 0.3] }
}
```

In the editor: right-click a material and choose **New Variant**. A material that
inherits from itself, directly or through others, draws the engine default and
says which file closed the loop.

## What a part may change

A material decides what a single part may change about it without becoming a
different material: its **`instanceParameters`**. The set a material may declare
is `Color`, `Transparency`, `Emissive`, `Metalness`, `Roughness`, `NormalScale`
and `AlphaCutoff`. Maps are not on it: a different texture is a different
material.

```luau
--!strict
local crate = workspace.Crate :: Part
crate:SetMaterialParameter("Color", Color3.fromRGB(200, 60, 40))
print(crate:GetMaterialParameter("Roughness")) -- the material's value
crate:ClearMaterialParameter("Color")
```

- Setting a parameter the material does not declare **raises**.
- Reading one that is not overridden returns the material's own value.
- A part that changes material keeps its overrides. One the new material does
  not declare is kept and ignored, and the Properties panel shows it struck
  through, so switching back restores the look.

**The engine default declares `Color` and `Transparency`**, so a part wearing
nothing is tinted and faded this way:

```luau
part:SetMaterialParameter("Color", Color3.new(1, 0, 0))
part:SetMaterialParameter("Transparency", 0.5)
```

A new material made in the editor declares **nothing**, so it is exactly what
its author made until they tick a parameter in the Material panel.

`part.MaterialParameters` reads the overrides as a fresh table.

## Changing a material at runtime

`Material.load` returns the **shared** handle for an asset, and it is
**read-only**: writing a property raises, because one write would change every
part wearing that asset.

`material:Clone()` returns a **runtime copy**: the same properties, writable,
never saved, and released when nothing points at it. Every part wearing the copy
changes with it, and nothing else does.

```luau
--!strict
local glowing = Material.load("asset://materials/lamp.material.json"):Clone()
glowing.Emissive = Color3.new(1, 0.8, 0.4)
for _, lamp in workspace.Lamps:GetChildren() do
    (lamp :: Part).Material = glowing
end
```

**Nothing clones implicitly.** Reading `part.Material` returns the handle the
part wears; it never makes a copy. `material.Source` is the asset either kind of
handle came from.

## Meshes

A `MeshPart` wearing nothing draws the materials its own file described, and a
`Color` it overrides tints them. Importing a model writes one material asset per
material in the file, under `content/materials/<model>/`, and the parts the
import builds wear them. A material whose images are embedded in the file (a
`.glb`, a data URI) has no file of its own to name and stays in the mesh.

## Multiplayer

A part's material, its overrides and any runtime copy it wears replicate. A
replica needs the same material files the authority has, as it needs the same
meshes.

## Converting an older project

A scene saved before materials were assets still opens, with every part's
colour and transparency as overrides on the default material. To convert a
project's files and any `Material` instances in them:

```
luaug migrate materials [path]
```

It lists the script lines that write `.Color` or `.Transparency`, because only
you know which of those are parts: a part's become
`part:SetMaterialParameter("Color", c)`, while a light's, an emitter's and a
`Part2D`'s keep their own `Color`.
