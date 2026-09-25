# Materials as assets: the kickoff and the ledger

The owner, on 2026-09-24, over a design the agent proposed: *"gostei"*. The
decision is [ADR 0090](../decisions/0090-a-material-is-an-asset-a-part-wears-one-and-a-script-clones-one.md),
and it supersedes ADR 0060. **Read the ADR before this file**: this is the order
of work and where each piece stands, and the ADR is what each piece has to be.

In one paragraph: a material is a `.material.json` asset in `content/` and never
an instance. A variant names a parent and writes only what differs. A part
**wears** one through `BasePart.Material` and has no `Color` or `Transparency` of
its own -- it may override only the parameters its material declares, and the
engine default material declares `Color` and `Transparency`, so a grey-box part
still tints. At runtime, `Material.load` gives a read-only handle and
`material:Clone()` gives the copy a script changes.

Legend: `[ ]` not started · `[~]` in progress · `[x]` done.

Each stage ends with `scripts/localgate.ps1` green -- all six stages, the Linux
one included -- then a push, then CI read. Nothing lands on a red `main`.

## What must hold at every stage

- **The screenshot goldens do not move.** The default material with its default
  parameters is what a plain part drew before, and a migrated material is what
  the instance described. A golden that moves is a bug in the conversion, and it
  is fixed rather than re-recorded.
- **The determinism traces move, and every move is named.** Expected, with the
  reason, in the commit that carries the new bytes (ADR 0060's surviving rule,
  `tests/determinism/README.md`):
  - **All five** under `tests/determinism/` -- `churn`, `terrain`, `ragdoll`,
    `character` and `example01` -- hold at least one `BasePart` (a
    `CharacterBody` is one), and the hash walks every declared property by
    name (ADR 0060): `BasePart` loses `Color` and `Transparency`, gains
    `MaterialParameters`, and `Material` changes type from an instance
    reference to a material handle hashed as a URN. Every trace is
    cross-platform since ADR 0083, so each is re-recorded once and must then
    agree on Windows, in the Tier-2 container and on CI's macOS.
  - A scenario added before this lands that moves for any other reason is
    **not** expected. Stop and find out why before re-recording it.
- **R2, R3, R7, R10, R17** as always. Every new user-facing message is an i18n
  key (`script.err.material_read_only`, `script.err.material_parameter_undeclared`,
  and whatever else the stages need). No backend type in the public API.
- **Stage only what you wrote.** Other work happens in this working tree at the
  same time. The owner's uncommitted edits to `editor.h`, `terrain_overlay.*`,
  `debug_overlay.cpp`, `script_editor_panel.cpp`, `ui_theme.cpp` and
  `docs/briefs/orbit-shell.md` are theirs: never stage them, never revert them,
  and if a change of yours has to touch one of those files, stage your hunks
  alone (`git add -p`).
- **After changing a `Doc` or a member in the IDL, run the generators** and
  commit what they write. A `luau` gate stage that fails and then passes has
  regenerated something that has to be committed.

## Stage 1 — The format and the library

- [x] `engine/asset/include/luaug/asset/material.h` + `src/material.cpp`: the
      `MaterialAsset` struct (every field `MaterialComponent` has today, with the
      same defaults, plus `parent` and `instanceParameters`), a reader, and a
      writer through `core::JsonWriter` in fixed key order. A base writes every
      field and a variant writes only its overrides.
- [x] Variant resolution: parent-first into one flat description. A cycle
      resolves to the engine default and is reported once, naming the file that
      closed it.
- [x] The engine default material, built in (not a file): white, `Metalness` 0,
      `Roughness` 0.7, declaring `Color` and `Transparency`.
- [x] A `MaterialLibrary` the host owns, keyed by URN, that loads through
      `ContentMounts` (compiled first, loose second) and reloads a changed file
      (ADR 0062). `scene` is L3 and has no filesystem: it holds a handle, and the
      host answers what the handle means. `tools/repo/checklayers.luau` has to
      stay green. *Built and tested in `asset` (`75231147`); the host takes
      ownership in Stage 2, when a world has something to ask it.*
- [x] Tests: round trip is byte-identical, key order is fixed, a variant
      inherits and overrides, a cycle is refused, an unknown field is reported
      and not fatal, a missing parent is reported.

## Stage 2 — A part wears a material

- [x] IDL (`api/defs/instances.api.luau`): remove the `Material` class; remove
      `BasePart.Color` and `BasePart.Transparency`; `BasePart.Material` becomes a
      `Material?` handle; add `BasePart.MaterialParameters` and the three methods
      (`SetMaterialParameter`, `GetMaterialParameter`, `ClearMaterialParameter`).
      Regenerate everything the generators write (C++ descriptors, `.d.luau`,
      the API dump, `docs/api/*.md`, the wire schema).
- [x] `scene`: `MaterialComponent` and its pool go; `PartComponent` holds the
      material handle and the override set. Setting an undeclared parameter
      raises; an override the current material does not declare is kept and
      ignored.
- [x] `World::worldHash`: the material as its URN, `MaterialParameters` in name
      order. A clone is hashed by its creation order and what differs from its
      source. File contents are not hashed.
- [x] Scene format version up. The new writer writes `Material` as a URN and
      `MaterialParameters` as an object. The old-version reader turns non-default
      `Color` and `Transparency` into overrides on the default material, and
      counts a `Material` instance as an unknown class.
- [x] Stamps: `Material` and `MaterialParameters` are ordinary overrides under
      ADR 0051. Remove the material-specific paths added for D133 and D142 (the
      general instance-reference rule stays for what is still instance-valued).
- [x] Render (`engine/render/src/render_world.cpp`): `materialOf` reads the
      library instead of a component; `tintBy` becomes "apply the declared
      overrides". Keep the current slot deduplication keyed by
      (material, overrides) -- moving overrides into per-draw data is a follow-up
      that only a bench can justify. `mesh_loader.cpp`'s `syncTextures` walks the
      library's materials instead of the pool, with the same sRGB-per-map rule.
- [x] `MeshPart` with no material still draws its file's own.
- [x] The goldens pass unchanged. The five traces are re-recorded with the
      reason in the commit, and the Windows, Tier-2 and macOS runs agree on them.
      *`38468f7a`, CI green on all three.*

## Stage 3 — The script surface

- [x] `Material` as a Luau data type (like `CFrame`): `Material.load(content)`,
      the properties readable, `Source`, `material:Clone()`. Writing a property of
      a loaded asset raises; writing one of a clone works. A clone is released
      when nothing points at it. Nothing clones implicitly.
- [x] `part.Material = m` accepts a loaded asset, a clone or `nil`.
- [x] Conformance specs under `tests/conformance/` for every rule in ADR 0090's
      runtime section, including the two raises and "reading `part.Material` does
      not clone".
- [x] `luau-analyze` strict over the regenerated `engine.d.luau`: a script that
      writes `part.Color` is a type error, and says so.

## Stage 4 — Migration

- [x] `luaug migrate materials` (`tools/cli/commands/migrate.luau`): each
      `Material` instance in a project's scenes and stamps becomes an asset under
      `content/materials/`, each reference becomes its URN, and each non-white
      `Color` or non-zero `Transparency` becomes an override. When the part's new
      material would not declare that parameter, the tool declares it on the
      asset it wrote rather than dropping the tint. Idempotent, and it prints what
      it did.
- [x] Run it over every example, template and test project in the repository.
      Hand-edit the Luau that writes `part.Color`/`part.Transparency`
      (`examples/*`, `tests/determinism/*`, `tests/screenshots/*`,
      `tests/bench/*`, `tests/twoworlds/*`, `tests/hotreload`, and the rest `rg`
      finds). **Not every `.Color =` is a part's** -- `Part2D`, lights,
      particles and UI keep theirs.
- [x] Review the diff by eye. The goldens are the proof that nothing changed
      look.

## Stage 5 — The compiler and the import

- [x] `assetc` compiles `.material.json` to `AssetKind::Material` (parameter
      block plus texture hashes), so the inert kind finally has its writer.
- [x] The colour-or-data decision for a loose image reads material files
      (`kMaterialMaps` stays the rule) instead of `Material` instances in scenes.
      The scene-scanning code goes.
- [x] The glTF import writes one material asset per material in the file, beside
      the model's other outputs, and the parts it builds wear them. A re-import
      writes assets that are missing and leaves existing ones alone. If that turns
      out wrong in practice, record it as a finding rather than guessing.

## Stage 6 — The editor

- [x] Content browser: `.material.json` rows with a rendered sphere thumbnail
      (the thumbnail system exists); *New Material*; *New Variant* on a material's
      menu.
- [x] A material panel: opens the asset, edits its fields and its
      `instanceParameters` with the existing preview sphere (`syncMaterialPreview`
      stops needing an instance), undo inside the panel, and save writes the file.
      Every open world updates on save.
- [x] Properties on a part: `Material` as a content picker and a drop target
      (dropping a material row on a part assigns its URN -- `assignStampTo`'s
      material use goes); below it, the parameters the material declares, each
      with override and revert, and a kept-but-undeclared one struck through.
- [x] Dropping a material on a part inside a placed stamp is an override, and it
      survives save and reopen (the D142 scenario, as a test).
- [x] The stamp stage no longer needs a material under its `Workspace`, so the
      D115 path that synced the stage's materials becomes a library lookup.

## Stage 7 — The wire

- [x] The wire schema (`api/generator/gen_wire.luau`) carries a part's material
      URN and `MaterialParameters`. A clone is its own wire object -- source URN
      plus changed properties -- sent before any part that names it (ADR 0077's
      ordering). `ProtocolVersion` 11 → 12.
- [x] A two-world test: the authority clones a material, changes its `Color`,
      puts it on a part, and the replica draws that colour.

## Stage 8 — Documentation and close

- [x] `docs/manual/world/parts.md` (it still says `Material` is absent -- stale
      since E3), `docs/manual/world/meshes.md`, `docs/manual/migrating/divergences.md`
      (a row for `Color`/`Transparency`), a new manual page on materials and
      variants, and `CHANGELOG.md` under Unreleased with the breaking change
      marked as breaking.
- [x] `docs/architecture.md` where it names `MaterialDef` among asset kinds.
- [x] `PROGRESS.md` updated, this ledger ticked, and a **Findings** section
      appended here: what ADR 0090 assumed that reality corrected.
- [x] Tell the owner the work is done and that the release carrying it is a
      major version under semantic versioning. **Tagging it is theirs.**

## Not in this work

Terrain layers as material assets (the second stage the owner agreed to, with
its own ADR), decals and particles as materials, 2D and UI colours, a visibility
switch for parts, and a material that names its own shader -- ADR 0090,
*Not decided here*.

## Findings

What building ADR 0090 found that the ADR and this ledger did not say.

1. **Stages 2, 3 and the hand-edits of 4 land as one commit.** Removing
   `BasePart.Color` breaks every script that writes it, and a `main` with the
   property gone and the scripts not yet migrated is red; so the IDL, the
   engine, the script surface and every example's Luau moved together
   (`38468f7a`). The migration TOOL and the scene files followed separately
   (`10ab6f03`), because a version 1 scene still opens.
2. **The editor's model moved with stage 2, its UI did not.** The editor's
   tests of the old design (a `Material` instance placed by a drop, a preview
   sphere pointed at an instance) could not survive the class going, so
   `createMaterial`, `createMaterialVariant`, `assignMaterialTo` and the
   material session with its own undo landed with the engine; the panels that
   drive them are stage 6.
3. **An override REPLACES; the old tint MULTIPLIED.** On the engine default --
   white, emitting nothing -- the two are the same number, which is why no
   golden moved. On an authored material they are not, so the migration writes
   what the part DREW (material colour times tint, and
   `1 - (1 - m)(1 - t)` for see-through) and declares the parameter on the
   asset, and a tinted glow becomes an `Emissive` override too.
4. **A `MeshPart` wearing nothing keeps the multiply.** Its base is its file's
   own material, and replacing that colour would discard a textured file's
   look; the default material's `Color` on such a part tints, which is what
   `BasePart.Color` always did to an unimported mesh.
5. **A material's `Transparency` goes to the draw, not the block.** The old
   material path put it in the block's alpha and never in the blended pass, so
   a translucent material drew opaque; the draw's alpha is where see-through
   decides the pass, as `BasePart.Transparency` always did.
6. **Dedup keys on the look, not on which overrides are set.** A white
   `Color` override and no override draw the same, and splitting them into two
   bind sets would move slot numbers and with them draw order; the key is the
   material plus every value that reaches the block.
7. **A clone lives by holds and a sweep.** A script handle holds its clone; a
   part wearing one keeps it by being asked at the sweep (run where a drain
   retires instances), so no write path has to remember to count. Clone ids
   are creation order and a restore puts the counter back -- the VM that held
   the old handles is rebuilt after a restore anyway.
8. **`Material.load` raises for a missing asset** (`script.err.material_not_found`).
   The ADR did not say; a typo that silently drew the default would be found by
   looking at the screen.
9. **`MaterialParameters` written whole is checked for shape, not for
   declaration.** It is a property, and the scene reader and a stamp's
   overrides set it before a part's `Material` may have been read; the checked
   path is `SetMaterialParameter`.
10. **A part's fade is no longer tweenable.** `Transparency` was the property
    every fade tweened, and a tween moves one property of an INSTANCE; a
    material parameter is neither. Tweening a clone's property, or a tween goal
    that names a parameter, is a follow-up if a game needs it -- ADR 0090's
    "hiding a part" question, from the animation side.
11. **Chunk records carry the default material's two parameters and nothing
    else.** A streamed cell's part record had a colour and a transparency; a
    part wearing any other material, or overriding any other parameter, stays a
    whole instance rather than being flattened into the grid.
12. **The pack reader refused its own `Material` kind.** `knownKind` stopped at
    `Raw`, which `Material` comes after; the kind had been in the table since
    ADR 0060 with no writer, so nothing had ever tried to read one. Fixed with
    the writer.
13. **The glTF import can write a material only for maps that are files.** An
    image embedded in a `.glb`, or a data URI, has no `Content` a material can
    name; such a material stays in the mesh and the part keeps drawing the
    file's own, which looks as it did. `asset::Model` now records each
    material's index in the source file, because its own list is in first-use
    order.
14. **The migration keeps each file's layout.** `@std/json` would reorder keys
    and reprint numbers, drowning the change in a diff of the whole file;
    `tools/cli/jsondoc.luau` keeps both, and `MaterialParameters` takes the
    place `Color` had.
15. **A conformance run mounts `tests/conformance/content`** when it exists: a
    spec tree is not a project, and the material specs need files to load.
16. **The material panel shows edits live and reverts an unsaved one.** An
    edit is put in the host's library at once, so every world wearing the
    material draws it; closing without saving forgets it, and the file's look
    comes back. The panel's undo is its own -- an edit to a file is not a step
    in the scene's history.
17. **The stage's instance preview is gone, not moved.** The ball a material is
    judged on is the content browser's thumbnail renderer, drawing a sphere that
    wears the material through the same library, refreshed after every edit;
    there is no preview geometry in any world any more.
18. **The owner's work in progress is written in a newer idiom than HEAD.** The
    material UI landed in `debug_overlay.cpp` twice -- once in the working tree
    beside the owner's orbit-shell edits (`beginEditorDialog`, `iconMenuItem`),
    once in HEAD's own (`BeginPopupModal`, `MenuItem`) for the commit -- and
    only the HEAD copy was staged.
19. **A clone travels with each part that wears it, not as an object of its
    own.** The protocol replicates instances, and a second kind of wire object
    was machinery for one use; carrying the clone's changes beside every part
    wearing it gives the ordering ADR 0077 asks for by construction -- a copy
    is never named before it has arrived -- and the replica keys it by the
    authority's number, so parts sharing one still share one. The cost is the
    copy's 52 bytes once per wearing part rather than once. The values are zero
    when a part wears no copy, so putting one back on is a change the diff sees.
20. **A replica's copies live in the top half of the clone id range**, which a
    replica's own scripts count up from the bottom of and never reach
    (`World::adoptMaterialClone`).
21. **A clone's map crosses the wire as a name**, so a script writing one
    interns it; the atom table the session already replicates carries it.
22. **A per-part `Color` split instancing, one material per colour** (D184,
    found benchmarking a user's game after this ledger closed). Parts that
    differ only by `Color` were distinct materials, so a snake of 250
    differently tinted segments was 250 draws. Materials now carry a FAMILY
    (`RenderWorld::materialFamilies`) -- the same bind set but for the rgb of
    the base colour -- the opaque sort key groups by it, and the instancer
    batches by it with each instance's colour in the three floats `GpuInstance`
    already had spare (`alphaTint.yzw`, read `nointerpolation` in
    `pbr_instanced.hlsl` only). The stride, the pipelines and the RHI did not
    change, and a draw drawn alone still binds its own material. Draws fell
    756 → 4 on that game and 119 → 17 on `11-ocean`; the frame time did not
    (`docs/perf-baselines.md`, "A user's game"), which is itself a finding: a
    whole-world batch is drawn into every shadow cascade.
