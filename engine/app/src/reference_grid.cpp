#include "luaug/app/reference_grid.h"

#include "luaug/render/debug_draw.h"

#include <cmath>

namespace luaug::app {

using core::f32;
using core::f64;
using core::i32;
using core::i64;

namespace {

// Dim, because a grid is a reference and not a subject: it has to be readable
// under the thing being placed rather than over it.
constexpr render::DebugColor kLineColor = render::DebugColor::fromLinear(0.30f, 0.32f, 0.36f);
// Every tenth line, brighter, so the eye can count without following one line
// across the whole picture. Ten because the step is nearly always a round
// number and ten of a round number is another one.
constexpr render::DebugColor kMajorColor = render::DebugColor::fromLinear(0.46f, 0.49f, 0.55f);
constexpr i32 kMajorEvery = 10;

} // namespace

f32 referenceGridStep(f32 step, f64 reach) noexcept
{
    if (!(step > 0.0f) || !(reach > 0.0))
        return step;

    // **Multiplied by ten until it fits, rather than divided into a fixed
    // count.** A grid whose spacing is the reach over sixty lines is a different
    // grid at every camera height, and one that moves as you fly is not a
    // reference. Powers of ten keep every line drawn a line the snap would also
    // have produced -- so what is on screen is always a subset of the real grid,
    // never a rounder one beside it.
    f32 spacing = step;
    const auto span = static_cast<f64>(kGridHalfLines);
    for (int decade = 0; decade < 8 && static_cast<f64>(spacing) * span < reach; ++decade)
        spacing *= 10.0f;
    return spacing;
}

void drawReferenceGrid(core::DVec3 focus, f64 groundY, f32 step, render::DebugDraw& draw)
{
    if (!(step > 0.0f))
        return;

    // How far out the grid needs to reach to be useful: proportional to how high
    // the camera is, because that is what decides how much ground is on screen.
    // Floored so a camera at ground level still has a grid around it.
    const f64 reach = std::max(std::abs(focus.y - groundY) * 3.0, 8.0);
    const f64 spacing = static_cast<f64>(referenceGridStep(step, reach));

    // **Snapped to the spacing**, so the lines land where the snap does. A grid
    // centred on the camera would slide under the thing being placed and line up
    // with nothing.
    const f64 centreX = std::floor(focus.x / spacing) * spacing;
    const f64 centreZ = std::floor(focus.z / spacing) * spacing;
    const f64 half = spacing * static_cast<f64>(kGridHalfLines);

    for (i32 index = -kGridHalfLines; index <= kGridHalfLines; ++index) {
        const f64 offset = static_cast<f64>(index) * spacing;
        // Counted from the WORLD origin rather than from the camera, so the
        // bright lines stay on the same world coordinates as the view moves --
        // a major line that slid about would be worse than none.
        const auto worldIndexX = static_cast<i64>(std::llround((centreX + offset) / spacing));
        const auto worldIndexZ = static_cast<i64>(std::llround((centreZ + offset) / spacing));

        const render::DebugColor alongZ = worldIndexX % kMajorEvery == 0 ? kMajorColor : kLineColor;
        draw.line(core::toVec3(core::DVec3{centreX + offset, groundY, centreZ - half}),
                  core::toVec3(core::DVec3{centreX + offset, groundY, centreZ + half}), alongZ);

        const render::DebugColor alongX = worldIndexZ % kMajorEvery == 0 ? kMajorColor : kLineColor;
        draw.line(core::toVec3(core::DVec3{centreX - half, groundY, centreZ + offset}),
                  core::toVec3(core::DVec3{centreX + half, groundY, centreZ + offset}), alongX);
    }
}

} // namespace luaug::app
