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
    bool hasNormals = false;
    bool hasTangents = false;
    bool hasUvs = false;
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
    // Whether the material a primitive uses samples a normal map, which is the
    // condition for generating tangents it does not carry.
    bool materialWantsTangents_ = false;
    std::vector<bool> visitedNodes_;
};

std::optional<core::EngineError> Importer::run()
{
    materialSlots_.assign(asset_.materials.size(), TextureRef::Missing);
    imageSlots_.assign(asset_.images.size(), TextureRef::Missing);
    visitedNodes_.assign(asset_.nodes.size(), false);

    if (auto error = collectInstances())
        return error;

    for (const MeshInstance& instance : instances_) {
        const fg::Mesh& mesh = asset_.meshes[instance.meshIndex];
        for (const fg::Primitive& primitive : mesh.primitives) {
            if (auto error = appendPrimitive(primitive, instance.transform))
                return error;
        }
    }

    if (out_.mesh.submeshes.empty())
        return core::makeError(LUAUG_TR("asset.gltf.err.no_mesh"));

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

    if (node.meshIndex.has_value()) {
        // `validate` has already established the index is in range.
        instances_.push_back(MeshInstance{node.meshIndex.value(), combined});
    }

    for (const std::size_t child : node.children) {
        if (auto error = visitNode(child, combined))
            return error;
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
    MaterialDef material;
    material.name = std::string(source.name);
    material.baseColorFactor = Color3{static_cast<f32>(source.pbrData.baseColorFactor.x()),
                                      static_cast<f32>(source.pbrData.baseColorFactor.y()),
                                      static_cast<f32>(source.pbrData.baseColorFactor.z())};
    material.baseColorAlpha = static_cast<f32>(source.pbrData.baseColorFactor.w());
    material.metallicFactor = static_cast<f32>(source.pbrData.metallicFactor);
    material.roughnessFactor = static_cast<f32>(source.pbrData.roughnessFactor);
    material.emissiveFactor =
        Color3{static_cast<f32>(source.emissiveFactor.x()), static_cast<f32>(source.emissiveFactor.y()),
               static_cast<f32>(source.emissiveFactor.z())};
    material.alphaMode = toAlphaMode(source.alphaMode);
    material.alphaCutoff = static_cast<f32>(source.alphaCutoff);
    material.doubleSided = source.doubleSided;

    // In the file's own declaration order, so which image lands in which slot
    // is a property of the document rather than of an iteration order (R10).
    if (source.pbrData.baseColorTexture.has_value()) {
        if (auto error = resolveTexture(source.pbrData.baseColorTexture.value(), material.baseColor))
            return error;
    }
    if (source.pbrData.metallicRoughnessTexture.has_value()) {
        if (auto error = resolveTexture(source.pbrData.metallicRoughnessTexture.value(), material.metallicRoughness))
            return error;
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
    }

    out_.mesh.indices.reserve(out_.mesh.indices.size() + staging.indices.size());
    for (const u32 index : staging.indices)
        out_.mesh.indices.push_back(static_cast<u32>(baseVertex) + index);

    out_.mesh.submeshes.push_back(submesh);
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
    const std::size_t unique =
        meshopt_optimizeVertexFetch(out_.mesh.vertices.data(), out_.mesh.indices.data(), out_.mesh.indices.size(),
                                    out_.mesh.vertices.data(), vertexCount, sizeof(Vertex));
    out_.mesh.vertices.resize(unique);
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

    // No extensions are enabled: gltf.h says no extension beyond the core
    // specification's PBR material is honoured, and a parser that enabled one
    // would populate fields nothing reads.
    fg::Parser parser;
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
