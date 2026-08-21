// Where the streaming pieces meet (roadmap M7, architecture.md §10).
//
// Four modules own a quarter of this each and none of them can see the others:
// `asset` decides which chunks should be resident, `platform` reads them,
// `scene` turns them into instances, and `script` holds the coroutine a
// `LoadAreaAsync` parked. This is the one place that knows all four exist,
// which is what `app` is for.
//
// It also owns the origin. The trigger for a rebase is a focus leaving its
// tolerance, and the focus set is streaming's -- so the decision belongs here
// rather than in the physics mirror, which has no idea where the camera is.
#pragma once

#include "luaug/asset/chunk.h"
#include "luaug/asset/streaming.h"
#include "luaug/core/id.h"
#include "luaug/core/types.h"
#include "luaug/platform/async_io.h"
#include "luaug/scene/streaming_glue.h"

#include <filesystem>
#include <map>
#include <memory>
#include <vector>

namespace luaug::asset {
class ContentMounts;
}

namespace luaug::scene {
class World;
class PhysicsSync;
} // namespace luaug::scene

namespace luaug::app {

using core::f64;
using core::u32;
using core::u64;
using core::usize;

class StreamingHost
{
public:
    // False when the project has no chunk index, which is every project before
    // this milestone and most projects after it. Not an error: a world that
    // fits in memory does not need streaming, and a host that logged a warning
    // for its absence would warn on every example in the tree.
    [[nodiscard]] bool load(const asset::ContentMounts& mounts, const std::filesystem::path& indexPath);

    // Idempotent, and that is the contract rather than an optimisation. The
    // frame loop calls this every frame so that a hot reload's new world is
    // picked up at the safe point -- and the glue is where the record of WHICH
    // instances belong to which resident chunk lives, so rebuilding it on an
    // unchanged world throws that record away. The manager goes on believing
    // those chunks are resident and asks to evict them; the fresh glue has
    // never heard of them and does nothing. That was D032: a world that grew by
    // a thousand instances every fifteen seconds and never shrank.
    void setWorld(scene::World* world, core::InstanceId streamRoot);
    void setPhysics(scene::PhysicsSync* physics) noexcept { m_physics = physics; }

    [[nodiscard]] bool active() const noexcept { return m_active; }

    // One frame. Reads the service's knobs and focus set out of the world,
    // scores, issues reads, materialises inside the budget, and rebases the
    // origin when the primary focus has drifted too far.
    void pump(f64 budgetMilliseconds);

    // Instances that became husks this frame, for the host to turn into
    // deferred `InstanceStreamedOut` fires.
    [[nodiscard]] std::vector<core::InstanceId> drainStreamedOut();

    // A `LoadAreaAsync` whose area has arrived. The host resumes the coroutine
    // and fires `AreaLoaded`; deciding WHICH are ready is this class's job
    // because only it knows what is resident.
    [[nodiscard]] bool areaResident(core::DVec3 position, f64 radius) const;

    [[nodiscard]] const asset::StreamingStats& stats() const noexcept { return m_manager.stats(); }
    [[nodiscard]] std::vector<asset::StreamingManager::ChunkView> view() const { return m_manager.view(); }
    [[nodiscard]] u64 rebases() const noexcept { return m_rebases; }

    // Wall-clock milliseconds the last `pump` spent, for the soak gate.
    //
    // The gate M7 owes is "zero hitches ATTRIBUTABLE to streaming", and this is
    // the attribution. A whole-frame time cannot make that claim: on a shared CI
    // runner most of a long frame is the runner, and a gate that fails on the
    // host machine being busy is a gate everyone learns to re-run.
    [[nodiscard]] f64 lastPumpMilliseconds() const noexcept { return m_lastPumpMs; }

    // Every chunk inside every focus's minimum ring is resident.
    [[nodiscard]] bool minimumRingResident() const noexcept { return m_manager.minimumRingResident(); }

private:
    void beginRead(asset::ChunkId id, const asset::ChunkIndexEntry& entry);
    [[nodiscard]] std::vector<asset::StreamingFocus> collectFoci() const;

    asset::StreamingManager m_manager;
    std::unique_ptr<scene::StreamingGlue> m_glue;
    const asset::ContentMounts* m_mounts = nullptr;
    // Every chunk's file, resolved ONCE when the index is read (D039).
    //
    // Resolution touches the filesystem -- it has to open the candidate to know
    // whether it is there -- and doing that inside the pump put a filesystem
    // call on the frame thread once per chunk load. On a slow filesystem that is
    // ten to thirty milliseconds of hitch for a question whose answer was fixed
    // when the project was built. Keyed by chunk id; an entry that is absent is
    // a chunk whose file the index names and the build did not produce.
    std::map<asset::ChunkId, std::filesystem::path> m_chunkPaths;
    scene::World* m_world = nullptr;
    // Half of `setWorld`'s identity check. The root matters as much as the
    // world: the same `World` re-rooted elsewhere is a different mount, and a
    // glue still holding the old root would parent chunks into a folder that is
    // no longer the stream root.
    core::InstanceId m_streamRoot;
    scene::PhysicsSync* m_physics = nullptr;
    bool m_active = false;
    u64 m_rebases = 0;
    f64 m_lastPumpMs = 0.0;

    // Which chunk an outstanding read belongs to. The IO service answers with
    // its own handle and nothing else, and a chunk id does not fit in a
    // callback that was declared before chunks existed.
    struct Pending
    {
        asset::ChunkId id;
    };
    std::vector<std::pair<platform::IoRequest, Pending>> m_reads;
};

} // namespace luaug::app
