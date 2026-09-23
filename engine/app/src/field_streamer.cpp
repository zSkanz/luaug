#include "luaug/app/field_streamer.h"

#include "luaug/app/streaming_host.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/platform/platform.h"
#include "luaug/scene/components.h"
#include "luaug/scene/voxel_fluid.h"
#include "luaug/scene/world.h"

#include <algorithm>

namespace luaug::app {
namespace {

[[nodiscard]] f64 millisecondsSince(u64 startedNs)
{
    return static_cast<f64>(platform::nowNs() - startedNs) / 1.0e6;
}

} // namespace

void FieldStreamer::setIndex(const asset::ChunkIndex& index, const CellResolver& resolve)
{
    m_paths.clear();
    for (const asset::ChunkIndexEntry& entry : index.chunks) {
        if (const std::optional<std::filesystem::path> path = resolve(entry); path.has_value())
            m_paths.emplace(entry.id, *path);
    }
    m_manager.setIndex(index);
    m_active = !index.chunks.empty();
    m_primed = !m_active;
    installCallbacks();
}

void FieldStreamer::setWorld(scene::World* world, core::InstanceId workspace)
{
    if (m_world == world && m_workspace == workspace)
        return;
    m_world = world;
    m_workspace = workspace;
    m_terrainCells.clear();
    m_voxelCells.clear();
    m_manager.forgetResidency();
    // A new world has none of its ground yet, and waits for it like the first.
    m_primed = !m_active;
    m_waitingSinceNs = platform::nowNs();
}

void FieldStreamer::installCallbacks()
{
    asset::StreamingCallbacks callbacks;
    callbacks.beginLoad = [this](asset::ChunkId id, const asset::ChunkIndexEntry&) {
        // Refused rather than failed when the IO service is saturated, on
        // `StreamingHost`'s terms: a full queue is not a broken cell.
        const platform::IoStats io = platform::ioStats();
        if (io.queued + io.inFlight >= platform::MaxIoRequests / 2)
            return false;
        // **High, above the parts' Normal**: a missing collider under a
        // character is a fall, and a missing prop is not.
        const auto path = m_paths.find(id);
        const platform::IoRequest request = path != m_paths.end()
                                                ? platform::readFileAsync(path->second, platform::IoPriority::High)
                                                : platform::IoRequest{};
        // A start that failed is reported AFTER the tick (D158): reported here,
        // the manager would mark the cell loading as this returns and it would
        // wait for a read that never began.
        if (!request.valid())
            m_failedStarts.push_back(id);
        else
            m_reads.emplace_back(request, id);
        return true;
    };
    callbacks.materializeBytes = [this](asset::ChunkId id, std::span<const std::byte> bytes) {
        return materialize(id, bytes);
    };
    callbacks.evict = [this](asset::ChunkId id) { evict(id); };
    m_manager.setCallbacks(std::move(callbacks));
}

scene::TerrainComponent* FieldStreamer::terrain() const
{
    // The one directly under the workspace, which is where a `Terrain` lives.
    scene::TerrainComponent* found = nullptr;
    m_world->terrains().forEach([&](core::InstanceId id, scene::TerrainComponent& component) {
        if (found == nullptr && m_world->parentOf(id) == m_workspace)
            found = &component;
    });
    return found;
}

scene::VoxelComponent* FieldStreamer::voxels() const
{
    scene::VoxelComponent* found = nullptr;
    m_world->voxels().forEach([&](core::InstanceId, scene::VoxelComponent& component) {
        if (found == nullptr)
            found = &component;
    });
    return found;
}

f64 FieldStreamer::materialize(asset::ChunkId id, std::span<const std::byte> bytes)
{
    const u64 started = platform::nowNs();
    if (id.layer == asset::FieldLayerTerrain) {
        asset::TerrainCell cell;
        if (asset::decodeTerrainCell(bytes, cell).has_value())
            return -1.0;
        // A world whose script removed its terrain has nowhere to put the
        // ground; the cell counts as resident, and costs what reading it did.
        if (scene::TerrainComponent* component = terrain(); component != nullptr) {
            component->field.shareFrom(cell.field);
            component->fieldRevision += 1;
            m_terrainCells[id] = std::move(cell);
        }
        return millisecondsSince(started);
    }

    asset::VoxelCell cell;
    if (asset::decodeVoxelCell(bytes, cell).has_value())
        return -1.0;
    if (scene::VoxelComponent* component = voxels(); component != nullptr) {
        component->grid.shareFrom(cell.grid);
        component->revision += 1;
        // Its water was saved without the steps it was due. A still lake
        // settles in one look; water that moves makes the cell one somebody
        // changed, which is what it now is.
        scene::wakeFluidsIn(*component, cell.grid);
        m_voxelCells[id] = std::move(cell);
    }
    return millisecondsSince(started);
}

void FieldStreamer::evict(asset::ChunkId id)
{
    if (id.layer == asset::FieldLayerTerrain) {
        const auto held = m_terrainCells.find(id);
        if (held == m_terrainCells.end())
            return;
        if (scene::TerrainComponent* component = terrain(); component != nullptr) {
            const core::u32 across = asset::terrainCellTiles(held->second.settings.voxelSize);
            if (asset::terrainCellUntouched(component->field, held->second, across)) {
                asset::removeTerrainCell(component->field, held->second);
                component->fieldRevision += 1;
            }
            else {
                ++m_kept;
            }
        }
        m_terrainCells.erase(held);
        return;
    }

    const auto held = m_voxelCells.find(id);
    if (held == m_voxelCells.end())
        return;
    if (scene::VoxelComponent* component = voxels(); component != nullptr) {
        const core::u32 across = asset::voxelCellChunks(held->second.blockSize);
        if (asset::voxelCellUntouched(component->grid, held->second, across)) {
            asset::removeVoxelCell(component->grid, held->second);
            component->revision += 1;
        }
        else {
            ++m_kept;
        }
    }
    m_voxelCells.erase(held);
}

void FieldStreamer::pump(f64 budgetMilliseconds)
{
    if (!m_active || m_world == nullptr)
        return;
    const u64 started = platform::nowNs();

    // Finished reads, inside the budget, on `StreamingHost::pump`'s terms
    // (D127 and D131: the drain is budgeted, and a failed read gives its slot
    // back).
    platform::pumpIo();
    for (std::size_t index = 0; index < m_reads.size();) {
        if (budgetMilliseconds > 0.0 && millisecondsSince(started) >= budgetMilliseconds)
            break;
        const platform::IoStatus status = platform::ioStatus(m_reads[index].first);
        if (status == platform::IoStatus::Pending) {
            ++index;
            continue;
        }
        std::vector<std::byte> bytes;
        if (status == platform::IoStatus::Ready && platform::takeIoResult(m_reads[index].first, bytes)) {
            m_manager.onChunkLoaded(m_reads[index].second, bytes);
        }
        else {
            platform::cancelIo(m_reads[index].first);
            m_manager.onChunkFailed(m_reads[index].second);
        }
        m_reads.erase(m_reads.begin() + static_cast<std::ptrdiff_t>(index));
    }

    // **The terrain radii for both kinds of cell.** `TerrainLoadRadius` and
    // `TerrainMinRadius` have named "cells of terrain" since they were
    // reserved; the block world is ground too. A zero there follows the focus's
    // own pair, which is the rule every layer has.
    std::vector<asset::StreamingFocus> foci = collectStreamingFoci(*m_world, m_workspace);
    for (asset::StreamingFocus& focus : foci) {
        focus.layers[asset::FieldLayerTerrain] = focus.layers[2];
        focus.layers[asset::FieldLayerVoxels] = focus.layers[2];
    }
    m_manager.setFoci(foci);
    m_manager.setEnabled(m_world->engineState().streamingEnabled);

    asset::StreamingBudget budget;
    budget.milliseconds = std::max(0.0, budgetMilliseconds - millisecondsSince(started));
    // **Thirty-two reads open, not the parts' eight.** A cell of ground is a
    // few kilobytes and the minimum ring is a couple of hundred of them, all of
    // which the simulation is waiting for; eight at a time made the first load
    // a matter of seconds for nothing but queueing.
    budget.maxInFlight = 32;
    m_manager.tick(budget);
    for (const asset::ChunkId id : m_failedStarts)
        m_manager.onChunkFailed(id);
    m_failedStarts.clear();

    if (!m_primed && !foci.empty() && m_manager.minimumRingResident()) {
        m_primed = true;
        // Said once, because how long the first load took is the number a
        // person tuning `TerrainMinRadius` needs.
        const core::I18nArg args[] = {{"cells", static_cast<core::i64>(m_manager.stats().chunksLoaded)},
                                      {"ms", static_cast<core::i64>(millisecondsSince(m_waitingSinceNs))}};
        core::log(core::LogLevel::Info, LUAUG_TR("app.info.field_primed"), args);
    }
}

} // namespace luaug::app
