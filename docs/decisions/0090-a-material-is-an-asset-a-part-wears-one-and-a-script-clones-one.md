# 0090 — A material is an asset, a part wears one, and a script clones one

- Status: accepted
- Date: 2026-09-24
- Supersedes: [0060](0060-a-material-is-an-instance-and-a-stamp-is-how-one-is-shared.md)
  (a material is an instance, and a stamp is how one is shared). Its last
  section -- *a plan states which traces it expects to move; it may not promise
  one will not* -- is not superseded and still holds.
- Amends: [0051](0051-a-prefab-is-inherited-and-an-edit-is-an-override.md)
  (a surface is no longer something a stamp carries as an instance) and
  [0069](0069-replication-reads-state-and-diffs-it.md) (the wire schema gains a
  part's material and its parameters)
- Decided by: the owner, 2026-09-24, in conversation, over a design the agent
  proposed and the owner accepted as written (*"gostei"*).

## Context

This is the third design of the material, and the record has to say why a third
one is not churn.

The first (E9, step A4) was a `.material.json` asset. It lived six hours: ADR
0060 replaced it with a `Material` **instance**, shared by being a stamp, on the
argument that stamps already answered every question a shared material asks --
one file, many instances, edit the file and every instance follows, override one
and only that one differs. That argument was true of *sharing*. What it did not
price was **where the instance lives**, and the defects that followed are all
that one cost, paid four times:

- **D133** -- a material placed beside a stamp being edited could not be carried
  by the stamp file, was written as `null`, and `restamp` pushed the null into
  every instance in the world.
- **D142** -- a material reference set inside a *placed* stamp was dropped on
  save, because an instance-valued property is never an override (ADR 0051).
- **D115** -- a material that existed only on the stamp stage had no texture
  loaded, because the frame loop synced one world and the stage is another.
- **D130** -- the property grid's instance-reference editor had been inert since
  M4, and the material was its first real user.

Each is fixed. None was a bug in the code that had it: they are the one fact that
**an instance reference means something only inside one world**, and this editor
has at least two (the scene and a stamp's stage), and every stamp is a tree of its
own. A surface is not a thing in the world -- it has no transform, it is not
simulated, it has no parent that means anything -- and putting it in the tree made
every boundary of the tree a place it could be lost.

The owner, on 2026-09-24, asked for the engine to work the way the two most
widely used commercial engines do: *"o material em si deixa de ser uma stamp ele
é um objeto que fica armazenado no content e somente no content"*, *"em uma part
eu não devo conseguir em si mexer na color dela más eu posso atribuir um material
a ela"*, *"o material eu posso instanciar ele ou seja criar uma copia dele durante
a execução e só alterar"*, and *"um material não é uma Instance em si ou seja não
pode ser colocado em workspace nem nada"*.

## Decision

### A material is an asset, and only an asset

A material is a file, `<name>.material.json`, under a project's `content/`.
`content/materials/` is the convention and not a rule, exactly as
`content/stamps/` is (ADR 0049): the kind is in the compound suffix. It is named
by a `Content` URN like every other asset (`asset://materials/brick.material.json`).

**`Material` stops being a class.** It leaves the IDL, `Instance.new("Material")`
raises as for any unknown class, and nothing called `Material` can be parented,
selected in the Explorer, or found in a `Workspace`. `MaterialComponent` leaves
`scene`.

The file is written through `core::JsonWriter` in fixed key order, so its text is
a pure function of what it holds:

```json
{
  "format": "luaug-material",
  "version": 1,
  "parent": "",
  "instanceParameters": ["Color"],
  "properties": { "Color": [1, 1, 1], "Roughness": 0.7, "ColorMap": "asset://textures/brick.png" }
}
```

`properties` holds the same fields the `Material` class held -- `Color`,
`Transparency`, `ColorMap`, `NormalMap`, `MetallicRoughnessMap`, `Emissive`,
`EmissiveMap`, `Metalness`, `Roughness`, `NormalScale`, `AlphaMode`,
`AlphaCutoff`, `DoubleSided` -- with the same meanings and defaults. A base
material writes every field; a variant writes only what it overrides.

### A variant is a material with a parent

`parent` names another material asset. A variant inherits every property it does
not write and every `instanceParameters` entry its parent declares, and may
declare more. Editing the parent changes every variant that has not overridden
the property -- which is the half of stamps the owner valued, in the place it
belongs.

The chain is resolved parent-first into one flat description. **A cycle is
refused**: the material resolves to the engine default, and the report says which
file closed the loop, once rather than every frame.

### A part wears a material, and has no colour of its own

`BasePart.Material` is a `Material?`: a handle to a loaded material asset, or to a
clone of one (below), or nothing. In a scene file it is written as the asset's
URN. **Nothing is the engine's default material**, which is built into the engine
rather than shipped as a file, and looks exactly as a plain part looks today:
white, dielectric, roughness 0.7.

**`BasePart.Color` and `BasePart.Transparency` are removed.** A part's look is
its material's. What replaces them is below, and it is narrower on purpose.

A `MeshPart` with no material draws what its own file described, which is what an
unimported mesh looks like -- unchanged from ADR 0060.

There is still no texture property on a part, and there will not be one.

### A material decides what one part may change about it

A material's `instanceParameters` lists which of its properties a part may
override **without becoming a different material**. The set a material may
declare from is closed: `Color`, `Transparency`, `Emissive`, `Metalness`,
`Roughness`, `NormalScale`, `AlphaCutoff`. Maps are not on it: a different
texture is a different material, and that is what lets the renderer batch.

- A part holds its overrides in `MaterialParameters`, written to a scene as an
  object keyed by name, in name order.
- `part:SetMaterialParameter(name, value)`, `part:GetMaterialParameter(name)` and
  `part:ClearMaterialParameter(name)` are the script surface. Setting one the
  current material does not declare **raises**. Reading one that is not
  overridden returns the material's value.
- A part that changes material keeps its overrides. One the new material does not
  declare is **kept and ignored**, and the Properties panel shows it struck
  through with a revert, so switching back to the first material restores the
  look rather than losing it.
- **The engine default material declares `Color` and `Transparency`.** A
  grey-box part can still be tinted and faded without anybody authoring a
  material, and every scene written before this decision opens looking the same.
- **A material the editor creates declares nothing**, so an authored material is
  exactly what its author wrote unless they opt a parameter in.

The first implementation may keep today's renderer deduplication -- one material
slot per distinct (material, overrides) -- which costs what (material, colour)
costs now. Moving overrides into per-draw data so that a thousand tints share one
slot is a follow-up, done when `tests/bench/instances500` or a new bench says it
matters, and not promised here.

### At runtime, an asset is read-only and a clone is the copy you change

- `Material.load(content)` returns the shared handle for an asset. It is
  **read-only**: writing a property raises. A script that could edit the asset
  would change every part wearing it, in every place that asset is used, for the
  rest of the session -- which is the edit an author makes in the editor, not a
  thing a running game does by accident.
- `material:Clone()` returns a **runtime material**: the same properties, owned
  by nobody, writable, never saved, released when nothing points at it. A clone
  of a clone is a clone. `material.Source` is the asset URN either came from.
- **A clone is never made implicitly.** Reading `part.Material` returns the
  handle the part wears; it does not copy it. The pattern where reading a
  renderer's material silently instantiates one is one of the best-known
  surprises in the engines this design follows, and it is not copied.

`Material` is a Luau **data type**, like `CFrame` or `Content`, and not a class:
it is PascalCase off the object (`material.Color`, `material:Clone()`) and
camelCase off the namespace (`Material.load`), per ADR 0034.

### Stamps keep everything else

A stamp can hold parts that wear materials, and because a material is now a URN
and not an instance, **there is no reference for a stamp boundary to lose**.
`Material` and `MaterialParameters` inside a placed stamp are ordinary overrides
under ADR 0051. The material-specific guards added for D133 and D142 become
unnecessary and are removed with the class they guarded; the general
instance-reference rule stays, for the properties that are still instance-valued.

### What a file says is input; what a script changed is state

`World::worldHash` hashes a part's material as **the asset URN** and its
`MaterialParameters` as ordinary property data, in name order. The contents of a
material file are **not** hashed, for the reason a script's source is not: it is
an input the run was given, not state the run produced. A clone is hashed by the
order it was created in and the properties that differ from its source, because
those were produced by the run and a script can read them back and branch on
them.

This corrects what the agent told the owner while proposing the design (that
parameters would stay out of the hash). A value a script can read is a value a
script can make a decision on, and R10 is about exactly that.

### The wire carries the surface, which it never did

`replication/src/extract.cpp` sends a part's `Color` and `Transparency` today and
**never sent its material at all**, so a replica draws every part bare. The wire
schema gains a part's material URN and its `MaterialParameters`. A clone is its
own wire object -- its source URN and the properties it changed -- created on the
replica before any part that names it, the ordering ADR 0077 already uses for
messages behind the spawns they name. That is a protocol bump.

### The pack gets its material kind, and the compiler reads materials directly

`AssetKind::Material = 6` has been in `pack.h` with no writer since ADR 0060
reversed the design that needed it. It gets its writer: `assetc` compiles a
material asset to its parameter block and the hashes of its textures, as the
comment beside the enum value always said it would.

Whether a loose image is colour or data is read from the material files that name
it (`ColorMap` and `EmissiveMap` are colour, `NormalMap` and
`MetallicRoughnessMap` are not; colour wins a tie), which replaces reading
`Material` instances out of scenes and stamps. The glTF import writes one material
asset per material in the file, and the parts it creates wear them.

### Old scenes open, and one command converts them

The scene format's version rises. A reader of the old version converts a part's
`Color` and `Transparency` into overrides on the default material when they are
not the default, which is lossless for every part that wore no material. A
`Material` instance in an old file is an unknown class to the new reader and is
counted in the load report like any other, so the part draws the default material
and the report says why. `luaug migrate materials` converts a project: each
`Material` instance becomes an asset, each reference becomes its URN, and each
tint becomes an override the new material declares. It is run over every
example and test project in this repository, and the diff is reviewed rather than
trusted.

## Consequences

- **The screenshot goldens must not move.** The default material times its
  default parameters is what a plain part drew before, and a migrated material is
  what the instance described. A golden that moves is a conversion bug, not a
  re-record.
- **Determinism traces will move**, because `Color` and `Transparency` stop being
  `BasePart` properties and `Material` and `MaterialParameters` change type. Per
  ADR 0060's surviving rule, the plan names every trace it expects to move and
  why, and does not promise any will hold still.
- **This is a breaking change to the public API**, and `CHANGELOG.md` declares
  semantic versioning: `part.Color = ...` stops working in every script that
  writes it. The release that ships this is a major version, which is the
  owner's decision to tag.
- **`api-design.md` describes an API this engine no longer is at one point**:
  `BasePart.Color` is among the most familiar members the design set out to keep.
  The migration guide's divergences page says so and says why.
- **The editor gains a material editor**: a content-browser row per material with
  a rendered thumbnail, *New Material* and *New Variant*, a panel that edits the
  file with its own preview, and a Properties section on a part that shows its
  material and the parameters that material lets it override. An edited material
  reloads itself in every open world, as ADR 0062 says a changed asset does.
- **A replica now draws surfaces.** That was missing and nobody had reported it,
  because the multiplayer example draws plain parts.

### Not decided here

- **Terrain layers as material assets.** The voxel palette (ADR 0082) stays
  numbered for now. Making each layer name a material asset is the second stage
  the owner agreed to, and it gets its own record.
- **Decals and particles as materials.** Both have their own image and tint
  today (ADR 0072). The format leaves room for them; nothing here moves them.
- **2D and UI** keep their own `Color`s. `Part2D.Color`, a light's `Color` and a
  frame's `BackgroundColor` are not surfaces in this sense and are untouched.
- **Hiding a part.** With `Transparency` a material parameter, a part whose
  material does not declare it cannot be faded by a script without a clone. A
  visibility switch that is not a surface property is a separate question, and
  asked only if a real game needs it.
- **A material that names its own shader** (the roadmap's carried item) still has
  no field. The format's `version` is where one would be added.

### Rejected

- **Keeping `BasePart.Color` as a multiplier over any material.** It is what
  ADR 0060 did, and it is cheap. It was rejected because the owner wants the
  material to govern a surface, and a tint every part may apply to every material
  means no material is ever quite what its author made. `instanceParameters`
  keeps the tint where an author allowed it.
- **Letting a running script edit a material asset.** One write would change
  every part that wears it, in every scene that uses it, until the process ends.
- **Cloning on read.** See above.
- **Keeping `Material` as an instance and making its references URNs.** It would
  close D133 and D142 and still leave a thing in the tree that has no business
  there, a class nobody may parent anywhere useful, and an Explorer row per
  surface.
