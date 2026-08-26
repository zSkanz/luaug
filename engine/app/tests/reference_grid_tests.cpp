// The reference grid, aimed at the arithmetic rather than at the lines.
//
// **What can be wrong here is thinning and alignment.** A grid whose spacing is
// the reach divided by a line count is a different grid at every camera height,
// and one that moves as you fly is not a reference; a grid centred on the camera
// slides under the thing being placed and lines up with nothing. Both look
// perfectly fine in a screenshot taken from one position.
#include "luaug/app/reference_grid.h"
#include "luaug/core/math.h"
#include "luaug/render/debug_draw.h"

#include <cmath>
#include <doctest/doctest.h>

using namespace luaug;

namespace {

// Whether any recorded segment lies on the plane's line `x = at`, within a
// tolerance a float grid can hold at these distances.
[[nodiscard]] bool hasLineAtX(const render::DebugDraw& draw, core::f32 at)
{
    for (const render::DebugVertex& vertex : draw.vertices()) {
        if (std::abs(vertex.position.x - at) < 1.0e-3f)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("the grid draws at the step it was given, when the step is big enough to see")
{
    // A metre step under a camera ten metres up: sixty lines each way reaches
    // sixty metres and the view needs about thirty, so nothing thins.
    CHECK(app::referenceGridStep(1.0f, 30.0) == doctest::Approx(1.0));
}

TEST_CASE("a step too fine for the view thins by powers of ten, not to a round number of its own")
{
    // **Powers of ten keep every line drawn a line the SNAP would also have
    // produced**, so what is on screen is always a subset of the real grid. A
    // spacing computed as reach-over-line-count would be a rounder grid beside
    // the one that catches, which is the one thing a reference grid may not be.
    const core::f32 thinned = app::referenceGridStep(0.01f, 300.0);
    CHECK(thinned > 0.01f);

    // Whatever it landed on, it is the step times a power of ten.
    const core::f64 ratio = static_cast<core::f64>(thinned) / 0.01;
    const core::f64 decades = std::log10(ratio);
    CHECK(std::abs(decades - std::round(decades)) < 1.0e-4);

    // And it reaches: sixty lines of it cover what the view needs.
    CHECK(static_cast<core::f64>(thinned) * 60.0 >= 300.0);
}

TEST_CASE("a step that would need more than eight decades stops rather than overflowing")
{
    // A guard rather than a feature. Nothing sensible asks for a millimetre grid
    // across a hundred kilometres, and a loop with no bound would multiply until
    // the float stopped meaning anything.
    const core::f32 step = app::referenceGridStep(0.001f, 1.0e12);
    CHECK(std::isfinite(step));
    CHECK(step > 0.0f);
}

TEST_CASE("the lines land on the step's own multiples, not on the camera")
{
    // A grid centred on the camera would slide under the thing being placed and
    // line up with nothing -- which is the failure that looks correct from every
    // single position and is wrong at all of them.
    render::DebugDraw draw;
    app::drawReferenceGrid(core::DVec3{7.3, 10.0, -4.9}, 0.0, 1.0f, draw);
    REQUIRE_FALSE(draw.vertices().empty());

    CHECK(hasLineAtX(draw, 7.0f));
    CHECK(hasLineAtX(draw, 8.0f));
    // And nothing at the camera's own fractional position.
    CHECK_FALSE(hasLineAtX(draw, 7.3f));
}

TEST_CASE("a step of zero or less draws nothing rather than looping forever")
{
    render::DebugDraw draw;
    app::drawReferenceGrid({}, 0.0, 0.0f, draw);
    CHECK(draw.vertices().empty());

    app::drawReferenceGrid({}, 0.0, -1.0f, draw);
    CHECK(draw.vertices().empty());
}

TEST_CASE("the grid is a fixed number of lines however high the camera is")
{
    // The other half of thinning: the line COUNT must not grow with the view, or
    // flying up would cost more every metre until the debug buffer is the frame.
    render::DebugDraw low;
    app::drawReferenceGrid(core::DVec3{0.0, 5.0, 0.0}, 0.0, 1.0f, low);

    render::DebugDraw high;
    app::drawReferenceGrid(core::DVec3{0.0, 4000.0, 0.0}, 0.0, 1.0f, high);

    CHECK(low.vertices().size() == high.vertices().size());
}
