// Terrain and block worlds streamed from disk (ADR 0075; F1 Part E).
//
// **The same policy as parts, over a grid of its own.** A partition cuts the
// field into cells of about 64 metres (`asset/field_cells.h`) and this streams
// them with an `asset::StreamingManager` of its own -- its own index, because a
// `ChunkId` on this grid names a different square from the same id on the
// parts' 256 m one -- scored by the same squared distance, evicted by the same
// hysteresis, and held to the same frame budget.
//
// **What goes in, and what comes out.** A cell that arrives is shared into the
// live field: what the field already holds there wins, because it is newer.
// The cell is then KEPT, and that second reference is the whole of how an edit
// is noticed -- the field is copy-on-write, so the first write to anything the
// cell brought clones it, and at eviction a cell whose objects are no longer
// the ones it brought, or whose square holds something it did not bring, is
// left where it is. Nobody raises a flag, so nobody can forget to: a cell a
// script dug into, or a player built on, is never streamed away with the work.
//
// **The ground arrives before the simulation runs on it.** Until the minimum
// ring around every focus has been resident once, `primed` is false, and the
// host holds its ticks -- the initial load every streamed world has, so a
// character spawned on streamed ground does not fall through it on frame one.
// After that, `StreamingService.PauseOutsideLoadedArea` decides, as it does
// for parts.
#pragma once

#include "luaug/asset/chunk.h"
#include "luaug/asset/field_cells.h"
#include "luaug/asset/streaming.h"
#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"
#include "luaug/platform/async_io.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace luaug::scene {
class World;
struct TerrainComponent;
struct VoxelComponent;
} // namespace luaug::scene

namespace luaug::app {

using core::f64;
using core::u64;

class FieldStreamer
{
public:
    using CellResolver = std::function<std::optional<std::filesystem::path>(const asset::ChunkIndexEntry&)>;

    // The partition's field index, each entry's file resolved once. An empty
    // index leaves the streamer inactive, which every world without a large
    // field is.
    void setIndex(const asset::ChunkIndex& index, const CellResolver& resolve);
    [[nodiscard]] bool active() const noexcept { return m_active; }

    // The world the cells go into, and the workspace whose `Terrain` and whose
    // camera they are about. A different world forgets what was resident: its
    // field went with it (`StreamingHost::setWorld` says the same).
    void setWorld(scene::World* world, core::InstanceId workspace);

    // One frame: finished reads, the foci, and the manager's tick.
    void pump(f64 budgetMilliseconds);

    // True once the minimum ring around the foci has been resident. Stays true.
    [[nodiscard]] bool primed() const noexcept { return m_primed; }
    [[nodiscard]] bool minimumRingResident() const noexcept { return m_manager.minimumRingResident(); }

    [[nodiscard]] const asset::StreamingStats& stats() const noexcept { return m_manager.stats(); }
    [[nodiscard]] asset::ChunkState stateOf(asset::ChunkId id) const noexcept { return m_manager.stateOf(id); }
    [[nodiscard]] const asset::ChunkIndex& index() const noexcept { return m_manager.index(); }

    // Cells an eviction left in place because somebody had changed them.
    [[nodiscard]] u64 kept() const noexcept { return m_kept; }

    // --- A terrain saved as cells, in the editor (ADR 0087) -------------------

    // Streams around this point rather than the world's own foci: the editor's
    // camera, which is not the scene's. Nothing is the world's foci again.
    void setFocusOverride(std::optional<core::DVec3> position) noexcept { m_focusOverride = position; }

    // **Takes a terrain's cell index as this streamer's terrain**, in place of
    // any it had, keeping every other kind of cell and what is resident of
    // them. An empty index stops streaming terrain. Called whenever the
    // workspace's terrain names a different index -- a scene opened, a terrain
    // converted to cells on its first large save, a terrain cleared.
    void adoptTerrain(const asset::ChunkIndex& index, const CellResolver& resolve);

    // **After the world was put back** -- an undo, a redo, a stop: the field is
    // whatever the snapshot held, which lacks every cell loaded since it was
    // taken. Each cell this streamer holds is shared back into the field, where
    // the field has nothing newer, so the ground loaded around the camera is
    // there again rather than a hole the streamer believes is filled.
    void reconcile();

    // Where a save writes one cell, and how it removes one.
    struct TerrainCellWriter
    {
        // Writes a cell's bytes; answers the URN it is at, or nothing on failure.
        std::function<std::optional<std::string>(asset::ChunkId, std::span<const std::byte>)> write;
        // Reads a cell as it is on disk, for merging ground that is not resident.
        std::function<std::optional<std::vector<std::byte>>(const asset::ChunkIndexEntry&)> read;
        // Removes the file of a cell nothing is left in.
        std::function<void(const asset::ChunkIndexEntry&)> remove;
        // How a written URN resolves for a later load.
        CellResolver resolve;
    };

    struct TerrainSaveReport
    {
        u64 written = 0;
        u64 removed = 0;
        u64 unchanged = 0;
        bool ok = true;
        // The terrain's whole index after the save, to be written beside the
        // cells: every cell the terrain has, resident or not.
        asset::ChunkIndex index;
    };

    // **Writes every cell whose ground differs from its file, and no other**
    // (ADR 0087): a cell loaded and not touched costs a comparison, a cell
    // nobody loaded costs nothing, and a cell somebody edited -- resident, or
    // kept in memory after its eviction was refused -- is written and becomes
    // an ordinary cell again, which the camera moving away may now let go.
    [[nodiscard]] TerrainSaveReport saveTerrain(const TerrainCellWriter& writer);

private:
    void installCallbacks();
    [[nodiscard]] scene::TerrainComponent* terrain() const;
    [[nodiscard]] scene::VoxelComponent* voxels() const;
    [[nodiscard]] f64 materialize(asset::ChunkId id, std::span<const std::byte> bytes);
    void evict(asset::ChunkId id);

    asset::StreamingManager m_manager;
    std::map<asset::ChunkId, std::filesystem::path> m_paths;
    // What each resident cell brought, held for the reason the header gives.
    std::map<asset::ChunkId, asset::TerrainCell> m_terrainCells;
    std::map<asset::ChunkId, asset::VoxelCell> m_voxelCells;
    std::vector<std::pair<platform::IoRequest, asset::ChunkId>> m_reads;
    // Loads the manager started whose read could not, reported after its tick.
    std::vector<asset::ChunkId> m_failedStarts;
    scene::World* m_world = nullptr;
    core::InstanceId m_workspace;
    bool m_active = false;
    bool m_primed = false;
    u64 m_kept = 0;
    // When the current wait for the ground began, for the log line that ends it.
    u64 m_waitingSinceNs = 0;
    std::optional<core::DVec3> m_focusOverride;
};

} // namespace luaug::app
