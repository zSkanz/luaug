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

#include <filesystem>

#include "luaug/core/id.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/render_world.h"
#include "luaug/rhi/device.h"

namespace luaug::scene
{
class World;
}

namespace luaug::render
{

class MeshLoader
{
public:
    // `contentRoot` is what an `asset://` URN resolves against -- the project's
    // own content directory, so a URN means the same thing to the engine as it
    // does in the file the developer wrote.
    void setContentRoot(std::filesystem::path root);

    // Loads every `MeshPart` content the world names and the library does not
    // yet hold. Call at the FrameStart safe point with a command list open and
    // no render pass: uploads are copies, and a copy cannot run inside a pass.
    //
    // Returns how many meshes it loaded this call, which is zero on the frames
    // that matter -- a non-zero count every frame means something is asking for
    // a file that keeps failing, and the log will say which.
    core::u32 sync(
        rhi::IDevice& device,
        rhi::ICmdList& cmd,
        const scene::World& world,
        core::InstanceId root,
        MeshCache& cache,
        MeshLibrary& library);

    // Releases every GPU resource this loader created. The cache's meshes are
    // the cache's to free; the textures are this one's.
    void destroy(rhi::IDevice& device);

private:
    std::filesystem::path contentRoot_;
    // Content URNs that failed to load, so a broken file costs one attempt and
    // one message rather than one of each per frame forever. Sorted, for the
    // same reason `MeshLibrary` is: R10 forbids an unordered container's order
    // reaching observable output, and a log is observable.
    std::vector<core::NameAtom> failed_;
    std::vector<rhi::TextureHandle> textures_;
};

} // namespace luaug::render
