#include "luaug/asset/gltf.h"

#include "luaug/asset/image.h"
#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <limits>
#include <meshoptimizer.h>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace luaug::asset {
namespace {

namespace fg = fastgltf;

// meshoptimizer's whole surface is `unsigned int*`, and `Mesh::indices` is
// `u32`. They are the same type on every target this engine builds for; if that
// ever stops being true the reinterpretation below would be silently wrong, so
// it is a compile failure instead.
static_assert(std::is_same_v<u32, unsigned int>, "meshoptimizer indexes with unsigned int; Mesh::indices must match");

// How far the overdraw pass may degrade vertex-cache efficiency (1.05 = 5%),
// which is the value meshoptimizer's own documentation uses for a general mesh.
constexpr f32 kOverdrawThreshold = 1.05f;

// A UV triangle smaller than this has no usable gradient, so the tangent it
// would produce is noise. Those vertices keep whatever the neighbouring
// triangles contributed, and fall back to an arbitrary perpendicular if nothing
// did.
constexpr f32 kDegenerateUvArea = 1e-12f;

struct MeshInstance
{
    std::size_t meshIndex = 0;
    // The node's world transform, already accumulated down the tree.
    core::Mat4 transform;
    // What the artist called this piece. Taken from the NODE rather than from
    // the mesh, because a file that instances one mesh at three nodes has three
    // pieces with three names and one shared geometry -- and it is the node
    // names an author recognises in an outliner.
    std::string name;
};

// One primitive's attributes, in the file's own vertex order, before they are
// appended to the model. Staged rather than written straight into
// `Mesh::vertices` because generating flat normals de-indexes the primitive,
// which changes the vertex count after the positions have been read.
struct Staging
{
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::array<f32, 4>> tangents;
    std::vector<std::array<f32, 2>> uvs;
    std::vector<u32> indices;
    std::vector<SkinVertex> skin;
    bool hasNormals = false;
    bool hasTangents = false;
    bool hasUvs = false;
    bool hasSkin = false;
};

[[nodiscard]] core::Mat4 toMat4(const fg::math::fmat4x4& source) noexcept
{
    // Both are column-major with `[column][row]` indexing, so this is a copy and
    // not a transpose. Stated because the opposite assumption produces a
    // transform that looks plausible until something is off-axis.
    core::Mat4 result;
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row)
            result.m[column][row] = source[column][row];
    }
    return result;
}

[[nodiscard]] Vec3 columnOf(const core::Mat4& matrix, std::size_t column) noexcept
{
    return Vec3{matrix.m[column][0], matrix.m[column][1], matrix.m[column][2]};
}

// The determinant of the rotation-scale block. Negative means the node mirrors,
// which per the glTF specification reverses the winding of every triangle
// under it.
[[nodiscard]] f32 determinant3(const core::Mat4& matrix) noexcept
{
    return core::dot(columnOf(matrix, 0), core::cross(columnOf(matrix, 1), columnOf(matrix, 2)));
}

// Normals transform by the inverse-transpose of the rotation-scale block, not
// by the block itself: under a non-uniform scale the two differ, and using the
// transform makes lighting that is *almost* right -- correct on a uniformly
// scaled model, wrong on a stretched one.
//
// Built as the cofactor matrix, which is `det(M) * M^-T`, so nothing is divided
// by a determinant that a degenerate node makes zero; every normal is
// normalized afterwards, so the leftover `det` factor is invisible. Its *sign*
// is not invisible -- a mirroring node would flip every normal the wrong way --
// so it is divided back out.
[[nodiscard]] core::Mat4 normalTransformOf(const core::Mat4& matrix) noexcept
{
    const Vec3 a = columnOf(matrix, 0);
    const Vec3 b = columnOf(matrix, 1);
    const Vec3 c = columnOf(matrix, 2);

    Vec3 cofactor0 = core::cross(b, c);
    Vec3 cofactor1 = core::cross(c, a);
    Vec3 cofactor2 = core::cross(a, b);

    if (core::dot(a, cofactor0) < 0.0f) {
        cofactor0 = -cofactor0;
        cofactor1 = -cofactor1;
        cofactor2 = -cofactor2;
    }

    core::Mat4 result;
    result.m[0][0] = cofactor0.x;
    result.m[0][1] = cofactor0.y;
    result.m[0][2] = cofactor0.z;
    result.m[1][0] = cofactor1.x;
    result.m[1][1] = cofactor1.y;
    result.m[1][2] = cofactor1.z;
    result.m[2][0] = cofactor2.x;
    result.m[2][1] = cofactor2.y;
    result.m[2][2] = cofactor2.z;
    return result;
}

[[nodiscard]] std::string describe(fg::Error error)
{
    return std::string(fg::getErrorName(error)) + ": " + std::string(fg::getErrorMessage(error));
}

// The bytes a buffer actually holds, or nothing when the parser left it
// unresolved. `Parser::loadGltf` is asked for LoadExternalBuffers and
// LoadExternalImages below, so every source reachable here is one of these
// three; anything else means the document referenced data that was never read,
// and reading through it would be reading through an empty span.
[[nodiscard]] std::span<const std::byte> bufferBytes(const fg::Asset& asset, std::size_t bufferIndex) noexcept
{
    if (bufferIndex >= asset.buffers.size())
        return {};

    return std::visit(
        [](const auto& source) -> std::span<const std::byte> {
            using Source = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<Source, fg::sources::Array>)
                return std::span<const std::byte>(source.bytes.data(), source.bytes.size());
            else if constexpr (std::is_same_v<Source, fg::sources::Vector>)
                return std::span<const std::byte>(source.bytes.data(), source.bytes.size());
            else if constexpr (std::is_same_v<Source, fg::sources::ByteView>)
                return std::span<const std::byte>(source.bytes.data(), source.bytes.size());
            else
                return {};
        },
        asset.buffers[bufferIndex].data);
}

// The bytes of one buffer view, bounds-checked against the buffer behind it.
//
// `fastgltf::validate` checks that a view names a real buffer but not that the
// view fits inside it, and `DefaultBufferDataAdapter` subspans without looking.
// A `.glb` off the internet is untrusted input, so the check happens here
// rather than in a debug assertion.
[[nodiscard]] bool bufferViewFits(const fg::Asset& asset, std::size_t viewIndex) noexcept
{
    if (viewIndex >= asset.bufferViews.size())
        return false;

    const fg::BufferView& view = asset.bufferViews[viewIndex];
    // EXT_meshopt_compression is not among the extensions the parser is asked
    // to load, so a compressed view can only arrive as unusable bytes.
    if (view.meshoptCompression != nullptr)
        return false;

    const std::span<const std::byte> bytes = bufferBytes(asset, view.bufferIndex);
    return view.byteOffset <= bytes.size() && view.byteLength <= bytes.size() - view.byteOffset;
}

// Whether every element the accessor claims is inside the view it names.
[[nodiscard]] bool accessorFits(const fg::Asset& asset, const fg::Accessor& accessor) noexcept
{
    const auto fitsIn = [&](std::size_t viewIndex, std::size_t elementSize, std::size_t count,
                            std::size_t byteOffset) noexcept {
        if (!bufferViewFits(asset, viewIndex))
            return false;

        const fg::BufferView& view = asset.bufferViews[viewIndex];
        // Every element is at least one byte, so a count larger than the view
        // cannot fit -- checked first so the stride arithmetic below cannot
        // overflow on a document that claims billions of elements.
        if (count == 0 || count > view.byteLength)
            return false;

        const std::size_t stride = view.byteStride.value_or(elementSize);
        const std::size_t span = (count - 1) * stride + elementSize;
        return byteOffset <= view.byteLength && span <= view.byteLength - byteOffset;
    };

    const std::size_t elementSize = fg::getElementByteSize(accessor.type, accessor.componentType);

    if (accessor.sparse.has_value()) {
        const fg::SparseAccessor& sparse = accessor.sparse.value();
        const std::size_t indexSize = fg::getComponentByteSize(sparse.indexComponentType);
        if (!fitsIn(sparse.indicesBufferView, indexSize, sparse.count, sparse.indicesByteOffset))
            return false;
        if (!fitsIn(sparse.valuesBufferView, elementSize, sparse.count, sparse.valuesByteOffset))
            return false;
    }

    // "When undefined, the accessor MUST be initialized with zeros" -- a valid
    // accessor with nothing to bounds-check.
    if (!accessor.bufferViewIndex.has_value())
        return true;

    return fitsIn(accessor.bufferViewIndex.value(), elementSize, accessor.count, accessor.byteOffset);
}

[[nodiscard]] AlphaMode toAlphaMode(fg::AlphaMode mode) noexcept
{
    switch (mode) {
    case fg::AlphaMode::Mask:
        return AlphaMode::Mask;
    case fg::AlphaMode::Blend:
        return AlphaMode::Blend;
    case fg::AlphaMode::Opaque:
        break;
    }
    return AlphaMode::Opaque;
}

// The importer's whole state for one file. A struct rather than a chain of
// free functions because the mapping tables -- which file material became which
// model material, which file image became which decoded one -- are threaded
// through every step and exist only for the duration of one import.
class Importer
{
public:
    Importer(const fg::Asset& asset, const GltfImportOptions& options, Model& out) noexcept
        : asset_(asset), options_(options), out_(out)
    {}

    [[nodiscard]] std::optional<core::EngineError> run();

private:
    [[nodiscard]] std::optional<core::EngineError> collectInstances();
    [[nodiscard]] std::optional<core::EngineError> visitNode(std::size_t nodeIndex, const core::Mat4& parent);
    [[nodiscard]] std::optional<core::EngineError> appendPrimitive(const fg::Primitive& primitive,
                                                                   const core::Mat4& transform);
    [[nodiscard]] std::optional<core::EngineError> readAttributes(const fg::Primitive& primitive, Staging& staging);
    // The skeleton and the clips. Both run BEFORE the primitives, because a
    // primitive appends to `skin` only when a skeleton exists -- see the
    // comment where it does.
    [[nodiscard]] std::optional<core::EngineError> readSkin();
    [[nodiscard]] std::optional<core::EngineError> readAnimations();
    [[nodiscard]] std::optional<core::EngineError> resolveMaterial(const fg::Optional<std::size_t>& materialIndex,
                                                                   u32& slot);
    [[nodiscard]] std::optional<core::EngineError> resolveTexture(const fg::TextureInfo& info, TextureRef& ref);
    [[nodiscard]] std::optional<core::EngineError> resolveImage(std::size_t imageIndex, u32& slot);

    void generateFlatNormals(Staging& staging) const;
    void generateTangents(Staging& staging) const;
    void optimize();

    const fg::Asset& asset_;
    const GltfImportOptions& options_;
    Model& out_;

    std::vector<MeshInstance> instances_;
    // Indexed by the file's own index; `TextureRef::Missing` until claimed. A
    // flat vector rather than a map so nothing here iterates a hash container
    // (R10) and the model's order follows the file's (Decision 7).
    std::vector<u32> materialSlots_;
    std::vector<u32> imageSlots_;
    // Appended once, the first time a primitive names no material.
    u32 defaultMaterialSlot_ = TextureRef::Missing;
    // glTF joint slot -> our sorted joint index. See `readSkin`.
    std::vector<u32> jointRemap_;
    // Whether the material a primitive uses samples a normal map, which is the
    // condition for generating tangents it does not carry.
    bool materialWantsTangents_ = false;
    std::vector<bool> visitedNodes_;
    // Every node's world transform, filled by the walk. `readSkin` runs after it
    // and needs the graph, not the joint chain -- see `Model::restPalette`.
    std::vector<core::Mat4> nodeWorld_;

    [[nodiscard]] std::optional<core::EngineError> bakeBindPose();
};

std::optional<core::EngineError> Importer::run()
{
    materialSlots_.assign(asset_.materials.size(), TextureRef::Missing);
    imageSlots_.assign(asset_.images.size(), TextureRef::Missing);
    visitedNodes_.assign(asset_.nodes.size(), false);
    nodeWorld_.assign(asset_.nodes.size(), core::Mat4{});

    if (auto error = collectInstances())
        return error;

    // Before the primitives: `appendPrimitive` appends to the skin stream only
    // when there is a skeleton to weight against, and it decides that by asking
    // whether `joints` is empty.
    if (auto error = readSkin())
        return error;
    if (auto error = readAnimations())
        return error;

    // The host's pass stops here: it wanted the skeleton and the clips, and
    // decoding an image or optimizing a vertex cache for it would be work for a
    // caller that has no GPU.
    if (options_.skeletonOnly)
        return std::nullopt;

    for (const MeshInstance& instance : instances_) {
        const fg::Mesh& mesh = asset_.meshes[instance.meshIndex];
        for (const fg::Primitive& primitive : mesh.primitives) {
            const std::size_t before = out_.mesh.submeshes.size();
            if (auto error = appendPrimitive(primitive, instance.transform))
                return error;
            // One name per submesh the primitive actually produced, so the two
            // arrays cannot drift -- a primitive that appended nothing appends
            // no name either.
            for (std::size_t added = before; added < out_.mesh.submeshes.size(); ++added)
                out_.submeshNames.push_back(instance.name);
        }
    }

    if (out_.mesh.submeshes.empty())
        return core::makeError(LUAUG_TR("asset.gltf.err.no_mesh"));

    // Before `optimize`, which remaps and reorders vertices: the bake writes
    // positions, normals and tangents, and doing it afterwards would be doing it
    // to a different array.
    if (auto error = bakeBindPose())
        return error;

    optimize();

    for (const Submesh& submesh : out_.mesh.submeshes)
        core::expand(out_.mesh.bounds, submesh.bounds);

    return std::nullopt;
}

std::optional<core::EngineError> Importer::collectInstances()
{
    // The scene the file itself points at, so two importers of the same file
    // agree; `scenes[0]` is the specification's fallback when there is no
    // `scene` property.
    if (!asset_.scenes.empty()) {
        const std::size_t sceneIndex = asset_.defaultScene.value_or(0);
        if (sceneIndex >= asset_.scenes.size())
            return core::makeError(LUAUG_TR("asset.gltf.err.invalid_document"), {}, "default scene is out of range");

        for (const std::size_t root : asset_.scenes[sceneIndex].nodeIndices) {
            if (auto error = visitNode(root, core::Mat4{}))
                return error;
        }
        return std::nullopt;
    }

    // No scenes at all: every node that nothing claims as a child is a root.
    // Rare, but a document that is only a node tree is legal and is what a
    // stripped-down exporter emits.
    std::vector<bool> claimed(asset_.nodes.size(), false);
    for (const fg::Node& node : asset_.nodes) {
        for (const std::size_t child : node.children) {
            if (child >= claimed.size())
                return core::makeError(LUAUG_TR("asset.gltf.err.invalid_document"), {}, "child node is out of range");
            claimed[child] = true;
        }
    }

    for (std::size_t index = 0; index < asset_.nodes.size(); ++index) {
        if (claimed[index])
            continue;
        if (auto error = visitNode(index, core::Mat4{}))
            return error;
    }
    return std::nullopt;
}

std::optional<core::EngineError> Importer::visitNode(std::size_t nodeIndex, const core::Mat4& parent)
{
    if (nodeIndex >= asset_.nodes.size())
        return core::makeError(LUAUG_TR("asset.gltf.err.invalid_document"), {}, "node index is out of range");

    // glTF's node graph is a tree: a node has at most one parent. A file that
    // says otherwise would either duplicate geometry or, if it names an
    // ancestor, recurse until the stack runs out.
    if (visitedNodes_[nodeIndex])
        return core::makeError(LUAUG_TR("asset.gltf.err.node_cycle"));
    visitedNodes_[nodeIndex] = true;

    const fg::Node& node = asset_.nodes[nodeIndex];
    // "First `nodeLocal`, then `parent`" -- the column-vector convention, so the
    // product reads backwards relative to the order the transforms happen in.
    const core::Mat4 nodeLocal = toMat4(fg::getTransformMatrix(node));
    const core::Mat4 combined = parent * nodeLocal;
    nodeWorld_[nodeIndex] = combined;

    if (node.meshIndex.has_value()) {
        // **A SKINNED mesh node's own transform is not the mesh's**, and glTF
        // says so in as many words: it MUST be ignored when the node carries a
        // skin, because the joints' global transforms place every vertex through
        // the inverse bind matrices. Applying it as well applies the export's
        // axis swap twice, which is a model lying on its back.
        //
        // It looked correct for years because it usually IS: a file whose
        // skinned mesh node sits at the identity has nothing to apply twice, and
        // every fixture in this repository is such a file.
        //
        // `validate` has already established the index is in range.
        // The node's name, or the mesh's when the node has none -- an exporter
        // that names one and not the other is common, and an unnamed piece falls
        // back to an ordinal at split time rather than here.
        std::string name(node.name);
        if (name.empty() && node.meshIndex.value() < asset_.meshes.size())
            name = std::string(asset_.meshes[node.meshIndex.value()].name);
        instances_.push_back(MeshInstance{node.meshIndex.value(), node.skinIndex.has_value() ? core::Mat4{} : combined,
                                          std::move(name)});
    }

    for (const std::size_t child : node.children) {
        if (auto error = visitNode(child, combined))
            return error;
    }
    return std::nullopt;
}

// **A rig this caller cannot pose is a rig whose bind pose is all it will show.**
//
// So the bind pose stops being a palette and becomes the geometry: every vertex
// is skinned once, here, and the skeleton is thrown away. What is lost is
// animation, and it was never available -- a 677-joint rig against a 64-matrix
// palette does not animate badly, it indexes off the end and scatters.
//
// The alternative is what the engine did before: draw the raw vertices and let
// the mesh node's transform stand in for the bind pose, which is right only when
// the two happen to agree.
std::optional<core::EngineError> Importer::bakeBindPose()
{
    if (options_.maxSkinJoints == 0 || out_.joints.empty() || out_.skin.empty())
        return std::nullopt;
    if (out_.joints.size() <= options_.maxSkinJoints)
        return std::nullopt;
    if (out_.skin.size() != out_.mesh.vertices.size())
        return core::makeError(LUAUG_TR("asset.gltf.err.invalid_document"), {}, "skin and vertex counts disagree");

    for (std::size_t index = 0; index < out_.mesh.vertices.size(); ++index) {
        const SkinVertex& influence = out_.skin[index];
        Vertex& vertex = out_.mesh.vertices[index];

        core::Vec3 position{};
        core::Vec3 normal{};
        core::Vec3 tangent{};
        f32 total = 0.0f;
        for (std::size_t lane = 0; lane < 4; ++lane) {
            const f32 weight = influence.weights[lane];
            if (weight <= 0.0f)
                continue;
            const auto joint = static_cast<std::size_t>(influence.joints[lane] + 0.5f);
            if (joint >= out_.restPalette.size())
                continue;
            const core::Mat4& bone = out_.restPalette[joint];
            total += weight;

            // The point through the whole matrix, the directions through its
            // upper 3x3: a normal does not translate, and a palette that
            // translates one is a lighting bug that follows the model around.
            const core::Vec3 movedPoint = core::transformPoint(bone, vertex.position);
            const core::Vec3 movedNormal = core::transformDirection(bone, vertex.normal);
            const core::Vec3 movedTangent =
                core::transformDirection(bone, core::Vec3{vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]});
            position = position + movedPoint * weight;
            normal = normal + movedNormal * weight;
            tangent = tangent + movedTangent * weight;
        }

        // A vertex nothing weights is a vertex the file forgot; it keeps what it
        // had rather than collapsing to the origin and dragging a triangle
        // across the model.
        if (total <= 0.0f)
            continue;

        vertex.position = position * (1.0f / total);
        // Renormalised rather than divided: the palette carries the export's
        // unit conversion, so every direction comes out of it scaled.
        vertex.normal = core::normalize(normal);
        const core::Vec3 unitTangent = core::normalize(tangent);
        vertex.tangent[0] = unitTangent.x;
        vertex.tangent[1] = unitTangent.y;
        vertex.tangent[2] = unitTangent.z;
    }

    // The skeleton goes, and with it the reason to keep a palette or a stream.
    // `Model::sourceJointCount` is what the loader reports afterwards.
    out_.skin.clear();
    out_.joints.clear();
    out_.restPalette.clear();
    out_.clips.clear();

    // The bounds were computed from the pre-bake positions by whoever ran first;
    // `run` expands them after this returns, from the submeshes.
    for (Submesh& submesh : out_.mesh.submeshes) {
        submesh.bounds = core::AABB{};
        for (u32 i = 0; i < submesh.indexCount; ++i) {
            const u32 vertexIndex = out_.mesh.indices[submesh.firstIndex + i];
            if (vertexIndex < out_.mesh.vertices.size())
                core::expand(submesh.bounds, out_.mesh.vertices[vertexIndex].position);
        }
    }
    return std::nullopt;
}

std::optional<core::EngineError> Importer::resolveImage(std::size_t imageIndex, u32& slot)
{
    if (imageIndex >= imageSlots_.size())
        return core::makeError(LUAUG_TR("asset.gltf.err.invalid_document"), {}, "image index is out of range");

    // Decoded once even when several materials sample it: the second material
    // finds the slot already claimed.
    if (imageSlots_[imageIndex] != TextureRef::Missing) {
        slot = imageSlots_[imageIndex];
        return std::nullopt;
    }

    const fg::Image& image = asset_.images[imageIndex];
    std::span<const std::byte> encoded;
    if (const auto* view = std::get_if<fg::sources::BufferView>(&image.data); view != nullptr) {
        if (!bufferViewFits(asset_, view->bufferViewIndex)) {
            return core::makeError(LUAUG_TR("asset.gltf.err.invalid_document"), {},
                                   "image " + std::to_string(imageIndex) + " names a buffer view outside its buffer");
        }
        const fg::BufferView& bufferView = asset_.bufferViews[view->bufferViewIndex];
        encoded = bufferBytes(asset_, bufferView.bufferIndex).subspan(bufferView.byteOffset, bufferView.byteLength);
    }
    else if (const auto* array = std::get_if<fg::sources::Array>(&image.data); array != nullptr) {
        encoded = std::span<const std::byte>(array->bytes.data(), array->bytes.size());
    }
    else if (const auto* vector = std::get_if<fg::sources::Vector>(&image.data); vector != nullptr) {
        encoded = std::span<const std::byte>(vector->bytes.data(), vector->bytes.size());
    }
    else if (const auto* bytes = std::get_if<fg::sources::ByteView>(&image.data); bytes != nullptr) {
        encoded = std::span<const std::byte>(bytes->bytes.data(), bytes->bytes.size());
    }
    else {
        // LoadExternalImages was asked for, so a source that is still a URI is
        // one the parser could not read -- a texture beside the file that is
        // not there.
        return core::makeError(LUAUG_TR("asset.gltf.err.image_missing"), {},
                               "image " + std::to_string(imageIndex) + " was not loaded");
    }

    Image decoded;
    if (auto error = decodeImage(encoded, decoded)) {
        return core::makeError(LUAUG_TR("asset.gltf.err.image_decode_failed"), {},
                               "image " + std::to_string(imageIndex) + ": " + error->message);
    }

    slot = static_cast<u32>(out_.images.size());
    imageSlots_[imageIndex] = slot;
    out_.images.push_back(std::move(decoded));
    return std::nullopt;
}

std::optional<core::EngineError> Importer::resolveTexture(const fg::TextureInfo& info, TextureRef& ref)
{
    if (info.textureIndex >= asset_.textures.size())
        return core::makeError(LUAUG_TR("asset.gltf.err.invalid_document"), {}, "texture index is out of range");

    const fg::Texture& texture = asset_.textures[info.textureIndex];
    // Only the core specification's image is honoured; the basis, DDS and WebP
    // indices belong to extensions this importer does not enable, so a texture
    // that carries only one of those has no image it can read.
    if (!texture.imageIndex.has_value()) {
        return core::makeError(LUAUG_TR("asset.gltf.err.image_missing"), {},
                               "texture " + std::to_string(info.textureIndex) + " names no core image");
    }

    u32 slot = TextureRef::Missing;
    if (auto error = resolveImage(texture.imageIndex.value(), slot))
        return error;

    ref.image = slot;
    // Recorded as the file declares it even though `Vertex` carries a single UV
    // set: the frozen `TextureRef` has the field precisely so a second set is
    // representable, and losing the number here would make a material that
    // samples TEXCOORD_1 indistinguishable from one that samples TEXCOORD_0.
    ref.uvSet = static_cast<u32>(info.texCoordIndex);
    return std::nullopt;
}

std::optional<core::EngineError> Importer::resolveMaterial(const fg::Optional<std::size_t>& materialIndex, u32& slot)
{
    if (!materialIndex.has_value()) {
        // "a draw with no material is a draw the renderer cannot make" -- one
        // default, appended once, shared by every primitive that names none.
        // Its field defaults are already glTF's default material.
        if (defaultMaterialSlot_ == TextureRef::Missing) {
            defaultMaterialSlot_ = static_cast<u32>(out_.materials.size());
            out_.materials.emplace_back();
        }
        slot = defaultMaterialSlot_;
        materialWantsTangents_ = false;
        return std::nullopt;
    }

    const std::size_t index = materialIndex.value();
    if (index >= materialSlots_.size())
        return core::makeError(LUAUG_TR("asset.gltf.err.invalid_document"), {}, "material index is out of range");

    if (materialSlots_[index] != TextureRef::Missing) {
        slot = materialSlots_[index];
        materialWantsTangents_ = out_.materials[slot].normal.present();
        return std::nullopt;
    }

    const fg::Material& source = asset_.materials[index];

    // **A specular-glossiness material is read as the metallic-roughness one it
    // would have been**, because that is the only model anything downstream has.
    //
    // The conversion is the archived extension's own: diffuse is the base
    // colour, glossiness is the complement of roughness, and metallic is zero --
    // the extension has no metalness, it expresses metal through a coloured
    // specular, which a renderer built on the other model cannot use. What is
    // lost is the specular colour, and losing it is right: a plausible material
    // with the right texture on it is what somebody downloading a model wants,
    // and refusing the file is what they had.
    //
    // Read INSTEAD of `pbrData` rather than on top of it. A file carrying the
    // extension has no metallic-roughness block to fall back on -- fastgltf hands
    // back the defaults for one, which are white and fully rough -- so mixing
    // them would paint the diffuse texture with a white base factor and call it
    // agreement.
    const fg::MaterialSpecularGlossiness* specGloss = source.specularGlossiness.get();

    MaterialDef material;
    material.name = std::string(source.name);
    if (specGloss != nullptr) {
        material.baseColorFactor =
            Color3{static_cast<f32>(specGloss->diffuseFactor.x()), static_cast<f32>(specGloss->diffuseFactor.y()),
                   static_cast<f32>(specGloss->diffuseFactor.z())};
        material.baseColorAlpha = static_cast<f32>(specGloss->diffuseFactor.w());
        material.metallicFactor = 0.0f;
        material.roughnessFactor = 1.0f - static_cast<f32>(specGloss->glossinessFactor);
    }
    else {
        material.baseColorFactor = Color3{static_cast<f32>(source.pbrData.baseColorFactor.x()),
                                          static_cast<f32>(source.pbrData.baseColorFactor.y()),
                                          static_cast<f32>(source.pbrData.baseColorFactor.z())};
        material.baseColorAlpha = static_cast<f32>(source.pbrData.baseColorFactor.w());
        material.metallicFactor = static_cast<f32>(source.pbrData.metallicFactor);
        material.roughnessFactor = static_cast<f32>(source.pbrData.roughnessFactor);
    }
    material.emissiveFactor =
        Color3{static_cast<f32>(source.emissiveFactor.x()), static_cast<f32>(source.emissiveFactor.y()),
               static_cast<f32>(source.emissiveFactor.z())};
    material.alphaMode = toAlphaMode(source.alphaMode);
    material.alphaCutoff = static_cast<f32>(source.alphaCutoff);
    material.doubleSided = source.doubleSided;

    // In the file's own declaration order, so which image lands in which slot
    // is a property of the document rather than of an iteration order (R10).
    if (specGloss != nullptr) {
        // The diffuse map IS the base colour map. There is no metallic-roughness
        // image to take: the extension's second texture packs specular in RGB
        // and glossiness in A, which is neither of the channels this renderer's
        // ORM map means, and reading it into that slot would tint every surface
        // by its own specular.
        if (specGloss->diffuseTexture.has_value()) {
            if (auto error = resolveTexture(specGloss->diffuseTexture.value(), material.baseColor))
                return error;
        }
    }
    else {
        if (source.pbrData.baseColorTexture.has_value()) {
            if (auto error = resolveTexture(source.pbrData.baseColorTexture.value(), material.baseColor))
                return error;
        }
        if (source.pbrData.metallicRoughnessTexture.has_value()) {
            if (auto error =
                    resolveTexture(source.pbrData.metallicRoughnessTexture.value(), material.metallicRoughness))
                return error;
        }
    }
    if (source.normalTexture.has_value()) {
        if (auto error = resolveTexture(source.normalTexture.value(), material.normal))
            return error;
        material.normalScale = static_cast<f32>(source.normalTexture->scale);
    }
    if (source.emissiveTexture.has_value()) {
        if (auto error = resolveTexture(source.emissiveTexture.value(), material.emissive))
            return error;
    }

    slot = static_cast<u32>(out_.materials.size());
    materialSlots_[index] = slot;
    materialWantsTangents_ = material.normal.present();
    out_.materials.push_back(std::move(material));
    return std::nullopt;
}

std::optional<core::EngineError> Importer::readAttributes(const fg::Primitive& primitive, Staging& staging)
{
    const auto* position = primitive.findAttribute("POSITION");
    if (position == primitive.attributes.cend())
        return core::makeError(LUAUG_TR("asset.gltf.err.missing_position"));

    const fg::Accessor& positionAccessor = asset_.accessors[position->accessorIndex];
    if (!accessorFits(asset_, positionAccessor))
        return core::makeError(LUAUG_TR("asset.gltf.err.accessor_out_of_range"), {}, "POSITION");

    const std::size_t vertexCount = positionAccessor.count;
    staging.positions.resize(vertexCount);
    fg::iterateAccessorWithIndex<fg::math::fvec3>(asset_, positionAccessor,
                                                  [&](fg::math::fvec3 value, std::size_t index) {
                                                      staging.positions[index] = Vec3{value.x(), value.y(), value.z()};
                                                  });

    // `validate` has already pinned each of these to the accessor type the
    // specification requires, which is why the reads below can name one element
    // type each.
    if (const auto* normal = primitive.findAttribute("NORMAL"); normal != primitive.attributes.cend()) {
        const fg::Accessor& accessor = asset_.accessors[normal->accessorIndex];
        if (accessor.count != vertexCount)
            return core::makeError(LUAUG_TR("asset.gltf.err.attribute_count_mismatch"), {}, "NORMAL");
        if (!accessorFits(asset_, accessor))
            return core::makeError(LUAUG_TR("asset.gltf.err.accessor_out_of_range"), {}, "NORMAL");

        staging.normals.resize(vertexCount);
        fg::iterateAccessorWithIndex<fg::math::fvec3>(asset_, accessor, [&](fg::math::fvec3 value, std::size_t index) {
            staging.normals[index] = Vec3{value.x(), value.y(), value.z()};
        });
        staging.hasNormals = true;
    }

    if (const auto* tangent = primitive.findAttribute("TANGENT"); tangent != primitive.attributes.cend()) {
        const fg::Accessor& accessor = asset_.accessors[tangent->accessorIndex];
        if (accessor.count != vertexCount)
            return core::makeError(LUAUG_TR("asset.gltf.err.attribute_count_mismatch"), {}, "TANGENT");
        if (!accessorFits(asset_, accessor))
            return core::makeError(LUAUG_TR("asset.gltf.err.accessor_out_of_range"), {}, "TANGENT");

        staging.tangents.resize(vertexCount);
        fg::iterateAccessorWithIndex<fg::math::fvec4>(asset_, accessor, [&](fg::math::fvec4 value, std::size_t index) {
            staging.tangents[index] = {value.x(), value.y(), value.z(), value.w()};
        });
        staging.hasTangents = true;
    }

    // TEXCOORD_0 only: `Vertex` carries one UV set (model.h), so a second one
    // has nowhere to go.
    if (const auto* uv = primitive.findAttribute("TEXCOORD_0"); uv != primitive.attributes.cend()) {
        const fg::Accessor& accessor = asset_.accessors[uv->accessorIndex];
        if (accessor.count != vertexCount)
            return core::makeError(LUAUG_TR("asset.gltf.err.attribute_count_mismatch"), {}, "TEXCOORD_0");
        if (!accessorFits(asset_, accessor))
            return core::makeError(LUAUG_TR("asset.gltf.err.accessor_out_of_range"), {}, "TEXCOORD_0");

        staging.uvs.resize(vertexCount);
        fg::iterateAccessorWithIndex<fg::math::fvec2>(asset_, accessor, [&](fg::math::fvec2 value, std::size_t index) {
            staging.uvs[index] = {value.x(), value.y()};
        });
        staging.hasUvs = true;
    }

    // JOINTS_0 and WEIGHTS_0, and only the first set. glTF allows JOINTS_1 for
    // meshes with more than four influences per vertex; `SkinVertex` carries
    // four (model.h says why), so a second set has nowhere to go and is
    // ignored rather than half-read.
    const auto* joints = primitive.findAttribute("JOINTS_0");
    const auto* weights = primitive.findAttribute("WEIGHTS_0");
    if (joints != primitive.attributes.cend() && weights != primitive.attributes.cend()) {
        const fg::Accessor& jointAccessor = asset_.accessors[joints->accessorIndex];
        const fg::Accessor& weightAccessor = asset_.accessors[weights->accessorIndex];
        if (jointAccessor.count != vertexCount || weightAccessor.count != vertexCount)
            return core::makeError(LUAUG_TR("asset.gltf.err.attribute_count_mismatch"), {}, "JOINTS_0");
        if (!accessorFits(asset_, jointAccessor) || !accessorFits(asset_, weightAccessor))
            return core::makeError(LUAUG_TR("asset.gltf.err.accessor_out_of_range"), {}, "JOINTS_0");

        staging.skin.resize(vertexCount);
        fg::iterateAccessorWithIndex<fg::math::u16vec4>(
            asset_, jointAccessor, [&](fg::math::u16vec4 value, std::size_t index) {
                // Rewritten into OUR joint order as they are read. glTF's
                // slots are the exporter's; ours are sorted parents-first
                // (`readSkin`), and a vertex still pointing at a glTF slot
                // would be weighted by whichever joint happened to land there.
                const auto remap = [&](core::u16 slot) {
                    return slot < jointRemap_.size() ? static_cast<f32>(jointRemap_[slot]) : 0.0f;
                };
                staging.skin[index].joints[0] = remap(value.x());
                staging.skin[index].joints[1] = remap(value.y());
                staging.skin[index].joints[2] = remap(value.z());
                staging.skin[index].joints[3] = remap(value.w());
            });
        fg::iterateAccessorWithIndex<fg::math::fvec4>(
            asset_, weightAccessor, [&](fg::math::fvec4 value, std::size_t index) {
                // Normalized on load rather than in the shader. An exporter is
                // allowed to emit weights that do not sum to one, and a vertex
                // whose influences sum to 0.98 shrinks by 2% every frame it is
                // skinned -- which reads as a mesh that slowly deflates.
                const float sum = value.x() + value.y() + value.z() + value.w();
                // Guarded with a floor rather than a comparison against zero:
                // MSVC reads `sum > 0 ? 1/sum : 0` as a possible division by
                // zero and /WX makes that an error, and a vertex with no
                // influences at all is a real thing an exporter emits.
                const float scale = sum > 0.0f ? 1.0f / std::fmax(sum, 1.0e-8f) : 0.0f;
                staging.skin[index].weights[0] = value.x() * scale;
                staging.skin[index].weights[1] = value.y() * scale;
                staging.skin[index].weights[2] = value.z() * scale;
                staging.skin[index].weights[3] = value.w() * scale;
            });
        staging.hasSkin = true;
    }

    if (primitive.indicesAccessor.has_value()) {
        const fg::Accessor& accessor = asset_.accessors[primitive.indicesAccessor.value()];
        if (!accessorFits(asset_, accessor))
            return core::makeError(LUAUG_TR("asset.gltf.err.accessor_out_of_range"), {}, "indices");
        if (accessor.type != fg::AccessorType::Scalar)
            return core::makeError(LUAUG_TR("asset.gltf.err.invalid_document"), {}, "index accessor is not scalar");

        staging.indices.resize(accessor.count);
        fg::iterateAccessorWithIndex<std::uint32_t>(
            asset_, accessor, [&](std::uint32_t value, std::size_t index) { staging.indices[index] = value; });
    }
    else {
        // Options::GenerateMeshIndices means this should not happen, but a
        // primitive drawing its vertices in order is what it would mean.
        staging.indices.resize(vertexCount);
        for (std::size_t index = 0; index < vertexCount; ++index)
            staging.indices[index] = static_cast<u32>(index);
    }

    if (staging.indices.size() % 3 != 0)
        return core::makeError(LUAUG_TR("asset.gltf.err.invalid_document"), {}, "index count is not a multiple of 3");

    for (const u32 index : staging.indices) {
        if (index >= vertexCount)
            return core::makeError(LUAUG_TR("asset.gltf.err.index_out_of_range"));
    }

    return std::nullopt;
}

void Importer::generateFlatNormals(Staging& staging) const
{
    // A flat normal belongs to a face, not to a vertex, so the primitive is
    // de-indexed first: a vertex shared by two faces cannot carry both their
    // normals. This is also what the glTF specification asks for -- "when
    // normals are not specified, client implementations MUST calculate flat
    // normals".
    const std::size_t indexCount = staging.indices.size();
    std::vector<Vec3> positions(indexCount);
    std::vector<std::array<f32, 2>> uvs;
    if (staging.hasUvs)
        uvs.resize(indexCount);

    for (std::size_t slot = 0; slot < indexCount; ++slot) {
        const std::size_t source = staging.indices[slot];
        positions[slot] = staging.positions[source];
        if (staging.hasUvs)
            uvs[slot] = staging.uvs[source];
    }

    std::vector<Vec3> normals(indexCount);
    for (std::size_t triangle = 0; triangle + 2 < indexCount; triangle += 3) {
        const Vec3 edge1 = positions[triangle + 1] - positions[triangle];
        const Vec3 edge2 = positions[triangle + 2] - positions[triangle];
        // Counter-clockwise winding, which is glTF's front face; `normalize`
        // yields zero rather than NaN for a degenerate triangle.
        const Vec3 normal = core::normalize(core::cross(edge1, edge2));
        normals[triangle] = normal;
        normals[triangle + 1] = normal;
        normals[triangle + 2] = normal;
    }

    staging.positions = std::move(positions);
    staging.normals = std::move(normals);
    staging.uvs = std::move(uvs);
    staging.hasNormals = true;
    // The tangents that were read, if any, indexed the old vertices; the
    // specification says they are ignored when normals are absent anyway.
    staging.tangents.clear();
    staging.hasTangents = false;

    staging.indices.resize(indexCount);
    for (std::size_t slot = 0; slot < indexCount; ++slot)
        staging.indices[slot] = static_cast<u32>(slot);
}

void Importer::generateTangents(Staging& staging) const
{
    // Lengyel's accumulation: each triangle contributes the surface gradient
    // along U and along V to its three vertices, and each vertex is then
    // orthogonalized against its own normal. `tangent.w` records which way the
    // bitangent points, which is what glTF stores instead of the bitangent.
    const std::size_t vertexCount = staging.positions.size();
    std::vector<Vec3> alongU(vertexCount, Vec3{});
    std::vector<Vec3> alongV(vertexCount, Vec3{});

    for (std::size_t triangle = 0; triangle + 2 < staging.indices.size(); triangle += 3) {
        const std::size_t i0 = staging.indices[triangle];
        const std::size_t i1 = staging.indices[triangle + 1];
        const std::size_t i2 = staging.indices[triangle + 2];

        const Vec3 edge1 = staging.positions[i1] - staging.positions[i0];
        const Vec3 edge2 = staging.positions[i2] - staging.positions[i0];

        const f32 du1 = staging.uvs[i1][0] - staging.uvs[i0][0];
        const f32 dv1 = staging.uvs[i1][1] - staging.uvs[i0][1];
        const f32 du2 = staging.uvs[i2][0] - staging.uvs[i0][0];
        const f32 dv2 = staging.uvs[i2][1] - staging.uvs[i0][1];

        const f32 area = du1 * dv2 - du2 * dv1;
        if (std::fabs(area) < kDegenerateUvArea)
            continue;

        const f32 scale = 1.0f / area;
        const Vec3 gradientU = (edge1 * dv2 - edge2 * dv1) * scale;
        const Vec3 gradientV = (edge2 * du1 - edge1 * du2) * scale;

        for (const std::size_t vertex : {i0, i1, i2}) {
            alongU[vertex] = alongU[vertex] + gradientU;
            alongV[vertex] = alongV[vertex] + gradientV;
        }
    }

    staging.tangents.resize(vertexCount);
    for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
        const Vec3 normal = staging.normals[vertex];
        Vec3 tangent = core::normalize(alongU[vertex] - normal * core::dot(normal, alongU[vertex]));
        if (tangent == Vec3{}) {
            // Nothing usable was accumulated -- an unreferenced vertex, or a
            // tangent parallel to the normal. Any perpendicular will do, and a
            // fixed choice keeps the result the same on every run.
            const Vec3 axis = std::fabs(normal.x) < 0.9f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
            tangent = core::normalize(core::cross(normal, axis));
        }

        const f32 handedness = core::dot(core::cross(normal, tangent), alongV[vertex]) < 0.0f ? -1.0f : 1.0f;
        staging.tangents[vertex] = {tangent.x, tangent.y, tangent.z, handedness};
    }
    staging.hasTangents = true;
}

std::optional<core::EngineError> Importer::appendPrimitive(const fg::Primitive& primitive, const core::Mat4& transform)
{
    // One vertex layout, one topology. A points or lines primitive has no
    // triangle for the renderer to draw, and a strip or a fan would have to be
    // expanded into a list -- worth doing the day a file needs it, and worth
    // refusing loudly rather than importing something that draws nothing until
    // then.
    if (primitive.type != fg::PrimitiveType::Triangles) {
        return core::makeError(LUAUG_TR("asset.gltf.err.unsupported_topology"), {},
                               "primitive mode " + std::to_string(static_cast<int>(primitive.type)));
    }

    u32 materialSlot = TextureRef::Missing;
    if (auto error = resolveMaterial(primitive.materialIndex, materialSlot))
        return error;

    Staging staging;
    if (auto error = readAttributes(primitive, staging))
        return error;

    // Positions are baked first so everything derived from them -- flat normals,
    // tangents, bounds -- is derived in the space the vertices end up in.
    for (Vec3& position : staging.positions)
        position = core::transformPoint(transform, position);

    // A mirroring node reverses the winding of every triangle under it (glTF
    // 2.0 §3.7.4). Without this the mesh renders inside out wherever an
    // exporter used a negative scale to flip a part.
    if (determinant3(transform) < 0.0f) {
        for (std::size_t triangle = 0; triangle + 2 < staging.indices.size(); triangle += 3)
            std::swap(staging.indices[triangle + 1], staging.indices[triangle + 2]);
    }

    if (!staging.hasNormals && options_.generateMissingNormals) {
        generateFlatNormals(staging);
    }
    else {
        const core::Mat4 normalTransform = normalTransformOf(transform);
        staging.normals.resize(staging.positions.size());
        for (Vec3& normal : staging.normals)
            normal = core::normalize(core::transformDirection(normalTransform, normal));
    }

    for (std::array<f32, 4>& tangent : staging.tangents) {
        // Tangents lie *in* the surface, so they follow the transform itself
        // rather than its inverse-transpose. `w` is a handedness, not a
        // direction, and is carried through untouched.
        const Vec3 direction =
            core::normalize(core::transformDirection(transform, Vec3{tangent[0], tangent[1], tangent[2]}));
        tangent = {direction.x, direction.y, direction.z, tangent[3]};
    }

    if (!staging.hasTangents && options_.generateMissingTangents && staging.hasNormals && staging.hasUvs &&
        materialWantsTangents_) {
        generateTangents(staging);
    }

    const std::size_t baseVertex = out_.mesh.vertices.size();
    // Indices are u32, so the model cannot address more vertices than that.
    if (baseVertex + staging.positions.size() > std::numeric_limits<u32>::max())
        return core::makeError(LUAUG_TR("asset.gltf.err.too_many_vertices"));

    Submesh submesh;
    submesh.firstIndex = static_cast<u32>(out_.mesh.indices.size());
    submesh.indexCount = static_cast<u32>(staging.indices.size());
    submesh.material = materialSlot;

    out_.mesh.vertices.reserve(baseVertex + staging.positions.size());
    for (std::size_t slot = 0; slot < staging.positions.size(); ++slot) {
        Vertex vertex;
        vertex.position = staging.positions[slot];
        if (staging.hasNormals)
            vertex.normal = staging.normals[slot];
        if (staging.hasTangents) {
            for (std::size_t lane = 0; lane < 4; ++lane)
                vertex.tangent[lane] = staging.tangents[slot][lane];
        }
        if (staging.hasUvs) {
            vertex.uv[0] = staging.uvs[slot][0];
            vertex.uv[1] = staging.uvs[slot][1];
        }

        core::expand(submesh.bounds, vertex.position);
        out_.mesh.vertices.push_back(vertex);
        // The skin stream is parallel to the vertex stream BY CONSTRUCTION, and
        // this is the line that constructs it: a primitive with no skin still
        // appends a rest entry, so a file whose second primitive is unskinned
        // cannot silently shift every joint index after it.
        if (!out_.joints.empty())
            out_.skin.push_back(staging.hasSkin ? staging.skin[slot] : SkinVertex{});
    }

    out_.mesh.indices.reserve(out_.mesh.indices.size() + staging.indices.size());
    for (const u32 index : staging.indices)
        out_.mesh.indices.push_back(static_cast<u32>(baseVertex) + index);

    out_.mesh.submeshes.push_back(submesh);
    return std::nullopt;
}

std::optional<core::EngineError> Importer::readSkin()
{
    // The first skin only. A file with two skins is two characters, and
    // model.h's "one glTF file is one mesh" already settles what to do with
    // one: it is two files.
    if (asset_.skins.empty())
        return std::nullopt;

    const fg::Skin& skin = asset_.skins[0];
    if (skin.joints.empty())
        return std::nullopt;

    // glTF's joint list is node indices in an order the exporter chose, and a
    // parent may come after its child. `Joint::parent` is documented as always
    // LESS than the joint's own index, so the pose resolves in one forward pass
    // instead of a graph walk per frame -- which means the list has to be
    // sorted into that order here, once, rather than every frame.
    std::vector<std::size_t> nodeOf(skin.joints.begin(), skin.joints.end());
    std::vector<u32> slotOfNode(asset_.nodes.size(), Joint::NoParent);
    for (std::size_t slot = 0; slot < nodeOf.size(); ++slot)
        slotOfNode[nodeOf[slot]] = static_cast<u32>(slot);

    // Parent of each joint, in glTF's own order, found by asking every node
    // which children it claims. glTF stores children and not parents.
    std::vector<u32> parentOf(nodeOf.size(), Joint::NoParent);
    for (std::size_t node = 0; node < asset_.nodes.size(); ++node) {
        for (const std::size_t child : asset_.nodes[node].children) {
            if (child < slotOfNode.size() && slotOfNode[child] != Joint::NoParent && node < slotOfNode.size() &&
                slotOfNode[node] != Joint::NoParent) {
                parentOf[slotOfNode[child]] = slotOfNode[node];
            }
        }
    }

    // Topological order, parents first. A cycle is impossible in a valid glTF
    // -- the nodes are a tree -- and `validate` has already said the document
    // is valid, so a joint whose parent is never emitted would be a bug in this
    // function rather than in the file.
    std::vector<u32> order;
    order.reserve(nodeOf.size());
    std::vector<bool> emitted(nodeOf.size(), false);
    bool progress = true;
    while (order.size() < nodeOf.size() && progress) {
        progress = false;
        for (u32 slot = 0; slot < nodeOf.size(); ++slot) {
            if (emitted[slot])
                continue;
            const u32 parent = parentOf[slot];
            if (parent != Joint::NoParent && !emitted[parent])
                continue;
            emitted[slot] = true;
            order.push_back(slot);
            progress = true;
        }
    }
    if (order.size() != nodeOf.size())
        return core::makeError(LUAUG_TR("asset.gltf.err.node_cycle"));

    std::vector<u32> sortedOf(nodeOf.size(), Joint::NoParent);
    for (u32 position = 0; position < order.size(); ++position)
        sortedOf[order[position]] = position;

    std::vector<core::Mat4> inverseBinds(nodeOf.size());
    if (skin.inverseBindMatrices.has_value()) {
        const fg::Accessor& accessor = asset_.accessors[skin.inverseBindMatrices.value()];
        if (!accessorFits(asset_, accessor))
            return core::makeError(LUAUG_TR("asset.gltf.err.accessor_out_of_range"), {}, "inverseBindMatrices");
        fg::iterateAccessorWithIndex<fg::math::fmat4x4>(asset_, accessor,
                                                        [&](fg::math::fmat4x4 value, std::size_t index) {
                                                            if (index < inverseBinds.size())
                                                                inverseBinds[index] = toMat4(value);
                                                        });
    }

    // **The bind pose, from the node graph, because only here is there one.**
    // One multiply per joint and no assumption about the joint list being closed
    // under parenthood -- this rig leaves intermediate nodes out of its skin, so
    // a chain rebuilt from `Joint::parent` returns half the joints as roots and
    // loses everything above each break.
    out_.sourceJointCount = static_cast<u32>(nodeOf.size());
    out_.restPalette.assign(nodeOf.size(), core::Mat4{});
    for (u32 slot = 0; slot < nodeOf.size(); ++slot)
        out_.restPalette[sortedOf[slot]] = nodeWorld_[nodeOf[slot]] * inverseBinds[slot];

    out_.joints.resize(nodeOf.size());
    for (u32 slot = 0; slot < nodeOf.size(); ++slot) {
        Joint& joint = out_.joints[sortedOf[slot]];
        const fg::Node& node = asset_.nodes[nodeOf[slot]];
        joint.parent = parentOf[slot] == Joint::NoParent ? Joint::NoParent : sortedOf[parentOf[slot]];
        joint.inverseBind = inverseBinds[slot];
        joint.name = std::string(node.name);

        // The rest pose, from the node's own TRS. A node stored as a matrix
        // rather than a TRS is decomposed by fastgltf, which is why both shapes
        // arrive here as the same three fields.
        if (const auto* trs = std::get_if<fg::TRS>(&node.transform); trs != nullptr) {
            // The widening is written out: a glTF translation is f32 and
            // `CFrameD` is the f64 source of truth (ADR 0014), and
            // `-Wdouble-promotion` is an error on `engine/` so that the
            // conversion is a decision rather than an accident.
            joint.localBind.position =
                core::DVec3{static_cast<core::f64>(trs->translation.x()), static_cast<core::f64>(trs->translation.y()),
                            static_cast<core::f64>(trs->translation.z())};
            joint.localBind.rotation =
                core::fromQuaternion(trs->rotation.x(), trs->rotation.y(), trs->rotation.z(), trs->rotation.w());
        }
    }

    // The joint indices in the vertex stream are glTF's slots, so they have to
    // be rewritten into the sorted order. Done on the staging side rather than
    // here, which is why the remap is kept.
    jointRemap_ = std::move(sortedOf);
    return std::nullopt;
}

std::optional<core::EngineError> Importer::readAnimations()
{
    if (out_.joints.empty() || asset_.animations.empty())
        return std::nullopt;

    // Which sorted joint a node drives, so a channel's node target becomes a
    // joint index. A channel that targets a node outside the skeleton is
    // skipped rather than refused: a file may animate a camera beside its
    // character, and that is not an error in the character.
    std::vector<u32> jointOfNode(asset_.nodes.size(), Joint::NoParent);
    for (std::size_t slot = 0; slot < asset_.skins[0].joints.size(); ++slot)
        jointOfNode[asset_.skins[0].joints[slot]] = jointRemap_[slot];

    for (const fg::Animation& animation : asset_.animations) {
        AnimationClip clip;
        clip.name = std::string(animation.name);

        for (const fg::AnimationChannel& channel : animation.channels) {
            if (!channel.nodeIndex.has_value())
                continue;
            const u32 joint = jointOfNode[channel.nodeIndex.value()];
            if (joint == Joint::NoParent)
                continue;

            AnimationChannel out;
            out.joint = joint;
            switch (channel.path) {
            case fg::AnimationPath::Translation:
                out.target = AnimationChannel::Target::Translation;
                out.stride = 3;
                break;
            case fg::AnimationPath::Rotation:
                out.target = AnimationChannel::Target::Rotation;
                out.stride = 4;
                break;
            case fg::AnimationPath::Scale:
                out.target = AnimationChannel::Target::Scale;
                out.stride = 3;
                break;
            default:
                // Morph-target weights. v1 has no morph targets, so a channel
                // driving them is dropped rather than half-read.
                continue;
            }

            const fg::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
            const fg::Accessor& times = asset_.accessors[sampler.inputAccessor];
            const fg::Accessor& values = asset_.accessors[sampler.outputAccessor];
            if (!accessorFits(asset_, times) || !accessorFits(asset_, values))
                return core::makeError(LUAUG_TR("asset.gltf.err.accessor_out_of_range"), {}, "animation");

            out.times.resize(times.count);
            fg::iterateAccessorWithIndex<float>(asset_, times, [&](float value, std::size_t index) {
                out.times[index] = value;
                clip.duration = std::max(clip.duration, value);
            });

            out.values.resize(values.count * out.stride);
            if (out.stride == 4) {
                fg::iterateAccessorWithIndex<fg::math::fvec4>(asset_, values,
                                                              [&](fg::math::fvec4 value, std::size_t index) {
                                                                  out.values[index * 4 + 0] = value.x();
                                                                  out.values[index * 4 + 1] = value.y();
                                                                  out.values[index * 4 + 2] = value.z();
                                                                  out.values[index * 4 + 3] = value.w();
                                                              });
            }
            else {
                fg::iterateAccessorWithIndex<fg::math::fvec3>(asset_, values,
                                                              [&](fg::math::fvec3 value, std::size_t index) {
                                                                  out.values[index * 3 + 0] = value.x();
                                                                  out.values[index * 3 + 1] = value.y();
                                                                  out.values[index * 3 + 2] = value.z();
                                                              });
            }

            // A channel with fewer values than times would sample out of its own
            // array. Refused rather than clamped: a clip that silently played
            // the last key forever is a character that freezes mid-stride.
            if (out.values.size() < out.times.size() * out.stride)
                return core::makeError(LUAUG_TR("asset.gltf.err.attribute_count_mismatch"), {}, "animation");

            clip.channels.push_back(std::move(out));
        }

        if (!clip.channels.empty())
            out_.clips.push_back(std::move(clip));
    }
    return std::nullopt;
}

void Importer::optimize()
{
    if (!options_.optimize || out_.mesh.vertices.empty() || out_.mesh.indices.empty())
        return;

    const std::size_t vertexCount = out_.mesh.vertices.size();

    // The cache and overdraw passes reorder triangles within one draw call, so
    // they run per submesh -- meshoptimizer says so explicitly, and running them
    // across the whole buffer would shuffle triangles between submeshes and
    // break every index range.
    for (const Submesh& submesh : out_.mesh.submeshes) {
        if (submesh.indexCount == 0)
            continue;

        u32* range = out_.mesh.indices.data() + submesh.firstIndex;
        meshopt_optimizeVertexCache(range, range, submesh.indexCount, vertexCount);
        // Overdraw wants cache-optimized input, which is why it is second.
        meshopt_optimizeOverdraw(range, range, submesh.indexCount, &out_.mesh.vertices[0].position.x, vertexCount,
                                 sizeof(Vertex), kOverdrawThreshold);
    }

    // Vertex fetch reorders the vertex buffer itself and rewrites index values
    // in place, so it is one pass over everything and it leaves each submesh's
    // index *range* exactly where it was.
    //
    // A SKINNED mesh cannot use the in-place form, and the reason is the whole
    // hazard of a second stream: the reorder would move the vertices and leave
    // `skin` in the old order, so every vertex would be weighted by another
    // vertex's bones. meshoptimizer's remap variant exists for exactly this --
    // it returns the permutation rather than applying it, and both streams are
    // then permuted by the same table.
    if (out_.skin.empty()) {
        const std::size_t unique =
            meshopt_optimizeVertexFetch(out_.mesh.vertices.data(), out_.mesh.indices.data(), out_.mesh.indices.size(),
                                        out_.mesh.vertices.data(), vertexCount, sizeof(Vertex));
        out_.mesh.vertices.resize(unique);
        return;
    }

    std::vector<unsigned int> remap(vertexCount);
    const std::size_t unique =
        meshopt_optimizeVertexFetchRemap(remap.data(), out_.mesh.indices.data(), out_.mesh.indices.size(), vertexCount);
    meshopt_remapIndexBuffer(out_.mesh.indices.data(), out_.mesh.indices.data(), out_.mesh.indices.size(),
                             remap.data());
    meshopt_remapVertexBuffer(out_.mesh.vertices.data(), out_.mesh.vertices.data(), vertexCount, sizeof(Vertex),
                              remap.data());
    meshopt_remapVertexBuffer(out_.skin.data(), out_.skin.data(), vertexCount, sizeof(SkinVertex), remap.data());
    out_.mesh.vertices.resize(unique);
    out_.skin.resize(unique);
}

} // namespace

std::optional<core::EngineError> importGltf(std::span<const std::byte> bytes,
                                            const std::filesystem::path& baseDirectory,
                                            const GltfImportOptions& options, Model& out)
{
    out = Model{};

    if (bytes.empty())
        return core::makeError(LUAUG_TR("asset.gltf.err.parse_failed"), {}, "empty input");

    auto data = fg::GltfDataBuffer::FromBytes(bytes.data(), bytes.size());
    if (!data)
        return core::makeError(LUAUG_TR("asset.gltf.err.parse_failed"), {}, describe(data.error()));

    // **One extension, and it is an archived one.**
    //
    // `KHR_materials_pbrSpecularGlossiness` was superseded by metallic-roughness
    // and moved to the archive years ago, and it is still what a great many
    // exported models declare -- in `extensionsRequired`, which is the part that
    // matters: a parser that does not know an extension listed there refuses the
    // whole document rather than ignoring a block it does not understand. So a
    // perfectly ordinary downloaded model was not "imported without its
    // materials", it was `UnknownRequiredExtension` and no mesh at all.
    //
    // Nothing else is enabled, and the rule that kept the list empty still
    // holds: a parser that enabled an extension nothing reads would populate
    // fields nothing reads. This one IS read -- see `readMaterial`.
    fg::Parser parser(fg::Extensions::KHR_materials_pbrSpecularGlossiness);
    // LoadExternalBuffers/Images resolve a relative URI against `baseDirectory`
    // -- a `.gltf` beside its `.bin` and its `.png`. GenerateMeshIndices gives
    // an unindexed primitive the trivial index buffer so there is one path
    // below rather than two.
    const fg::Options parseOptions =
        fg::Options::LoadExternalBuffers | fg::Options::LoadExternalImages | fg::Options::GenerateMeshIndices;

    auto parsed = parser.loadGltf(data.get(), baseDirectory, parseOptions);
    if (!parsed)
        return core::makeError(LUAUG_TR("asset.gltf.err.parse_failed"), {}, describe(parsed.error()));

    // fastgltf parses permissively and validates on request. Without this an
    // out-of-range accessor, buffer view or material index reaches the reads
    // below as undefined behaviour rather than as an error, and a runtime
    // importer's input is whatever file a project happens to contain.
    if (const fg::Error error = fg::validate(parsed.get()); error != fg::Error::None)
        return core::makeError(LUAUG_TR("asset.gltf.err.invalid_document"), {}, describe(error));

    Importer importer(parsed.get(), options, out);
    if (auto error = importer.run()) {
        // "Returns an error rather than a partial model" -- whatever was built
        // before the failure is discarded, not handed back.
        out = Model{};
        return error;
    }
    return std::nullopt;
}

} // namespace luaug::asset
