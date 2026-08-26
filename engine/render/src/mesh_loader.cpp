#include "luaug/render/mesh_loader.h"

#include "luaug/asset/content.h"
#include "luaug/asset/gltf.h"
#include "luaug/asset/image.h"
#include "luaug/asset/mesh_format.h"
#include "luaug/asset/primitives.h"
#include "luaug/asset/texture.h"
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

[[nodiscard]] rhi::TextureFormat toRhi(asset::TextureFormat format) noexcept
{
    switch (format) {
    case asset::TextureFormat::Bc1Rgb:
        return rhi::TextureFormat::Bc1RgbaUnorm;
    case asset::TextureFormat::Bc3Rgba:
        return rhi::TextureFormat::Bc3RgbaUnorm;
    case asset::TextureFormat::Bc5Rg:
        return rhi::TextureFormat::Bc5RgUnorm;
    case asset::TextureFormat::Bc7Rgba:
        return rhi::TextureFormat::Bc7RgbaUnorm;
    case asset::TextureFormat::Rgba8:
    case asset::TextureFormat::Unknown:
        break;
    }
    return rhi::TextureFormat::Rgba8Unorm;
}

// A transcoded texture, with every mip it carries. The block-compressed path is
// the point of the pipeline: a BC7 texture is a quarter of the GPU memory an
// RGBA8 one costs, and the memory ceiling is what M7's gate measures.
[[nodiscard]] rhi::TextureHandle uploadTranscoded(rhi::IDevice& device, rhi::ICmdList& cmd,
                                                  const asset::TextureAsset& texture, const char* debugName)
{
    if (!texture.valid())
        return {};

    const rhi::TextureHandle handle = device.createTexture({
        .format = toRhi(texture.format),
        .usage = rhi::TextureUsage::Sampled,
        .width = texture.width,
        .height = texture.height,
        .layers = 1,
        .mipLevels = static_cast<u32>(texture.mips.size()),
        .debugName = debugName,
    });
    if (!handle.valid())
        return {};

    for (u32 level = 0; level < static_cast<u32>(texture.mips.size()); ++level) {
        const asset::TextureMip& mip = texture.mips[level];
        cmd.uploadTexture(handle, std::span<const std::byte>(texture.pixels.data() + mip.offset, mip.size), level);
    }
    return handle;
}

// Everything after "the geometry is on the GPU and the images are uploaded",
// shared by the two feeds. A compiled mesh out of a pack and a glTF parsed on
// the way in differ in how they arrive and in nothing after that -- and one
// copy of this is what keeps them agreeing.
void fillEntry(MeshLibrary::Entry& entry, const core::AABB& bounds, std::span<const asset::Submesh> submeshes,
               std::span<const asset::MaterialDef> materials, std::span<const rhi::TextureHandle> images)
{
    entry.bounds = bounds;
    entry.sectionCount = static_cast<u32>(submeshes.size());
    entry.sectionMaterial.reserve(submeshes.size());
    for (const asset::Submesh& submesh : submeshes)
        entry.sectionMaterial.push_back(submesh.material);

    const auto textureOf = [&](const asset::TextureRef& reference) -> rhi::TextureHandle {
        if (!reference.present() || reference.image >= images.size())
            return {};
        // The importer records the UV set the file declared, and this vertex
        // layout carries one. A material sampling TEXCOORD_1 would silently
        // read TEXCOORD_0, so it is dropped instead -- untextured is a visible
        // wrong, silently-wrong-texture is not.
        if (reference.uvSet != 0)
            return {};
        return images[reference.image];
    };

    entry.materials.reserve(materials.size());
    for (const asset::MaterialDef& source : materials) {
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

        material.setMaps(textureOf(source.baseColor), textureOf(source.normal), textureOf(source.metallicRoughness),
                         textureOf(source.emissive));

        entry.materials.push_back(material);
    }
}

} // namespace

void MeshLoader::setContentRoot(std::filesystem::path root)
{
    contentRoot_ = std::move(root);
}

void MeshLoader::destroy(rhi::IDevice& device)
{
    // **Waited for, not abandoned.** A decode job writes into buffers these
    // entries own, and returning while one is running frees the memory the pool
    // is still writing into -- a use-after-free that reproduces on a fast
    // machine and never on a slow one, at shutdown, where a crash reads as "the
    // editor crashed when I closed it" and points at nothing.
    releasePendingTextures();

    for (const rhi::TextureHandle texture : textures_) {
        if (texture.valid())
            device.destroy(texture);
    }
    textures_.clear();
    failed_.clear();
}

MeshLoader::~MeshLoader()
{
    // No device here, so no texture can be freed -- `destroy` is what does that,
    // and a caller who forgot it has leaked them whatever this does. What cannot
    // be left is a job still writing into memory this object is about to
    // release, so that much happens unconditionally.
    releasePendingTextures();
}

void MeshLoader::releasePendingTextures() noexcept
{
    for (PendingTexture& pending : pendingTextures_) {
        if (pending.decode.valid())
            jobs::wait(pending.decode);
        if (pending.read.valid())
            platform::cancelIo(pending.read);
    }
    pendingTextures_.clear();
}

// **The deferred half of `syncTextures`** (D118). See the header for the
// measurement: a 1024-square PNG costs 14 to 36 ms to decode, and the
// synchronous path loads every missing map it finds in one frame with no budget
// at all -- so pointing a part at a four-map material froze the frame after the
// write for a tenth of a second.
//
// Three stages, and only the last one is on the frame, because only the frame
// has a command list. The same shape `StreamingHost::pump` and the content
// browser's thumbnails use, for the same reason.
core::u32 MeshLoader::pumpTextures(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world,
                                   TextureLibrary& library)
{
    u32 loaded = 0;

    // **Completions land during `pumpIo` and nowhere else** (`async_io.h`), so
    // this pumps rather than assuming somebody else did. `StreamingHost::pump`
    // and the content browser's thumbnails each pump their own for the same
    // reason: the service is a drain, draining it twice costs nothing, and a
    // subsystem that only worked when another one happened to be running is a
    // subsystem that works on the flagship and not on a fresh project.
    platform::pumpIo();

    const auto markFailed = [&](core::NameAtom urn) {
        const auto position = std::lower_bound(failed_.begin(), failed_.end(), urn,
                                               [](core::NameAtom a, core::NameAtom b) { return a.id < b.id; });
        if (position == failed_.end() || position->id != urn.id)
            failed_.insert(position, urn);
    };

    for (usize index = 0; index < pendingTextures_.size();) {
        PendingTexture& pending = pendingTextures_[index];
        const auto drop = [&] {
            pendingTextures_.erase(pendingTextures_.begin() + static_cast<std::ptrdiff_t>(index));
        };

        if (pending.read.valid()) {
            const platform::IoStatus status = platform::ioStatus(pending.read);
            if (status == platform::IoStatus::Pending) {
                ++index;
                continue;
            }

            pending.work = std::make_unique<TextureWork>();
            const bool got =
                status == platform::IoStatus::Ready && platform::takeIoResult(pending.read, pending.work->bytes);
            // **A request that ended without being TAKEN still holds its
            // slot** -- `takeIoResult` releases one only for `Ready`, and the
            // pool is a fixed 512. A project with missing files would fill it
            // and every later read would be refused, which reads as "textures
            // stopped loading after a while" and has nothing in the log.
            // `cancelIo` is the documented way to let a terminal one go.
            if (!got)
                platform::cancelIo(pending.read);
            pending.read = {};
            if (!got || pending.work->bytes.empty()) {
                // A material without its texture still draws, in its own
                // numbers. A material refused for a missing map would take the
                // surface with it.
                const std::array<core::I18nArg, 1> args{
                    core::I18nArg{"path", std::string(world.atoms().text(pending.urn))}};
                core::log(core::LogLevel::Warn, LUAUG_TR("render.err.material_texture_missing"), args);
                markFailed(pending.urn);
                drop();
                continue;
            }

            // **One pointer, to memory that does not move.** Another map asked
            // for between now and the job finishing reallocates
            // `pendingTextures_`, so anything the job addresses has to live
            // somewhere the vector is not.
            TextureWork* work = pending.work.get();
            pending.decode = jobs::schedule("texture-decode", jobs::Domain::AssetIo, [work]() noexcept {
                work->ok = !asset::decodeImage(work->bytes, work->image).has_value();
                // The encoded bytes are the biggest allocation in the pipeline
                // and nothing needs them again.
                work->bytes.clear();
                work->bytes.shrink_to_fit();
            });
            if (!pending.decode.valid()) {
                markFailed(pending.urn);
                drop();
                continue;
            }
            ++index;
            continue;
        }

        if (!jobs::finished(pending.decode)) {
            ++index;
            continue;
        }

        if (pending.work == nullptr || !pending.work->ok) {
            const std::array<core::I18nArg, 1> args{
                core::I18nArg{"path", std::string(world.atoms().text(pending.urn))}};
            core::log(core::LogLevel::Warn, LUAUG_TR("render.err.material_texture_missing"), args);
            markFailed(pending.urn);
            drop();
            continue;
        }

        const rhi::TextureHandle handle = uploadImage(device, cmd, pending.work->image, "material");
        if (!handle.valid()) {
            markFailed(pending.urn);
            drop();
            continue;
        }
        textures_.push_back(handle);
        library.set(pending.urn, handle);
        ++loaded;
        drop();
    }

    return loaded;
}

// Whether this URN is already on its way in. Linear over a list bounded by
// `MaxTexturesInFlight`, which is a handful.
bool MeshLoader::textureInFlight(core::NameAtom urn) const noexcept
{
    for (const PendingTexture& pending : pendingTextures_) {
        if (pending.urn == urn)
            return true;
    }
    return false;
}

core::u32 MeshLoader::syncTextures(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world,
                                   TextureLibrary& library)
{
    // Completions first, so a read that finished during the frame is uploaded in
    // the same one and the slot it frees is available to whatever the walk below
    // asks for. `StreamingHost::pump` orders its own pipeline the same way.
    core::u32 loaded = deferredTextures_ ? pumpTextures(device, cmd, world, library) : 0u;

    const auto load = [&](core::NameAtom urn) {
        if (urn.id == 0 || library.find(urn).valid())
            return;
        if (std::binary_search(failed_.begin(), failed_.end(), urn,
                               [](core::NameAtom a, core::NameAtom b) { return a.id < b.id; })) {
            return;
        }

        const std::string text(world.atoms().text(urn));
        // Remembered as failed before anything else can go wrong, so a map that
        // is not there costs one attempt rather than one per frame.
        const auto markFailed = [&]() {
            const auto position = std::lower_bound(failed_.begin(), failed_.end(), urn,
                                                   [](core::NameAtom a, core::NameAtom b) { return a.id < b.id; });
            failed_.insert(position, urn);
        };

        const asset::ResolvedContent resolved = mounts_ != nullptr ? mounts_->resolve(text) : asset::ResolvedContent{};
        const std::filesystem::path path =
            resolved.source == asset::ResolvedContent::Source::Loose ? resolved.path : resolve(contentRoot_, text);

        if (deferredTextures_) {
            // Queued rather than read, and only up to a bound: a world whose
            // materials name four hundred maps must not open four hundred files
            // and hold four hundred decoded images at once.
            if (textureInFlight(urn) || pendingTextures_.size() >= MaxTexturesInFlight)
                return;
            PendingTexture pending;
            pending.urn = urn;
            // `Normal`, above a thumbnail and below a chunk the camera is about
            // to reach: this is a surface somebody is looking at right now.
            pending.read = platform::readFileAsync(path, platform::IoPriority::Normal);
            if (pending.read.valid()) {
                pendingTextures_.push_back(std::move(pending));
                return;
            }
            // **No IO service, or its pool is full: fall through and read it
            // here.** Deferring is a way of doing this work, not a permission to
            // skip it -- a build where `initIo` failed must still show its
            // textures, and a frame that costs a decode is better than a
            // material that is white forever. The synchronous path below is the
            // one this mode is an optimisation OF.
        }

        std::vector<std::byte> bytes;
        asset::Image image;
        if (!platform::readFile(path, bytes) || asset::decodeImage(bytes, image).has_value()) {
            // A material without its texture still draws, in its own numbers. A
            // material refused for a missing map would take the surface with it.
            const std::array<core::I18nArg, 1> args{core::I18nArg{"path", path.string()}};
            core::log(core::LogLevel::Warn, LUAUG_TR("render.err.material_texture_missing"), args);
            markFailed();
            return;
        }

        const rhi::TextureHandle handle = uploadImage(device, cmd, image, "material");
        if (!handle.valid()) {
            markFailed();
            return;
        }
        textures_.push_back(handle);
        library.set(urn, handle);
        ++loaded;
    };

    world.materials().forEach([&](core::InstanceId id, const scene::MaterialComponent& material) {
        (void)id;
        load(material.colorMap);
        load(material.normalMap);
        load(material.metallicRoughnessMap);
        load(material.emissiveMap);
    });

    return loaded;
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
                     MeshCache& cache, MeshLibrary& library, SkeletonLibrary* skeletons,
                     std::vector<core::NameAtom>* completed)
{
    if (!root.valid())
        return 0;

    u32 loaded = 0;

    core::u32 meshesThisCall = 0;
    world.meshParts().forEach([&](core::InstanceId id, const scene::MeshPartComponent& meshPart) {
        (void)id;
        const core::NameAtom content = meshPart.meshContent;
        if (content.id == 0 || library.find(content) != nullptr)
            return;
        if (std::binary_search(failed_.begin(), failed_.end(), content,
                               [](core::NameAtom a, core::NameAtom b) { return a.id < b.id; }))
            return;

        const std::string urn(world.atoms().text(content));

        // Remembered as failed before anything else can go wrong, so every
        // early return below costs one attempt rather than one per frame.
        const auto markFailed = [&]() {
            const auto position = std::lower_bound(failed_.begin(), failed_.end(), content,
                                                   [](core::NameAtom a, core::NameAtom b) { return a.id < b.id; });
            failed_.insert(position, content);
        };

        // Two feeds, one library. A mounted pack answers with a compiled mesh
        // -- meshopt streams and transcodable textures, nothing to parse; a
        // content directory answers with the source file, parsed on the way in.
        // ADR 0010 keeps the second forever as the dev-mode path, and the first
        // is what a shipped game reads.
        const asset::ResolvedContent resolved = mounts_ != nullptr ? mounts_->resolve(urn) : asset::ResolvedContent{};

        // **One mesh per call while deferred** (D125). A parse is the largest
        // synchronous thing left in a frame -- measured at 191 ms for a
        // 60,000-vertex glTF with 677 joints, plus 21 ms to read it -- and
        // `sync` loaded EVERY missing mesh it found in one frame with no budget
        // at all, so dropping a folder of five models in meant one frame of
        // roughly a second.
        //
        // A budget of one rather than a millisecond count, because a parse
        // cannot be split: any budget needs a floor of one whole mesh, and one
        // whole mesh is already more than a frame. What this buys is that N
        // meshes cost N frames instead of one frame N times as long, which is
        // the difference between a tool that hitches and a tool that stops.
        //
        // **Why not on the job pool.** `jobs.h` rule 1 is explicit -- no
        // blocking IO on a worker -- and `importGltf` reads a glTF's external
        // `.bin` and `.png` files itself, from `baseDirectory`, as part of
        // parsing. The COMPILED path below has no external files and does go to
        // the pool; the loose path cannot until the importer can be handed
        // buffers somebody else read, which is a change to `asset` and is
        // recorded rather than smuggled in here. E9's whole direction -- a loose
        // `.gltf` stops working and everything arrives compiled -- retires this
        // caveat rather than fixing it.
        if (deferredMeshes_) {
            if (meshesThisCall > 0)
                return;
            ++meshesThisCall;
        }

        MeshLibrary::Entry entry;
        core::EngineError uploadError;
        core::u32 triangles = 0;

        if (resolved.source == asset::ResolvedContent::Source::Pack && resolved.kind == asset::AssetKind::Mesh) {
            asset::CompiledMesh compiled;
            if (auto error = asset::decodeMesh(resolved.bytes, compiled); error.has_value()) {
                core::logText(core::LogLevel::Warn, error->message);
                markFailed();
                return;
            }

            // THE WHOLE CHAIN, flattened into one index buffer with one section
            // list, and a range per level. One upload and one bind: choosing a
            // level is choosing a range of indices, never a different resource,
            // which is what keeps the selector free to change its mind every
            // frame without touching the GPU.
            //
            // A mesh with no chain -- one level -- lands here as one range and
            // draws byte-identically to how it did before this existed.
            asset::Mesh geometry;
            geometry.vertices = std::move(compiled.vertices);
            geometry.bounds = compiled.bounds;

            std::vector<MeshLodRange> lods;
            lods.reserve(compiled.lods.size());
            for (const asset::MeshLod& lod : compiled.lods) {
                const auto indexBase = static_cast<core::u32>(geometry.indices.size());
                const auto sectionBase = static_cast<core::u32>(geometry.submeshes.size());
                geometry.indices.insert(geometry.indices.end(), lod.indices.begin(), lod.indices.end());
                for (asset::Submesh submesh : lod.submeshes) {
                    // Rebased into the combined buffer. The submesh order is
                    // identical at every level (`asset/mesh_format.h`), so a
                    // draw that named section N of one level means section N of
                    // any other -- which is what makes the swap invisible
                    // upstream.
                    submesh.firstIndex += indexBase;
                    geometry.submeshes.push_back(submesh);
                }
                lods.push_back(MeshLodRange{
                    .firstSection = sectionBase,
                    .sectionCount = static_cast<core::u32>(lod.submeshes.size()),
                    .error = lod.error,
                });
            }

            // LEVEL ZERO's triangles, not the whole chain's: this number is
            // what a stats panel calls "the mesh", and counting every level
            // would report a mesh roughly twice the size of the one on screen.
            triangles = static_cast<core::u32>(compiled.lods[0].indices.size() / 3);

            const bool skinned = !compiled.joints.empty() && !compiled.skin.empty();
            // A SKINNED mesh takes level zero only, and that is a decision
            // rather than an omission: the joint and weight streams are indexed
            // by vertex and the simplifier is free to drop vertices, so a level
            // above zero would need its own skin stream re-derived. Characters
            // are also the last thing a game wants simplified, being the thing
            // the camera is usually nearest to.
            asset::Mesh skinnedGeometry;
            if (skinned) {
                skinnedGeometry.vertices = geometry.vertices;
                skinnedGeometry.bounds = geometry.bounds;
                skinnedGeometry.indices = compiled.lods[0].indices;
                skinnedGeometry.submeshes = compiled.lods[0].submeshes;
            }
            const MeshHandle handle =
                skinned ? cache.createSkinned(device, cmd, skinnedGeometry, compiled.skin, &uploadError)
                        : cache.create(device, cmd, geometry, MeshUsage::Static, &uploadError, lods);
            if (!handle.valid()) {
                core::logText(core::LogLevel::Warn, uploadError.message);
                markFailed();
                return;
            }
            entry.mesh = handle;

            std::vector<rhi::TextureHandle> images;
            images.reserve(compiled.images.size());
            for (const asset::TextureSlot& slot : compiled.images) {
                asset::TextureAsset texture;
                const std::span<const std::byte> blob = mounts_->blob(slot.hash);
                if (blob.empty() || asset::transcodeTexture(blob, transcode_, texture).has_value()) {
                    // A material without its texture still draws, tinted. A
                    // mesh refused for a missing texture would take the whole
                    // world with it.
                    images.push_back({});
                    continue;
                }
                const rhi::TextureHandle uploaded = uploadTranscoded(device, cmd, texture, "material");
                if (uploaded.valid())
                    textures_.push_back(uploaded);
                images.push_back(uploaded);
            }

            // LEVEL ZERO's submeshes, not the flattened list. `sectionCount`
            // is how many draws an instance emits, and every level has the same
            // submeshes in the same order -- so the flattened list would emit a
            // draw per section PER LEVEL and render the mesh several times over.
            fillEntry(entry, geometry.bounds, compiled.lods[0].submeshes, compiled.materials, images);
            entry.positions.reserve(geometry.vertices.size());
            for (const asset::Vertex& vertex : geometry.vertices)
                entry.positions.push_back(vertex.position);
            library.set(content, entry);
            if (completed != nullptr)
                completed->push_back(content);

            if (skeletons != nullptr && !compiled.joints.empty())
                skeletons->set(content, SkeletonLibrary::Entry{std::move(compiled.joints), std::move(compiled.clips)});
        }
        else {
            const std::filesystem::path path =
                resolved.source == asset::ResolvedContent::Source::Loose ? resolved.path : resolve(contentRoot_, urn);

            std::vector<std::byte> bytes;
            if (!platform::readFile(path, bytes)) {
                const std::array<core::I18nArg, 1> args{core::I18nArg{"path", path.string()}};
                core::log(core::LogLevel::Warn, LUAUG_TR("render.err.mesh_file_missing"), args);
                markFailed();
                return;
            }

            asset::Model model;
            // The palette is a fixed array in a uniform block, so the number of
            // joints this renderer can pose is a fact the importer needs: past it,
            // a rig is not posed badly, it indexes off the end and scatters.
            const asset::GltfImportOptions importOptions{.maxSkinJoints = kMaxSkinJoints};
            if (auto error = asset::importGltf(bytes, path.parent_path(), importOptions, model); error.has_value()) {
                // **The reason, and not only that there was one.** `MeshContent`
                // promises that a file which fails to import "reports why,
                // because a part that silently becomes invisible is harder to
                // diagnose than one that says it could not load" -- and this
                // line was throwing the why away. The message is the sentence a
                // catalog can translate; the detail is what the parser actually
                // objected to, which is the half somebody needs to fix a file.
                std::string reported = error->message;
                if (!error->detail.empty()) {
                    reported += " (";
                    reported += error->detail;
                    reported += ")";
                }
                reported += " -- ";
                reported += path.string();
                core::logText(core::LogLevel::Warn, reported);
                markFailed();
                return;
            }
            triangles = static_cast<core::u32>(model.mesh.indices.size() / 3);

            // **Said out loud, because a character that quietly stopped being
            // animatable is a bug report.** The numbers are the answer: this is
            // not a file that failed, it is a rig larger than anything this
            // build can pose, imported as the geometry it will always be.
            if (model.bakedBindPose()) {
                const core::I18nArg args[] = {
                    {"path", path.string()},
                    {"joints", static_cast<core::i64>(model.sourceJointCount)},
                    {"budget", static_cast<core::i64>(kMaxSkinJoints)},
                };
                core::log(core::LogLevel::Info, LUAUG_TR("render.info.mesh_bind_pose_baked"), args);
            }

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
            entry.mesh = handle;

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

            fillEntry(entry, model.mesh.bounds, model.mesh.submeshes, model.materials, images);
            entry.positions.reserve(model.mesh.vertices.size());
            for (const asset::Vertex& vertex : model.mesh.vertices)
                entry.positions.push_back(vertex.position);
            library.set(content, entry);
            if (completed != nullptr)
                completed->push_back(content);

            // The skeleton half of the same file, handed to whoever asked for
            // it. Read here rather than in a second pass because the file was
            // already parsed once and parsing it again to find the joints would
            // be the clearest kind of waste.
            if (skeletons != nullptr && !model.joints.empty())
                skeletons->set(content, SkeletonLibrary::Entry{std::move(model.joints), std::move(model.clips)});
        }

        ++loaded;

        const std::array<core::I18nArg, 2> args{
            core::I18nArg{"path", urn},
            core::I18nArg{"triangles", static_cast<core::i64>(triangles)},
        };
        core::log(core::LogLevel::Info, LUAUG_TR("render.info.mesh_loaded"), args);
    });

    return loaded;
}

} // namespace luaug::render
