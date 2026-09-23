// Fluids in the block world (V1): the rules, and that they are deterministic.
#include "luaug/scene/voxel_fluid.h"

#include <doctest/doctest.h>
#include <utility>

using namespace luaug;
using asset::BlockId;

namespace {

constexpr BlockId Stone = 1;
constexpr BlockId Water = 2;

// A stone floor at y = -1, forty blocks square, and water that reaches four
// and moves every tick.
struct Pond
{
    scene::VoxelComponent voxels;
    core::u64 tick = 0;

    Pond()
    {
        voxels.types.push_back(scene::VoxelBlockType{});
        scene::VoxelBlockType water;
        water.fluidReach = 4;
        water.fluidTicks = 1;
        voxels.types.push_back(water);
        (void)voxels.grid.fill(-20, -1, -20, 20, -1, 20, Stone);
    }

    void place(core::i32 x, core::i32 y, core::i32 z, BlockId id)
    {
        (void)voxels.grid.set(x, y, z, id);
        scene::wakeFluids(voxels, x, y, z);
    }

    void run(int ticks)
    {
        for (int at = 0; at < ticks; ++at)
            (void)scene::stepFluids(voxels, ++tick);
    }

    [[nodiscard]] BlockId at(core::i32 x, core::i32 y, core::i32 z) const { return voxels.grid.get(x, y, z); }

    [[nodiscard]] int waterBlocks() const
    {
        int count = 0;
        for (core::i32 y = -1; y <= 8; ++y) {
            for (core::i32 z = -12; z <= 12; ++z) {
                for (core::i32 x = -12; x <= 12; ++x)
                    count += asset::blockTypeOf(at(x, y, z)) == Water ? 1 : 0;
            }
        }
        return count;
    }
};

} // namespace

TEST_CASE("a source on a floor spreads its reach and no further, one level shallower per block")
{
    Pond pond;
    pond.place(0, 0, 0, Water);
    pond.run(20);

    CHECK(pond.at(0, 0, 0) == Water);
    for (core::i32 out = 1; out <= 4; ++out) {
        CHECK(pond.at(out, 0, 0) == asset::blockWithState(Water, static_cast<core::u32>(out)));
        CHECK(pond.at(0, 0, -out) == asset::blockWithState(Water, static_cast<core::u32>(out)));
    }
    CHECK(pond.at(5, 0, 0) == asset::AirBlock);
    // Distance is counted in steps, so a diagonal block is as far as its two
    // sides added.
    CHECK(pond.at(2, 0, 2) == asset::blockWithState(Water, 4));
    CHECK(pond.at(3, 0, 2) == asset::AirBlock);
    // **Settled water costs nothing**: nothing is left due.
    CHECK(pond.voxels.fluidWakes.empty());
    // A fluid never rises: nothing appeared above the source.
    CHECK(pond.at(0, 1, 0) == asset::AirBlock);
}

TEST_CASE("a fluid falls before it spreads, and lands as if it were a source")
{
    Pond pond;
    // A pillar five blocks tall with a source on top.
    (void)pond.voxels.grid.fill(0, 0, 0, 0, 4, 0, Stone);
    pond.place(0, 5, 0, Water);
    pond.run(40);

    // Off the pillar's edge, the first block out has nothing under it: it is
    // there, and it pours rather than spreading.
    CHECK(pond.at(1, 5, 0) == asset::blockWithState(Water, 1));
    CHECK(pond.at(2, 5, 0) == asset::AirBlock);
    for (core::i32 y = 0; y <= 4; ++y)
        CHECK(pond.at(1, y, 0) == asset::blockWithState(Water, asset::FluidFalling));
    // Landed on the floor, the fall spreads its whole reach again.
    CHECK(pond.at(5, 0, 0) == asset::blockWithState(Water, 4));
    CHECK(pond.at(6, 0, 0) == asset::AirBlock);
}

TEST_CASE("a wall holds water back until it is broken")
{
    Pond pond;
    (void)pond.voxels.grid.fill(2, 0, -6, 2, 2, 6, Stone);
    pond.place(0, 0, 0, Water);
    pond.run(20);
    CHECK(pond.at(3, 0, 0) == asset::AirBlock);

    pond.place(2, 0, 0, asset::AirBlock);
    pond.run(20);
    CHECK(pond.at(2, 0, 0) == asset::blockWithState(Water, 2));
    CHECK(pond.at(3, 0, 0) == asset::blockWithState(Water, 3));
}

TEST_CASE("take the source away and what it fed drains")
{
    Pond pond;
    (void)pond.voxels.grid.fill(0, 0, 0, 0, 2, 0, Stone);
    pond.place(0, 3, 0, Water);
    pond.run(30);
    REQUIRE(pond.waterBlocks() > 20);

    pond.place(0, 3, 0, asset::AirBlock);
    pond.run(60);
    CHECK(pond.waterBlocks() == 0);
    CHECK(pond.voxels.fluidWakes.empty());
}

TEST_CASE("the same world and the same ticks make the same water, tick for tick")
{
    Pond first;
    Pond second;
    for (Pond* pond : {&first, &second}) {
        (void)pond->voxels.grid.fill(-3, 0, 4, 3, 1, 4, Stone);
        pond->place(0, 2, 0, Water);
        pond->place(-4, 0, -4, Water);
    }
    for (int tick = 0; tick < 40; ++tick) {
        first.run(1);
        second.run(1);
        REQUIRE(first.voxels.grid.digest() == second.voxels.grid.digest());
        REQUIRE(first.voxels.fluidWakes == second.voxels.fluidWakes);
    }
}

TEST_CASE("a slower fluid moves at its own pace, and one that is not a fluid never moves")
{
    Pond pond;
    pond.voxels.types[Water - 1u].fluidTicks = 10;
    pond.place(0, 0, 0, Water);
    pond.run(1);
    CHECK(pond.at(1, 0, 0) == asset::blockWithState(Water, 1)); // woken: due at once
    pond.run(5);
    CHECK(pond.at(2, 0, 0) == asset::AirBlock); // the next step is ten ticks on
    pond.run(10);
    CHECK(pond.at(2, 0, 0) == asset::blockWithState(Water, 2));

    Pond still;
    still.voxels.types[Water - 1u].fluidReach = 0;
    still.place(0, 0, 0, Water);
    still.run(20);
    CHECK(still.at(1, 0, 0) == asset::AirBlock);
    CHECK(still.voxels.fluidWakes.empty());
}

TEST_CASE("water that arrives in a streamed cell is woken, and nothing else is")
{
    Pond pond;
    // Already in the world, and still: nothing wakes it.
    (void)pond.voxels.grid.set(-10, 0, -10, Water);
    // What a streamed cell brings: a source with room to spread.
    asset::VoxelGrid arrived;
    (void)arrived.set(10, 0, 10, Water);
    (void)pond.voxels.grid.set(10, 0, 10, Water);

    scene::wakeFluidsIn(pond.voxels, arrived);
    pond.run(10);
    CHECK(pond.at(11, 0, 10) == asset::blockWithState(Water, 1));
    CHECK(pond.at(-9, 0, -10) == asset::AirBlock);
}

TEST_CASE("lava that reaches water sets as stone where they meet, and the water stays water")
{
    Pond pond;
    constexpr BlockId Lava = 3;
    scene::VoxelBlockType lava;
    lava.fluidReach = 3;
    lava.fluidTicks = 1;
    pond.voxels.types.push_back(lava);
    scene::setFluidReaction(pond.voxels, Lava, Water, Stone);
    REQUIRE(pond.voxels.fluidReactions.size() == 1);

    // Water spreading from the left, lava from the right: they meet between.
    pond.place(-4, 0, 0, Water);
    pond.place(4, 0, 0, Lava);
    pond.run(20);

    // Where lava would have touched water, there is stone instead, and the
    // water around it is still water.
    bool setStone = false;
    for (core::i32 x = -3; x <= 3; ++x) {
        if (pond.at(x, 0, 0) == Stone)
            setStone = true;
    }
    CHECK(setStone);
    CHECK(asset::blockTypeOf(pond.at(-3, 0, 0)) == Water);
    CHECK(pond.at(4, 0, 0) == Lava);
    // No lava block anywhere touches water.
    for (core::i32 x = -8; x <= 8; ++x) {
        for (core::i32 z = -8; z <= 8; ++z) {
            if (asset::blockTypeOf(pond.at(x, 0, z)) != Lava)
                continue;
            for (const auto& [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}})
                CHECK(asset::blockTypeOf(pond.at(x + dx, 0, z + dz)) != Water);
        }
    }

    // Removed, it is gone.
    scene::setFluidReaction(pond.voxels, Lava, Water, asset::AirBlock);
    CHECK(pond.voxels.fluidReactions.empty());
}
