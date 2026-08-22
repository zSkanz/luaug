#include "luaug/scene/streaming_glue.h"

#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <chrono>
#include <string>

namespace luaug::scene {
namespace {

[[nodiscard]] core::f64 nowMs() noexcept
{
    // A frame budget rather than simulation state, exactly as
    // `asset::StreamingManager` explains: which instances a chunk creates is
    // decided by the chunk, and only how many chunks fit in a frame depends on
    // the clock.
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<core::f64>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()) / 1.0e6;
}

} // namespace

StreamingGlue::StreamingGlue(World& world, core::InstanceId root) : m_world(world), m_root(root)
{}

core::f64 StreamingGlue::materialize(asset::ChunkId id, const asset::Chunk& chunk)
{
    const core::f64 started = nowMs();

    if (m_chunks.find(id) != m_chunks.end()) {
        // Already resident. Not an error: a manager that asked twice would be a
        // bug, but silently building a second copy of a chunk would be a world
        // with two of everything and no way to tell.
        return 0.0;
    }

    const ClassId folderClass = m_world.classes().findId(m_world.atoms().intern("Folder"));
    const ClassId partClass = m_world.classes().findId(m_world.atoms().intern("Part"));
    // Resolved but not required: a chunk of nothing but `Part`s must not fail
    // because a registry has no `MeshPart` in it. A class is a dependency of
    // the instances that need it, not of the chunk that might have had one.
    const ClassId meshPartClass = m_world.classes().findId(m_world.atoms().intern("MeshPart"));
    if (folderClass == InvalidClass || partClass == InvalidClass) {
        return 0.0;
    }

    Resident resident;
    // A folder per chunk, named for its coordinates. Not decoration: it is what
    // makes a streamed world navigable in the explorer, and it is what eviction
    // removes when nothing inside it is held.
    resident.folder = m_world.create(folderClass);
    if (!resident.folder.valid()) {
        return 0.0;
    }
    const std::string folderName =
        "Chunk_" + std::to_string(id.x) + "_" + std::to_string(id.z) + "_" + std::to_string(id.layer);
    m_world.setName(resident.folder, m_world.atoms().intern(folderName));
    (void)m_world.setParent(resident.folder, m_root);
    // Made by streaming, not authored by anybody, so a scene does not record it.
    // Marking the folder is enough: the serializer skips a marked instance and
    // everything under it, and the parts inside were never separately written
    // down either.
    m_world.setGenerated(resident.folder, true);

    resident.instances.reserve(chunk.instances.size());
    for (const asset::ChunkInstance& source : chunk.instances) {
        const bool isMesh = source.kind == asset::ChunkInstance::Kind::MeshPart && meshPartClass != InvalidClass;
        const core::InstanceId id2 = m_world.create(isMesh ? meshPartClass : partClass);
        if (!id2.valid()) {
            continue;
        }

        if (PartComponent* part = m_world.parts().find(id2); part != nullptr) {
            part->cframe = source.cframe;
            part->size = source.size;
            part->color = source.color;
            part->transparency = source.transparency;
            part->shape = static_cast<core::i32>(source.shape);
        }
        if (isMesh) {
            if (MeshPartComponent* mesh = m_world.meshParts().find(id2); mesh != nullptr) {
                mesh->meshContent = m_world.atoms().intern(chunk.stringAt(source.meshContent));
            }
        }
        if (RigidBodyComponent* body = m_world.rigidBodies().find(id2); body != nullptr) {
            body->anchored = source.anchored;
        }

        const std::string_view name = chunk.stringAt(source.name);
        if (!name.empty()) {
            m_world.setName(id2, m_world.atoms().intern(name));
        }

        (void)m_world.setParent(id2, resident.folder);
        resident.instances.push_back(id2);
    }

    m_residentInstances += static_cast<u32>(resident.instances.size());
    m_chunks.emplace(id, std::move(resident));
    return nowMs() - started;
}

void StreamingGlue::evict(asset::ChunkId id)
{
    const auto at = m_chunks.find(id);
    if (at == m_chunks.end()) {
        return;
    }

    for (const core::InstanceId instance : at->second.instances) {
        // §4's contract, and the reason this is not just a destroy: an instance
        // a script still holds would leave the Luau side with a handle to
        // nothing, so it is reparented to nil and becomes a husk. That is
        // exactly what `StreamingService.InstanceStreamedOut` reports.
        if (m_probe && m_probe(instance)) {
            (void)m_world.setParent(instance, core::InstanceId{});
            m_streamedOut.push_back(instance);
            m_husksCreated += 1;
            continue;
        }
        (void)m_world.destroy(instance);
    }

    m_residentInstances -= std::min<u32>(m_residentInstances, static_cast<u32>(at->second.instances.size()));

    // The folder goes only when it is EMPTY, which is a narrower condition than
    // "nothing was kept": a husk reparented to nil has already left it, but
    // something a script parented in there has not -- and destroying the folder
    // out from under that is how a streamed world eats things that were never
    // its to remove.
    if (m_world.childCount(at->second.folder) == 0) {
        (void)m_world.destroy(at->second.folder);
    }

    m_chunks.erase(at);
}

std::vector<core::InstanceId> StreamingGlue::drainStreamedOut()
{
    std::vector<core::InstanceId> drained;
    drained.swap(m_streamedOut);
    return drained;
}

void StreamingGlue::clear()
{
    for (const auto& entry : m_chunks) {
        for (const core::InstanceId instance : entry.second.instances) {
            (void)m_world.destroy(instance);
        }
        (void)m_world.destroy(entry.second.folder);
    }
    m_chunks.clear();
    m_streamedOut.clear();
    m_residentInstances = 0;
}

} // namespace luaug::scene
