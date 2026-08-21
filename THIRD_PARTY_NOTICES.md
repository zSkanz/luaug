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
| sdl_shadercross | main-2026-06-26 | `e55cf5e31ced` | zlib | https://github.com/libsdl-org/SDL_shadercross | `third_party/sdl_shadercross/` |
| spirv_cross | sdl-shadercross-pin | `1a6169566c73` | Apache-2.0 | https://github.com/KhronosGroup/SPIRV-Cross | `third_party/spirv_cross/` |
| jolt | 5.6.0 | `e77f175595e6` | MIT | https://github.com/jrouwe/JoltPhysics | `third_party/jolt/` |
| box2d | 3.1.1 | _not vendored yet_ | MIT | https://github.com/erincatto/box2d | `third_party/box2d/` |
| miniaudio | 0.11.25 | `9634bedb5b5a` | MIT-0 OR Unlicense | https://github.com/mackron/miniaudio | `third_party/miniaudio/` |
| fastgltf | 0.9.0 | `0d1b67a28c49` | MIT | https://github.com/spnda/fastgltf | `third_party/fastgltf/` |
| simdjson | 3.12.3 | `7382dc2be88e` | Apache-2.0 OR MIT | https://github.com/simdjson/simdjson | `third_party/simdjson/` |
| meshoptimizer | 1.2 | `9d9890c73011` | MIT | https://github.com/zeux/meshoptimizer | `third_party/meshoptimizer/` |
| basis_universal | 2.5x | _not vendored yet_ | Apache-2.0 | https://github.com/BinomialLLC/basis_universal | `third_party/basis_universal/` |
| ktx | latest-tag | _not vendored yet_ | Apache-2.0 | https://github.com/KhronosGroup/KTX-Software | `third_party/ktx/` |
| stb | master-2026-08-01 | `2c980bb59875` | MIT OR Public Domain | https://github.com/nothings/stb | `third_party/stb/` |
| imgui | 1.92.9b-docking | `b48d1afbe8ee` | MIT | https://github.com/ocornut/imgui | `third_party/imgui/` |
| recastnavigation | main | _not vendored yet_ | zlib | https://github.com/recastnavigation/recastnavigation | `third_party/recastnavigation/` |
| gamenetworkingsockets | 1.6.x | _not vendored yet_ | BSD-3-Clause | https://github.com/ValveSoftware/GameNetworkingSockets | `third_party/gamenetworkingsockets/` |
| enet | latest-tag | _not vendored yet_ | MIT | https://github.com/lsalzman/enet | `third_party/enet/` |
| assimp | 6.0.x | _not vendored yet_ | BSD-3-Clause | https://github.com/assimp/assimp | `third_party/assimp/` |
| doctest | 2.5.3 | `2d0a9359a60c` | MIT | https://github.com/doctest/doctest | `third_party/doctest/` |
| blake3 | latest-tag | _not vendored yet_ | CC0-1.0 OR Apache-2.0 | https://github.com/BLAKE3-team/BLAKE3 | `third_party/blake3/` |
| xxhash | 0.8.3 | `e626a72bc232` | BSD-2-Clause | https://github.com/Cyan4973/xxHash | `third_party/xxhash/` |

## Fetched binary artifacts

Build-time tools that are **not** vendored and never committed: they are
downloaded at configure time into a cache under `$LUAUG_BUILD_ROOT` and pinned
by SHA256 (ADR 0032). They do not ship inside the engine or a packaged game.

| Artifact | Version | License | Platform | SHA256 | Source |
|---|---|---|---|---|---|
| dxc | v1.9.2602 | NCSA AND MIT | windows-x64 | `a1e89031421cf3c1…` | https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602/dxc_2026_02_20.zip |
| dxc | v1.9.2602 | NCSA AND MIT | linux-x64 | `a1d3e3b5e1c5685b…` | https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602/linux_dxc_2026_02_20.x86_64.tar.gz |

Policy: permissive licenses only (MIT / BSD / zlib / Apache-2.0 / public domain).
No GPL, no LGPL, and no commercial dependency in the default build path. Adding
or upgrading a dependency requires human approval (see `MASTER_PROMPT.md`,
escalation rules) and an ADR.

Tooling installed via rokit (not vendored): Lute 1.0.0 (MIT), luau-lsp 1.69.0
(MIT), StyLua 2.5.2 (MPL-2.0 — a build tool, never linked into the engine or
shipped games).
