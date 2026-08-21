#include "luaug/render/mesh_loader.h"

#include "luaug/asset/gltf.h"
#include "luaug/asset/image.h"
#include "luaug/asset/primitives.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/file.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace luaug::render {
namespace {

using core::f32;
using core::u32;

constexpr std::string_view kAssetScheme = "asset://";

// `asset://models/tree.glb` resolves under the project's content directory. A
// URN with no scheme is taken as a path relative to the same root rather than
// rejected: the error a developer gets from a mistyped scheme should be "no such
// file", naming the path that was tried, and not a lecture about URNs.
[[nodiscard]] std::filesystem::path resolve(const std::filesystem::path& root, std::string_view urn)
{
    std::string_view relative = urn;
    if (relative.substr(0, kAssetScheme.size()) == kAssetScheme)
        relative.remove_prefix(kAssetScheme.size());
    return root / std::filesystem::path(relative);
}

[[nodiscard]] rhi::TextureHandle uploadImage(rhi::IDevice& device, rhi::ICmdList& cmd, const asset::Image& image,
                                             const char* debugName)
{
    if (!image.valid())
        return {};

    const rhi::TextureHandle handle = device.createTexture({
        .format = rhi::TextureFormat::Rgba8Unorm,
        .usage = rhi::TextureUsage::Sampled,
        .width = image.width,
        .height = image.height,
        .debugName = debugName,
    });
    if (!handle.valid())
        return {};

    cmd.uploadTexture(handle, image.pixels, 0);
    return handle;
}

} // namespace

void MeshLoader::setContentRoot(std::filesystem::path root)
{
    contentRoot_ = std::move(root);
}

void MeshLoader::destroy(rhi::IDevice& device)
{
    for (const rhi::TextureHandle texture : textures_) {
        if (texture.valid())
            device.destroy(texture);
    }
    textures_.clear();
    failed_.clear();
}

void MeshLoader::syncPrimitives(rhi::IDevice& device, rhi::ICmdList& cmd, scene::World& world, MeshCache& cache,
                                MeshLibrary& library)
{
    if (primitivesUploaded_)
        return;
    primitivesUploaded_ = true;

    for (core::i32 shape = 0; shape < static_cast<core::i32>(asset::PrimitiveShape::Count); ++shape) {
        const char* name = primitiveContent(shape);
        if (name == nullptr)
            continue;

        const asset::Mesh mesh = asset::makePrimitive(static_cast<asset::PrimitiveShape>(shape));
        core::EngineError uploadError;
        const MeshHandle handle = cache.create(device, cmd, mesh, MeshUsage::Static, &uploadError);
        if (!handle.valid()) {
            // A failure here leaves every `Part` on the debug wire path, which
            // is exactly what that path is for -- so it is a warning and not a
            // reason to refuse to draw a frame.
            core::logText(core::LogLevel::Warn, uploadError.message);
            continue;
        }

        MeshLibrary::Entry entry;
        entry.mesh = handle;
        entry.bounds = mesh.bounds;
        entry.sectionCount = 1;
        entry.sectionMaterial.push_back(0);
        // No materials: a `Part`'s look is its own properties, and `extract`
        // builds the material from them. An entry with an empty material list
        // is already handled -- the mesh loop falls back to `RenderMaterial{}`
        // for a section whose material the importer did not produce.
        library.set(world.atoms().intern(name), entry);
    }
}

u32 MeshLoader::sync(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world, core::InstanceId root,
                     MeshCache& cache, MeshLibrary& library, SkeletonLibrary* skeletons)
{
    if (!root.valid())
        return 0;

    u32 loaded = 0;

    world.meshParts().forEach([&](core::InstanceId id, const scene::MeshPartComponent& meshPart) {
        (void)id;
        const core::NameAtom content = meshPart.meshContent;
        if (content.id == 0 || library.find(content) != nullptr)
            return;
        if (std::binary_search(failed_.begin(), failed_.end(), content,
                               [](core::NameAtom a, core::NameAtom b) { return a.id < b.id; }))
            return;

        const std::string urn(world.atoms().text(content));
        const std::filesystem::path path = resolve(contentRoot_, urn);

        // Remembered as failed before anything else can go wrong, so every
        // early return below costs one attempt rather than one per frame.
        const auto markFailed = [&]() {
            const auto position = std::lower_bound(failed_.begin(), failed_.end(), content,
                                                   [](core::NameAtom a, core::NameAtom b) { return a.id < b.id; });
            failed_.insert(position, content);
        };

        std::vector<std::byte> bytes;
        if (!platform::readFile(path, bytes)) {
            const std::array<core::I18nArg, 1> args{core::I18nArg{"path", path.string()}};
            core::log(core::LogLevel::Warn, LUAUG_TR("render.err.mesh_file_missing"), args);
            markFailed();
            return;
        }

        asset::Model model;
        if (auto error = asset::importGltf(bytes, path.parent_path(), {}, model); error.has_value()) {
            core::logText(core::LogLevel::Warn, error->message);
            markFailed();
            return;
        }

        core::EngineError uploadError;
        // A file with a skin gets the second stream and a file without gets
        // exactly what M4 uploaded -- which is what keeps an unskinned draw
        // byte-identical to the one the goldens recorded.
        const MeshHandle handle = model.skinned()
                                      ? cache.createSkinned(device, cmd, model.mesh, model.skin, &uploadError)
                                      : cache.create(device, cmd, model.mesh, MeshUsage::Static, &uploadError);
        if (!handle.valid()) {
            core::logText(core::LogLevel::Warn, uploadError.message);
            markFailed();
            return;
        }

        MeshLibrary::Entry entry;
        entry.mesh = handle;
        entry.bounds = model.mesh.bounds;
        entry.sectionCount = static_cast<u32>(model.mesh.submeshes.size());
        entry.sectionMaterial.reserve(model.mesh.submeshes.size());
        for (const asset::Submesh& submesh : model.mesh.submeshes)
            entry.sectionMaterial.push_back(submesh.material);

        // Images upload once each even when two materials share one, because
        // the importer already decoded them once for the same reason.
        std::vector<rhi::TextureHandle> images;
        images.reserve(model.images.size());
        for (const asset::Image& image : model.images) {
            const rhi::TextureHandle texture = uploadImage(device, cmd, image, "material");
            if (texture.valid())
                textures_.push_back(texture);
            images.push_back(texture);
        }

        const auto textureOf = [&](const asset::TextureRef& reference) -> rhi::TextureHandle {
            if (!reference.present() || reference.image >= images.size())
                return {};
            // The importer records the UV set the file declared, and this
            // vertex layout carries one. A material sampling TEXCOORD_1
            // would silently read TEXCOORD_0, so it is dropped instead --
            // untextured is a visible wrong, silently-wrong-texture is not.
            if (reference.uvSet != 0)
                return {};
            return images[reference.image];
        };

        entry.materials.reserve(model.materials.size());
        for (const asset::MaterialDef& source : model.materials) {
            RenderMaterial material;
            material.uniforms.baseColor[0] = source.baseColorFactor.r;
            material.uniforms.baseColor[1] = source.baseColorFactor.g;
            material.uniforms.baseColor[2] = source.baseColorFactor.b;
            material.uniforms.baseColor[3] = source.baseColorAlpha;
            material.uniforms.emissive[0] = source.emissiveFactor.r;
            material.uniforms.emissive[1] = source.emissiveFactor.g;
            material.uniforms.emissive[2] = source.emissiveFactor.b;
            material.uniforms.metallicRoughnessNormalCutoff[0] = source.metallicFactor;
            material.uniforms.metallicRoughnessNormalCutoff[1] = source.roughnessFactor;
            material.uniforms.metallicRoughnessNormalCutoff[2] = source.normalScale;
            material.uniforms.metallicRoughnessNormalCutoff[3] =
                source.alphaMode == asset::AlphaMode::Mask ? source.alphaCutoff : 0.0f;

            material.baseColor = textureOf(source.baseColor);
            material.normal = textureOf(source.normal);
            material.metallicRoughness = textureOf(source.metallicRoughness);
            material.emissive = textureOf(source.emissive);

            material.uniforms.textureFlags[0] = material.baseColor.valid() ? 1.0f : 0.0f;
            material.uniforms.textureFlags[1] = material.normal.valid() ? 1.0f : 0.0f;
            material.uniforms.textureFlags[2] = material.metallicRoughness.valid() ? 1.0f : 0.0f;
            material.uniforms.textureFlags[3] = material.emissive.valid() ? 1.0f : 0.0f;

            entry.materials.push_back(material);
        }

        library.set(content, entry);

        // The skeleton half of the same file, handed to whoever asked for it.
        // Read here rather than in a second pass because the file was already
        // parsed once and parsing it again to find the joints would be the
        // clearest kind of waste.
        if (skeletons != nullptr && !model.joints.empty()) {
            skeletons->set(content, SkeletonLibrary::Entry{std::move(model.joints), std::move(model.clips)});
        }

        ++loaded;

        const std::array<core::I18nArg, 2> args{
            core::I18nArg{"path", urn},
            core::I18nArg{"triangles", static_cast<core::i64>(model.mesh.indices.size() / 3)},
        };
        core::log(core::LogLevel::Info, LUAUG_TR("render.info.mesh_loaded"), args);
    });

    return loaded;
}

} // namespace luaug::render
