# 0010 — Asset stack: fastgltf + meshoptimizer + basis/KTX2 + stb; assimp offline-only

- Status: accepted
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
