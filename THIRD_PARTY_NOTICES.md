# Third-Party Notices

LuauG vendors third-party source under `third_party/`. Each dependency keeps its
own license; this file is the aggregated index. It is regenerated from
[`third_party/manifest.json`](third_party/manifest.json) — edit the manifest, not
this file, and keep the two in sync (CI checks it once M0 activates the builds).

Rows marked *target* are planned pins; exact versions/commits are captured at
vendor time (milestone M0) and this table is updated in the same commit.

| Dependency | Version (target) | License | Upstream | Vendored path |
|---|---|---|---|---|
| Luau | 0.734 | MIT | https://github.com/luau-lang/luau | `third_party/luau/` |
| SDL | 3.4.x | zlib | https://github.com/libsdl-org/SDL | `third_party/sdl3/` |
| SDL_shadercross | latest tagged | zlib | https://github.com/libsdl-org/SDL_shadercross | `third_party/sdl_shadercross/` |
| Jolt Physics | 5.6.0 | MIT | https://github.com/jrouwe/JoltPhysics | `third_party/jolt/` |
| Box2D | 3.1.1 | MIT | https://github.com/erincatto/box2d | `third_party/box2d/` |
| miniaudio | 0.11.25 | MIT-0 / Unlicense | https://github.com/mackron/miniaudio | `third_party/miniaudio/` |
| fastgltf | 0.9.x | MIT | https://github.com/spnda/fastgltf | `third_party/fastgltf/` |
| meshoptimizer | 1.x | MIT | https://github.com/zeux/meshoptimizer | `third_party/meshoptimizer/` |
| basis_universal | 2.5x | Apache-2.0 | https://github.com/BinomialLLC/basis_universal | `third_party/basis_universal/` |
| KTX-Software (libktx) | latest tagged | Apache-2.0 | https://github.com/KhronosGroup/KTX-Software | `third_party/ktx/` |
| stb | pinned commit | MIT / Public Domain | https://github.com/nothings/stb | `third_party/stb/` |
| Dear ImGui | 1.92.x (docking tag) | MIT | https://github.com/ocornut/imgui | `third_party/imgui/` |
| Clay | pinned commit | zlib | https://github.com/nicbarker/clay | `third_party/clay/` |
| Recast & Detour | main (org fork) | zlib | https://github.com/recastnavigation/recastnavigation | `third_party/recastnavigation/` |
| GameNetworkingSockets | 1.6.x | BSD-3-Clause | https://github.com/ValveSoftware/GameNetworkingSockets | `third_party/gamenetworkingsockets/` |
| ENet | latest tagged | MIT | https://github.com/lsalzman/enet | `third_party/enet/` |
| assimp (offline importer tool only) | 6.0.x | BSD-3-Clause | https://github.com/assimp/assimp | `third_party/assimp/` |
| doctest | latest tagged | MIT | https://github.com/doctest/doctest | `third_party/doctest/` |
| BLAKE3 | latest tagged | CC0 / Apache-2.0 | https://github.com/BLAKE3-team/BLAKE3 | `third_party/blake3/` |
| xxHash | latest tagged | BSD-2-Clause | https://github.com/Cyan4973/xxHash | `third_party/xxhash/` |

Policy: permissive licenses only (MIT / BSD / zlib / Apache-2.0 / public domain).
No GPL, no LGPL, and no commercial dependency in the default build path. Adding
or upgrading a dependency requires human approval (see `MASTER_PROMPT.md`,
escalation rules) and an ADR.

Tooling installed via rokit (not vendored): Lute 1.0.0 (MIT), luau-lsp 1.69.0
(MIT), StyLua 2.5.2 (MPL-2.0 — a build tool, never linked into the engine or
shipped games).
