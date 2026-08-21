// The exotic-format importer: FBX, OBJ, COLLADA, PLY and STL, through assimp.
//
// **Offline only, and structurally so** (ADR 0010). This header lives under
// `tools/`, assimp is linked into `assetc` and into nothing else, and the
// layering gate reads real `#include` edges -- so "the runtime never links an
// importer" is a fact about the build graph rather than a promise in a comment.
// The engine reads `.lmesh`, which is a format assimp has never heard of.
//
// **glTF stays on fastgltf even though assimp can read it.** The runtime
// pipeline's importer and the exotic-format importer are different jobs: one is
// the path every shipped asset takes and was proved at M4, the other is a
// convenience for getting somebody's existing model into the engine once. Two
// importers for one format is two behaviours for one format, and the day they
// disagree is the day nobody can say which is right.
//
// Everything it produces is an `asset::Model` -- the same struct `importGltf`
// fills -- so an FBX and a glTF are indistinguishable by the time the mesh
// compiler sees them. That is the point: the pipeline downstream has one shape
// to handle.
#pragma once

#include "luaug/asset/model.h"
#include "luaug/core/error.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace luaug::assetc {

// Whether this extension is one the exotic importer handles.
//
// A closed list rather than "whatever assimp compiled with", because the build
// chooses which importers exist (third_party/CMakeLists.txt) and a file that
// classifies as a mesh and then fails to import is a worse error than one that
// rides through as an opaque blob.
[[nodiscard]] bool isExoticMesh(std::string_view extension) noexcept;

// Imports one file. `directory` is where external references -- an OBJ's MTL, a
// COLLADA file's images -- are resolved from.
//
// Present even in a build without assimp (`LUAUG_ASSETC_ASSIMP=OFF`), where it
// returns a keyed error saying so. A missing feature that says its own name is
// better than a link failure or a file silently treated as raw.
[[nodiscard]] std::optional<core::EngineError> importExotic(std::span<const std::byte> bytes,
                                                            const std::filesystem::path& directory,
                                                            std::string_view extension, asset::Model& out);

} // namespace luaug::assetc
