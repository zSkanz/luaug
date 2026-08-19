# Third-Party Notices

GENERATED FILE — do not edit. Regenerate with:

```
lute tools/repo/vendor.luau notices
```

LuauG vendors third-party source under `third_party/`. Each dependency keeps
its own license; this file is the aggregated index, generated from
[`third_party/manifest.json`](third_party/manifest.json) (ADR 0021).

Rows without a pinned commit are declared for a later milestone and are not
vendored yet; the roadmap allows lazy vendoring so long as the row exists.

| Dependency | Version | Commit | License | Upstream | Vendored path |
|---|---|---|---|---|---|
| luau | 0.734 | `3fc82b1071ab` | MIT | https://github.com/luau-lang/luau | `third_party/luau/` |
| sdl3 | 3.4.14 | `147a8ee32dbf` | zlib | https://github.com/libsdl-org/SDL | `third_party/sdl3/` |
| sdl_shadercross | latest-tag | _not vendored yet_ | zlib | https://github.com/libsdl-org/SDL_shadercross | `third_party/sdl_shadercross/` |
| jolt | 5.6.0 | _not vendored yet_ | MIT | https://github.com/jrouwe/JoltPhysics | `third_party/jolt/` |
| box2d | 3.1.1 | _not vendored yet_ | MIT | https://github.com/erincatto/box2d | `third_party/box2d/` |
| miniaudio | 0.11.25 | _not vendored yet_ | MIT-0 OR Unlicense | https://github.com/mackron/miniaudio | `third_party/miniaudio/` |
| fastgltf | 0.9.x | _not vendored yet_ | MIT | https://github.com/spnda/fastgltf | `third_party/fastgltf/` |
| meshoptimizer | 1.x | _not vendored yet_ | MIT | https://github.com/zeux/meshoptimizer | `third_party/meshoptimizer/` |
| basis_universal | 2.5x | _not vendored yet_ | Apache-2.0 | https://github.com/BinomialLLC/basis_universal | `third_party/basis_universal/` |
| ktx | latest-tag | _not vendored yet_ | Apache-2.0 | https://github.com/KhronosGroup/KTX-Software | `third_party/ktx/` |
| stb | master-2026-08-01 | `2c980bb59875` | MIT OR Public Domain | https://github.com/nothings/stb | `third_party/stb/` |
| imgui | 1.92.x-docking | _not vendored yet_ | MIT | https://github.com/ocornut/imgui | `third_party/imgui/` |
| clay | pinned-commit | _not vendored yet_ | zlib | https://github.com/nicbarker/clay | `third_party/clay/` |
| recastnavigation | main | _not vendored yet_ | zlib | https://github.com/recastnavigation/recastnavigation | `third_party/recastnavigation/` |
| gamenetworkingsockets | 1.6.x | _not vendored yet_ | BSD-3-Clause | https://github.com/ValveSoftware/GameNetworkingSockets | `third_party/gamenetworkingsockets/` |
| enet | latest-tag | _not vendored yet_ | MIT | https://github.com/lsalzman/enet | `third_party/enet/` |
| assimp | 6.0.x | _not vendored yet_ | BSD-3-Clause | https://github.com/assimp/assimp | `third_party/assimp/` |
| doctest | 2.5.3 | `2d0a9359a60c` | MIT | https://github.com/doctest/doctest | `third_party/doctest/` |
| blake3 | latest-tag | _not vendored yet_ | CC0-1.0 OR Apache-2.0 | https://github.com/BLAKE3-team/BLAKE3 | `third_party/blake3/` |
| xxhash | latest-tag | _not vendored yet_ | BSD-2-Clause | https://github.com/Cyan4973/xxHash | `third_party/xxhash/` |

Policy: permissive licenses only (MIT / BSD / zlib / Apache-2.0 / public domain).
No GPL, no LGPL, and no commercial dependency in the default build path. Adding
or upgrading a dependency requires human approval (see `MASTER_PROMPT.md`,
escalation rules) and an ADR.

Tooling installed via rokit (not vendored): Lute 1.0.0 (MIT), luau-lsp 1.69.0
(MIT), StyLua 2.5.2 (MPL-2.0 — a build tool, never linked into the engine or
shipped games).
