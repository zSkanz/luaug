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
- [~] A `MaterialLibrary` the host owns, keyed by URN, that loads through
      `ContentMounts` (compiled first, loose second) and reloads a changed file
      (ADR 0062). `scene` is L3 and has no filesystem: it holds a handle, and the
      host answers what the handle means. `tools/repo/checklayers.luau` has to
      stay green. *Built and tested in `asset` (`75231147`); the host takes
      ownership in Stage 2, when a world has something to ask it.*
- [x] Tests: round trip is byte-identical, key order is fixed, a variant
      inherits and overrides, a cycle is refused, an unknown field is reported
      and not fatal, a missing parent is reported.

## Stage 2 — A part wears a material

- [ ] IDL (`api/defs/instances.api.luau`): remove the `Material` class; remove
      `BasePart.Color` and `BasePart.Transparency`; `BasePart.Material` becomes a
      `Material?` handle; add `BasePart.MaterialParameters` and the three methods
      (`SetMaterialParameter`, `GetMaterialParameter`, `ClearMaterialParameter`).
      Regenerate everything the generators write (C++ descriptors, `.d.luau`,
      the API dump, `docs/api/*.md`, the wire schema).
- [ ] `scene`: `MaterialComponent` and its pool go; `PartComponent` holds the
      material handle and the override set. Setting an undeclared parameter
      raises; an override the current material does not declare is kept and
      ignored.
- [ ] `World::worldHash`: the material as its URN, `MaterialParameters` in name
      order. A clone is hashed by its creation order and what differs from its
      source. File contents are not hashed.
- [ ] Scene format version up. The new writer writes `Material` as a URN and
      `MaterialParameters` as an object. The old-version reader turns non-default
      `Color` and `Transparency` into overrides on the default material, and
      counts a `Material` instance as an unknown class.
- [ ] Stamps: `Material` and `MaterialParameters` are ordinary overrides under
      ADR 0051. Remove the material-specific paths added for D133 and D142 (the
      general instance-reference rule stays for what is still instance-valued).
- [ ] Render (`engine/render/src/render_world.cpp`): `materialOf` reads the
      library instead of a component; `tintBy` becomes "apply the declared
      overrides". Keep the current slot deduplication keyed by
      (material, overrides) -- moving overrides into per-draw data is a follow-up
      that only a bench can justify. `mesh_loader.cpp`'s `syncTextures` walks the
      library's materials instead of the pool, with the same sRGB-per-map rule.
- [ ] `MeshPart` with no material still draws its file's own.
- [ ] The goldens pass unchanged. The five traces are re-recorded with the
      reason in the commit, and the Windows, Tier-2 and macOS runs agree on them.

## Stage 3 — The script surface

- [ ] `Material` as a Luau data type (like `CFrame`): `Material.load(content)`,
      the properties readable, `Source`, `material:Clone()`. Writing a property of
      a loaded asset raises; writing one of a clone works. A clone is released
      when nothing points at it. Nothing clones implicitly.
- [ ] `part.Material = m` accepts a loaded asset, a clone or `nil`.
- [ ] Conformance specs under `tests/conformance/` for every rule in ADR 0090's
      runtime section, including the two raises and "reading `part.Material` does
      not clone".
- [ ] `luau-analyze` strict over the regenerated `engine.d.luau`: a script that
      writes `part.Color` is a type error, and says so.

## Stage 4 — Migration

- [ ] `luaug migrate materials` (`tools/cli/commands/migrate.luau`): each
      `Material` instance in a project's scenes and stamps becomes an asset under
      `content/materials/`, each reference becomes its URN, and each non-white
      `Color` or non-zero `Transparency` becomes an override. When the part's new
      material would not declare that parameter, the tool declares it on the
      asset it wrote rather than dropping the tint. Idempotent, and it prints what
      it did.
- [ ] Run it over every example, template and test project in the repository.
      Hand-edit the Luau that writes `part.Color`/`part.Transparency`
      (`examples/*`, `tests/determinism/*`, `tests/screenshots/*`,
      `tests/bench/*`, `tests/twoworlds/*`, `tests/hotreload`, and the rest `rg`
      finds). **Not every `.Color =` is a part's** -- `Part2D`, lights,
      particles and UI keep theirs.
- [ ] Review the diff by eye. The goldens are the proof that nothing changed
      look.

## Stage 5 — The compiler and the import

- [ ] `assetc` compiles `.material.json` to `AssetKind::Material` (parameter
      block plus texture hashes), so the inert kind finally has its writer.
- [ ] The colour-or-data decision for a loose image reads material files
      (`kMaterialMaps` stays the rule) instead of `Material` instances in scenes.
      The scene-scanning code goes.
- [ ] The glTF import writes one material asset per material in the file, beside
      the model's other outputs, and the parts it builds wear them. A re-import
      writes assets that are missing and leaves existing ones alone. If that turns
      out wrong in practice, record it as a finding rather than guessing.

## Stage 6 — The editor

- [ ] Content browser: `.material.json` rows with a rendered sphere thumbnail
      (the thumbnail system exists); *New Material*; *New Variant* on a material's
      menu.
- [ ] A material panel: opens the asset, edits its fields and its
      `instanceParameters` with the existing preview sphere (`syncMaterialPreview`
      stops needing an instance), undo inside the panel, and save writes the file.
      Every open world updates on save.
- [ ] Properties on a part: `Material` as a content picker and a drop target
      (dropping a material row on a part assigns its URN -- `assignStampTo`'s
      material use goes); below it, the parameters the material declares, each
      with override and revert, and a kept-but-undeclared one struck through.
- [ ] Dropping a material on a part inside a placed stamp is an override, and it
      survives save and reopen (the D142 scenario, as a test).
- [ ] The stamp stage no longer needs a material under its `Workspace`, so the
      D115 path that synced the stage's materials becomes a library lookup.

## Stage 7 — The wire

- [ ] The wire schema (`api/generator/gen_wire.luau`) carries a part's material
      URN and `MaterialParameters`. A clone is its own wire object -- source URN
      plus changed properties -- sent before any part that names it (ADR 0077's
      ordering). `ProtocolVersion` 11 → 12.
- [ ] A two-world test: the authority clones a material, changes its `Color`,
      puts it on a part, and the replica draws that colour.

## Stage 8 — Documentation and close

- [ ] `docs/manual/world/parts.md` (it still says `Material` is absent -- stale
      since E3), `docs/manual/world/meshes.md`, `docs/manual/migrating/divergences.md`
      (a row for `Color`/`Transparency`), a new manual page on materials and
      variants, and `CHANGELOG.md` under Unreleased with the breaking change
      marked as breaking.
- [ ] `docs/architecture.md` where it names `MaterialDef` among asset kinds.
- [ ] `PROGRESS.md` updated, this ledger ticked, and a **Findings** section
      appended here: what ADR 0090 assumed that reality corrected.
- [ ] Tell the owner the work is done and that the release carrying it is a
      major version under semantic versioning. **Tagging it is theirs.**

## Not in this work

Terrain layers as material assets (the second stage the owner agreed to, with
its own ADR), decals and particles as materials, 2D and UI colours, a visibility
switch for parts, and a material that names its own shader -- ADR 0090,
*Not decided here*.

## Findings

*(Appended as the stages land.)*
