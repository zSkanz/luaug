# 0036 — simdjson is vendored for fastgltf, and fastgltf's downloader is made unreachable

- Status: accepted
- Date: 2026-08-20

## Context
ADR 0010 chose fastgltf for runtime glTF import on its merits, and no document
in this repository records that fastgltf has a dependency of its own. It has
one, unconditionally: `third_party/fastgltf/CMakeLists.txt:77-82` links
`simdjson::simdjson` when that target exists and otherwise compiles
`deps/simdjson` into the library. None of the fifteen `option()` lines at the
top of that file can turn it off.

When the target does not already exist and `find_package(simdjson CONFIG)`
fails — which is the state of a hermetic build like this one —
`cmake/dependencies.cmake:17-20` calls `file(DOWNLOAD)` twice against
`raw.githubusercontent.com` for simdjson 3.12.3's amalgamated header/source
pair and writes them into `${CMAKE_CURRENT_SOURCE_DIR}/deps/simdjson`, i.e.
inside the vendored tree. There is no hash check; a failed download is noticed
only by the file's absence or by a version string parsed back out of the header
it has just written.

That default violates four things already decided here: **R5** (a dependency
with no manifest row, at a version nobody approved), **R13** (a write inside a
vendored tree), **R14** (a write into the source tree), and **ADR 0032's rule**
that an artifact fetched at configure time is pinned by SHA256 and cached under
`$LUAUG_BUILD_ROOT`. It also means no offline configure and a network round trip
on every clean CI configure — on the library whose selling point is load speed.

The alternative considered was switching to **cgltf** (MIT, single header, no
dependencies), which would delete the problem outright. It was rejected because
ADR 0010 compared the two deliberately and chose fastgltf for a 5–7× import
speed that the roadmap keeps on the permanent dev-mode path, and because a
transitive dependency that is permissively licensed and vendorable is a smaller
price than reversing a considered decision.

## Decision
**simdjson joins the manifest as a vendored dependency**, pinned at **v3.12.3**
— the exact version fastgltf's own CMake targets, on the same reasoning the
`spirv_cross` row already uses: pin at what upstream tests against. It is
available under **Apache-2.0 OR MIT**, both on R6's list.

The tree is vendored whole, with no exclusions: at 14 MB it does not justify
arguing with ADR 0021, and `.gitignore`'s own comment says our ignore rules have
no business deciding which of an upstream tree's tracked files survive. Only
`singleheader/simdjson.{h,cpp}` is compiled — that pair is committed upstream
and is precisely the artifact fastgltf would otherwise have downloaded.

`third_party/CMakeLists.txt` defines the `simdjson::simdjson` target **before**
`fastgltf` is added, so fastgltf's first branch is taken and the downloader is
never reached.

And, because "unreachable today" is not the same as "unreachable", a patch under
`third_party/patches/fastgltf/` replaces the download branch with a
`FATAL_ERROR`. A future fastgltf that changes how it looks for simdjson must
fail loudly at configure time rather than quietly resume fetching from the
network into our tree.

## Consequences
One more vendored dependency, and a hermetic, offline, pin-checked configure
in exchange. The patch is the part that keeps paying: it converts an upstream
default that would silently re-break four rules into a build error naming the
reason.

Two obligations follow. Upgrading fastgltf means re-reading its
`cmake/dependencies.cmake` and re-pinning simdjson to whatever the new version
targets — the patch will fail to apply if the file moved, which is the intended
alarm. And the vendored `deps/` directory must never appear: if
`third_party/fastgltf/deps/simdjson/` exists after a configure, the guard has
been defeated and this ADR is no longer true.
