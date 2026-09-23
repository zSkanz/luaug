#include "luaug/scene/voxel_fluid.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace luaug::scene {
namespace {

using asset::BlockId;
using core::i32;
using core::u32;
using core::u64;
using core::usize;
using Position = std::array<i32, 3>;

constexpr std::array<Position, 6> Neighbours{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
constexpr std::array<std::array<i32, 2>, 4> Sides{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

[[nodiscard]] bool anyFluid(const VoxelComponent& voxels) noexcept
{
    return std::any_of(voxels.types.begin(), voxels.types.end(),
                       [](const VoxelBlockType& type) { return type.fluidReach > 0; });
}

// Asks for `at` to be looked at by `due`, keeping the earlier of two asks.
void schedule(VoxelComponent& voxels, const Position& at, u64 due)
{
    const auto [entry, inserted] = voxels.fluidWakes.emplace(at, due);
    if (!inserted)
        entry->second = std::min(entry->second, due);
}

void wakeAround(VoxelComponent& voxels, const Position& at, u64 due)
{
    schedule(voxels, at, due);
    for (const Position& step : Neighbours)
        schedule(voxels, Position{at[0] + step[0], at[1] + step[1], at[2] + step[2]}, due);
}

// What the block at `at` should be, from the grid as it stands. Pure: the step
// decides every block it visits this way before writing any of them.
[[nodiscard]] BlockId decide(const VoxelComponent& voxels, const Position& at)
{
    const asset::VoxelGrid& grid = voxels.grid;
    const BlockId here = grid.get(at[0], at[1], at[2]);
    const BlockId hereType = asset::blockTypeOf(here);
    const bool hereFluid = here != asset::AirBlock && isFluidType(voxels, hereType);
    // Solid blocks are no fluid's business, and a source stays a source.
    if (here != asset::AirBlock && !hereFluid)
        return here;
    if (hereFluid && asset::blockStateOf(here) == 0)
        return here;

    // Whether the fluid at `side` stands on something and can therefore spread:
    // a fluid over air pours rather than spreading, and one over its own
    // flowing water is still being fed from above.
    const auto spreads = [&](i32 x, i32 y, i32 z) {
        const BlockId below = grid.get(x, y - 1, z);
        if (below == asset::AirBlock)
            return false;
        const BlockId self = grid.get(x, y, z);
        return !(asset::blockTypeOf(below) == asset::blockTypeOf(self) && asset::blockStateOf(below) != 0);
    };

    // Which fluid this block is about: its own; else the one above it; else
    // the lowest-numbered type beside it that could spread here, so two fluids
    // meeting at a block settle it the same way everywhere.
    const BlockId above = grid.get(at[0], at[1] + 1, at[2]);
    const BlockId aboveType = asset::blockTypeOf(above);
    BlockId fluid = hereFluid ? hereType : asset::AirBlock;
    if (fluid == asset::AirBlock && above != asset::AirBlock && isFluidType(voxels, aboveType))
        fluid = aboveType;
    if (fluid == asset::AirBlock) {
        for (const std::array<i32, 2>& side : Sides) {
            const BlockId next = grid.get(at[0] + side[0], at[1], at[2] + side[1]);
            const BlockId nextType = asset::blockTypeOf(next);
            if (next == asset::AirBlock || !isFluidType(voxels, nextType))
                continue;
            if (!spreads(at[0] + side[0], at[1], at[2] + side[1]))
                continue;
            if (fluid == asset::AirBlock || nextType < fluid)
                fluid = nextType;
        }
    }
    if (fluid == asset::AirBlock)
        return here;

    // Down first: fed from above, a block falls, and a falling block is full.
    if (above != asset::AirBlock && aboveType == fluid)
        return asset::blockWithState(fluid, asset::FluidFalling);

    // Sideways: one level weaker than the strongest neighbour that spreads, a
    // falling block landing counting as a source.
    u32 nearest = asset::MaxFluidReach + 1;
    for (const std::array<i32, 2>& side : Sides) {
        const BlockId next = grid.get(at[0] + side[0], at[1], at[2] + side[1]);
        if (next == asset::AirBlock || asset::blockTypeOf(next) != fluid)
            continue;
        if (!spreads(at[0] + side[0], at[1], at[2] + side[1]))
            continue;
        const u32 state = asset::blockStateOf(next);
        const u32 level = (state & asset::FluidFalling) != 0 ? 0u : (state & asset::FluidLevelMask);
        nearest = std::min(nearest, level);
    }
    const u32 reach = voxels.types[fluid - 1u].fluidReach;
    if (nearest + 1 <= reach)
        return asset::blockWithState(fluid, nearest + 1);
    // Nothing feeds it: it drains.
    return asset::AirBlock;
}

} // namespace

bool isFluidType(const VoxelComponent& voxels, asset::BlockId type) noexcept
{
    return type != asset::AirBlock && type <= voxels.types.size() && voxels.types[type - 1u].fluidReach > 0;
}

void wakeFluids(VoxelComponent& voxels, i32 x, i32 y, i32 z)
{
    if (!anyFluid(voxels))
        return;
    // Due at once: the next step, whatever tick it runs on.
    wakeAround(voxels, Position{x, y, z}, 0);
}

void wakeFluidsInBox(VoxelComponent& voxels, i32 minX, i32 minY, i32 minZ, i32 maxX, i32 maxY, i32 maxZ)
{
    if (!anyFluid(voxels))
        return;
    if (minX > maxX)
        std::swap(minX, maxX);
    if (minY > maxY)
        std::swap(minY, maxY);
    if (minZ > maxZ)
        std::swap(minZ, maxZ);
    // The box's own shell and the layer outside it, face by face, so a box a
    // thousand blocks across costs its surface and not its volume.
    for (i32 y = minY - 1; y <= maxY + 1; ++y) {
        for (i32 z = minZ - 1; z <= maxZ + 1; ++z) {
            const bool edgeRow = y <= minY || y >= maxY || z <= minZ || z >= maxZ;
            if (edgeRow) {
                for (i32 x = minX - 1; x <= maxX + 1; ++x)
                    schedule(voxels, Position{x, y, z}, 0);
                continue;
            }
            for (const i32 x : {minX - 1, minX, maxX, maxX + 1})
                schedule(voxels, Position{x, y, z}, 0);
        }
    }
}

void wakeAllFluids(VoxelComponent& voxels)
{
    wakeFluidsIn(voxels, voxels.grid);
}

void wakeFluidsIn(VoxelComponent& voxels, const asset::VoxelGrid& arrived)
{
    if (!anyFluid(voxels))
        return;
    const auto edge = static_cast<i32>(asset::VoxelChunkEdge);
    for (const asset::VoxelChunkKey key : arrived.chunkKeys()) {
        const asset::VoxelChunk* chunk = arrived.findChunk(key);
        if (chunk == nullptr)
            continue;
        for (i32 y = 0; y < edge; ++y) {
            for (i32 z = 0; z < edge; ++z) {
                for (i32 x = 0; x < edge; ++x) {
                    const BlockId id =
                        chunk->blocks[asset::voxelIndex(static_cast<u32>(x), static_cast<u32>(y), static_cast<u32>(z))];
                    if (id != asset::AirBlock && isFluidType(voxels, asset::blockTypeOf(id)))
                        wakeAround(voxels, Position{key.x * edge + x, key.y * edge + y, key.z * edge + z}, 0);
                }
            }
        }
    }
}

u32 stepFluids(VoxelComponent& voxels, u64 tick)
{
    if (voxels.fluidWakes.empty())
        return 0;

    // What is due, in position order, up to the budget.
    std::vector<Position> due;
    for (auto entry = voxels.fluidWakes.begin();
         entry != voxels.fluidWakes.end() && due.size() < MaxFluidUpdatesPerTick;) {
        if (entry->second <= tick) {
            due.push_back(entry->first);
            entry = voxels.fluidWakes.erase(entry);
        }
        else {
            ++entry;
        }
    }
    if (due.empty())
        return 0;

    // Decide everything from the grid as it stood, then write everything.
    std::vector<std::pair<Position, BlockId>> writes;
    for (const Position& at : due) {
        const BlockId next = decide(voxels, at);
        if (next != voxels.grid.get(at[0], at[1], at[2]))
            writes.emplace_back(at, next);
    }
    for (const auto& [at, next] : writes) {
        const BlockId before = voxels.grid.get(at[0], at[1], at[2]);
        (void)voxels.grid.set(at[0], at[1], at[2], next);
        // At the pace of the fluid that moved -- the one arriving, or the one
        // that drained away.
        const BlockId type = asset::blockTypeOf(next != asset::AirBlock ? next : before);
        const u64 pace = isFluidType(voxels, type) ? std::max<u64>(voxels.types[type - 1u].fluidTicks, 1) : 1;
        for (const Position& step : Neighbours)
            schedule(voxels, Position{at[0] + step[0], at[1] + step[1], at[2] + step[2]}, tick + pace);
    }
    if (!writes.empty())
        voxels.revision += 1;
    return static_cast<u32>(writes.size());
}

} // namespace luaug::scene
