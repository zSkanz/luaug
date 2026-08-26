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

void StreamingManager::forgetResidency() noexcept
{
    for (Entry& entry : m_entries) {
        entry.state = ChunkState::Unloaded;
        entry.decoded = Chunk{};
        entry.bytes = 0;
        entry.wanted = false;
    }
    // The in-flight COUNTER has to go with them, and it is the subtle half.
    // `onChunkLoaded` only decrements for an entry still marked `Loading`, so
    // reads outstanding across this call would never be credited back and the
    // counter would climb to `maxInFlight` and stay there -- streaming that
    // stops after a hot reload, which is a worse bug than the one this fixes.
    // The completions themselves are harmless: they decode into slots nothing
    // has decided about yet, and a chunk's bytes on disk do not change.
    m_inFlight = 0;
    m_stats.bytesResident = 0;
    m_stats.resident = 0;
    m_stats.decoded = 0;
}

void StreamingManager::setFoci(std::span<const StreamingFocus> foci)
{
    m_foci.assign(foci.begin(), foci.end());
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

    // Scores first, for every chunk, because both halves below read them and
    // computing them twice would be the same answer at twice the price.
    //
    // **The radius is asked of the focus AND of the chunk's layer** (ADR 0053).
    // One radius per focus is what a single grid forces, and it resolves badly
    // in one direction whichever way it is set: a mountain and a pebble cannot
    // share a distance. A cell is wanted when ANY focus wants it, which is the
    // same union the score already takes.
    // **A world with no focus at all is a world that does not stream** (S8.2).
    //
    // "No focus" is not "a focus that wants nothing": a project that streams
    // always registers one, so a project that has none has said it does not.
    // Treating that as "nothing is wanted" made every scene-based project
    // materialise an EMPTY world unless it happened to know the word
    // `StreamingMode = "Persistent"` -- which the starter template had to say,
    // about a fifteen-part scene, in order to appear at all. A one-word
    // incantation standing between somebody and their first frame is the worst
    // shape a default can have, because nothing on screen says what is missing.
    //
    // The cost is a project that registers its focus LATE: it loads everything
    // on the frames before it does, then evicts. That is a real cost paid by a
    // rare pattern, and it is strictly better than a world nobody can see.
    const bool everythingWanted = m_foci.empty();

    for (usize i = 0; i < m_entries.size(); ++i) {
        const ChunkIndexEntry& indexEntry = m_index.chunks[i];
        Entry& entry = m_entries[i];

        if (everythingWanted) {
            // Scored as though they were all equally near, which they are: with
            // no focus there is nothing to be near TO, and the loader's order
            // then falls back to the index's, which is a property of the
            // operation sequence (R10) rather than of an address.
            entry.score = 0.0;
            entry.wanted = true;
            continue;
        }

        f64 best = std::numeric_limits<f64>::infinity();
        bool inside = false;
        bool pastDrop = true;
        for (const StreamingFocus& focus : m_foci) {
            const f64 distance = core::distanceSquared(indexEntry.bounds, focus.position);
            best = std::min(best, distance);

            const f64 loadRadius = focus.loadRadiusFor(indexEntry.id.layer);
            const f64 wantDistance = loadRadius * loadRadius;
            const f64 dropRadius = loadRadius * EvictHysteresis;
            if (distance <= wantDistance) {
                inside = true;
            }
            if (distance <= dropRadius * dropRadius) {
                pastDrop = false;
            }
        }
        entry.score = best;

        // Hysteresis, and this is where it lives: a chunk becomes wanted inside
        // the load radius and stops being wanted only once it is past the wider
        // one. Between the two it keeps whatever it already was.
        if (inside) {
            entry.wanted = true;
        }
        else if (pastDrop) {
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
            const f64 minRadius = focus.minRadiusFor(m_index.chunks[i].id.layer);
            const f64 minSquared = minRadius * minRadius;
            if (core::distanceSquared(m_index.chunks[i].bounds, focus.position) <= minSquared &&
                m_entries[i].state != ChunkState::Resident) {
                return false;
            }
        }
    }
    return true;
}

} // namespace luaug::asset
