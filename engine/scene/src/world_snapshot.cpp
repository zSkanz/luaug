// `World::snapshot` and `World::restore` (ADR 0016, and the promises
// `components.h`, `component_pool.h`, `value.h` and `enum_registry.h` were
// written against).
//
// There is no traversal here and that is the point. Every piece of a world's
// state is either a dense array of POD, a slot map of trivially copyable
// records, or a flat map of vectors -- so both directions are assignments, and
// the cost is proportional to what the world holds rather than to how deeply it
// is nested. The design work happened in the headers; this file is what those
// headers were for.
#include "luaug/scene/world.h"

namespace luaug::scene {

WorldSnapshot World::snapshot() const
{
    WorldSnapshot out;
    eachPool(*this, out, [](const auto& pool, auto& target) { target = pool; });

    out.instances = m_instances;
    out.tagged = m_tagged;
    out.pendingRetire = m_pendingRetire;
    out.streamingFoci = m_streamingFoci;
    out.collisionGroups = m_collisionGroups;
    out.engineState = m_engineState;
    out.rngState = m_rng.state();
    out.rngIncrement = m_rng.increment();

    // The change queue is deliberately absent. A snapshot is taken at a frame
    // boundary where the queue is empty, and a queue captured anywhere else
    // would be a list of facts about a world the restore is about to replace.
    return out;
}

void World::restore(const WorldSnapshot& snapshot)
{
    eachPool(*this, snapshot, [](auto& pool, const auto& source) { pool = source; });

    // Not an assignment: the instance map is the one container where putting
    // the bytes back is not enough, because generations decide what an
    // outstanding handle means. See `SlotMap::restoreFrom`.
    m_instances.restoreFrom(snapshot.instances);

    m_tagged = snapshot.tagged;
    m_pendingRetire = snapshot.pendingRetire;
    m_streamingFoci = snapshot.streamingFoci;
    m_collisionGroups = snapshot.collisionGroups;
    m_engineState = snapshot.engineState;
    m_rng.setState(snapshot.rngState, snapshot.rngIncrement);

    // Cleared rather than restored, and cleared rather than left alone: the
    // entries describe instances that may no longer exist, and the only thing
    // that reads them is the VM the caller rebuilds after this returns.
    m_changes.clear();
}

} // namespace luaug::scene
