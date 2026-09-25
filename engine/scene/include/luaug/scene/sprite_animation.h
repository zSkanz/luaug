// `SpriteAnimator`'s step (ADR 0102): a sprite sheet played on the simulation
// clock.
//
// **It writes through the world's own verb**, `World::setProperty`, the way a
// tween does: the frame it lands on is its parent's `ImageRectOffset` and
// `ImageRectSize`, and a write the world did not see would be one the undo, the
// wire and a `Changed` listener all miss. Only a rectangle that actually moved
// is written, so a sprite held on one frame costs no change per tick.
#pragma once

#include "luaug/core/types.h"

namespace luaug::scene {

class World;

// Every animator in pool order, by `fixedDt` seconds. Called by the host in
// `PreAnimation`'s half of the tick, after its drain.
void stepSpriteAnimators(World& world, core::f64 fixedDt);

} // namespace luaug::scene
