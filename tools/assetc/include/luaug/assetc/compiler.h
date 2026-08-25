// `assetc` -- the offline asset compiler (roadmap M7, M7 brief Decision 1).
//
// `luaug build-assets` is a Luau command that drives this binary. The split is
// where the two halves belong: the Luau side owns the project model -- where
// the content directory is, what is stale, what the console says -- and this
// side owns the codecs, because Lute cannot run a mesh simplifier and a texture
// encoder is a C++ library this repository already vendors.
//
// **Determinism is designed in rather than tested for afterwards**, and it is
// four rules:
//
//   1. Inputs are sorted by relative path before anything is processed. A
//      directory iterator's order is the filesystem's, and it differs between
//      machines.
//   2. Every encoder parameter is pinned in this tool rather than defaulted, so
//      an upstream default change is a diff here instead of a silent rebuild of
//      every asset.
//   3. No timestamps, no absolute paths and no machine names reach the output
//      bytes. A URN uses forward slashes on every host.
//   4. Encoding is single-threaded, because meshoptimizer's format-version
//      setters are process-global and documented as not thread-safe.
//
// The gate is a double build: compile the same tree twice and diff the manifest
// and the pack.
#pragma once

#include "luaug/asset/chunk.h"
#include "luaug/asset/mesh_format.h"
#include "luaug/asset/pack.h"
#include "luaug/core/content_hash.h"
#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace luaug::assetc {

using core::ContentHash;
using core::f32;
using core::u32;
using core::u64;
using core::usize;

enum class SourceKind
{
    Mesh,
    Texture,
    // A `*.chunk.json` cell of the streamed world. Compiled into a `.lchunk`
    // beside the pack rather than into it: a pack is read whole at mount, and a
    // world bigger than memory cannot have its instance lists resident
    // (`asset/chunk.h`).
    Chunk,
    // Copied through untouched: a font, a catalog, a shader blob. Copying
    // rather than refusing is what lets a project put anything it likes in its
    // content directory.
    Raw,
};

[[nodiscard]] const char* sourceKindName(SourceKind kind) noexcept;

struct SourceFile
{
    std::filesystem::path path;
    std::filesystem::path relative;
    SourceKind kind = SourceKind::Raw;
};

// One row of the content manifest: the name a script uses, and what it resolves
// to.
struct ManifestEntry
{
    std::string urn;
    ContentHash hash;
    asset::AssetKind kind = asset::AssetKind::Unknown;
    usize originalBytes = 0;
    usize storedBytes = 0;
    // Reported for a mesh, so `luaug build-assets` can say what it produced
    // rather than only that it finished.
    u32 lodCount = 0;
    u32 vertexCount = 0;
    u32 meshletCount = 0;
};

struct CompileOptions
{
    std::filesystem::path inputRoot;
    asset::MeshCompileOptions mesh;
};

// One compiled cell, written as its own file so the streaming manager can read
// it one at a time.
struct ChunkOutput
{
    // Relative to the chunk output directory, and the tail of the URN the index
    // names -- so the two cannot disagree about where a chunk is.
    std::string relativePath;
    std::vector<std::byte> bytes;
};

struct CompileResult
{
    bool ok = false;
    std::string diagnostic;

    std::vector<std::byte> pack;
    std::string manifest;
    std::vector<ManifestEntry> entries;

    // Empty for a project with no streamed world, which is every project before
    // this milestone.
    std::vector<ChunkOutput> chunks;
    std::string chunkIndex;

    u32 meshCount = 0;
    u32 textureCount = 0;
    u32 rawCount = 0;
    u32 chunkCount = 0;
};

// Every regular file under `root`, sorted by relative path. Empty with a
// diagnostic when the directory cannot be walked.
[[nodiscard]] std::vector<SourceFile> collectSources(const std::filesystem::path& root, std::string& diagnostic);

// The whole build, in memory. In memory because the two outputs -- a pack and a
// manifest -- have to agree with each other, and writing one before the other
// is known would leave a half-built content directory behind on failure.
[[nodiscard]] CompileResult compile(const CompileOptions& options);

// Encodes decoded pixels into the engine's texture container.
//
// `srgb` says what KIND of data the pixels are, and it is a property of the slot
// the texture fills rather than of the file: base colour and emissive are
// colour, normal and metallic-roughness are numbers. The same PNG can be both,
// in two materials, and the encoder cannot tell.
[[nodiscard]] std::optional<core::EngineError> encodeTexture(const asset::Image& image, bool srgb,
                                                             std::vector<std::byte>& out);

[[nodiscard]] std::string writeManifest(std::span<const ManifestEntry> entries);

[[nodiscard]] bool writeFile(const std::filesystem::path& path, std::span<const std::byte> bytes,
                             std::string& diagnostic);

} // namespace luaug::assetc
