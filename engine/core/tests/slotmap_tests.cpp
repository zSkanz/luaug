#include "luaug/core/slotmap.h"

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>

using luaug::core::InstanceId;
using luaug::core::SlotMap;
using luaug::core::u32;
using luaug::core::usize;

namespace doctest {

// Without this a failing handle comparison reads `{?} == {?}`, and the two
// halves of the handle are the whole point of every check below.
template <>
struct StringMaker<InstanceId>
{
    static String convert(const InstanceId& id)
    {
        return String("id{") + std::to_string(id.index).c_str() + ", gen " + std::to_string(id.generation).c_str() +
               "}";
    }
};

} // namespace doctest

namespace {

// Instances are not copyable in the ECS -- they own children, connections and
// eventually component storage -- so the container has to work for a type that
// can only be moved. A `std::vector<std::optional<T>>` that ever copied would
// fail to compile here rather than quietly copying in release.
struct MoveOnly
{
    explicit MoveOnly(int initial) noexcept : value(initial) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
    ~MoveOnly() = default;

    int value = 0;
};

} // namespace

TEST_CASE("insert hands back a handle that resolves to the value")
{
    SlotMap<int> map;

    const InstanceId first = map.insert(10);
    const InstanceId second = map.insert(20);

    REQUIRE(first.valid());
    REQUIRE(second.valid());
    CHECK(first != second);

    REQUIRE(map.find(first) != nullptr);
    REQUIRE(map.find(second) != nullptr);
    CHECK(*map.find(first) == 10);
    CHECK(*map.find(second) == 20);
    CHECK(map.contains(first));
    CHECK(map.size() == 2);
}

TEST_CASE("a default-constructed handle never resolves")
{
    // Generation 0 is the null handle, which is what zero-initialised memory
    // reads as. If this resolved, every uninitialised InstanceId in the engine
    // would silently point at slot 0.
    SlotMap<int> map;
    const InstanceId occupant = map.insert(1);
    CHECK(occupant.index == 0);

    CHECK(map.find(InstanceId{}) == nullptr);
    CHECK_FALSE(map.contains(InstanceId{}));
    // Same slot, null generation: the index is right and the handle is still
    // rejected.
    CHECK(map.find(InstanceId{0, 0}) == nullptr);
}

TEST_CASE("a handle to an index past the end does not resolve")
{
    SlotMap<int> map;
    (void)map.insert(1);

    CHECK(map.find(InstanceId{99, 1}) == nullptr);
}

TEST_CASE("a stale handle stops resolving the moment the slot is erased")
{
    SlotMap<int> map;
    const InstanceId id = map.insert(7);

    CHECK(map.erase(id));

    CHECK(map.find(id) == nullptr);
    CHECK_FALSE(map.contains(id));
    CHECK(map.size() == 0);
    CHECK(map.empty());
}

TEST_CASE("a reused slot rejects the handle its previous occupant had")
{
    // The reason the generation exists. Without it the second insert would hand
    // the old handle a live object of a different identity -- use-after-Destroy
    // reading as a successful call, which is the failure api-design.md's keyed
    // error was written to make impossible.
    SlotMap<int> map;

    const InstanceId original = map.insert(100);
    REQUIRE(map.erase(original));
    const InstanceId reused = map.insert(200);

    CHECK(reused.index == original.index);
    CHECK(reused.generation != original.generation);
    CHECK(map.find(original) == nullptr);
    REQUIRE(map.find(reused) != nullptr);
    CHECK(*map.find(reused) == 200);
}

TEST_CASE("erasing an already-stale handle reports failure rather than succeeding twice")
{
    SlotMap<int> map;
    const InstanceId id = map.insert(1);

    CHECK(map.erase(id));
    CHECK_FALSE(map.erase(id));
    CHECK_FALSE(map.erase(InstanceId{}));
    CHECK(map.size() == 0);

    // A double erase must not double-decrement the live count into a wrap, and
    // must not queue the slot twice: if it did, two later inserts would hand out
    // the same index.
    const InstanceId a = map.insert(2);
    const InstanceId b = map.insert(3);
    CHECK(a.index != b.index);
    CHECK(map.size() == 2);
}

TEST_CASE("clear empties the map without resurrecting any handle")
{
    SlotMap<int> map;
    const InstanceId first = map.insert(1);
    const InstanceId second = map.insert(2);

    map.clear();

    CHECK(map.size() == 0);
    CHECK(map.empty());
    CHECK(map.find(first) == nullptr);
    CHECK(map.find(second) == nullptr);
    // Slot storage survives -- clear frees occupants, not capacity.
    CHECK(map.slotCount() == 2);

    // Refilling must not make the pre-clear handles valid again: generations are
    // bumped by the clear, not reset by it.
    (void)map.insert(3);
    (void)map.insert(4);
    CHECK(map.find(first) == nullptr);
    CHECK(map.find(second) == nullptr);
    CHECK(map.size() == 2);
}

TEST_CASE("clear on an empty map is a no-op")
{
    SlotMap<int> map;
    map.clear();
    CHECK(map.size() == 0);
    CHECK(map.slotCount() == 0);

    const InstanceId id = map.insert(1);
    CHECK(map.find(id) != nullptr);
}

TEST_CASE("size counts occupants, slotCount counts what iteration walks")
{
    SlotMap<int> map;
    const InstanceId a = map.insert(1);
    (void)map.insert(2);
    (void)map.insert(3);

    CHECK(map.size() == 3);
    CHECK(map.slotCount() == 3);

    REQUIRE(map.erase(a));
    CHECK(map.size() == 2);
    // The hole is still a slot. Anything sizing a parallel array off slotCount
    // depends on this not shrinking.
    CHECK(map.slotCount() == 3);

    (void)map.insert(4);
    CHECK(map.size() == 3);
    CHECK(map.slotCount() == 3);
}

TEST_CASE("forEach visits live slots only, in ascending index order")
{
    // R10: iteration order has to be a pure function of the operation sequence.
    // Ascending slot index is that function, and this test is what stops a later
    // "optimisation" -- iterate the dense array, skip the free list -- from
    // quietly making it depend on erase order instead.
    SlotMap<int> map;
    std::vector<InstanceId> ids;
    for (int value = 0; value < 5; ++value)
        ids.push_back(map.insert(value * 10));

    REQUIRE(map.erase(ids[1]));
    REQUIRE(map.erase(ids[3]));

    std::vector<u32> visited;
    std::vector<int> values;
    map.forEach([&visited, &values](InstanceId id, int& value) {
        visited.push_back(id.index);
        values.push_back(value);
    });

    CHECK(visited == std::vector<u32>{0, 2, 4});
    CHECK(values == std::vector<int>{0, 20, 40});
}

TEST_CASE("forEach hands back handles that still resolve")
{
    SlotMap<int> map;
    (void)map.insert(1);
    const InstanceId second = map.insert(2);
    REQUIRE(map.erase(second));
    const InstanceId reused = map.insert(3);

    // The generation forEach reports must be the slot's current one, not the
    // one it had when the slot was first filled.
    std::vector<InstanceId> seen;
    map.forEach([&seen](InstanceId id, int&) { seen.push_back(id); });

    REQUIRE(seen.size() == 2);
    CHECK(seen[1] == reused);
    for (const InstanceId id : seen)
        CHECK(map.find(id) != nullptr);
}

TEST_CASE("forEach can mutate through its reference")
{
    SlotMap<int> map;
    const InstanceId id = map.insert(1);

    map.forEach([](InstanceId, int& value) { value += 41; });

    REQUIRE(map.find(id) != nullptr);
    CHECK(*map.find(id) == 42);
}

TEST_CASE("a const map reads through const find and const forEach")
{
    SlotMap<int> map;
    const InstanceId id = map.insert(5);

    const SlotMap<int>& readOnly = map;
    REQUIRE(readOnly.find(id) != nullptr);
    CHECK(*readOnly.find(id) == 5);

    usize visited = 0;
    readOnly.forEach([&visited](InstanceId, const int&) { ++visited; });
    CHECK(visited == 1);
}

TEST_CASE("slot reuse is last-freed-first-filled")
{
    // LIFO is not an arbitrary choice presented as one: it is the property that
    // makes the index a pure function of the operation sequence, so two runs of
    // the same script hand out the same indices. Pinning it here means a change
    // to the free list shows up as a failing test rather than as a replay that
    // diverges on tick 4000.
    SlotMap<int> map;
    std::vector<InstanceId> ids;
    for (int value = 0; value < 4; ++value)
        ids.push_back(map.insert(value));

    REQUIRE(map.erase(ids[1]));
    REQUIRE(map.erase(ids[3]));

    CHECK(map.insert(10).index == 3);
    CHECK(map.insert(11).index == 1);
    // Free list exhausted: the next one has to extend the storage.
    CHECK(map.insert(12).index == 4);
}

TEST_CASE("a move-only value type is stored, found, erased and iterated")
{
    SlotMap<MoveOnly> map;

    const InstanceId first = map.insert(MoveOnly{1});
    const InstanceId second = map.insert(MoveOnly{2});

    REQUIRE(map.find(first) != nullptr);
    CHECK(map.find(first)->value == 1);

    REQUIRE(map.erase(first));
    CHECK(map.find(first) == nullptr);

    // Reusing the freed slot has to move-assign over a disengaged optional, not
    // copy-assign over a live one.
    const InstanceId reused = map.insert(MoveOnly{3});
    CHECK(reused.index == first.index);
    REQUIRE(map.find(reused) != nullptr);
    CHECK(map.find(reused)->value == 3);

    std::vector<int> values;
    map.forEach([&values](InstanceId, MoveOnly& item) { values.push_back(item.value); });
    CHECK(values == std::vector<int>{3, 2});

    CHECK(map.find(second)->value == 2);
}

TEST_CASE("erase destroys the value it held")
{
    // Destroy has to actually run destructors: a slot map that only marked slots
    // free would leak every instance the world ever created, and the leak would
    // be invisible until a long session ran out of memory.
    std::weak_ptr<int> observer;

    {
        auto owned = std::make_shared<int>(7);
        observer = owned;
        SlotMap<std::shared_ptr<int>> shared;
        const InstanceId id = shared.insert(std::move(owned));
        REQUIRE_FALSE(observer.expired());
        REQUIRE(shared.erase(id));
        CHECK(observer.expired());
    }

    // And clear() has to do the same, not merely reset the count.
    {
        auto owned = std::make_shared<int>(8);
        observer = owned;
        SlotMap<std::shared_ptr<int>> shared;
        (void)shared.insert(std::move(owned));
        REQUIRE_FALSE(observer.expired());
        shared.clear();
        CHECK(observer.expired());
    }
}

TEST_CASE("many inserts and erases keep the map consistent")
{
    // Deliberately not random: a churn test that seeds itself from anything but
    // a constant reproduces a failure once and never again.
    SlotMap<int> map;
    std::vector<InstanceId> live;

    for (int round = 0; round < 200; ++round) {
        live.push_back(map.insert(round));
        if (round % 3 == 0 && live.size() >= 2) {
            const InstanceId victim = live.front();
            live.erase(live.begin());
            CHECK(map.erase(victim));
            CHECK(map.find(victim) == nullptr);
        }
    }

    CHECK(map.size() == live.size());
    for (const InstanceId id : live)
        CHECK(map.find(id) != nullptr);

    usize walked = 0;
    map.forEach([&walked](InstanceId, int&) { ++walked; });
    CHECK(walked == map.size());
}
