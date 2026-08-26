#include "luaug/asset/streaming.h"
#include "luaug/core/i18n.h"

#include <algorithm>
#include <cstddef>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace luaug::asset;
using luaug::core::engineCatalog;
using luaug::core::f32;
using luaug::core::f64;
using luaug::core::usize;

namespace {

void seedRealCatalog()
{
    const auto result = engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// A square of chunks around the origin, each with one instance in it. Small
// enough to reason about and big enough that a radius selects a subset.
[[nodiscard]] ChunkIndex gridIndex(int halfWidth, f32 chunkSize = 256.0f)
{
    ChunkIndex index;
    index.chunkSize = chunkSize;
    for (int z = -halfWidth; z <= halfWidth; ++z) {
        for (int x = -halfWidth; x <= halfWidth; ++x) {
            ChunkIndexEntry entry;
            entry.id = ChunkId{x, z, 0};
            entry.bounds = chunkBounds(entry.id, chunkSize);
            entry.bounds.min.y = -1.0;
            entry.bounds.max.y = 1.0;
            entry.urn = "asset://world/c.lchunk";
            entry.instanceCount = 1;
            entry.bytes = 128;
            index.chunks.push_back(entry);
        }
    }
    std::sort(index.chunks.begin(), index.chunks.end(),
              [](const ChunkIndexEntry& a, const ChunkIndexEntry& b) { return a.id < b.id; });
    return index;
}

// Drives the manager the way a host would, and records what it was asked to do.
struct Harness
{
    StreamingManager manager;
    std::vector<ChunkId> loadRequests;
    std::vector<ChunkId> materialized;
    std::vector<ChunkId> evicted;
    f64 materializeCost = 0.0;
    bool refuseLoads = false;

    explicit Harness(ChunkIndex index)
    {
        manager.setIndex(std::move(index));
        StreamingCallbacks callbacks;
        callbacks.beginLoad = [this](ChunkId id, const ChunkIndexEntry&) {
            if (refuseLoads) {
                return false;
            }
            loadRequests.push_back(id);
            return true;
        };
        callbacks.materialize = [this](ChunkId id, const Chunk&) {
            materialized.push_back(id);
            return materializeCost;
        };
        callbacks.evict = [this](ChunkId id) { evicted.push_back(id); };
        manager.setCallbacks(std::move(callbacks));
    }

    // What the IO would have delivered: a real encoded chunk, so the decode
    // path is exercised rather than mocked.
    void deliver(ChunkId id)
    {
        Chunk chunk;
        chunk.id = id;
        chunk.bounds = chunkBounds(id, manager.index().chunkSize);
        chunk.instances.push_back(ChunkInstance{});
        manager.onChunkLoaded(id, encodeChunk(chunk));
    }

    void deliverAllRequested()
    {
        const std::vector<ChunkId> pending = loadRequests;
        loadRequests.clear();
        for (const ChunkId id : pending) {
            deliver(id);
        }
    }

    // Runs until nothing new happens, which is what a few frames of a settled
    // camera look like.
    void settle(const StreamingBudget& budget, int frames = 40)
    {
        for (int i = 0; i < frames; ++i) {
            manager.tick(budget);
            deliverAllRequested();
            manager.tick(budget);
        }
    }
};

// The centre of cell (0, 0). Standing on the world origin is standing on the
// CORNER where four cells meet, which makes "the chunks around it" mean sixteen
// rather than nine -- true, and not what these cases are about.
constexpr luaug::core::DVec3 CellCentre{128.0, 0.0, 128.0};

[[nodiscard]] StreamingFocus focusAt(luaug::core::DVec3 position, f64 minRadius, f64 loadRadius)
{
    StreamingFocus focus;
    focus.position = position;
    focus.minRadius = minRadius;
    focus.loadRadius = loadRadius;
    return focus;
}

} // namespace

TEST_CASE("a focus loads the chunks around it and nothing else")
{
    seedRealCatalog();
    Harness harness(gridIndex(4));

    const StreamingFocus foci[] = {focusAt(CellCentre, 256.0, 300.0)};
    harness.manager.setFoci(foci);

    StreamingBudget budget;
    budget.milliseconds = 1000.0;
    harness.settle(budget);

    // A 300 m radius from the centre of a 256 m cell reaches that cell and its
    // eight neighbours and no further: a diagonal neighbour's nearest point is
    // 181 m away, and the ring beyond starts at 384.
    CHECK(harness.manager.stats().resident == 9);
    CHECK(harness.manager.stateOf(ChunkId{0, 0, 0}) == ChunkState::Resident);
    CHECK(harness.manager.stateOf(ChunkId{1, 1, 0}) == ChunkState::Resident);
    CHECK(harness.manager.stateOf(ChunkId{2, 0, 0}) == ChunkState::Unloaded);
    CHECK(harness.manager.stateOf(ChunkId{4, 4, 0}) == ChunkState::Unloaded);
}

TEST_CASE("the nearest chunk is asked for first")
{
    seedRealCatalog();
    Harness harness(gridIndex(3));

    const StreamingFocus foci[] = {focusAt(CellCentre, 256.0, 700.0)};
    harness.manager.setFoci(foci);

    StreamingBudget budget;
    budget.milliseconds = 1000.0;
    budget.maxInFlight = 64;
    harness.manager.tick(budget);

    REQUIRE_FALSE(harness.loadRequests.empty());
    // The cell the focus stands in scores zero and nothing outranks it.
    CHECK(harness.loadRequests.front() == ChunkId{0, 0, 0});

    // And the order is monotonic in distance rather than in index order, which
    // is what "priority queue" means when the gate says it.
    f64 previous = -1.0;
    for (const ChunkId id : harness.loadRequests) {
        const f64 score = luaug::core::distanceSquared(chunkBounds(id, 256.0f), CellCentre);
        CHECK(score >= previous);
        previous = score;
    }
}

TEST_CASE("the time budget stops the work and the next frame resumes it")
{
    seedRealCatalog();
    Harness harness(gridIndex(4));

    const StreamingFocus foci[] = {focusAt(CellCentre, 256.0, 1200.0)};
    harness.manager.setFoci(foci);

    StreamingBudget generous;
    generous.milliseconds = 1000.0;
    generous.maxInFlight = 256;
    harness.manager.tick(generous);
    harness.deliverAllRequested();

    // Every chunk is decoded and waiting. Now materialise under a budget that
    // one chunk exhausts: exactly one may land per frame.
    StreamingBudget tight;
    tight.milliseconds = 0.5;
    harness.materializeCost = 10.0;

    harness.manager.tick(tight);
    const usize afterOne = harness.materialized.size();
    CHECK(afterOne == 1);

    harness.manager.tick(tight);
    CHECK(harness.materialized.size() == 2);

    // The budget is time and not a count, which is the roadmap's own note: a
    // cheap chunk does not consume a frame.
    harness.materializeCost = 0.0;
    harness.manager.tick(generous);
    CHECK(harness.materialized.size() > 3);
}

TEST_CASE("hysteresis keeps a focus on a boundary from thrashing")
{
    seedRealCatalog();
    Harness harness(gridIndex(3));

    StreamingBudget budget;
    budget.milliseconds = 1000.0;
    budget.maxInFlight = 64;

    // Just inside: the chunk two cells out is wanted.
    const StreamingFocus inside[] = {focusAt(CellCentre, 256.0, 520.0)};
    harness.manager.setFoci(inside);
    harness.settle(budget);
    REQUIRE(harness.manager.stateOf(ChunkId{2, 0, 0}) == ChunkState::Resident);
    harness.evicted.clear();

    // Step back out by a metre. Without hysteresis this is an eviction, and
    // stepping forward again is a reload -- once per frame, forever, and the
    // symptom is a stutter nobody can find.
    const StreamingFocus outside[] = {focusAt(CellCentre, 256.0, 380.0)};
    harness.manager.setFoci(outside);
    harness.manager.tick(budget);
    CHECK(harness.evicted.empty());
    CHECK(harness.manager.stateOf(ChunkId{2, 0, 0}) == ChunkState::Resident);

    // Far enough past the wider ring and it does go.
    const StreamingFocus away[] = {focusAt({-2000.0, 0.0, 0.0}, 256.0, 300.0)};
    harness.manager.setFoci(away);
    harness.manager.tick(budget);
    CHECK_FALSE(harness.evicted.empty());
}

TEST_CASE("a chunk that leaves the ring is evicted and its bytes stop counting")
{
    seedRealCatalog();
    Harness harness(gridIndex(4));

    StreamingBudget budget;
    budget.milliseconds = 1000.0;
    budget.maxInFlight = 64;

    const StreamingFocus here[] = {focusAt(CellCentre, 256.0, 300.0)};
    harness.manager.setFoci(here);
    harness.settle(budget);
    const luaug::core::u64 residentBytes = harness.manager.stats().bytesResident;
    CHECK(residentBytes > 0);

    const StreamingFocus faraway[] = {focusAt({4000.0, 0.0, 4000.0}, 256.0, 300.0)};
    harness.manager.setFoci(faraway);
    harness.manager.tick(budget);

    CHECK(harness.evicted.size() == 9);
    CHECK(harness.manager.stateOf(ChunkId{0, 0, 0}) == ChunkState::Unloaded);
    // The ceiling the gate measures is only meaningful if eviction actually
    // gives the bytes back.
    CHECK(harness.manager.stats().bytesResident < residentBytes);
    CHECK(harness.manager.stats().chunksEvicted == 9);
}

TEST_CASE("the in-flight cap bounds how much is outstanding at once")
{
    seedRealCatalog();
    Harness harness(gridIndex(4));

    const StreamingFocus foci[] = {focusAt(CellCentre, 256.0, 1200.0)};
    harness.manager.setFoci(foci);

    StreamingBudget budget;
    budget.milliseconds = 1000.0;
    budget.maxInFlight = 3;

    harness.manager.tick(budget);
    // Every one of those is a chunk payload in memory; the cap is what bounds
    // the peak the gate measures.
    CHECK(harness.loadRequests.size() == 3);

    harness.manager.tick(budget);
    CHECK(harness.loadRequests.size() == 3);

    harness.deliverAllRequested();
    harness.manager.tick(budget);
    CHECK(harness.loadRequests.size() == 3);
}

TEST_CASE("a refused load is retried and a failed one is not")
{
    seedRealCatalog();
    Harness harness(gridIndex(1));

    const StreamingFocus foci[] = {focusAt(CellCentre, 256.0, 300.0)};
    harness.manager.setFoci(foci);

    StreamingBudget budget;
    budget.milliseconds = 1000.0;
    budget.maxInFlight = 64;

    // An IO queue that is momentarily full is not a broken chunk.
    harness.refuseLoads = true;
    harness.manager.tick(budget);
    CHECK(harness.loadRequests.empty());
    CHECK(harness.manager.stateOf(ChunkId{0, 0, 0}) == ChunkState::Unloaded);

    harness.refuseLoads = false;
    harness.manager.tick(budget);
    CHECK_FALSE(harness.loadRequests.empty());

    // A chunk that failed once will fail again, and retrying it every frame is
    // how a broken world becomes an unresponsive one.
    harness.manager.onChunkFailed(ChunkId{0, 0, 0});
    CHECK(harness.manager.stateOf(ChunkId{0, 0, 0}) == ChunkState::Failed);
    harness.loadRequests.clear();
    for (int i = 0; i < 10; ++i) {
        harness.manager.tick(budget);
    }
    for (const ChunkId id : harness.loadRequests) {
        CHECK_FALSE(id == ChunkId{0, 0, 0});
    }
    CHECK(harness.manager.stats().failed == 1);
}

TEST_CASE("a chunk whose bytes are malformed fails rather than materialising")
{
    seedRealCatalog();
    Harness harness(gridIndex(1));

    const std::vector<std::byte> rubbish(64, std::byte{0x7e});
    harness.manager.onChunkLoaded(ChunkId{0, 0, 0}, rubbish);

    CHECK(harness.manager.stateOf(ChunkId{0, 0, 0}) == ChunkState::Failed);
    CHECK(harness.materialized.empty());
}

TEST_CASE("the minimum ring reports whether the world may be walked into")
{
    seedRealCatalog();
    Harness harness(gridIndex(3));

    const StreamingFocus foci[] = {focusAt(CellCentre, 300.0, 900.0)};
    harness.manager.setFoci(foci);

    // Nothing resident yet: architecture.md §10's integrity rule says the
    // character may not advance, and this is the question it asks.
    CHECK_FALSE(harness.manager.minimumRingResident());

    StreamingBudget budget;
    budget.milliseconds = 1000.0;
    budget.maxInFlight = 64;
    harness.settle(budget);
    CHECK(harness.manager.minimumRingResident());
}

TEST_CASE("disabling stops new work without forgetting the world")
{
    seedRealCatalog();
    Harness harness(gridIndex(2));

    const StreamingFocus foci[] = {focusAt(CellCentre, 256.0, 300.0)};
    harness.manager.setFoci(foci);

    StreamingBudget budget;
    budget.milliseconds = 1000.0;
    budget.maxInFlight = 64;
    harness.settle(budget);
    const luaug::core::u32 resident = harness.manager.stats().resident;
    REQUIRE(resident > 0);

    harness.manager.setEnabled(false);
    harness.loadRequests.clear();
    const StreamingFocus moved[] = {focusAt({1000.0, 0.0, 128.0}, 256.0, 300.0)};
    harness.manager.setFoci(moved);
    harness.manager.tick(budget);

    // Nothing new is scheduled; what was resident and is still wanted stays.
    CHECK(harness.loadRequests.empty());
    CHECK(harness.manager.stateOf(ChunkId{1, 0, 0}) == ChunkState::Resident);
}

TEST_CASE("the view reports every chunk for the overlay")
{
    seedRealCatalog();
    Harness harness(gridIndex(1));

    const StreamingFocus foci[] = {focusAt(CellCentre, 256.0, 100.0)};
    harness.manager.setFoci(foci);
    StreamingBudget budget;
    budget.milliseconds = 1000.0;
    harness.settle(budget);

    const std::vector<StreamingManager::ChunkView> view = harness.manager.view();
    CHECK(view.size() == 9);
    int resident = 0;
    for (const StreamingManager::ChunkView& entry : view) {
        if (entry.state == ChunkState::Resident) {
            resident += 1;
            CHECK(entry.score == 0.0);
        }
    }
    CHECK(resident == 1);
    CHECK(std::string(chunkStateName(ChunkState::Resident)) == "resident");
}

TEST_CASE("a size class keeps its own distance")
{
    seedRealCatalog();

    // The gate's own item (ADR 0053): with three layers configured, a large
    // object stays resident at a distance that has already evicted a small one.
    // A mountain and a pebble stop sharing a radius, which is the choice a
    // single grid forces and always resolves badly in one direction.
    ChunkIndex index;
    index.chunkSize = 256.0f;
    for (int layer = 0; layer < 3; ++layer) {
        for (int x = 0; x < 6; ++x) {
            ChunkIndexEntry entry;
            entry.id = ChunkId{x, 0, layer};
            entry.bounds = chunkBounds(entry.id, index.chunkSize);
            entry.bounds.min.y = -1.0;
            entry.bounds.max.y = 1.0;
            entry.urn = "asset://world/c.lchunk";
            entry.instanceCount = 1;
            index.chunks.push_back(entry);
        }
    }
    std::sort(index.chunks.begin(), index.chunks.end(),
              [](const ChunkIndexEntry& a, const ChunkIndexEntry& b) { return a.id < b.id; });

    Harness harness(index);
    StreamingFocus focus;
    focus.position = {128.0, 0.0, 128.0};
    focus.minRadius = 100.0;
    focus.loadRadius = 200.0;
    focus.layers[1] = StreamingLayerRadii{300.0, 600.0};
    focus.layers[2] = StreamingLayerRadii{600.0, 1200.0};
    harness.manager.setFoci({&focus, 1});

    StreamingBudget budget;
    budget.milliseconds = 1000.0;
    harness.settle(budget);

    const auto residentAt = [&harness](int x, int layer) {
        return harness.manager.stateOf(ChunkId{x, 0, layer}) == ChunkState::Resident;
    };

    // Cell 2 starts 384 m out and cell 4 starts 896 m out. Detail gave up on
    // both; structures keep the first and terrain keeps the second. Three
    // radii, three answers about the same piece of ground.
    CHECK(residentAt(0, 0));
    CHECK_FALSE(residentAt(2, 0));
    CHECK(residentAt(2, 1));
    CHECK_FALSE(residentAt(4, 1));
    CHECK(residentAt(4, 2));

    // And the must-have ring is asked per layer too, so a world whose terrain
    // ring is not yet resident is not walkable even where its props are.
    StreamingFocus far;
    far.position = {1400.0, 0.0, 128.0};
    far.minRadius = 100.0;
    far.loadRadius = 200.0;
    far.layers[2] = StreamingLayerRadii{2000.0, 2400.0};
    harness.manager.setFoci({&far, 1});
    harness.manager.tick(budget);
    CHECK_FALSE(harness.manager.minimumRingResident());
}

TEST_CASE("a layer with no radius of its own follows the focus's own pair")
{
    seedRealCatalog();

    // What makes a world built before layers meant anything behave exactly as
    // it did: every cell of it is layer 0, and layer 0 IS the base pair.
    ChunkIndex index = gridIndex(3);
    Harness harness(index);

    StreamingFocus focus;
    focus.position = {128.0, 0.0, 128.0};
    focus.minRadius = 200.0;
    focus.loadRadius = 400.0;
    CHECK(focus.loadRadiusFor(0) == doctest::Approx(400.0));
    CHECK(focus.loadRadiusFor(2) == doctest::Approx(400.0));
    CHECK(focus.minRadiusFor(1) == doctest::Approx(200.0));
    // A layer the array does not have -- which a hand-written index could name
    // -- takes the base pair rather than being refused.
    CHECK(focus.loadRadiusFor(9) == doctest::Approx(400.0));
    CHECK(focus.loadRadiusFor(-1) == doctest::Approx(400.0));

    harness.manager.setFoci({&focus, 1});
    StreamingBudget budget;
    budget.milliseconds = 1000.0;
    harness.settle(budget);
    CHECK(harness.manager.stats().resident > 0);
}

// --- A world with no focus (S8.2) --------------------------------------------

TEST_CASE("a world nobody is looking at loads everything, because it does not stream")
{
    // **"No focus" is not "a focus that wants nothing".** A project that streams
    // always registers one, so a project that has none has said it does not.
    // Treating that as "nothing is wanted" made every scene-based project
    // materialise an EMPTY world unless it knew the word
    // `StreamingMode = "Persistent"` -- which the starter template had to say,
    // about a fifteen-part scene, in order to appear at all. A one-word
    // incantation between somebody and their first frame is the worst shape a
    // default can have, because nothing on screen says what is missing.
    seedRealCatalog();
    Harness harness(gridIndex(1));
    harness.manager.setFoci({});

    StreamingBudget budget;
    budget.milliseconds = 1000.0;
    harness.settle(budget);

    CHECK(harness.manager.stats().resident == 9);
}

TEST_CASE("registering a focus takes the rule back off")
{
    // The cost of the rule, paid where it belongs: a project that registers its
    // focus late loads everything on the frames before it does, then evicts.
    // What must NOT happen is the everything-wanted state surviving a focus.
    seedRealCatalog();
    Harness harness(gridIndex(2));
    harness.manager.setFoci({});

    StreamingBudget budget;
    budget.milliseconds = 1000.0;
    harness.settle(budget);
    REQUIRE(harness.manager.stats().resident == 25);

    const StreamingFocus foci[] = {focusAt(CellCentre, 1.0, 1.0)};
    harness.manager.setFoci(foci);
    harness.settle(budget);

    // Everything outside the one cell is out of range now, so the eviction pass
    // takes it -- which is the proof the rule is off rather than merely unused.
    CHECK(harness.manager.stats().resident < 25);
    CHECK(harness.manager.stateOf(ChunkId{0, 0, 0}) == ChunkState::Resident);
}
