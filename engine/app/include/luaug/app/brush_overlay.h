// The sculpting brush: where a drag puts its stamps, and the ring you aim with
// (F1 Part F).
//
// Its own unit for the reason `reference_grid`, `skeleton_overlay` and
// `chunk_overlay` are: **the part that can be wrong is arithmetic**, and a bug
// that only reproduces by dragging is one nobody fixes twice. How far apart the
// stamps of a stroke fall, and how a ring lies on a surface it is not
// perpendicular to, are both functions over numbers that a test can drive with
// no window, no GPU and no pointer.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <vector>

namespace luaug::render {
class DebugDraw;
}

namespace luaug::app {

// Where a stroke between two pointer positions puts its edits.
//
// **A drag is sampled once a frame and a brush is not a rubber stamp**: at a
// hundred and twenty frames a second a slow drag stamps on top of itself, and at
// thirty a fast one leaves a dotted line of craters with gaps between them. So
// the stroke is walked at a fixed spacing in WORLD space rather than once per
// frame, which makes the result a function of where the pointer went and not of
// how fast the machine was.
//
// `spacing` is a fraction of the radius: a brush that stamps every quarter of
// its own width overlaps enough to read as a stroke. Zero or negative spacing
// falls back to one stamp at `to`, because a spacing that cannot advance would
// otherwise be an unbounded loop.
//
// **`from` equal to `to` is one stamp, not none.** That is the first frame of
// every drag and also a click, and a brush that did nothing until the pointer
// moved would feel broken in exactly the case somebody tries first.
//
// **The walk starts at `from` and the remainder is left at the `to` end**, so a
// caller continuing a stroke resumes from the last stamp exactly and the
// unwalked fraction is carried by the pointer into the next frame. Stamping `to`
// instead would put that fraction at the `from` end where a continuing caller
// drops it, and the loss accumulates -- see the source for the numbers.
[[nodiscard]] std::vector<core::DVec3> strokeStamps(core::DVec3 from, core::DVec3 to, double radius, double spacing);

// Whether the pointer has travelled far enough since the last stamp to deserve
// another one.
//
// **This is the other half of "a drag is not a rubber stamp", and without it
// `strokeStamps` alone is not enough.** `strokeStamps` always stamps `to`, which
// is right for a click and for the end of a segment; called every frame with the
// pointer's current position it would stamp once per frame again, and the ground
// left behind would depend on the framerate after all. So a stroke advances only
// in whole steps: below one, the frame stamps nothing and the stroke's anchor
// stays where it was, accumulating the movement until it is worth a stamp.
//
// The visible cost is that the newest stamp can lag the pointer by up to one
// step -- a quarter of a brush width at the default spacing, which is inside the
// brush that is already there. The ring is what follows the pointer exactly.
[[nodiscard]] bool strokeAdvanced(core::DVec3 from, core::DVec3 to, double radius, double spacing);

// The maximum number of stamps one call may produce.
//
// **A ceiling rather than a budget**, and it is about a pathological input
// rather than performance: a pointer that jumps a kilometre -- an alt-tab, a
// teleport, a camera cut mid-drag -- would otherwise ask for a hundred thousand
// edits in one frame. Past the ceiling the stroke is the endpoints and nothing
// between, which is visibly a gap rather than a hang.
inline constexpr core::usize MaxStrokeStamps = 64;

// Draws the aiming ring: a circle of `radius` about `centre`, lying in the plane
// `normal` describes, plus a short stalk along the normal so the brush reads as
// sitting ON a surface rather than floating in front of it.
//
// **Lines only, and that is a limit rather than a style.** `DebugDraw` is a line
// list and stays one; a shaded falloff decal is not expressible here and is not
// attempted -- it needs either a second pipeline (the debug one carries no depth
// state at all) or an editor-appended draw item, and there is no hook after
// extraction to append one from.
//
// **Submit it AFTER `DebugDraw::rebaseTo`.** The rebase subtracts in `f32`, and
// a world-space submission costs half a millimetre at four kilometres from the
// origin -- on the one thing in the frame being placed precisely.
void drawBrushRing(core::Vec3 centre, core::Vec3 normal, float radius, render::DebugDraw& debug);

} // namespace luaug::app
