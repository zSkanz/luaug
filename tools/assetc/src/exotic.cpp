#include "luaug/assetc/exotic.h"

#include "luaug/asset/image.h"
#include "luaug/core/i18n.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#if LUAUG_ASSETC_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#endif

namespace luaug::assetc {
namespace {

// The extensions this tool claims. A CLOSED list rather than assimp's own
// registry, because the build chooses which importers are compiled in
// (third_party/CMakeLists.txt) and a file that classifies as a mesh and then
// fails to import is a worse error than one that rides through as raw.
constexpr std::array<std::string_view, 5> ExoticExtensions{".fbx", ".obj", ".dae", ".ply", ".stl"};

} // namespace

bool isExoticMesh(std::string_view extension) noexcept
{
    return std::find(ExoticExtensions.begin(), ExoticExtensions.end(), extension) != ExoticExtensions.end();
}

#if !LUAUG_ASSETC_ASSIMP

std::optional<core::EngineError> importExotic(std::span<const std::byte> bytes, const std::filesystem::path& directory,
                                              std::string_view extension, asset::Model& out)
{
    (void)bytes;
    (void)directory;
    (void)out;
    // Named rather than silent. A build with the importer switched off should
    // say which file it could not read and why, not treat a model as an opaque
    // blob and produce a pack that is quietly missing a mesh.
    const core::I18nArg args[] = {{"extension", std::string(extension)}};
    return core::makeError(LUAUG_TR("assetc.err.exotic_disabled"), args);
}

#else

namespace {

[[nodiscard]] core::Vec3 toVec3(const aiVector3D& value) noexcept
{
    return core::Vec3{value.x, value.y, value.z};
}

[[nodiscard]] core::Color3 toColor(const aiColor3D& value) noexcept
{
    return core::Color3{value.r, value.g, value.b};
}

// One assimp material, translated. What is NOT translated is as important as
// what is: assimp's material model is a superset of glTF's and the engine's is
// glTF's, so anything with no home here is dropped rather than approximated
// into the nearest field.
[[nodiscard]] asset::MaterialDef translateMaterial(const aiMaterial& source)
{
    asset::MaterialDef material;

    aiString name;
    if (source.Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
        material.name = name.C_Str();
    }

    aiColor3D colour{1.0f, 1.0f, 1.0f};
    if (source.Get(AI_MATKEY_BASE_COLOR, colour) == AI_SUCCESS) {
        material.baseColorFactor = toColor(colour);
    }
    else if (source.Get(AI_MATKEY_COLOR_DIFFUSE, colour) == AI_SUCCESS) {
        // A format with no PBR channel at all -- an OBJ, an old FBX -- puts its
        // albedo in the diffuse slot. Read as base colour, which is the closest
        // honest reading rather than an approximation of one.
        material.baseColorFactor = toColor(colour);
    }

    float value = 0.0f;
    if (source.Get(AI_MATKEY_METALLIC_FACTOR, value) == AI_SUCCESS) {
        material.metallicFactor = value;
    }
    else {
        // **Not one.** glTF's default is fully metallic, which is right for a
        // file that declares a PBR material and says nothing; a file with no
        // PBR model at all is describing a painted surface, and treating it as
        // metal makes every imported OBJ look like a mirror.
        material.metallicFactor = 0.0f;
    }
    if (source.Get(AI_MATKEY_ROUGHNESS_FACTOR, value) == AI_SUCCESS) {
        material.roughnessFactor = value;
    }

    aiColor3D emissive{0.0f, 0.0f, 0.0f};
    if (source.Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
        material.emissiveFactor = toColor(emissive);
    }

    int twoSided = 0;
    if (source.Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
        material.doubleSided = twoSided != 0;
    }

    float opacity = 1.0f;
    if (source.Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS && opacity < 1.0f) {
        material.baseColorAlpha = opacity;
        material.alphaMode = asset::AlphaMode::Blend;
    }
    return material;
}

} // namespace

std::optional<core::EngineError> importExotic(std::span<const std::byte> bytes, const std::filesystem::path& directory,
                                              std::string_view extension, asset::Model& out)
{
    out = asset::Model{};

    Assimp::Importer importer;

    // **From MEMORY with the extension as a hint**, not from a path. The caller
    // already read the bytes -- content addressing means every file is read
    // once and hashed -- and handing assimp a path would read it a second time.
    //
    // The post-process set is chosen rather than inherited:
    //
    //   Triangulate         -- the engine draws triangles and nothing else.
    //   GenSmoothNormals    -- an OBJ often has none, and a mesh with no normals
    //                          renders black. Smooth rather than flat because a
    //                          flat-shaded import of a smooth model is a visible
    //                          downgrade and the reverse is not.
    //   CalcTangentSpace    -- the vertex layout carries a tangent and the
    //                          normal-map shader needs it.
    //   JoinIdenticalVertices -- these formats commonly store one vertex per
    //                          triangle corner; without this an OBJ cube is 36
    //                          vertices instead of 24.
    //   ImproveCacheLocality is deliberately OFF: `compileMesh` already runs
    //   meshoptimizer's own vertex-cache optimisation, and two reorderings is
    //   one wasted pass over every mesh.
    //   PreTransformVertices is deliberately OFF: it would bake the scene graph
    //   into one mesh, which is right for this pipeline (a `MeshPart` is one
    //   mesh) but loses the node names a future skeleton import needs. The
    //   flattening happens below, explicitly, where it can be read.
    const unsigned int flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace |
                               aiProcess_JoinIdenticalVertices | aiProcess_GenUVCoords |
                               aiProcess_ValidateDataStructure;

    const std::string hint(extension.empty() || extension.front() != '.' ? extension : extension.substr(1));
    const aiScene* scene = importer.ReadFileFromMemory(bytes.data(), bytes.size(), flags, hint.c_str());
    if (scene == nullptr || scene->mRootNode == nullptr) {
        const core::I18nArg args[] = {{"detail", std::string(importer.GetErrorString())}};
        return core::makeError(LUAUG_TR("assetc.err.exotic_import_failed"), args);
    }
    if (scene->mNumMeshes == 0) {
        return core::makeError(LUAUG_TR("assetc.err.exotic_no_geometry"));
    }

    out.materials.reserve(scene->mNumMaterials);
    for (unsigned int index = 0; index < scene->mNumMaterials; ++index) {
        out.materials.push_back(translateMaterial(*scene->mMaterials[index]));
    }
    if (out.materials.empty()) {
        // Every submesh names a material, so one has to exist. `Model`'s own
        // doc says the importer appends the default rather than leaving a draw
        // the renderer cannot make.
        out.materials.emplace_back();
    }

    // **Every mesh in the file becomes one submesh of one mesh**, which is what
    // a `MeshPart` is: one drawable thing. A file with a scene graph in it is
    // flattened, and its node TRANSFORMS are deliberately not applied -- these
    // formats put a model at the origin in its own space, and a file that does
    // not is a file whose author expected a scene importer rather than a mesh
    // importer. That is a different feature and nobody has asked for it.
    // Both default to the EMPTY box, so `expand` from a default is correct and
    // a mesh nobody filled cannot be mistaken for a point at the origin
    // (`core/math.h`).
    core::AABB bounds;

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh& mesh = *scene->mMeshes[meshIndex];
        if (mesh.mNumVertices == 0 || mesh.mFaces == nullptr) {
            continue;
        }

        asset::Submesh submesh;
        submesh.firstIndex = static_cast<core::u32>(out.mesh.indices.size());
        submesh.material = mesh.mMaterialIndex < out.materials.size() ? mesh.mMaterialIndex : 0u;

        const auto vertexBase = static_cast<core::u32>(out.mesh.vertices.size());
        core::AABB submeshBounds;

        for (unsigned int index = 0; index < mesh.mNumVertices; ++index) {
            asset::Vertex vertex;
            vertex.position = toVec3(mesh.mVertices[index]);
            if (mesh.mNormals != nullptr) {
                vertex.normal = toVec3(mesh.mNormals[index]);
            }
            if (mesh.mTangents != nullptr && mesh.mBitangents != nullptr) {
                const core::Vec3 tangent = toVec3(mesh.mTangents[index]);
                const core::Vec3 bitangent = toVec3(mesh.mBitangents[index]);
                vertex.tangent[0] = tangent.x;
                vertex.tangent[1] = tangent.y;
                vertex.tangent[2] = tangent.z;
                // The handedness, derived the way glTF defines it: the sign of
                // the bitangent against `cross(normal, tangent)`. Storing a
                // constant here would flip the normal map on half the meshes in
                // any file that mirrors geometry.
                const core::Vec3 expected = core::cross(vertex.normal, tangent);
                vertex.tangent[3] = core::dot(expected, bitangent) < 0.0f ? -1.0f : 1.0f;
            }
            if (mesh.mTextureCoords[0] != nullptr) {
                vertex.uv[0] = mesh.mTextureCoords[0][index].x;
                vertex.uv[1] = mesh.mTextureCoords[0][index].y;
            }
            out.mesh.vertices.push_back(vertex);

            core::expand(submeshBounds, vertex.position);
        }

        for (unsigned int face = 0; face < mesh.mNumFaces; ++face) {
            const aiFace& triangle = mesh.mFaces[face];
            // Triangulate ran, so anything else is a degenerate assimp kept --
            // a point or a line. Skipped rather than trusted: three indices is
            // what the rest of the pipeline assumes.
            if (triangle.mNumIndices != 3) {
                continue;
            }
            for (unsigned int corner = 0; corner < 3; ++corner) {
                out.mesh.indices.push_back(vertexBase + triangle.mIndices[corner]);
            }
        }

        submesh.indexCount = static_cast<core::u32>(out.mesh.indices.size()) - submesh.firstIndex;
        if (submesh.indexCount == 0) {
            // Nothing drawable came out, so the vertices are dead weight. Rolled
            // back rather than left in the buffer.
            out.mesh.vertices.resize(vertexBase);
            continue;
        }
        submesh.bounds = submeshBounds;
        out.mesh.submeshes.push_back(submesh);

        core::expand(bounds, submeshBounds);
    }

    if (out.mesh.submeshes.empty()) {
        return core::makeError(LUAUG_TR("assetc.err.exotic_no_geometry"));
    }
    out.mesh.bounds = bounds;

    // Images are NOT imported. An FBX may embed textures and an OBJ names them
    // in an MTL beside it, and following either is a second resolution path
    // with its own rules about relative paths and its own failure modes. A
    // material's factors survive; its maps do not, and the model imports as an
    // untextured surface rather than as an error.
    //
    // Stated rather than left to be discovered, because "my textures did not
    // come through" is the first thing anybody will notice.
    (void)directory;
    return std::nullopt;
}

#endif

} // namespace luaug::assetc
