# 0065 — A loose `.gltf` is not a runtime format; everything arrives compiled

- Status: accepted
- Date: 2026-08-27
- Supersedes, in part: [0010](0010-asset-stack.md) — the
  "fast dev-mode loads" it promises are now the compiled path, not a source parse.

## Context

The engine had **two feeds for the same asset**, and had had since M4. A mounted
pack answered with a compiled mesh; a content directory answered with the source
file, parsed on the way in. ADR 0010 kept the second deliberately as the dev-mode
path — the thing that made a project runnable the moment somebody dropped a file
into `content/`.

Every milestone since has paid four costs for that convenience, and none of them
were visible from the code paying them:

- **A parse on the frame thread.** Measured at 191 ms for the model E9 opened
  for, plus 21 ms to read it. `MeshLoader::sync` could afford exactly one mesh
  per call (D125) because of it, and a parse cannot be split, so a folder of five
  models dropped in was five frames of a fifth of a second each.
- **No LOD chain, no meshlets.** The simplifier and the meshlet builder run in
  `assetc`. A loose file reached the GPU as whatever the exporter wrote.
- **Textures as raw RGBA8, with no mips.** Four times the GPU memory of the BC7
  the compiler was *already producing and nobody was reading* — `syncTextures`
  went to the PNG beside the `.ktx2` every single time.
- **Formats that only the compiler can read did not work at all.** An FBX has no
  loose reader, so "drop a file in and it works" was true of glTF and false of
  everything else, which is the worst shape a convenience can have.

The fourth is the one that settles it. A dev-mode path that works for one format
is not a dev-mode path; it is a special case with good PR.

## Decision

**A loose vendor file is a source, not an asset.** The runtime reads compiled
blobs and nothing else.

1. **`MeshLoader`'s loose branch is deleted.** A `MeshPart` whose content has no
   compiled form draws nothing and says so by key
   (`render.err.mesh_not_compiled`), naming the URN and what to do about it.
2. **`WorldHost::syncSkeletons` reads the compiled mesh.** It used to build a
   path by hand and parse the source with `skeletonOnly` — the only one of the
   three loose feeds that ran on the sim thread. Resolving rather than
   path-joining also fixed something the old reader could not do at all: a URN
   with a fragment (`...gltf#Torso`) named a file that does not exist on disk, so
   a ragdoll built from one piece of a split model found no rig.
3. **`syncTextures` prefers the compiled form**, and needs no IO to do it — a
   mount's bytes are already resident. This is what finally makes "BC7 and mips
   reach editor content" true rather than merely produced.
4. **Opening a project compiles what has no compiled form, in every host mode.**
   `openProjectContent` mounts the source tree, compiles through the same
   `assetc::importOne` a command-line build uses, and mounts the store above it.
   The editor, `--headless`, a replay and a capture gate all take this one call.

### Why a source file may still be read for a texture

`syncTextures` keeps its source-file branch and `sync` does not, and the
asymmetry is deliberate: the pathology was never "a file was read", it was a
191 ms parse on the frame thread that reached out to read its own companion
files. A PNG is one read that the async IO pipeline already handles, and a map
written by a script or in a format the compiler declines should still draw.

### Why the whole-model row stays

E9's step 12 emits a blob per primitive under a URN fragment *beside* the whole
model. A note in the compiler said step 14 would remove the whole-model row.
It does not, and the reason is that removing it would break single-piece files
outright — every skinned file and every static single-primitive file produces one
piece, so the whole-model row is their only name. What made the row look
redundant was the import placing a `Model` of named parts; that is true of new
content and says nothing about the scenes that already name the model. The row
costs one blob per multi-primitive file and keeps every existing scene working.

## Consequences

**A project cloned from git still works with no command.** That was assumption 3
of the E9 plan and it is now load-bearing rather than a convenience: it is the
only thing standing between a fresh clone and a world of invisible parts. It is
cheap — `importOne` goes through the same content-addressed cache, so a re-open
compiles nothing and reads a manifest. Measured on `examples/02-meshes`: 0.08 s
cold for twelve blobs, 0.03 s warm and silent.

**A build with no compiler cannot compile.** `luaug_assetc_lib` is linked under
`LUAUG_DEBUG_UI`, which is what keeps the basis encoder and assimp out of a
shipped game (ADR 0010's own reasoning, unchanged). Such a build reads a pack,
which is what it is for. A player pointed at an uncompiled source tree now gets
a keyed warning instead of a slow, silent, mip-less success — which is the honest
answer, and the one that gets fixed.

**The mesh budget outlived the parse it was sized for**, and stays. One mesh per
call was sized against a 191 ms parse; a compiled mesh is `decodeMesh` over
meshopt streams. What remains is the upload — vertex and index buffers plus a
transcode per texture slot — and a folder of five models would still put all of
it in one frame. Raising it is a measurement, not a deletion, and there is no
measurement yet.

**One golden moved, and only one.** `tests/screenshots/lavapipe/specular.png`, by
a single pixel at a maximum channel delta of 4 — meshopt vertex quantisation.
The plan predicted the rendercapture streams would move too; they did not,
because the meshes in those scenes are 12 and 48 triangles and the simplifier
produces no chain for them. The determinism traces and the animation replay did
not move either: the compiled joints and clips hash identically to the parsed
ones.

## Verification

`engine/app/tests/content_import_tests.cpp`, which asserts the claim from the
side that can only be true if the loose reader is gone: **compile a project, then
delete every source file, and require that the mesh and the map still load.** A
test that only checked "the mesh loaded" would have passed on the old build too,
because the loose reader loaded meshes perfectly well — that was the problem
with it.

It also carries `import_matches_build`, the last of E9's declared verification:
the editor's import and a command-line build agree per URN, per hash, per kind
and per stored size.
