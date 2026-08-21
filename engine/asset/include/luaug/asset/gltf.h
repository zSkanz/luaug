// glTF 2.0 import at runtime (roadmap M4, ADR 0010).
//
// Runtime rather than offline on purpose: the roadmap keeps this path forever
// as the dev-mode one, and M7's offline importer produces packs beside it
// rather than replacing it. So this has to be fast enough to sit inside a hot
// reload, which is why fastgltf is the parser (ADR 0010) and why nothing here
// allocates per-vertex.
//
// Reads `.gltf` and `.glb`, with buffers and images either embedded or beside
// the file. `baseDirectory` is what a relative URI in the document resolves
// against; a `.glb` with everything embedded never uses it.
//
// What this does NOT do, and none of it is an oversight (M4 brief, NOT in
// scope): skins and animations are skipped rather than half-imported, because
// `AnimationPlayer` ships at M6; morph targets, cameras and lights inside the
// file are ignored, because the world's camera and lights are Instances that a
// script owns, not things a mesh file dictates; and no extension beyond the
// core specification's PBR material is honoured.
#pragma once

#include "luaug/asset/model.h"
#include "luaug/core/error.h"

#include <filesystem>
#include <optional>
#include <span>

namespace luaug::asset {

struct GltfImportOptions
{
    // meshoptimizer's vertex-cache, overdraw and vertex-fetch passes (ADR 0010,
    // "essential, not optional"). On by default and switchable only so a test
    // can assert what the optimizer changed by comparing against the raw import
    // -- a flag with no caller would be speculative.
    bool optimize = true;

    // Generate flat normals when the file has none, and tangents when it has
    // normals and a normal map but no tangents. glTF permits both omissions and
    // the PBR shader requires both, so the alternative to generating them is a
    // black mesh.
    bool generateMissingNormals = true;
    bool generateMissingTangents = true;

    // Read the skeleton and the clips only: no geometry, no materials, no
    // images. What the HOST wants from a skinned file, because animation is
    // simulation -- it advances on the SimClock, it has to run in a headless
    // replay, and a script reads `TimePosition` off it -- while the vertices and
    // the textures belong to the renderer.
    //
    // Parsing one file twice is the honest cost of that split in v1, and it is a
    // parse rather than an upload: this pass decodes no image and touches no
    // vertex. M7's asset pipeline is where a file becomes one artefact both
    // halves read.
    bool skeletonOnly = false;
};

// Imports one file's mesh, materials and images into `out`.
//
// `bytes` is the whole file, because that is what an asynchronous read hands
// back and because a `.glb`'s chunks are offsets into it.
//
// Returns an error rather than a partial model: a mesh that imported half its
// primitives draws something plausible and wrong, which is worse than a mesh
// that says why it is missing. `out` is left empty on failure.
[[nodiscard]] std::optional<core::EngineError> importGltf(std::span<const std::byte> bytes,
                                                          const std::filesystem::path& baseDirectory,
                                                          const GltfImportOptions& options, Model& out);

} // namespace luaug::asset
