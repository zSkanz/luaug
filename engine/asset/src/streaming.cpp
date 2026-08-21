#include "luaug/asset/streaming.h"

#include "luaug/platform/platform.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace luaug::asset {
namespace {

// The gap between "load me" and "drop me", as a multiplier on the load radius.
//
// Not a tuning value that happened to work: a single radius makes a focus
// standing on a boundary load and evict the same chunk every frame, and the
// symptom is not a wrong world but a stutter nobody can find. A quarter of the
// radius is wide enough that a character walking at ten metres a second takes
// several seconds to cross it, and narrow enough that the extra ring is a few
// chunks rather than a second world.
constexpr f64 EvictHysteresis = 1.25;

[[nodiscard]] f64 nowMs() noexcept
{
    // Wall clock, and legitimately so: this is a FRAME budget rather than
    // simulation state. Nothing here reaches the world hash -- which chunks are
    // resident is decided by distance, and only the ORDER work is finished in
    // depends on the clock. R10 is about the simulation, and a chunk that
    // materialises one frame later still materialises the same instances.
    return static_cast<f64>(platform::nowNs()) / 1.0e6;
}

} // namespace

const char* chunkStateName(ChunkState state) noexcept
{
    switch (state) {
    case ChunkState::Unloaded:
        return "unloaded";
    case ChunkState::Loading:
        return "loading";
    case ChunkState::Decoded:
        return "decoded";
    case ChunkState::Resident:
        return "resident";
    case ChunkState::Failed:
        return "failed";
    }
    return "unknown";
}

void StreamingManager::setIndex(ChunkIndex index)
{
    m_index = std::move(index);
    m_entries.assign(m_index.chunks.size(), Entry{});
    m_inFlight = 0;
}

void StreamingManager::setFoci(std::span<const StreamingFocus> foci)
{
    m_foci.assign(foci.begin(), foci.end());
}

f64 StreamingManager::scoreOf(const ChunkIndexEntry& entry) const noexcept
{
    f64 best = std::numeric_limits<f64>::infinity();
    for (const StreamingFocus& focus : m_foci) {
        best = std::min(best, core::distanceSquared(entry.bounds, focus.position));
    }
    return best;
}

void StreamingManager::onChunkLoaded(ChunkId id, std::span<const std::byte> bytes)
{
    const ChunkIndexEntry* const indexEntry = m_index.find(id);
    if (indexEntry == nullptr) {
        return;
    }
    const usize slot = static_cast<usize>(indexEntry - m_index.chunks.data());
    Entry& entry = m_entries[slot];
    if (entry.state == ChunkState::Loading && m_inFlight > 0) {
        m_inFlight -= 1;
    }

    Chunk chunk;
    if (decodeChunk(bytes, chunk).has_value()) {
        entry.state = ChunkState::Failed;
        m_stats.failed += 1;
        return;
    }

    entry.decoded = std::move(chunk);
    entry.bytes = static_cast<u32>(bytes.size());
    entry.state = ChunkState::Decoded;
}

void StreamingManager::onChunkFailed(ChunkId id)
{
    const ChunkIndexEntry* const indexEntry = m_index.find(id);
    if (indexEntry == nullptr) {
        return;
    }
    Entry& entry = m_entries[static_cast<usize>(indexEntry - m_index.chunks.data())];
    if (entry.state == ChunkState::Loading && m_inFlight > 0) {
        m_inFlight -= 1;
    }
    entry.state = ChunkState::Failed;
    m_stats.failed += 1;
}

void StreamingManager::tick(const StreamingBudget& budget)
{
    const f64 started = nowMs();

    f64 loadRadius = 0.0;
    for (const StreamingFocus& focus : m_foci) {
        loadRadius = std::max(loadRadius, focus.loadRadius);
    }
    const f64 wantDistance = loadRadius * loadRadius;
    const f64 dropDistance = (loadRadius * EvictHysteresis) * (loadRadius * EvictHysteresis);

    // Scores first, for every chunk, because both halves below read them and
    // computing them twice would be the same answer at twice the price.
    for (usize i = 0; i < m_entries.size(); ++i) {
        const ChunkIndexEntry& indexEntry = m_index.chunks[i];
        Entry& entry = m_entries[i];
        entry.score = scoreOf(indexEntry);

        // Hysteresis, and this is where it lives: a chunk becomes wanted inside
        // the load radius and stops being wanted only once it is past the wider
        // one. Between the two it keeps whatever it already was.
        if (entry.score <= wantDistance) {
            entry.wanted = true;
        }
        else if (entry.score > dropDistance) {
            entry.wanted = false;
        }
    }

    // Eviction first and unbudgeted: dropping a chunk frees memory, and a frame
    // that ran out of budget before it could evict is a frame that grows the
    // heap while trying to protect the frame time. It is also cheap -- the scene
    // glue destroys instances it already has ids for.
    //
    // Disabled means disabled in BOTH directions, and the test is what settled
    // it: `Enabled = false` freezes the streaming set rather than half of it.
    // Evicting while refusing to load would drain the world one ring at a time
    // and leave nothing to come back to.
    if (m_enabled && m_callbacks.evict) {
        for (usize i = 0; i < m_entries.size(); ++i) {
            Entry& entry = m_entries[i];
            if (entry.wanted || entry.state != ChunkState::Resident) {
                continue;
            }
            m_callbacks.evict(m_index.chunks[i].id);
            entry.state = ChunkState::Unloaded;
            entry.decoded = Chunk{};
            m_stats.chunksEvicted += 1;
            m_stats.bytesResident -= std::min<u64>(m_stats.bytesResident, entry.bytes);
            entry.bytes = 0;
        }
    }

    if (m_enabled) {
        // Wanted chunks in score order. A sorted index list rather than a heap:
        // the set changes wholesale every frame as the focus moves, and a heap
        // whose keys all change is a heap rebuilt anyway.
        std::vector<usize> pending;
        pending.reserve(m_entries.size());
        for (usize i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].wanted &&
                (m_entries[i].state == ChunkState::Unloaded || m_entries[i].state == ChunkState::Decoded)) {
                pending.push_back(i);
            }
        }
        std::sort(pending.begin(), pending.end(), [this](usize a, usize b) {
            if (m_entries[a].score != m_entries[b].score) {
                return m_entries[a].score < m_entries[b].score;
            }
            // Ties break by chunk id rather than by slot, so the order two
            // equidistant chunks materialise in is a property of the world and
            // not of how the index happened to be sorted (R10).
            return m_index.chunks[a].id < m_index.chunks[b].id;
        });

        // The budget is charged with BOTH the wall clock and what the host says
        // materialising cost it. Wall clock alone is wrong and the test found
        // it: `materialize` may hand its work to a queue and return, in which
        // case the frame's real cost is a number only the host knows.
        f64 charged = 0.0;
        for (const usize slot : pending) {
            if ((nowMs() - started) + charged >= budget.milliseconds) {
                break;
            }
            Entry& entry = m_entries[slot];
            const ChunkIndexEntry& indexEntry = m_index.chunks[slot];

            if (entry.state == ChunkState::Decoded) {
                if (!m_callbacks.materialize) {
                    continue;
                }
                // The host reports what it cost, so the budget is charged with
                // the real number rather than an estimate. A chunk that takes
                // three milliseconds spends the whole frame's budget and the
                // next chunk waits -- which is the entire point.
                charged += m_callbacks.materialize(indexEntry.id, entry.decoded);
                entry.state = ChunkState::Resident;
                entry.decoded = Chunk{};
                m_stats.chunksLoaded += 1;
                m_stats.bytesResident += entry.bytes;
                continue;
            }

            if (m_inFlight >= budget.maxInFlight) {
                continue;
            }
            if (m_callbacks.beginLoad && m_callbacks.beginLoad(indexEntry.id, indexEntry)) {
                entry.state = ChunkState::Loading;
                m_inFlight += 1;
            }
        }
    }

    m_stats.resident = 0;
    m_stats.loading = 0;
    m_stats.decoded = 0;
    for (const Entry& entry : m_entries) {
        switch (entry.state) {
        case ChunkState::Resident:
            m_stats.resident += 1;
            break;
        case ChunkState::Loading:
            m_stats.loading += 1;
            break;
        case ChunkState::Decoded:
            m_stats.decoded += 1;
            break;
        default:
            break;
        }
    }

    m_stats.worstTickMs = std::max(m_stats.worstTickMs, nowMs() - started);
}

ChunkState StreamingManager::stateOf(ChunkId id) const noexcept
{
    const ChunkIndexEntry* const entry = m_index.find(id);
    if (entry == nullptr) {
        return ChunkState::Unloaded;
    }
    return m_entries[static_cast<usize>(entry - m_index.chunks.data())].state;
}

void StreamingManager::resetStats() noexcept
{
    const u32 resident = m_stats.resident;
    const u64 bytes = m_stats.bytesResident;
    m_stats = StreamingStats{};
    // The two that describe the CURRENT world rather than what has happened to
    // it survive a reset, because a counter reset that also reported zero
    // resident chunks would be lying about the world.
    m_stats.resident = resident;
    m_stats.bytesResident = bytes;
}

std::vector<StreamingManager::ChunkView> StreamingManager::view() const
{
    std::vector<ChunkView> out;
    out.reserve(m_entries.size());
    for (usize i = 0; i < m_entries.size(); ++i) {
        out.push_back(ChunkView{m_index.chunks[i].id, m_entries[i].state, m_entries[i].score});
    }
    return out;
}

bool StreamingManager::minimumRingResident() const noexcept
{
    for (usize i = 0; i < m_entries.size(); ++i) {
        for (const StreamingFocus& focus : m_foci) {
            const f64 minSquared = focus.minRadius * focus.minRadius;
            if (core::distanceSquared(m_index.chunks[i].bounds, focus.position) <= minSquared &&
                m_entries[i].state != ChunkState::Resident) {
                return false;
            }
        }
    }
    return true;
}

} // namespace luaug::asset
