// Turning a `MeshPart`'s content URN into geometry on the GPU (roadmap M4).
//
// This is the runtime import path ADR 0010 keeps forever as the dev-mode one.
// It lives in `render` rather than in `app` because the result is GPU geometry
// and the renderer owns that; `asset` produces CPU-side data and knows nothing
// about a device, which is architecture.md §2's split.
//
// **Loading is synchronous, and that is a deliberate narrowing of this
// milestone's own brief.** Decision 1 said loads would go on the `jobs` pool
// with the result applied at the FrameStart safe point. What is here reads the
// file on the calling thread -- at that safe point, so nothing mutates
// mid-frame, but without the pool. The reason is that the pool buys latency
// hiding for a streaming system, M4 has no streaming, and a background loader
// with one caller and no eviction policy is the speculative half of the design.
// M7 is the milestone that has something to stream and is where this grows a
// queue.
#pragma once

#include "luaug/asset/content.h"
#include "luaug/asset/texture.h"
#include "luaug/core/id.h"
#include "luaug/jobs/jobs.h"
#include "luaug/platform/async_io.h"
#include "luaug/render/animation.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/render_world.h"
#include "luaug/rhi/device.h"

#include <filesystem>
#include <memory>

namespace luaug::scene {
class World;
}

namespace luaug::render {

class MeshLoader
{
public:
    // `contentRoot` is what an `asset://` URN resolves against -- the project's
    // own content directory, so a URN means the same thing to the engine as it
    // does in the file the developer wrote.
    void setContentRoot(std::filesystem::path root);

    // Where a URN resolves when a pack is mounted. Optional and not owned: a
    // host with no packs -- every example before M7, the capture harness --
    // leaves it null and gets exactly the loose-file path it had. Two feeds,
    // one library.
    void setContentMounts(const asset::ContentMounts* mounts) noexcept { mounts_ = mounts; }

    // What the device can sample. The default allows BC7, which is the whole
    // point of shipping transcodable textures; a headless or capture run sets
    // `forceUncompressed` so a golden does not depend on a GPU's format
    // support.
    void setTranscodeOptions(const asset::TranscodeOptions& options) noexcept { transcode_ = options; }

    // Loads every `MeshPart` content the world names and the library does not
    // yet hold. Call at the FrameStart safe point with a command list open and
    // no render pass: uploads are copies, and a copy cannot run inside a pass.
    //
    // Returns how many meshes it loaded this call, which is zero on the frames
    // that matter -- a non-zero count every frame means something is asking for
    // a file that keeps failing, and the log will say which.
    // `skeletons` is where a skinned file's joints and clips go. A pointer
    // because a caller that draws but does not simulate -- a screenshot tool, a
    // capture harness -- has nowhere to put them, and because the library
    // belongs to the HOST rather than to the renderer: animation advances on the
    // SimClock and has to run in a headless replay.
    core::u32 sync(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world, core::InstanceId root,
                   MeshCache& cache, MeshLibrary& library, SkeletonLibrary* skeletons = nullptr);

    // Loads every texture the world's `Material` instances name and the library
    // does not yet hold. Same safe point, same rules, same return as `sync`.
    //
    // Over the MATERIAL pool rather than over the parts: two parts sharing a
    // material would otherwise be asked about twice, and a material nothing
    // points at yet -- one somebody is building -- would never load its maps and
    // would look broken in the property panel that is showing it.
    //
    // **The URNs name SOURCE images** -- the `.png` the artist shipped -- and
    // this decodes and uploads them. That is the dev-mode path ADR 0010 keeps
    // forever; a shipped game's textures arrive compiled and named by hash.
    core::u32 syncTextures(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world,
                           TextureLibrary& library);

    // **Whether a texture may be read and decoded off the frame.**
    //
    // Measured on ordinary 1024-square PNGs out of a texture pack: 14 to 36 ms
    // to decode, each. `syncTextures` loads every missing map it finds in one
    // frame and has no budget at all, so pointing a part at a material with
    // four maps costs a hundred milliseconds on the frame after the write. That
    // is a freeze on exactly the frame somebody changed something, which is how
    // an editor that measures fine comes to feel like it reloads the world
    // every time you touch it. A 4K source is sixteen times worse.
    //
    // Deferred, the read goes to `platform::readFileAsync`, the decode goes to
    // the job pool, and only the upload happens on the frame -- the same three
    // stages the streaming host and the browser's thumbnails already use, and
    // for the same measurement.
    //
    // **Off by default, and that default is what keeps every golden
    // byte-identical.** A capture records the frame it was told to record, and a
    // texture that arrives two frames later is a different picture; the same is
    // true of a screenshot gate and of any headless run whose output is
    // compared. Those all want the loader finished before the frame is. An
    // interactive shell wants the opposite, and says so.
    void setDeferredTextures(bool deferred) noexcept { deferredTextures_ = deferred; }

    // How many textures are being read or decoded right now. Zero in the
    // synchronous mode, always.
    [[nodiscard]] core::usize texturesInFlight() const noexcept { return pendingTextures_.size(); }

    // How many maps may be on their way in at once. Bounded because a world
    // whose materials name four hundred textures must not open four hundred
    // files and hold four hundred decoded images at the same time -- and because
    // the pictures somebody is looking at now are worth more than the ones two
    // rooms away, which a queue of four hundred cannot express.
    static constexpr core::usize MaxTexturesInFlight = 4;

    // Builds and uploads the five `Enum.PartShape` solids and registers them in
    // `library` under their reserved URNs (`primitiveContent`). Idempotent: the
    // second call does nothing, which is what makes it safe to put at the top of
    // a per-frame `sync`.
    //
    // It is here rather than in the renderer because this is the module that
    // already turns geometry into GPU buffers -- and because the whole point of
    // M4's constraint is that generated geometry takes the SAME route an
    // imported mesh does. Interning the names needs a mutable atom table, which
    // is why this takes the world rather than a const reference to one.
    void syncPrimitives(rhi::IDevice& device, rhi::ICmdList& cmd, scene::World& world, MeshCache& cache,
                        MeshLibrary& library);

    // Releases every GPU resource this loader created. The cache's meshes are
    // the cache's to free; the textures are this one's.
    void destroy(rhi::IDevice& device);

    // Waits for any decode still running, because one is writing into memory
    // this object owns. See the definition: abandoning it is a use-after-free at
    // shutdown, which is the hardest kind to attribute.
    ~MeshLoader();

    MeshLoader() = default;
    MeshLoader(const MeshLoader&) = delete;
    MeshLoader& operator=(const MeshLoader&) = delete;

private:
    // The three stages of a deferred load, run once a frame from `syncTextures`.
    core::u32 pumpTextures(rhi::IDevice& device, rhi::ICmdList& cmd, const scene::World& world,
                           TextureLibrary& library);
    [[nodiscard]] bool textureInFlight(core::NameAtom urn) const noexcept;
    void releasePendingTextures() noexcept;

    std::filesystem::path contentRoot_;
    const asset::ContentMounts* mounts_ = nullptr;
    asset::TranscodeOptions transcode_;
    bool primitivesUploaded_ = false;
    bool deferredTextures_ = false;

    // Everything a decode job touches, in ONE heap allocation.
    //
    // **Not a field of `PendingTexture`, and that is the whole point.** The
    // vector below grows whenever another map is asked for, which can happen on
    // the same frame a job is running -- so a job holding the address of
    // anything inside an element would be writing into freed memory after the
    // reallocation. Three fields on the heap together is one indirection and no
    // way to get it wrong; three pointers into a vector is three chances to.
    struct TextureWork
    {
        std::vector<std::byte> bytes;
        asset::Image image;
        bool ok = false;
    };

    // One texture on its way in.
    struct PendingTexture
    {
        core::NameAtom urn;
        platform::IoRequest read;
        jobs::JobHandle decode;
        std::unique_ptr<TextureWork> work;
    };
    std::vector<PendingTexture> pendingTextures_;
    // Content URNs that failed to load, so a broken file costs one attempt and
    // one message rather than one of each per frame forever. Sorted, for the
    // same reason `MeshLibrary` is: R10 forbids an unordered container's order
    // reaching observable output, and a log is observable.
    std::vector<core::NameAtom> failed_;
    std::vector<rhi::TextureHandle> textures_;
};

} // namespace luaug::render
