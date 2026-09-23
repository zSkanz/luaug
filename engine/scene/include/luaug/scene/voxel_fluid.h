// Fluids in the block world (V1): water that finds its level.
//
// **The rules are the ones block games settled on**, because players already
// know them and they are cheap:
//
// - A block of a fluid type placed as itself is a SOURCE, and stays.
// - Fluid flows DOWN first: the block under any fluid fills, falling, and a
//   falling block is full.
// - A fluid that cannot fall spreads SIDEWAYS, one level weaker per block, up to
//   its type's reach. Only a fluid standing on something spreads; one over air
//   pours instead.
// - A flowing block that nothing feeds any more drains away, one step at a time,
//   back towards whatever still does.
//
// **Stepped on the simulation clock, and only where something changed.** A
// world full of still lakes costs nothing: a block is looked at only when it,
// or a neighbour, was changed -- by a script, by the editor, or by the step
// before. Each fluid type has its own pace, in ticks per step.
//
// **Deterministic (R10).** The blocks due are visited in position order, every
// new state is decided from the grid as it stood before the step, and then all
// of them are written: no block sees a neighbour's half-finished step, and the
// same world and the same tick make the same water on every machine.
#pragma once

#include "luaug/core/types.h"
#include "luaug/scene/components.h"

namespace luaug::scene {

// The most blocks one tick's step decides. The rest wait for the next tick, in
// the same order, so a flood the size of a lake spreads over a few ticks rather
// than costing one of them a second.
inline constexpr core::u32 MaxFluidUpdatesPerTick = 8192;

// Whether a block TYPE is a fluid in this registry.
[[nodiscard]] bool isFluidType(const VoxelComponent& voxels, asset::BlockId type) noexcept;

// Asks the next step to look at a block and its six neighbours, because one of
// them changed. What a script or a tool calls after writing the grid; a world
// with no fluid type registered does nothing here.
void wakeFluids(VoxelComponent& voxels, core::i32 x, core::i32 y, core::i32 z);

// The same for a box and the blocks around it -- after `FillBlocks`. Only its
// shell and the layer outside it are woken: inside a filled box every block's
// neighbours are the same block, and nothing there can change.
void wakeFluidsInBox(VoxelComponent& voxels, core::i32 minX, core::i32 minY, core::i32 minZ, core::i32 maxX,
                     core::i32 maxY, core::i32 maxZ);

// Wakes every fluid block in the world: after a scene is read, whose water was
// saved without the steps it was due, and would otherwise stand still until
// something next to it changed.
void wakeAllFluids(VoxelComponent& voxels);

// The same for the blocks of `arrived` only -- a streamed cell just merged into
// the world, whose water was written down without its steps too.
void wakeFluidsIn(VoxelComponent& voxels, const asset::VoxelGrid& arrived);

// One tick of every fluid. `tick` is the simulation's own count. Returns how
// many blocks changed; bumps `revision` when any did.
core::u32 stepFluids(VoxelComponent& voxels, core::u64 tick);

} // namespace luaug::scene
