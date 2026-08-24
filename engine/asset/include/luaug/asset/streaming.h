// The policy engine for architecture.md §10's streaming pipeline.
//
// It decides WHICH chunks should be resident and in what order, and it decides
// nothing else: it does not create instances, it does not touch the scene, and
// it does not know what a chunk contains. That split is what lets the same
// manager drive a headless replay and a windowed game, and what lets it be
// tested without a world.
//
// **The budget is time, not a count** (roadmap M7's own performance note): a
// chunk's cost varies with what is in it, and the gate is stated as "zero
// hitches >33 ms attributable to streaming", so the budget and the gate measure
// the same thing. `tick` stops handing out work when the frame's millisecond
// budget is gone and resumes next frame.
//
// **Load and evict have different thresholds.** A single radius makes a focus
// standing on a boundary load and unload the same chunk every frame -- and the
// symptom is not a wrong world, it is a stutter nobody can find. The gap is
// hysteresis and it is a property of the design rather than a tuning value that
// happened to work.
#pragma once

#include "luaug/asset/chunk.h"
#include "luaug/core/types.h"

#include <array>
#include <functional>
#include <span>
#include <vector>

namespace luaug::asset {

using core::f32;
using core::f64;
using core::u32;
using core::u64;
using core::usize;

enum class ChunkState : core::u8
{
    // Known to the index and nothing more.
    Unloaded,
    // An IO read is outstanding.
    Loading,
    // Bytes are here and decoded; the instances are not in the world yet.
    Decoded,
    // Materialised into the scene.
    Resident,
    // The read or the decode failed. Terminal until the manager is reset: a
    // chunk that failed once will fail again, and retrying it every frame is
    // how a broken world becomes an unresponsive one.
    Failed,
};

[[nodiscard]] const char* chunkStateName(ChunkState state) noexcept;

// One size class's pair, or nothing (ADR 0053). A cell's `layer` is its class --
// 0 detail, 1 structures, 2 terrain features -- and a mountain and a pebble stop
// sharing a radius, which is the choice a single grid forces and always resolves
// badly in one direction.
//
// **Zero means "the focus's own pair"**, which is what makes a world whose cells
// are all layer 0 behave exactly as it did before layers meant anything.
struct StreamingLayerRadii
{
    f64 minRadius = 0.0;
    f64 loadRadius = 0.0;
};

// A thing the world streams around. `MinRadius` is the must-have ring
// architecture.md §10 guarantees resident before the focus may advance into it;
// `LoadRadius` is the best-effort one.
struct StreamingFocus
{
    core::DVec3 position;
    f64 minRadius = 512.0;
    f64 loadRadius = 1024.0;

    // Indexed by `ChunkId::layer`. A layer outside the array -- including a
    // negative one, which a hand-written index could carry -- takes the pair
    // above rather than being refused: a cell in a class this build has no
    // radius for is still a cell, and streaming it at the base distance is the
    // answer that keeps a world whole.
    std::array<StreamingLayerRadii, static_cast<usize>(ChunkLayerCount)> layers{};

    [[nodiscard]] f64 minRadiusFor(core::i32 layer) const noexcept
    {
        const f64 own = inRange(layer) ? layers[static_cast<usize>(layer)].minRadius : 0.0;
        return own > 0.0 ? own : minRadius;
    }

    [[nodiscard]] f64 loadRadiusFor(core::i32 layer) const noexcept
    {
        const f64 own = inRange(layer) ? layers[static_cast<usize>(layer)].loadRadius : 0.0;
        return own > 0.0 ? own : loadRadius;
    }

private:
    [[nodiscard]] static bool inRange(core::i32 layer) noexcept { return layer >= 0 && layer < ChunkLayerCount; }
};

struct StreamingBudget
{
    // How long `tick` may spend handing out and finishing work this frame.
    f64 milliseconds = 2.0;
    // How many reads may be outstanding at once. Bounds memory: every one of
    // them is a chunk payload in flight.
    u32 maxInFlight = 8;
};

struct StreamingStats
{
    u32 resident = 0;
    u32 loading = 0;
    u32 decoded = 0;
    u32 failed = 0;
    u64 chunksLoaded = 0;
    u64 chunksEvicted = 0;
    u64 bytesResident = 0;
    // The worst `tick` in milliseconds since the last reset, which is the
    // number the gate's hitch histogram is really about.
    f64 worstTickMs = 0.0;
};

// What the manager asks its host to do. Injected rather than inherited so a
// test can drive the whole policy with three lambdas and no scene.
struct StreamingCallbacks
{
    // Begin reading a chunk. Returns false when the host could not start it,
    // which the manager treats as "try again later" rather than as a failure --
    // an IO queue that is momentarily full is not a broken chunk.
    std::function<bool(ChunkId, const ChunkIndexEntry&)> beginLoad;
    // Put a decoded chunk's instances into the world. Returns the milliseconds
    // it took, so the manager can charge the budget with the real cost rather
    // than an estimate.
    std::function<f64(ChunkId, const Chunk&)> materialize;
    // Take them out again.
    std::function<void(ChunkId)> evict;
};

class StreamingManager
{
public:
    void setIndex(ChunkIndex index);
    [[nodiscard]] const ChunkIndex& index() const noexcept { return m_index; }

    void setCallbacks(StreamingCallbacks callbacks) { m_callbacks = std::move(callbacks); }

    // Forgets that anything is resident WITHOUT asking for an eviction.
    // Narrow on purpose: the one legitimate caller is a host whose scene went
    // away underneath it, where the instances are already gone and an evict
    // callback would be asking a glue that never materialised them to destroy
    // ids that no longer exist. Every other path must evict properly -- this
    // one drops the record because the record's subject is gone.
    void forgetResidency() noexcept;

    // Replaces the focus set wholesale. A vector rather than an add/remove pair
    // because the caller has the authoritative list every frame anyway, and two
    // ways to change one set is one way too many.
    void setFoci(std::span<const StreamingFocus> foci);
    [[nodiscard]] std::span<const StreamingFocus> foci() const noexcept { return m_foci; }

    // Turns streaming off without forgetting anything: resident chunks stay
    // resident and nothing new is scheduled. `StreamingService.Enabled`.
    void setEnabled(bool enabled) noexcept { m_enabled = enabled; }
    [[nodiscard]] bool enabled() const noexcept { return m_enabled; }

    // The host reports a finished read. Bytes are decoded here rather than in
    // the host so that a malformed chunk is one error path.
    void onChunkLoaded(ChunkId id, std::span<const std::byte> bytes);
    void onChunkFailed(ChunkId id);

    // One frame's worth of streaming. Issues loads in score order, materialises
    // what has arrived until the budget runs out, and evicts what has left the
    // outer ring.
    void tick(const StreamingBudget& budget);

    [[nodiscard]] ChunkState stateOf(ChunkId id) const noexcept;
    [[nodiscard]] const StreamingStats& stats() const noexcept { return m_stats; }
    void resetStats() noexcept;

    // Every chunk whose state is worth showing, in index order, for the overlay
    // the deliverable owes.
    struct ChunkView
    {
        ChunkId id;
        ChunkState state = ChunkState::Unloaded;
        f64 score = 0.0;
    };
    [[nodiscard]] std::vector<ChunkView> view() const;

    // True when every chunk inside a focus's MIN radius is resident. This is
    // architecture.md §10's integrity rule -- "the min-radius ring is guaranteed
    // resident before the character may advance into it" -- and it is what
    // `StreamingService.PauseOutsideLoadedArea` reads.
    [[nodiscard]] bool minimumRingResident() const noexcept;

private:
    struct Entry
    {
        ChunkState state = ChunkState::Unloaded;
        // Distance squared to the nearest focus. Lower is more urgent.
        f64 score = 0.0;
        bool wanted = false;
        Chunk decoded;
        u32 bytes = 0;
    };

    ChunkIndex m_index;
    std::vector<Entry> m_entries;
    std::vector<StreamingFocus> m_foci;
    StreamingCallbacks m_callbacks;
    StreamingStats m_stats;
    bool m_enabled = true;
    u32 m_inFlight = 0;
};

} // namespace luaug::asset
