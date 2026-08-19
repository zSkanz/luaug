# 0032 — Binary toolchain artifacts are fetched and hash-pinned, not vendored

- Status: accepted
- Date: 2026-08-19
- Amends: [ADR 0021](0021-vendoring-in-tree.md) (vendoring in-tree)
- Relates to: [ADR 0006](0006-hlsl-via-sdl-shadercross.md) (HLSL via SDL_shadercross)

## Context

ADR 0021 models every dependency the same way: an upstream **source tree** at a
commit SHA, copied into `third_party/`, never edited in place. That model has
served every dependency so far. SDL_shadercross breaks it.

Compiling HLSL (ADR 0006) requires DirectXShaderCompiler, and DXC is not
optional or partial: it is SDL_shadercross's only HLSL front-end, so without it
there is no SPIR-V, and because MSL is derived from SPIR-V, no Metal either
(`src/SDL_shadercross.c:629-642`; with the flag off, `:568-571` is a stub that
sets an error). DXC arrives two ways, and both violate something:

- **From source.** DXC is a fork of LLVM 3.7 — `project(LLVM)` at
  `dxc/CMakeLists.txt:26` — measuring 120 MB and 17,648 files, requiring
  Python 3 and TableGen. Vendoring it fits ADR 0021 exactly, and costs an LLVM
  build on every cold CI tier against a cold build already near nine minutes.
- **As Microsoft's prebuilt binaries**, which SDL_shadercross already pins by
  SHA256 (`build-scripts/download-prebuilt-DirectXShaderCompiler.cmake:1-4`).
  Small and fast, but they are binaries, and ADR 0021 has no row type for one.

Two facts made the second path's *committed* form unacceptable rather than
merely awkward.

**The archives contain an unassigned proprietary EULA.** The package's own
`ReleaseNotes.md:9-16` maps `d3d12shader.h` to MIT and "all other files" to
`LICENSE-LLVM.txt` (NCSA — permissive, attribution-only, redistribution
allowed). But `LICENSE-MS.txt` — "MICROSOFT SOFTWARE LICENSE TERMS" — also
ships, referenced by nothing. Its terms restrict redistribution in ways that
conflict with Apache-2.0, including a prohibition on distribution "so that any
part of it becomes subject to any license that requires … that others have the
right to modify it", which Apache-2.0 §2 grants. The evidence that NCSA governs
is strong: Microsoft's own NuGet package declares the same table with no EULA,
the upstream README names the University of Illinois licence, and the same
`LICENSE-MS.txt` ships byte-identically in the *Linux* tarball while saying
"solely for use on Windows". It is still a contradiction inside the artifact,
and resolving it is a legal reading, not an engineering one.

**The cost compounds.** The minimum useful set is 50.9 MiB (Windows x64 18.8,
Linux x64 32.1) — roughly 20–25 MiB in the pack — and opaque binaries do not
delta, so every future DXC bump adds another full copy to history forever.

## Decision

**A dependency may be a binary release artifact, fetched at configure time and
pinned by SHA256, rather than a source tree vendored in-tree.** It is a distinct
row kind in `third_party/manifest.json`, marked `"kind": "binary"`, carrying a
per-platform URL and hash instead of a commit.

Rules for such a row:

1. **The hash is the pin.** A download whose SHA256 does not match the manifest
   is a hard configure error, never a warning. There is no "update the hash to
   whatever we got" path in tooling — changing a hash is a human edit to the
   manifest, exactly as changing a commit SHA is.
2. **Fetched once per machine, into the build root.** The cache lives under
   `$env{LUAUG_BUILD_ROOT}`, so a second configure, a second preset and a second
   clone all reuse it, and a machine that has fetched once builds offline
   afterwards.
3. **Source stays the default.** This is for artifacts whose source form is
   disproportionate to their role — a build-time compiler, not a library the
   engine links. Anything that ends up inside the shipped binary is vendored as
   source.
4. **Notices still come from the manifest.** The row carries its licence, and
   `THIRD_PARTY_NOTICES.md` is generated from it as for any other row.

`spirv_cross`, which the engine's shader tooling links, is vendored as source
in the ordinary way.

## Consequences

The EULA question dissolves rather than being decided: LuauG never redistributes
the binaries. Each developer's machine downloads a public release from Microsoft
under whatever terms govern that download — which is the same act as installing
a toolchain, and is not what the disputed clauses restrict. The repository stays
free of a licence contradiction it would otherwise have to publish an opinion
about.

History stays clean: no 25 MiB of opaque blobs per DXC version, permanently.

**What is given up is the strongest form of ADR 0021's hermetic-build
property.** That ADR's rationale names network-restricted console CI. After this
amendment the guarantee is "hermetic after the first fetch on a machine" rather
than "hermetic from a bare clone". For console CI — post-v1, and gated behind
an NDA toolchain that will need its own arrangements regardless — the cache
directory can be seeded from a trusted mirror, which is the same thing every
console SDK already requires. The cost is real and is accepted here rather than
discovered later.

**A gap this does not close: there is no macOS DXC binary.** Microsoft publishes
only Windows and Linux x64 builds, which is why SDL_shadercross's own CI builds
DXC from source on its macOS runner alone. LuauG's macOS tier is compile-only
until post-v1, and `architecture.md` §8 already builds shadercross as a host
tool used when cross-compiling, so shaders for a macOS build are produced on a
Tier-1 or Tier-2 host. The consequence to state plainly: **a developer working
on macOS gets no local shader compilation and therefore no shader hot reload**,
which is a partial loss of ADR 0006's promise on that platform. Closing it means
vendoring DXC from source for macOS specifically, and that decision waits until
someone actually develops on a Mac.
