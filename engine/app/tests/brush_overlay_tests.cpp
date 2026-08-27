// The sculpting brush's arithmetic (F1 Part F).
//
// **A bug that only reproduces by dragging is one nobody fixes twice**, which is
// why the two things that can be wrong here are free functions over numbers
// rather than code inside a pointer handler: how far apart a stroke's stamps
// fall, and how a ring lies on a surface it is not perpendicular to.
#include "luaug/app/brush_overlay.h"
#include "luaug/render/debug_draw.h"

#include <cmath>
#include <doctest/doctest.h>
#include <span>

using namespace luaug;
using namespace luaug::app;

namespace {

[[nodiscard]] double distanceBetween(core::DVec3 a, core::DVec3 b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double dz = b.z - a.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

TEST_CASE("a stroke that has not moved is one stamp, not none")
{
    // The first frame of every drag, and also a click. A brush that did nothing
    // until the pointer moved would feel broken in exactly the case somebody
    // tries first.
    const std::vector<core::DVec3> stamps = strokeStamps({1.0, 2.0, 3.0}, {1.0, 2.0, 3.0}, 2.0, 0.25);
    REQUIRE(stamps.size() == 1);
    CHECK(stamps[0].x == 1.0);
    CHECK(stamps[0].y == 2.0);
    CHECK(stamps[0].z == 3.0);
}

TEST_CASE("a stroke is walked at a fixed spacing in world space, not once a frame")
{
    // **The reason this exists.** At a hundred and twenty frames a second a slow
    // drag stamps on top of itself; at thirty a fast one leaves a dotted line of
    // craters. Walking the stroke in metres makes the result a function of where
    // the pointer went rather than of how fast the machine was.
    const double radius = 2.0;
    const double spacing = 0.25;
    const std::vector<core::DVec3> stamps = strokeStamps({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, radius, spacing);

    // Ten metres at half a metre a stamp, from zero to ten inclusive.
    REQUIRE(stamps.size() == 21);
    CHECK(stamps.front().x == doctest::Approx(0.0));
    CHECK(stamps.back().x == doctest::Approx(10.0));
    for (core::usize at = 1; at < stamps.size(); ++at) {
        CHECK(distanceBetween(stamps[at - 1], stamps[at]) == doctest::Approx(radius * spacing).epsilon(0.001));
    }
}

TEST_CASE("the first stamp is where the stroke was, and the remainder is left")
{
    // **The detail the whole thing turns on**, and the obvious alternative is
    // wrong. Walking backwards from `to` guarantees the pointer's own position
    // is stamped -- which reads as right -- but it puts the fractional part of
    // the distance at the `from` end, where a caller continuing the stroke from
    // `to` silently drops it. Over a drag it accumulates, and a stroke cut into
    // forty frames lays a third more stamps than the same stroke cut into four.
    //
    // Seven point three metres does not divide by the half-metre step, which is
    // the case that catches it.
    const std::vector<core::DVec3> stamps = strokeStamps({0.0, 0.0, 0.0}, {7.3, 0.0, 0.0}, 2.0, 0.25);
    REQUIRE(stamps.size() == 15);
    CHECK(stamps.front().x == doctest::Approx(0.0));
    // Fourteen half-metre steps is seven metres; the last three tenths are not
    // walked, and the pointer carries them into the next frame.
    CHECK(stamps.back().x == doctest::Approx(7.0));
    CHECK(stamps.back().x < 7.3);
}

TEST_CASE("a pointer that jumped a kilometre costs a comparison, not a hundred thousand edits")
{
    // An alt-tab, a teleport, a camera cut mid-drag. Past the ceiling the stroke
    // is its endpoints and nothing between -- a visible gap, which is
    // recoverable, rather than a frame that never ends.
    const std::vector<core::DVec3> stamps = strokeStamps({0.0, 0.0, 0.0}, {1000.0, 0.0, 0.0}, 1.0, 0.25);
    REQUIRE(stamps.size() == 2);
    CHECK(stamps.front().x == 0.0);
    CHECK(stamps.back().x == 1000.0);
    CHECK(stamps.size() <= MaxStrokeStamps);
}

TEST_CASE("a stroke that has not advanced a whole step is not walked")
{
    // The gate the editor holds a stroke behind. Below one step the frame stamps
    // nothing and the anchor stays put, accumulating until the movement is worth
    // a stamp -- which is what stops a high framerate stamping more often than a
    // low one.
    CHECK_FALSE(strokeAdvanced({0.0, 0.0, 0.0}, {0.49, 0.0, 0.0}, 2.0, 0.25));
    CHECK(strokeAdvanced({0.0, 0.0, 0.0}, {0.51, 0.0, 0.0}, 2.0, 0.25));
    // Exactly one step advances: the stamp lands on the pointer.
    CHECK(strokeAdvanced({0.0, 0.0, 0.0}, {0.5, 0.0, 0.0}, 2.0, 0.25));
    // A spacing that cannot advance never advances.
    CHECK_FALSE(strokeAdvanced({0.0, 0.0, 0.0}, {100.0, 0.0, 0.0}, 2.0, 0.0));
    CHECK_FALSE(strokeAdvanced({0.0, 0.0, 0.0}, {100.0, 0.0, 0.0}, 0.0, 0.25));
}

TEST_CASE("a spacing that cannot advance is one stamp rather than an unbounded loop")
{
    for (const double spacing : {0.0, -1.0}) {
        const std::vector<core::DVec3> stamps = strokeStamps({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, 2.0, spacing);
        CAPTURE(spacing);
        REQUIRE(stamps.size() == 1);
        CHECK(stamps[0].x == 10.0);
    }
    // And so is a radius of zero, for the same reason.
    const std::vector<core::DVec3> zeroRadius = strokeStamps({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, 0.0, 0.25);
    CHECK(zeroRadius.size() == 1);
}

TEST_CASE("the ring lies in the plane its normal describes, whichever way it points")
{
    // **The case a fixed cross-product axis gets wrong is a brush aimed straight
    // down**, which is the commonest one there is: crossing with a fixed axis
    // produces a zero-length vector exactly when the normal happens to BE that
    // axis. So the basis is built from whichever axis the normal is least
    // aligned with, and every one of these has to produce a real circle.
    for (const core::Vec3 normal :
         {core::Vec3{0.0f, 1.0f, 0.0f}, core::Vec3{1.0f, 0.0f, 0.0f}, core::Vec3{0.0f, 0.0f, 1.0f},
          core::Vec3{0.0f, -1.0f, 0.0f}, core::Vec3{0.577f, 0.577f, 0.577f}}) {
        render::DebugDraw debug;
        const core::Vec3 centre{4.0f, 5.0f, 6.0f};
        const float radius = 3.0f;
        drawBrushRing(centre, normal, radius, debug);

        // A line is two vertices, which is the buffer the GPU reads.
        const std::span<const render::DebugVertex> vertices = debug.vertices();
        REQUIRE(vertices.size() > 16);

        const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        const core::Vec3 unit{normal.x / length, normal.y / length, normal.z / length};

        // Every ring point is `radius` from the centre and in the plane. The
        // stalk is the one line that is not, and it is the last two vertices.
        for (core::usize at = 0; at + 2 < vertices.size(); ++at) {
            const core::Vec3 point = vertices[at].position;
            const core::Vec3 offset{point.x - centre.x, point.y - centre.y, point.z - centre.z};
            const float distance = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
            CHECK(distance == doctest::Approx(static_cast<double>(radius)).epsilon(0.01));
            const float along = offset.x * unit.x + offset.y * unit.y + offset.z * unit.z;
            CHECK(std::abs(along) < 0.01f);
        }

        // And the stalk points along the normal, which is what makes a ring on a
        // steep surface read as sitting on it rather than floating in the air.
        const core::Vec3 stalkFrom = vertices[vertices.size() - 2].position;
        const core::Vec3 stalkTo = vertices[vertices.size() - 1].position;
        const core::Vec3 direction{stalkTo.x - stalkFrom.x, stalkTo.y - stalkFrom.y, stalkTo.z - stalkFrom.z};
        const float alongNormal = direction.x * unit.x + direction.y * unit.y + direction.z * unit.z;
        CHECK(alongNormal > 0.0f);
    }
}

TEST_CASE("a ring with no radius draws nothing rather than a point")
{
    render::DebugDraw debug;
    drawBrushRing(core::Vec3{}, core::Vec3{0.0f, 1.0f, 0.0f}, 0.0f, debug);
    CHECK(debug.vertices().empty());
}
