// Picking, aimed at the places a click never lands.
//
// The bug this file exists for is the aspect-ratio error: swap a width for a
// height, or forget that a viewport is a panel rather than the window, and the
// centre of the screen is still exactly right while every edge is wrong. A
// person testing an editor aims at what they meant to hit and never finds it.
// So the cases here are the corners, an off-origin viewport, a non-square one,
// and a click on nothing.
#include "luaug/app/picking.h"
#include "luaug/core/math.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <cmath>
#include <doctest/doctest.h>

#include "inspector_fixture.h"

using namespace luaug;
using luaug::app::gizmoDragAngle;
using luaug::app::gizmoDragPoint;
using luaug::app::GizmoFrame;
using luaug::app::GizmoHandle;
using luaug::app::GizmoMode;
using luaug::app::intersectBox;
using luaug::app::metresPerPixel;
using luaug::app::pickGizmo;
using luaug::app::PickRay;
using luaug::app::rayThroughPixel;
using luaug::app::ViewportRect;
using luaug::app::worldToViewport;
using luaug::core::f32;

namespace {

// `doctest::Approx` holds a double, so comparing an f32 against one promotes --
// and Clang's `-Wdouble-promotion` is right to say so under `-Werror`. Widening
// deliberately in one named place beats a cast at every call site and beats
// pretending the comparison is not happening.
[[nodiscard]] double wide(f32 value) noexcept
{
    return static_cast<double>(value);
}
constexpr double kEpsilon = 1e-4;

// A camera at the world origin looking down -Z, which is what `lookAt` and
// `perspective` agree forward means.
struct TestCamera
{
    core::Mat4 projection;
    core::Mat4 view;
    core::DVec3 origin;
};

TestCamera cameraAt(core::DVec3 eye, core::Vec3 target, f32 fovYDegrees, f32 aspect)
{
    const f32 fov = fovYDegrees * 3.14159265f / 180.0f;
    return TestCamera{
        core::perspective(fov, aspect, 0.1f, 1000.0f),
        // The view is built in the camera-relative space the renderer works in,
        // so the eye is the origin of that space and the target is relative
        // to it -- which is why `origin` is carried separately.
        core::lookAt(core::Vec3{0.0f, 0.0f, 0.0f}, target, core::Vec3{0.0f, 1.0f, 0.0f}),
        eye,
    };
}

core::CFrameD boxAt(core::DVec3 position)
{
    return core::CFrameD{position, core::Mat3{}};
}
} // namespace

TEST_CASE("a ray through the centre of the viewport points straight ahead")
{
    const TestCamera camera = cameraAt({0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}, 60.0f, 16.0f / 9.0f);
    const ViewportRect rect{0.0f, 0.0f, 1920.0f, 1080.0f};

    const PickRay ray = rayThroughPixel(camera.projection, camera.view, camera.origin, rect, {960.0f, 540.0f});

    CHECK(wide(ray.direction.x) == doctest::Approx(0.0).epsilon(kEpsilon));
    CHECK(wide(ray.direction.y) == doctest::Approx(0.0).epsilon(kEpsilon));
    CHECK(wide(ray.direction.z) == doctest::Approx(-1.0).epsilon(kEpsilon));
}

TEST_CASE("the corners open outwards, and the horizontal spread is the aspect ratio")
{
    // 90 degrees vertical makes the vertical tangent exactly 1, so the numbers
    // below are the aspect ratio itself rather than something derived from it.
    // An aspect bug cannot hide behind arithmetic here.
    const f32 aspect = 16.0f / 9.0f;
    const TestCamera camera = cameraAt({0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}, 90.0f, aspect);
    const ViewportRect rect{0.0f, 0.0f, 1600.0f, 900.0f};

    const PickRay topLeft = rayThroughPixel(camera.projection, camera.view, camera.origin, rect, {0.0f, 0.0f});
    const PickRay bottomRight = rayThroughPixel(camera.projection, camera.view, camera.origin, rect, {1600.0f, 900.0f});

    // Normalised, so compare the ratios rather than the components.
    CHECK(wide((topLeft.direction.x / -topLeft.direction.z)) == doctest::Approx(-wide(aspect)).epsilon(kEpsilon));
    CHECK(wide((topLeft.direction.y / -topLeft.direction.z)) == doctest::Approx(1.0).epsilon(kEpsilon));
    CHECK(wide((bottomRight.direction.x / -bottomRight.direction.z)) ==
          doctest::Approx(wide(aspect)).epsilon(kEpsilon));
    CHECK(wide((bottomRight.direction.y / -bottomRight.direction.z)) == doctest::Approx(-1.0).epsilon(kEpsilon));

    // The tell for a swapped width and height: a tall viewport must spread
    // LESS horizontally than a wide one, at the same field of view.
    const TestCamera tall = cameraAt({0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}, 90.0f, 9.0f / 16.0f);
    const ViewportRect tallRect{0.0f, 0.0f, 900.0f, 1600.0f};
    const PickRay tallCorner = rayThroughPixel(tall.projection, tall.view, tall.origin, tallRect, {0.0f, 0.0f});
    CHECK(std::abs(tallCorner.direction.x / tallCorner.direction.z) <
          std::abs(topLeft.direction.x / topLeft.direction.z));
}

TEST_CASE("a viewport is a panel, so its offset moves the ray and its size does not follow the window")
{
    const TestCamera camera = cameraAt({0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}, 60.0f, 1.0f);

    // The same pixel is the centre of one viewport and a corner of another.
    const ViewportRect panel{300.0f, 100.0f, 800.0f, 800.0f};
    const PickRay centre = rayThroughPixel(camera.projection, camera.view, camera.origin, panel, {700.0f, 500.0f});
    CHECK(wide(centre.direction.x) == doctest::Approx(0.0).epsilon(kEpsilon));
    CHECK(wide(centre.direction.y) == doctest::Approx(0.0).epsilon(kEpsilon));

    const ViewportRect window{0.0f, 0.0f, 800.0f, 800.0f};
    const PickRay offCentre = rayThroughPixel(camera.projection, camera.view, camera.origin, window, {700.0f, 500.0f});
    CHECK(offCentre.direction.x > 0.01f);
    CHECK(offCentre.direction.y < -0.01f);
}

TEST_CASE("a collapsed viewport gives forward rather than a NaN")
{
    const TestCamera camera = cameraAt({0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}, 60.0f, 1.0f);
    const ViewportRect collapsed{0.0f, 0.0f, 0.0f, 0.0f};

    const PickRay ray = rayThroughPixel(camera.projection, camera.view, camera.origin, collapsed, {0.0f, 0.0f});

    CHECK(std::isfinite(ray.direction.x));
    CHECK(std::isfinite(ray.direction.y));
    CHECK(std::isfinite(ray.direction.z));
}

TEST_CASE("the ray carries the camera's f64 world position, not a narrowed one")
{
    // Four kilometres out, which is inside the flagship's world and outside
    // what f32 keeps to the millimetre.
    const core::DVec3 far{4321.5, 12.25, -8765.75};
    const TestCamera camera = cameraAt(far, {0.0f, 0.0f, -1.0f}, 60.0f, 1.0f);
    const ViewportRect rect{0.0f, 0.0f, 100.0f, 100.0f};

    const PickRay ray = rayThroughPixel(camera.projection, camera.view, camera.origin, rect, {50.0f, 50.0f});

    CHECK(ray.origin.x == doctest::Approx(far.x));
    CHECK(ray.origin.z == doctest::Approx(far.z));
}

TEST_CASE("a box in front is hit at its near face, and one behind is not hit at all")
{
    const PickRay forward{{0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}};

    const auto ahead = intersectBox(forward, boxAt({0.0, 0.0, -10.0}), {2.0f, 2.0f, 2.0f});
    REQUIRE(ahead.has_value());
    CHECK(wide(*ahead) == doctest::Approx(9.0).epsilon(kEpsilon));

    CHECK_FALSE(intersectBox(forward, boxAt({0.0, 0.0, 10.0}), {2.0f, 2.0f, 2.0f}).has_value());
}

TEST_CASE("a ray that starts inside a box hits it at zero rather than missing it")
{
    const PickRay inside{{0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}};

    const auto hit = intersectBox(inside, boxAt({0.0, 0.0, 0.0}), {10.0f, 10.0f, 10.0f});
    REQUIRE(hit.has_value());
    CHECK(wide(*hit) == doctest::Approx(0.0));
}

TEST_CASE("a rotated box is tested in its own space")
{
    // Forty-five degrees about Y. A thin tall slab edge-on to the ray is missed
    // if the rotation is ignored and hit if it is not -- the two answers differ,
    // which is the only useful kind of test for a transform.
    const f32 c = 0.70710678f;
    core::CFrameD rotated;
    rotated.position = {0.0, 0.0, -10.0};
    rotated.rotation = core::Mat3{{{c, 0.0f, -c}, {0.0f, 1.0f, 0.0f}, {c, 0.0f, c}}};

    const core::Vec3 slab{8.0f, 8.0f, 0.5f};

    // Down the +X side of the slab: within its long axis once rotated, outside
    // its thin one if the rotation were dropped.
    const PickRay offAxis{{3.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}};
    const auto rotatedHit = intersectBox(offAxis, rotated, slab);

    core::CFrameD unrotated = rotated;
    unrotated.rotation = core::Mat3{};
    const auto unrotatedHit = intersectBox(offAxis, unrotated, slab);

    CHECK(rotatedHit.has_value());
    REQUIRE(unrotatedHit.has_value());
    CHECK(wide(*rotatedHit) != doctest::Approx(wide(*unrotatedHit)).epsilon(kEpsilon));
}

TEST_CASE("a ray parallel to a slab is rejected by the origin rather than by a divide")
{
    // Straight up, past a box that is beside it. Without the parallel guard this
    // is a zero divide and the answer is whatever infinity compares as.
    const PickRay up{{0.0, 0.0, 0.0}, {0.0f, 1.0f, 0.0f}};
    CHECK_FALSE(intersectBox(up, boxAt({50.0, 0.0, 0.0}), {1.0f, 1.0f, 1.0f}).has_value());

    const auto through = intersectBox(up, boxAt({0.0, 20.0, 0.0}), {1.0f, 1.0f, 1.0f});
    REQUIRE(through.has_value());
    CHECK(wide(*through) == doctest::Approx(19.5).epsilon(kEpsilon));
}

TEST_CASE("picking a world returns the nearest part, and empty space returns nothing")
{
    // The fixture's classes rather than the shipped ones, for the same reason
    // `inspector_tests.cpp` uses them: picking reads the part pool and never
    // asks what class anything is, and a test that used `Part` would be
    // asserting that alongside what it means to assert.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId near = fixture.widget(world, "Near");
    const core::InstanceId far = fixture.widget(world, "Far");

    scene::PartComponent nearPart;
    nearPart.cframe = boxAt({0.0, 0.0, -5.0});
    nearPart.size = {2.0f, 2.0f, 2.0f};
    world.parts().add(near, nearPart);

    scene::PartComponent farPart;
    farPart.cframe = boxAt({0.0, 0.0, -50.0});
    farPart.size = {2.0f, 2.0f, 2.0f};
    world.parts().add(far, farPart);

    const PickRay forward{{0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}};
    const auto hit = app::pickNearest(world, forward);
    REQUIRE(hit.has_value());
    CHECK(hit->instance == near);

    const PickRay away{{0.0, 0.0, 0.0}, {0.0f, 1.0f, 0.0f}};
    CHECK_FALSE(app::pickNearest(world, away).has_value());
}

TEST_CASE("a transparent part is pickable, because an editor must be able to select what it cannot see")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId ghost = fixture.widget(world, "Ghost");

    scene::PartComponent part;
    part.cframe = boxAt({0.0, 0.0, -5.0});
    part.size = {2.0f, 2.0f, 2.0f};
    part.transparency = 1.0f;
    world.parts().add(ghost, part);

    const PickRay forward{{0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}};
    const auto hit = app::pickNearest(world, forward);
    REQUIRE(hit.has_value());
    CHECK(hit->instance == ghost);
}

namespace {

// An ABSOLUTE tolerance, because every quantity below is a length in pixels or
// in metres and a relative one would be tight at the origin and loose four
// kilometres out -- which is the opposite of what these cases are checking.
[[nodiscard]] bool close(f32 value, f32 expected, f32 tolerance) noexcept
{
    const f32 difference = value - expected;
    return (difference < 0.0f ? -difference : difference) <= tolerance;
}

} // namespace

// --- E2: the manipulators ---------------------------------------------------
//
// Every one of these is a case that is right at the centre of the screen and
// wrong somewhere else, which is the whole reason this file exists rather than
// a person dragging things and looking.

TEST_CASE("worldToViewport is the exact inverse of rayThroughPixel, at the corners too")
{
    // Not square, deliberately: an aspect-ratio error is exactly zero at the
    // centre and grows towards the edges, so a square viewport cannot see it.
    const TestCamera camera = cameraAt({120.0, 4.0, -60.0}, {0.0f, 0.0f, -1.0f}, 55.0f, 21.0f / 9.0f);
    const ViewportRect rect{37.0f, 11.0f, 1680.0f, 720.0f};

    const core::Vec2 pixels[] = {
        {rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f},
        {rect.x + 0.5f, rect.y + 0.5f},
        {rect.x + rect.width - 0.5f, rect.y + 0.5f},
        {rect.x + 0.5f, rect.y + rect.height - 0.5f},
        {rect.x + rect.width - 0.5f, rect.y + rect.height - 0.5f},
    };

    for (const core::Vec2 pixel : pixels) {
        const PickRay ray = rayThroughPixel(camera.projection, camera.view, camera.origin, rect, pixel);

        // A point forty metres down that ray must project back to the pixel it
        // came from. Round-tripped rather than compared against a second
        // formula, because a second formula is a second thing to be wrong.
        const core::DVec3 point = ray.origin + core::toDVec3(ray.direction * 40.0f);
        const std::optional<core::Vec2> back =
            worldToViewport(camera.projection, camera.view, camera.origin, rect, point);
        REQUIRE(back.has_value());
        CHECK(close(back->x, pixel.x, 0.01f));
        CHECK(close(back->y, pixel.y, 0.01f));
    }
}

TEST_CASE("a point behind the camera has no place on the screen")
{
    const TestCamera camera = cameraAt({0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}, 60.0f, 16.0f / 9.0f);
    const ViewportRect rect{0.0f, 0.0f, 1920.0f, 1080.0f};

    // Projected without the check this comes out in FRONT of the camera on the
    // opposite side, which is a handle drawn where nothing is.
    CHECK_FALSE(worldToViewport(camera.projection, camera.view, camera.origin, rect, {0.0, 0.0, 10.0}).has_value());
    CHECK(worldToViewport(camera.projection, camera.view, camera.origin, rect, {0.0, 0.0, -10.0}).has_value());
}

TEST_CASE("a manipulator is the same size on screen however far away it is")
{
    const TestCamera camera = cameraAt({0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}, 60.0f, 16.0f / 9.0f);
    const ViewportRect rect{0.0f, 0.0f, 1920.0f, 1080.0f};

    const f32 near = metresPerPixel(camera.projection, rect, camera.origin, {0.0, 0.0, -10.0});
    const f32 far = metresPerPixel(camera.projection, rect, camera.origin, {0.0, 0.0, -100.0});
    REQUIRE(near > 0.0f);
    // Ten times the distance is ten times the metres per pixel, which is what
    // makes a gizmo built in metres and scaled by this a constant number of
    // pixels.
    CHECK(close(far / near, 10.0f, 0.001f));

    // Measured rather than asserted: a gizmo asked to be ninety pixels long
    // must project to ninety pixels of screen.
    const core::DVec3 centre{0.0, 0.0, -37.0};
    const f32 size = metresPerPixel(camera.projection, rect, camera.origin, centre) * 90.0f;
    const std::optional<core::Vec2> from = worldToViewport(camera.projection, camera.view, camera.origin, rect, centre);
    const std::optional<core::Vec2> to = worldToViewport(camera.projection, camera.view, camera.origin, rect,
                                                         centre + core::DVec3{static_cast<core::f64>(size), 0.0, 0.0});
    REQUIRE(from.has_value());
    REQUIRE(to.has_value());
    CHECK(close(to->x - from->x, 90.0f, 0.5f));
}

namespace {

// A gizmo on the world's axes, ninety pixels of it, wherever it is put.
[[nodiscard]] GizmoFrame gizmoAt(const TestCamera& camera, const ViewportRect& rect, core::DVec3 position)
{
    return GizmoFrame{core::CFrameD{position, core::Mat3{}},
                      metresPerPixel(camera.projection, rect, camera.origin, position) * 90.0f};
}

// The ray through the pixel a world point falls at, which is how a test aims at
// a handle it can only describe in world space.
[[nodiscard]] PickRay rayAtWorld(const TestCamera& camera, const ViewportRect& rect, core::DVec3 point)
{
    const std::optional<core::Vec2> pixel = worldToViewport(camera.projection, camera.view, camera.origin, rect, point);
    REQUIRE(pixel.has_value());
    return rayThroughPixel(camera.projection, camera.view, camera.origin, rect, *pixel);
}

} // namespace

TEST_CASE("a click on an arm picks that arm, and one on the middle picks the middle")
{
    const TestCamera camera = cameraAt({0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}, 60.0f, 16.0f / 9.0f);
    const ViewportRect rect{0.0f, 0.0f, 1920.0f, 1080.0f};
    const core::DVec3 centre{0.0, 0.0, -30.0};
    const GizmoFrame frame = gizmoAt(camera, rect, centre);

    // Two thirds along each arm, which is arm and not centre and not past the
    // tip.
    const core::DVec3 axis[3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    for (core::u8 index = 0; index < 3; ++index) {
        // Z points at the camera here, so its arm is the near-parallel case and
        // is checked on its own below.
        if (index == 2)
            continue;
        const auto reach = static_cast<core::f64>(frame.size) * 0.7;
        const core::DVec3 on =
            centre + core::DVec3{axis[index].x * reach, axis[index].y * reach, axis[index].z * reach};
        const std::optional<GizmoHandle> hit = pickGizmo(rayAtWorld(camera, rect, on), frame, GizmoMode::Translate);
        REQUIRE(hit.has_value());
        CHECK(hit->axis == index);
        CHECK_FALSE(hit->plane);
        CHECK_FALSE(hit->uniform);
    }

    const std::optional<GizmoHandle> middle = pickGizmo(rayAtWorld(camera, rect, centre), frame, GizmoMode::Translate);
    REQUIRE(middle.has_value());
    CHECK(middle->uniform);

    // Well outside everything.
    const core::DVec3 away = centre + core::DVec3{static_cast<core::f64>(frame.size) * 6.0, 0.0, 0.0};
    CHECK_FALSE(pickGizmo(rayAtWorld(camera, rect, away), frame, GizmoMode::Translate).has_value());
}

TEST_CASE("an arm pointing at the camera is refused rather than divided by")
{
    // The Z arm points straight down the view direction. There is no nearest
    // pair between a ray and a line it is parallel to, and the wrong answer here
    // is not a miss but a NaN that poisons everything downstream.
    const TestCamera camera = cameraAt({0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}, 60.0f, 16.0f / 9.0f);
    const ViewportRect rect{0.0f, 0.0f, 1920.0f, 1080.0f};
    const core::DVec3 centre{0.0, 0.0, -30.0};
    const GizmoFrame frame = gizmoAt(camera, rect, centre);

    const PickRay straight = rayAtWorld(camera, rect, centre);
    const GizmoHandle zAxis{2, false, false, 0.0f};
    const std::optional<core::DVec3> point = gizmoDragPoint(straight, frame, zAxis);
    if (point.has_value()) {
        CHECK(std::isfinite(point->x));
        CHECK(std::isfinite(point->y));
        CHECK(std::isfinite(point->z));
    }
}

TEST_CASE("a drag along an arm reports a point on that arm, and the delta is the drag")
{
    const TestCamera camera = cameraAt({0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}, 60.0f, 16.0f / 9.0f);
    const ViewportRect rect{0.0f, 0.0f, 1920.0f, 1080.0f};
    const core::DVec3 centre{0.0, 0.0, -30.0};
    const GizmoFrame frame = gizmoAt(camera, rect, centre);
    const GizmoHandle xAxis{0, false, false, 0.0f};

    const core::DVec3 grabbed = centre + core::DVec3{static_cast<core::f64>(frame.size) * 0.7, 0.0, 0.0};
    const std::optional<core::DVec3> start = gizmoDragPoint(rayAtWorld(camera, rect, grabbed), frame, xAxis);
    REQUIRE(start.has_value());
    // The solve lands on the axis, which is what "a point on that arm" means.
    CHECK(close(static_cast<f32>(start->y - centre.y), 0.0f, 0.01f));
    CHECK(close(static_cast<f32>(start->z - centre.z), 0.0f, 0.01f));

    // A drag that starts OFF the axis still solves onto it -- somebody grabbing
    // an arm is never exactly on its centre line.
    const core::DVec3 offAxis = grabbed + core::DVec3{0.0, static_cast<core::f64>(frame.size) * 0.05, 0.0};
    const std::optional<core::DVec3> moved = gizmoDragPoint(rayAtWorld(camera, rect, offAxis), frame, xAxis);
    REQUIRE(moved.has_value());
    CHECK(close(static_cast<f32>(moved->y - centre.y), 0.0f, 0.01f));

    // And a pointer four metres further along the axis is four metres of drag.
    const core::DVec3 further = grabbed + core::DVec3{4.0, 0.0, 0.0};
    const std::optional<core::DVec3> end = gizmoDragPoint(rayAtWorld(camera, rect, further), frame, xAxis);
    REQUIRE(end.has_value());
    CHECK(close(static_cast<f32>(end->x - start->x), 4.0f, 0.01f));
}

TEST_CASE("a plane handle drags in its own plane and never out of it")
{
    const TestCamera camera = cameraAt({0.0, 20.0, 0.0}, {0.0f, -1.0f, -0.4f}, 60.0f, 16.0f / 9.0f);
    const ViewportRect rect{0.0f, 0.0f, 1920.0f, 1080.0f};
    const core::DVec3 centre{0.0, 0.0, -30.0};
    const GizmoFrame frame = gizmoAt(camera, rect, centre);

    // The XZ plane: its normal is Y, which is the axis the drag does not move
    // along.
    const GizmoHandle floor{1, true, false, 0.0f};
    const core::DVec3 grabbed =
        centre + core::DVec3{static_cast<core::f64>(frame.size) * 0.4, 0.0, static_cast<core::f64>(frame.size) * 0.4};

    const std::optional<core::DVec3> start = gizmoDragPoint(rayAtWorld(camera, rect, grabbed), frame, floor);
    REQUIRE(start.has_value());
    CHECK(close(static_cast<f32>(start->y - centre.y), 0.0f, 0.01f));

    const core::DVec3 further = grabbed + core::DVec3{3.0, 0.0, -2.0};
    const std::optional<core::DVec3> end = gizmoDragPoint(rayAtWorld(camera, rect, further), frame, floor);
    REQUIRE(end.has_value());
    CHECK(close(static_cast<f32>(end->y - start->y), 0.0f, 0.01f));
    CHECK(close(static_cast<f32>(end->x - start->x), 3.0f, 0.01f));
    CHECK(close(static_cast<f32>(end->z - start->z), -2.0f, 0.01f));
}

TEST_CASE("a rotate ring is picked at its radius and reports the angle round it")
{
    // The camera looks down -Z, so the Z ring is face-on and unambiguous -- and
    // the other two are edge-on, which is the case that must NOT be picked
    // instead. Looking straight down the Y axis was the obvious way to write
    // this and is the wrong one: `lookAt` with an up vector parallel to the look
    // direction has no answer, and the matrix it returns is not a camera.
    const TestCamera camera = cameraAt({0.0, 0.0, 0.0}, {0.0f, 0.0f, -1.0f}, 60.0f, 16.0f / 9.0f);
    const ViewportRect rect{0.0f, 0.0f, 1920.0f, 1080.0f};
    const core::DVec3 centre{0.0, 0.0, -30.0};
    const GizmoFrame frame = gizmoAt(camera, rect, centre);

    const core::DVec3 onRing = centre + core::DVec3{static_cast<core::f64>(frame.size), 0.0, 0.0};
    const std::optional<GizmoHandle> hit = pickGizmo(rayAtWorld(camera, rect, onRing), frame, GizmoMode::Rotate);
    REQUIRE(hit.has_value());
    CHECK(hit->axis == 2);

    // A quarter turn round it, measured rather than assumed. The Z ring's own
    // two axes are X and Y in that order, so a point on +X is at atan2(0, r) and
    // one on +Y at atan2(r, 0).
    const GizmoHandle ring{2, false, false, 0.0f};
    const std::optional<f32> from = gizmoDragAngle(rayAtWorld(camera, rect, onRing), frame, ring);
    const core::DVec3 quarter = centre + core::DVec3{0.0, static_cast<core::f64>(frame.size), 0.0};
    const std::optional<f32> to = gizmoDragAngle(rayAtWorld(camera, rect, quarter), frame, ring);
    REQUIRE(from.has_value());
    REQUIRE(to.has_value());

    f32 turned = *to - *from;
    while (turned > 3.14159265f)
        turned -= 6.2831853f;
    while (turned < -3.14159265f)
        turned += 6.2831853f;
    CHECK(close(std::abs(turned), 1.5707963f, 0.01f));

    // The middle of a rotate gizmo is nothing: there is no uniform rotation, and
    // a centre handle that answered would rotate a selection about an axis
    // nobody chose.
    CHECK_FALSE(pickGizmo(rayAtWorld(camera, rect, centre), frame, GizmoMode::Rotate).has_value());
}

TEST_CASE("a manipulator four kilometres out is picked exactly as one at arm's length")
{
    // The whole of ADR 0014 in one case: the origin is f64 and every tolerance
    // in the picker is a fraction of the gizmo, so distance changes nothing.
    const TestCamera camera = cameraAt({4000.0, 12.0, -4000.0}, {0.0f, 0.0f, -1.0f}, 60.0f, 16.0f / 9.0f);
    const ViewportRect rect{0.0f, 0.0f, 1920.0f, 1080.0f};
    const core::DVec3 centre{4000.0, 12.0, -4030.0};
    const GizmoFrame frame = gizmoAt(camera, rect, centre);

    const core::DVec3 on = centre + core::DVec3{static_cast<core::f64>(frame.size) * 0.7, 0.0, 0.0};
    const std::optional<GizmoHandle> hit = pickGizmo(rayAtWorld(camera, rect, on), frame, GizmoMode::Translate);
    REQUIRE(hit.has_value());
    CHECK(hit->axis == 0);

    const GizmoHandle xAxis{0, false, false, 0.0f};
    const std::optional<core::DVec3> start = gizmoDragPoint(rayAtWorld(camera, rect, on), frame, xAxis);
    const std::optional<core::DVec3> end =
        gizmoDragPoint(rayAtWorld(camera, rect, on + core::DVec3{2.0, 0.0, 0.0}), frame, xAxis);
    REQUIRE(start.has_value());
    REQUIRE(end.has_value());
    CHECK(close(static_cast<f32>(end->x - start->x), 2.0f, 0.01f));
}
