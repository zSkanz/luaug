// Where a chunk becomes instances (architecture.md §10, "budgeted
// materialization ... through the same reflection path as scripts").
//
// The streaming MANAGER decides which chunks should be resident; this decides
// what "resident" means to the world. The split is what lets the policy be
// tested with three lambdas and no scene, and it is why this file knows about
// chunks and the manager does not know about instances.
//
// **Eviction honours §4.** An instance a script still holds a reference to
// cannot simply be destroyed -- the Luau side would be left with a handle to
// nothing -- so it is reparented to nil and becomes a husk, exactly the
// contract `StreamingService.InstanceStreamedOut` documents. Everything else is
// destroyed outright, because a world that never frees is a world with a
// memory ceiling it cannot meet.
#pragma once

#include "luaug/asset/chunk.h"
#include "luaug/core/id.h"
#include "luaug/core/types.h"

#include <functional>
#include <unordered_map>
#include <vector>

namespace luaug::scene {

class World;

using core::u32;
using core::u64;
using core::usize;

class StreamingGlue
{
public:
    StreamingGlue(World& world, core::InstanceId root);

    // Creates a chunk's instances under a folder of its own, and returns the
    // milliseconds it took -- which is what the manager charges its frame
    // budget with. Measuring here rather than there is deliberate: only this
    // side knows how much of the cost was reflection and how much was the
    // hierarchy.
    core::f64 materialize(asset::ChunkId id, const asset::Chunk& chunk);

    // Removes them again. An instance carrying `Persistent` survives, and one a
    // script still references becomes a husk rather than a dangling id.
    void evict(asset::ChunkId id);

    // Asked before evicting. Returns true when something outside the streaming
    // system still holds the instance, in which case it is reparented to nil
    // instead of destroyed. Injected because `scene` cannot see the VM: whether
    // a Luau value references an instance is `script`'s knowledge.
    void setReferenceProbe(std::function<bool(core::InstanceId)> probe) { m_probe = std::move(probe); }

    // Fired for every instance that became a husk, drained by the host into the
    // deferred signal `StreamingService.InstanceStreamedOut` names.
    [[nodiscard]] std::vector<core::InstanceId> drainStreamedOut();

    [[nodiscard]] usize residentChunks() const noexcept { return m_chunks.size(); }
    [[nodiscard]] u32 residentInstances() const noexcept { return m_residentInstances; }
    [[nodiscard]] u64 husksCreated() const noexcept { return m_husksCreated; }

    // Everything this glue put in the world, for a host tearing a world down.
    void clear();

private:
    struct Resident
    {
        core::InstanceId folder;
        std::vector<core::InstanceId> instances;
    };

    struct ChunkKey
    {
        [[nodiscard]] usize operator()(const asset::ChunkId& id) const noexcept
        {
            // Three small integers into one hash. Multiplicative rather than
            // xor, because a world's chunk ids differ in low bits and an xor
            // would collide (3, 5) with (5, 3).
            usize hash = static_cast<usize>(static_cast<u32>(id.x));
            hash = hash * 1000003u + static_cast<usize>(static_cast<u32>(id.z));
            hash = hash * 1000003u + static_cast<usize>(static_cast<u32>(id.layer));
            return hash;
        }
    };

    struct ChunkEqual
    {
        [[nodiscard]] bool operator()(const asset::ChunkId& a, const asset::ChunkId& b) const noexcept
        {
            return a == b;
        }
    };

    World& m_world;
    core::InstanceId m_root;
    // Keyed by chunk, and the ITERATION order of this map never reaches
    // observable output: eviction is driven by the manager's sorted list and
    // every read here is a lookup (R10).
    std::unordered_map<asset::ChunkId, Resident, ChunkKey, ChunkEqual> m_chunks;
    std::function<bool(core::InstanceId)> m_probe;
    std::vector<core::InstanceId> m_streamedOut;
    u32 m_residentInstances = 0;
    u64 m_husksCreated = 0;
};

} // namespace luaug::scene
