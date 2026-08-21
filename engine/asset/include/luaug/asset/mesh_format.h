// The engine mesh format, `.lmesh` (roadmap M7, ADR 0010).
//
// What the offline pipeline turns a glTF file into, and what the runtime reads
// back: meshopt-encoded vertex and index streams, a LOD chain, meshlets, the
// materials, and the CONTENT HASHES of the textures those materials sample --
// not the textures themselves, because a texture shared by forty meshes should
// be one blob in the pack and not forty.
//
// **A section table rather than a fixed struct.** Every section is a 4-byte
// tag, an offset and a length, sorted by tag; a reader looks sections up by
// name and ignores tags it does not know. That is what lets a later milestone
// add cluster data or a second UV set without a format break, and it is what
// lets THIS milestone leave a section out -- an unskinned mesh simply has no
// `SKIN`, rather than a zero-length one everybody has to remember to check.
//
// **Determinism is a property of the encoder** (M7 brief, Decision 1), and
// meshoptimizer has a trap in it: `meshopt_encodeVertexVersion` and
// `meshopt_encodeIndexVersion` are process-global and documented as not
// thread-safe (`meshoptimizer.h:380-385`, `:274-280`). So the format version
// pins both explicitly rather than inheriting a default, and encoding runs on
// one thread.
//
// Every offset is bounds-checked before it is followed, for the same reason
// `pack.h` gives: this decoder reads files a person may have copied, truncated
// or downloaded halfway.
#pragma once

#include "luaug/asset/model.h"
#include "luaug/core/content_hash.h"
#include "luaug/core/error.h"
#include "luaug/core/types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace luaug::asset {

using core::ContentHash;
using core::u32;
using core::u64;
using core::u8;
using core::usize;

inline constexpr char MeshMagic[4] = {'L', 'G', 'M', 'S'};
inline constexpr u32 MeshFormatVersion = 1;

// The ceiling on one mesh, and it exists for a reason the fuzz case found: the
// vertex and skin streams are meshopt-COMPRESSED, so their element count cannot
// be derived from the section length the way every fixed-size record's can, and
// a corrupted count would otherwise be believed all the way into a resize. Four
// million vertices is 192 MB of vertex data and about eight times the largest
// mesh anything real ships; a file claiming more is refused by name rather than
// allocated for.
inline constexpr u32 MaxMeshVertices = 4u * 1024u * 1024u;

// A texture a material samples, named by what it contains. `srgb` is a property
// of the SLOT rather than of the file: the same image is colour data under
// `baseColor` and linear data under `metallicRoughness`, and the encoder that
// wrote it has no way to know which.
struct TextureSlot
{
    ContentHash hash;
    bool srgb = false;
};

// One level of detail: its own index buffer and its own submesh ranges.
//
// Submeshes are per-LOD because simplification is per-SUBMESH -- a material
// boundary is a seam the simplifier must not weld across, or a tree's leaves
// start being drawn with the trunk's shader.
struct MeshLod
{
    std::vector<u32> indices;
    std::vector<Submesh> submeshes;
    // meshoptimizer's reported error for this level, in the units
    // `meshopt_simplifyScale` returns -- a fraction of the mesh's own extent,
    // so it is comparable between a chair and a mountain. Zero for LOD 0.
    core::f32 error = 0.0f;
};

// One meshlet: a cluster of triangles with its own bounding sphere and normal
// cone. **Emitted from day one and consumed by nothing** (ADR 0010): the
// GPU-driven path is post-v1, and an asset re-baked later is an asset every
// shipped game has to re-download.
struct Meshlet
{
    u32 vertexOffset = 0;
    u32 triangleOffset = 0;
    u32 vertexCount = 0;
    u32 triangleCount = 0;

    core::Vec3 center;
    core::f32 radius = 0.0f;
    core::Vec3 coneAxis;
    core::f32 coneCutoff = 0.0f;
};

struct MeshletData
{
    std::vector<Meshlet> meshlets;
    std::vector<u32> vertices;
    std::vector<u8> triangles;
};

// The whole compiled mesh, in memory.
struct CompiledMesh
{
    std::vector<Vertex> vertices;
    // Empty for a static mesh; otherwise one entry per vertex.
    std::vector<SkinVertex> skin;
    // At least one. `lods[0]` is the full-detail mesh.
    std::vector<MeshLod> lods;
    std::vector<MaterialDef> materials;
    // Indexed by `TextureRef::image`, exactly as `Model::images` is.
    std::vector<TextureSlot> images;
    std::vector<Joint> joints;
    std::vector<AnimationClip> clips;
    // For LOD 0 only.
    MeshletData meshlets;
    core::AABB bounds;

    [[nodiscard]] bool skinned() const noexcept { return !joints.empty() && !skin.empty(); }
};

struct MeshCompileOptions
{
    // Including LOD 0. Four is what a streamed world wants: full, half, quarter
    // and a silhouette.
    u32 maxLods = 4;
    // Each level targets this fraction of the previous level's index count.
    core::f32 lodStep = 0.5f;
    // Simplification stops rather than exceed this, in `simplifyScale` units.
    // A level that cannot reach its target without going over is dropped, and
    // the chain ends there -- a LOD that looks wrong is worse than no LOD.
    core::f32 lodTargetError = 0.05f;
    // A level that saved less than this fraction of the previous level's
    // triangles is not worth a draw call or a residency slot.
    core::f32 lodMinReduction = 0.15f;

    bool buildMeshlets = true;
    u32 meshletMaxVertices = 64;
    // 124 rather than 128: it is what upstream's own examples use, because the
    // triangle array is indexed in groups of four.
    u32 meshletMaxTriangles = 124;
    core::f32 meshletConeWeight = 0.5f;
};

// Builds the LOD chain and the meshlets from an imported `Model`, and pairs its
// materials with the content hashes of the textures they sample. `images` is
// parallel to `Model::images`.
[[nodiscard]] std::optional<core::EngineError> compileMesh(const Model& model, std::span<const TextureSlot> images,
                                                           const MeshCompileOptions& options, CompiledMesh& out);

// A pure function of its input: the same `CompiledMesh` encodes to the same
// bytes on every machine, which is what makes the asset build reproducible.
[[nodiscard]] std::vector<std::byte> encodeMesh(const CompiledMesh& mesh);

[[nodiscard]] std::optional<core::EngineError> decodeMesh(std::span<const std::byte> bytes, CompiledMesh& out);

} // namespace luaug::asset
