# engine/ — Native C++ Core

One directory per module, each with `include/luaug/<module>/`, `src/`,
`tests/`, and a `CMakeLists.txt` declaring its layer via `luaug_add_module()`.
The authoritative module map, layering rules (L0–L6), and per-module contracts
live in [`../docs/architecture.md`](../docs/architecture.md) §2 — read that
before creating any module.

Layer summary: `core` → `jobs`, `platform` → `rhi`, `physics`, `net`, `audio`,
`asset` → `scene` → `render`, `input`, `nav` → `ui`, `script` → `app`.
Backend implementations link only into `app` (ADR 0023); `script` is the only
module that includes Luau headers; `scene` never includes render/ui/input/
script. `tools/repo/checklayers.luau` enforces the matrix in CI.

Populated starting at milestone M0 (`core` first) per `docs/roadmap.md`.
