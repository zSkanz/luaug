// The outline of a tilemap's solid tiles (the 2D layer, post-v1 phase 3).
//
// **A floor of tiles collides as one line, not as a row of boxes.** A box per
// tile -- or per run of tiles -- leaves seams, and a character sliding along
// a floor or a wall catches on the corner of the next box: the "ghost
// collision" every 2D engine has had to solve. The answer is the outline:
// every edge between a solid cell and an empty one, joined into closed loops
// around each solid region, straight runs merged into single segments.
#pragma once

#include "luaug/core/math.h"
#include "luaug/scene/components.h"

#include <vector>

namespace luaug::scene {

// Each loop goes counter-clockwise around solid ground, so the ground is on
// the left of every segment and the outside -- what collides -- on the right.
// In cell units: point (x, y) is the corner of cell (x, y) nearest the origin.
// Where two regions touch only at a corner, each gets a loop of its own.
// Loops in a deterministic order (R10): by the corner each starts from, leftmost
// and then lowest first.
[[nodiscard]] std::vector<std::vector<core::Vec2>> tileOutlines(const Tilemap2DComponent& tilemap);

} // namespace luaug::scene
