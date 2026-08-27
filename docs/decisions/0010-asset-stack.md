# 0010 — Asset stack: fastgltf + meshoptimizer + basis/KTX2 + stb; assimp offline-only

- Status: accepted; superseded in part by [0065](0065-a-loose-gltf-is-not-a-runtime-format.md)
- Date: 2026-08-19

## Context
Open-world streaming lives or dies on the asset pipeline: fast loads, LOD
chains, transcodable textures, content addressing. Runtime binary size and
attack surface matter; assimp is powerful but large with a history of parser
CVEs.

## Decision
Runtime: **fastgltf** (SIMD glTF, ~5–7× faster than cgltf) + **meshoptimizer**
(LOD generation, meshlets, vertex/index compression — essential, not optional)
+ **basis_universal/KTX2** (one transcodable texture → BC/ETC/ASTC per
platform) + **stb** (images, truetype). Offline importer CLI only: **assimp**
for the long tail of exotic formats, normalizing everything to glTF 2.0 —
**never linked into the engine runtime**. All content is content-addressed
(BLAKE3) with deterministic pipeline outputs (same input → same hashes).

## Consequences
Fast dev-mode loads and a shippable pack format; meshlet data emitted at import
from day one keeps the future GPU-driven rendering path open. CVE surface of
assimp is confined to a local offline tool.

## Amendment, 2026-08-27 (ADR 0065)

"Fast dev-mode loads" was implemented as a SECOND feed -- the runtime parsing the
source `.gltf` when no pack answered -- and that feed is now deleted. The choice
of libraries here is unchanged, and so is assimp's confinement to an offline
tool. What changed is where the compiler runs: opening a project compiles
whatever has no compiled form, so a dev-mode load is the compiled path rather
than a parse. It is also the only way an FBX ever worked, which is what made the
second feed indefensible rather than merely redundant.
