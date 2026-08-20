#include <doctest/doctest.h>

// doctest stringifies whatever a CHECK compares, and that needs the stream
// operators for std::string and std::string_view to be visible here.
#include <ostream>

#include <string>
#include <vector>

#include "luaug/core/slotmap.h"
#include "luaug/scene/component_pool.h"

using luaug::core::InstanceId;
using luaug::core::SlotMap;
using luaug::core::usize;
using luaug::scene::ComponentPool;

namespace
{

struct Payload
{
    int value = 0;
};

// Ids come from a real SlotMap rather than being hand-built, because the
// property under test -- that a recycled slot does not inherit the previous
// occupant's component -- depends on the generations a SlotMap actually hands
// out.
struct Ids
{
    SlotMap<int> slots;

    InstanceId next() { return slots.insert(0); }
};

} // namespace

TEST_CASE("component pool stores and finds by id")
{
    Ids ids;
    ComponentPool<Payload> pool;

    const InstanceId a = ids.next();
    const InstanceId b = ids.next();

    pool.add(a, Payload{1});
    pool.add(b, Payload{2});

    REQUIRE(pool.find(a) != nullptr);
    CHECK(pool.find(a)->value == 1);
    CHECK(pool.find(b)->value == 2);
    CHECK(pool.contains(a));
    CHECK(pool.size() == 2);

    CHECK(pool.find(InstanceId{}) == nullptr);
    CHECK(pool.find(InstanceId{999, 1}) == nullptr);
}

TEST_CASE("adding twice overwrites rather than duplicating")
{
    Ids ids;
    ComponentPool<Payload> pool;
    const InstanceId a = ids.next();

    pool.add(a, Payload{1});
    pool.add(a, Payload{7});

    CHECK(pool.size() == 1);
    CHECK(pool.denseSize() == 1);
    CHECK(pool.find(a)->value == 7);
}

TEST_CASE("a recycled slot does not inherit the previous occupant's component")
{
    Ids ids;
    ComponentPool<Payload> pool;

    const InstanceId original = ids.next();
    pool.add(original, Payload{42});
    ids.slots.erase(original);

    const InstanceId recycled = ids.slots.insert(0);
    REQUIRE(recycled.index == original.index);
    REQUIRE(recycled.generation != original.generation);

    // The whole reason the pool checks the owner rather than only the sparse
    // index: without it the new instance silently adopts the old one's state.
    CHECK(pool.find(recycled) == nullptr);

    // The old component is still there, because nothing told the pool its owner
    // died -- the pool knows only what it is told, and in the engine
    // `retireDestroyed` is what tells it. What must NOT happen is the stale
    // entry outliving the slot: adding for the recycled id reuses that dense
    // slot rather than appending beside it, so `forEach` never yields a
    // component whose entity is gone.
    REQUIRE(pool.find(original) != nullptr);
    pool.add(recycled, Payload{7});

    CHECK(pool.find(original) == nullptr);
    CHECK(pool.find(recycled)->value == 7);
    CHECK(pool.size() == 1);
    CHECK(pool.denseSize() == 1);

    usize visited = 0;
    pool.forEach([&](InstanceId owner, Payload&) {
        ++visited;
        CHECK(owner == recycled);
    });
    CHECK(visited == 1);
}

TEST_CASE("remove leaves a hole that iteration skips")
{
    Ids ids;
    ComponentPool<Payload> pool;
    const InstanceId a = ids.next();
    const InstanceId b = ids.next();
    const InstanceId c = ids.next();

    pool.add(a, Payload{1});
    pool.add(b, Payload{2});
    pool.add(c, Payload{3});
    CHECK(pool.remove(b));
    CHECK_FALSE(pool.remove(b));

    CHECK(pool.size() == 2);
    // The hole keeps its position, so the dense array is still three long.
    CHECK(pool.denseSize() == 3);

    std::vector<int> seen;
    pool.forEach([&](InstanceId, Payload& payload) { seen.push_back(payload.value); });
    CHECK(seen == std::vector<int>{1, 3});
}

TEST_CASE("iteration is dense insertion order and a removal does not reorder survivors")
{
    Ids ids;
    ComponentPool<Payload> pool;
    std::vector<InstanceId> created;
    for (int index = 0; index < 6; ++index)
    {
        const InstanceId id = ids.next();
        created.push_back(id);
        pool.add(id, Payload{index});
    }

    // This is the property the whole sparse-set design exists for (R10): a
    // swap-and-pop would be faster and would make the order depend on removal
    // history, which is exactly what a deterministic simulation cannot have.
    pool.remove(created[1]);
    pool.remove(created[4]);

    std::vector<int> seen;
    pool.forEach([&](InstanceId, Payload& payload) { seen.push_back(payload.value); });
    CHECK(seen == std::vector<int>{0, 2, 3, 5});
}

TEST_CASE("compact closes holes and preserves relative order")
{
    Ids ids;
    ComponentPool<Payload> pool;
    std::vector<InstanceId> created;
    for (int index = 0; index < 5; ++index)
    {
        const InstanceId id = ids.next();
        created.push_back(id);
        pool.add(id, Payload{index});
    }

    pool.remove(created[0]);
    pool.remove(created[2]);
    pool.compact();

    CHECK(pool.size() == 3);
    CHECK(pool.denseSize() == 3);

    std::vector<int> seen;
    pool.forEach([&](InstanceId, Payload& payload) { seen.push_back(payload.value); });
    CHECK(seen == std::vector<int>{1, 3, 4});

    // Every survivor is still reachable by id: compaction moved the payloads,
    // so the sparse indices had to move with them.
    CHECK(pool.find(created[1])->value == 1);
    CHECK(pool.find(created[3])->value == 3);
    CHECK(pool.find(created[4])->value == 4);
    CHECK(pool.find(created[0]) == nullptr);
    CHECK(pool.find(created[2]) == nullptr);
}

TEST_CASE("clear empties the pool")
{
    Ids ids;
    ComponentPool<Payload> pool;
    const InstanceId a = ids.next();
    pool.add(a, Payload{1});

    pool.clear();
    CHECK(pool.size() == 0);
    CHECK(pool.denseSize() == 0);
    CHECK(pool.find(a) == nullptr);
}

TEST_CASE("a non-trivial payload survives the pool")
{
    Ids ids;
    ComponentPool<std::string> pool;
    const InstanceId a = ids.next();
    const InstanceId b = ids.next();

    pool.add(a, std::string("first"));
    pool.add(b, std::string("second"));
    pool.remove(a);
    pool.compact();

    REQUIRE(pool.find(b) != nullptr);
    CHECK(*pool.find(b) == "second");
}
