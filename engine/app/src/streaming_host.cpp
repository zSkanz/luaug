#include "luaug/app/streaming_host.h"

#include "luaug/asset/content.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/file.h"
#include "luaug/platform/platform.h"
#include "luaug/scene/components.h"
#include "luaug/scene/physics_sync.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <cmath>

namespace luaug::app {
namespace {

using core::LogLevel;

// The IO priority a chunk's score earns it. Three bands rather than a
// continuum, because `IoPriority` has four and the fourth is for speculative
// work nothing here does: what matters is that the cell a focus stands in
// overtakes the ring behind it.
[[nodiscard]] platform::IoPriority priorityFor(f64 scoreSquared, f64 minRadius) noexcept
{
    const f64 minSquared = minRadius * minRadius;
    if (scoreSquared <= minSquared) {
        return platform::IoPriority::Critical;
    }
    if (scoreSquared <= minSquared * 4.0) {
        return platform::IoPriority::High;
    }
    return platform::IoPriority::Normal;
}

} // namespace

bool StreamingHost::load(const asset::ContentMounts& mounts, const std::filesystem::path& indexPath)
{
    m_mounts = &mounts;

    std::string text;
    if (!platform::readTextFile(indexPath, text)) {
        // Absent is the normal case: a world that fits in memory does not need
        // streaming, and warning about it would warn on every example here.
        return false;
    }

    asset::ChunkIndex index;
    if (auto error = asset::readChunkIndex(text, index); error.has_value()) {
        core::logText(LogLevel::Warn, error->message);
        return false;
    }

    return addIndex(index, [&mounts](const asset::ChunkIndexEntry& entry) -> std::optional<std::filesystem::path> {
        const asset::ResolvedContent resolved = mounts.resolve(entry.urn);
        if (resolved.source != asset::ResolvedContent::Source::Loose) {
            // A chunk in a PACK would be resident already, which defeats the
            // point (`asset/chunk.h`). Anything else is a file that is not there.
            return std::nullopt;
        }
        return resolved.path;
    });
}

bool StreamingHost::addIndex(const asset::ChunkIndex& index, const ChunkResolver& resolve)
{
    if (index.chunks.empty()) {
        return false;
    }

    // Resolved here and never again (D039). Resolution opens the candidate file
    // to decide whether it is there, and doing that inside the pump put a
    // filesystem call on the frame thread once per chunk load -- ten to thirty
    // milliseconds of hitch, in the container, for a question whose answer was
    // settled when the project was built.
    //
    // A missing file is reported HERE too, once per chunk, rather than the first
    // time a focus walks near it. A world whose index names files the build did
    // not produce is a broken build, and hearing about it at boot is strictly
    // better than hearing about it three minutes into a fly-through.
    asset::ChunkIndex merged = m_manager.index();
    if (merged.chunks.empty()) {
        merged.chunkSize = index.chunkSize;
    }
    else if (merged.chunkSize != index.chunkSize) {
        // Every cell in one world agrees about the grid, which is what makes a
        // position a cell id at all. Two answers is not a world.
        const core::I18nArg args[] = {{"content", std::string("chunkSize")}};
        core::log(LogLevel::Warn, LUAUG_TR("app.warn.chunk_missing"), args);
        return false;
    }

    for (const asset::ChunkIndexEntry& entry : index.chunks) {
        if (merged.find(entry.id) != nullptr) {
            const core::I18nArg args[] = {{"content", entry.urn}};
            core::log(LogLevel::Warn, LUAUG_TR("app.warn.chunk_duplicate"), args);
            continue;
        }
        const std::optional<std::filesystem::path> path = resolve(entry);
        if (!path.has_value()) {
            const core::I18nArg args[] = {{"content", entry.urn}};
            core::log(LogLevel::Warn, LUAUG_TR("app.warn.chunk_missing"), args);
            continue;
        }
        m_chunkPaths.insert_or_assign(entry.id, *path);
        merged.chunks.push_back(entry);
    }

    std::sort(merged.chunks.begin(), merged.chunks.end(),
              [](const asset::ChunkIndexEntry& a, const asset::ChunkIndexEntry& b) { return a.id < b.id; });
    m_manager.setIndex(std::move(merged));
    installCallbacks();

    const core::I18nArg args[] = {{"count", static_cast<core::i64>(m_manager.index().chunks.size())}};
    core::log(LogLevel::Info, LUAUG_TR("app.info.streaming_world"), args);
    m_active = true;
    return true;
}

void StreamingHost::installCallbacks()
{
    asset::StreamingCallbacks callbacks;
    callbacks.beginLoad = [this](asset::ChunkId id, const asset::ChunkIndexEntry& entry) {
        const platform::IoStats io = platform::ioStats();
        // Refused rather than failed when the IO service is saturated: the
        // manager retries a refusal and gives up on a failure, and a full queue
        // is not a broken chunk.
        if (io.queued + io.inFlight >= platform::MaxIoRequests / 2) {
            return false;
        }
        beginRead(id, entry);
        return true;
    };
    callbacks.materialize = [this](asset::ChunkId id, const asset::Chunk& chunk) {
        return m_glue != nullptr ? m_glue->materialize(id, chunk) : 0.0;
    };
    callbacks.evict = [this](asset::ChunkId id) {
        if (m_glue != nullptr) {
            m_glue->evict(id);
        }
    };
    m_manager.setCallbacks(std::move(callbacks));
}

void StreamingHost::setWorld(scene::World* world, core::InstanceId streamRoot)
{
    // The early return IS the fix (D032). See the header for what rebuilding
    // the glue on an unchanged world costs.
    if (m_world == world && m_streamRoot == streamRoot) {
        return;
    }

    // A real world change means the previous world's residency record went
    // with the world, so the manager has to stop believing in it too. Without
    // this the first frame after a reload asks the NEW glue to evict chunks it
    // never materialised -- the same silent no-op, one frame wide.
    m_world = world;
    m_streamRoot = streamRoot;
    m_glue = world != nullptr ? std::make_unique<scene::StreamingGlue>(*world, streamRoot) : nullptr;
    m_manager.forgetResidency();
}

void StreamingHost::beginRead(asset::ChunkId id, const asset::ChunkIndexEntry& entry)
{
    // A LOOKUP, not a resolution. See `load` for why.
    const auto path = m_chunkPaths.find(id);
    if (path == m_chunkPaths.end()) {
        m_manager.onChunkFailed(id);
        return;
    }

    f64 minRadius = static_cast<f64>(asset::DefaultChunkSize);
    for (const asset::StreamingFocus& focus : m_manager.foci()) {
        minRadius = std::max(minRadius, focus.minRadius);
    }

    const platform::IoRequest request = platform::readFileAsync(
        path->second,
        priorityFor(core::distanceSquared(entry.bounds,
                                          m_manager.foci().empty() ? core::DVec3{} : m_manager.foci().front().position),
                    minRadius));
    if (!request.valid()) {
        m_manager.onChunkFailed(id);
        return;
    }
    m_reads.emplace_back(request, Pending{id});
}

std::vector<asset::StreamingFocus> StreamingHost::collectFoci() const
{
    std::vector<asset::StreamingFocus> foci;
    if (m_world == nullptr) {
        return foci;
    }

    const scene::EngineState& state = m_world->engineState();

    // The size classes (ADR 0053). Layer 0 is detail and IS the load/min pair
    // below, which is what makes a world of cells that are all layer 0 behave
    // exactly as it did. A zero here means "follow that pair", and the same
    // smaller-of-the-two rule applies for the same reason.
    const auto layerPair = [](f64 minRadius, f64 loadRadius) {
        asset::StreamingLayerRadii radii;
        radii.loadRadius = loadRadius;
        radii.minRadius = loadRadius > 0.0 ? std::min(minRadius, loadRadius) : minRadius;
        return radii;
    };

    // One focus, or nothing when the id names something with no position. Lifted
    // out of the loop so the camera fallback below is the same rule and not a
    // second copy of it.
    const auto focusAt = [&](core::InstanceId id, asset::StreamingFocus& out) {
        if (!m_world->alive(id)) {
            return false;
        }
        if (const scene::PartComponent* part = m_world->parts().find(id); part != nullptr) {
            out.position = part->cframe.position;
        }
        else if (const scene::CameraComponent* camera = m_world->cameras().find(id); camera != nullptr) {
            out.position = camera->cframe.position;
        }
        else {
            return false;
        }
        return true;
    };

    for (const core::InstanceId id : m_world->streamingFoci()) {
        asset::StreamingFocus focus;
        if (!focusAt(id, focus)) {
            continue;
        }
        focus.loadRadius = state.streamingLoadRadius;
        // The smaller of the two, rather than refusing a `MinRadius` larger
        // than the load radius at the write: the two are set in whichever order
        // a script happens to write them, and a property that depends on
        // statement order is a trap.
        focus.minRadius = std::min(state.streamingMinRadius, state.streamingLoadRadius);

        focus.layers[1] = layerPair(state.streamingStructureMinRadius, state.streamingStructureLoadRadius);
        focus.layers[2] = layerPair(state.streamingTerrainMinRadius, state.streamingTerrainLoadRadius);
        foci.push_back(focus);
    }

    // **A world with no registered focus streams around the camera** (D098).
    //
    // Nothing populates `streamingFoci` but `StreamingService:AddFocus`, and
    // `partitionProject` runs for every non-editor run with a scene and has no
    // floor -- so a project somebody has just made, with a camera and two parts
    // in it, gets a streaming grid and never loads a single cell of it. The
    // world is empty and both halves report success: the log says the scene was
    // partitioned, and the world says nothing at all.
    //
    // The fallback is narrowed to exactly that case rather than making the
    // camera a focus always. A game that registers its own foci is untouched --
    // this list is not empty for it -- so nothing that works today changes, and
    // the only worlds that gain a focus are the ones that had none and were
    // therefore showing nothing. "Show me the world" is what a person means by
    // pointing a camera at it, and it is what the editor already assumes.
    if (foci.empty()) {
        // Found by walking up from the stream root rather than held as a member:
        // the root is already the identity this host is keyed on, and the
        // `Workspace` above it is the one whose camera is the one being looked
        // through.
        const scene::WorkspaceComponent* workspace = nullptr;
        for (core::InstanceId cursor = m_streamRoot; cursor.valid(); cursor = m_world->parentOf(cursor)) {
            workspace = m_world->workspaces().find(cursor);
            if (workspace != nullptr)
                break;
        }

        asset::StreamingFocus focus;
        if (workspace != nullptr && focusAt(workspace->currentCamera, focus)) {
            focus.loadRadius = state.streamingLoadRadius;
            focus.minRadius = std::min(state.streamingMinRadius, state.streamingLoadRadius);
            focus.layers[1] = layerPair(state.streamingStructureMinRadius, state.streamingStructureLoadRadius);
            focus.layers[2] = layerPair(state.streamingTerrainMinRadius, state.streamingTerrainLoadRadius);
            foci.push_back(focus);
        }
    }
    return foci;
}

void StreamingHost::pump(f64 budgetMilliseconds)
{
    m_lastPumpMs = 0.0;
    if (!m_active || m_world == nullptr) {
        return;
    }

    // Measured for the GATE rather than for the budget, which does its own
    // accounting inside the manager. What M7 has to prove is "zero hitches
    // ATTRIBUTABLE to streaming", and attribution needs a number that is only
    // streaming: a whole-frame time on a shared CI runner is mostly the runner.
    // R10 is not in the way -- nothing measured here reaches the simulation.
    const u64 startedNs = platform::nowNs();

    // Completed reads first, so a chunk that arrived during the frame can
    // materialise in the same one rather than waiting for the next.
    //
    // **And the budget covers this loop too.** It used to be accounted for only
    // inside `m_manager.tick(budget)` below, so the drain above it was bounded
    // by nothing but `initIo`'s `maxInFlight` -- worst case, eight chunk decodes
    // on one frame with no way to stop part way through. M7's gate claims zero
    // hitches attributable to streaming, and a decode nobody can interrupt is
    // exactly the shape of one. What is left in `m_reads` stays there; the loop
    // resumes next frame where it stopped, which it was already written to do.
    platform::pumpIo();
    for (usize index = 0; index < m_reads.size();) {
        if (budgetMilliseconds > 0.0 && static_cast<f64>(platform::nowNs() - startedNs) / 1.0e6 >= budgetMilliseconds) {
            break;
        }
        const platform::IoStatus status = platform::ioStatus(m_reads[index].first);
        if (status == platform::IoStatus::Pending) {
            ++index;
            continue;
        }

        std::vector<std::byte> bytes;
        if (status == platform::IoStatus::Ready && platform::takeIoResult(m_reads[index].first, bytes)) {
            m_manager.onChunkLoaded(m_reads[index].second.id, bytes);
        }
        else {
            m_manager.onChunkFailed(m_reads[index].second.id);
        }
        m_reads.erase(m_reads.begin() + static_cast<std::ptrdiff_t>(index));
    }

    const std::vector<asset::StreamingFocus> foci = collectFoci();
    m_manager.setFoci(foci);
    m_manager.setEnabled(m_world->engineState().streamingEnabled);

    asset::StreamingBudget budget;
    budget.milliseconds = budgetMilliseconds;
    m_manager.tick(budget);

    // The origin follows the PRIMARY focus -- the first one -- rather than an
    // average of them. An average between two characters walking apart is a
    // point neither of them is near, and it drifts every frame.
    if (m_physics != nullptr && !foci.empty() && m_physics->shouldRebase(foci.front().position)) {
        m_physics->setOrigin(foci.front().position);
        m_rebases += 1;
    }

    m_lastPumpMs = static_cast<f64>(platform::nowNs() - startedNs) / 1.0e6;
}

std::vector<core::InstanceId> StreamingHost::drainStreamedOut()
{
    return m_glue != nullptr ? m_glue->drainStreamedOut() : std::vector<core::InstanceId>{};
}

bool StreamingHost::areaResident(core::DVec3 position, f64 radius) const
{
    const f64 radiusSquared = radius * radius;
    bool sawOne = false;
    for (const asset::ChunkIndexEntry& entry : m_manager.index().chunks) {
        if (core::distanceSquared(entry.bounds, position) > radiusSquared) {
            continue;
        }
        sawOne = true;
        if (m_manager.stateOf(entry.id) != asset::ChunkState::Resident) {
            return false;
        }
    }
    // An area with no chunks in it is "loaded" -- there is nothing to wait for.
    // Returning false would hang a `LoadAreaAsync` aimed past the edge of the
    // world, which is a mistake worth surviving rather than deadlocking on.
    (void)sawOne;
    return true;
}

} // namespace luaug::app
