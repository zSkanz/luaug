// The reference grid the viewport draws while placing something (S5.13).
//
// **Lines you can see and a snap you cannot are two grids**, and the one that
// catches is the invisible one. So this draws at the SAME spacing the translate
// snap uses rather than at a round number of its own: turning it on is how "why
// did it land there" stops being a question.
//
// Its own unit for the reason `skeleton_overlay` and `chunk_overlay` are: the
// part that can be wrong is arithmetic -- how far the grid reaches, where it is
// centred, how it thins out when the step is small and the camera is high -- and
// a free function over a step and a camera is a function a test can drive.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

namespace luaug::render {
class DebugDraw;
}

namespace luaug::app {

// How many lines the grid is allowed to draw in each direction from the centre.
//
// **A cap and not a radius**, because the step is a person's choice and a
// millimetre step over a hundred metres is two hundred thousand lines. Past the
// cap the grid thins -- it draws every tenth line instead of every one -- which
// keeps it a picture of the same grid rather than a picture of a smaller one.
inline constexpr core::i32 kGridHalfLines = 60;

// Appends a grid of `step` metres centred under `focus`, in WORLD space, which
// is what every other debug line records -- `DebugDraw` rebases the whole buffer
// once after extraction (D011).
//
// The grid is drawn on the plane `y = groundY`, snapped to the step so its lines
// land where the snap does. Appends nothing for a step that is not positive.
void drawReferenceGrid(core::DVec3 focus, core::f64 groundY, core::f32 step, render::DebugDraw& draw);

// What `drawReferenceGrid` will actually use: the step it draws at after
// thinning, which is `step` times a power of ten. Exposed so a test can assert
// the thinning rather than counting lines.
[[nodiscard]] core::f32 referenceGridStep(core::f32 step, core::f64 reach) noexcept;

} // namespace luaug::app
