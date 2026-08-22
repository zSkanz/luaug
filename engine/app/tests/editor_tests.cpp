// The editor's model, minus the pixels — the same split `inspector_tests.cpp`
// makes and for the same reason. What a click DECIDES is checkable here; what
// the panel draws is a screenshot's business.
//
// The case that matters most is the last one: clicking empty space clears the
// selection. It is the behaviour a person notices only by editing the object
// they thought they had let go of, which is to say only after doing damage.
#include "luaug/app/editor.h"
#include "luaug/core/math.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <cmath>
#include <doctest/doctest.h>

#include "inspector_fixture.h"

using namespace luaug;
using luaug::app::Editor;
using luaug::app::Inspector;
using luaug::app::ViewportRect;

namespace {
// A camera at the origin looking down -Z with a square 800x600 viewport, which
// is deliberately not square: an aspect bug that survives `picking_tests.cpp`
// would have to survive it here too.
void aimEditor(Editor& editor)
{
    editor.setViewport(ViewportRect{320.0f, 48.0f, 800.0f, 600.0f});
    editor.setCamera(core::perspective(1.0472f, 800.0f / 600.0f, 0.1f, 1000.0f),
                     core::lookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}),
                     core::DVec3{0.0, 0.0, 0.0});
}

core::InstanceId partAt(app::testing::Fixture& fixture, scene::World& world, std::string_view name,
                        core::DVec3 position, core::Vec3 size)
{
    const core::InstanceId id = fixture.widget(world, name);
    scene::PartComponent part;
    part.cframe = core::CFrameD{position, core::Mat3{}};
    part.size = size;
    world.parts().add(id, part);
    return id;
}
} // namespace

TEST_CASE("a click in the middle of the viewport selects what is in front of the camera")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId target = partAt(fixture, world, "Target", {0.0, 0.0, -20.0}, {4.0f, 4.0f, 4.0f});

    Editor editor;
    Inspector inspector;
    aimEditor(editor);

    editor.requestPick({400.0f, 300.0f});
    CHECK(editor.pickPending());

    const auto hit = editor.resolvePick(world, inspector);
    REQUIRE(hit.has_value());
    CHECK(hit->instance == target);
    CHECK(inspector.selection() == target);
    CHECK_FALSE(editor.pickPending());
}

TEST_CASE("the viewport's window offset does not displace the ray a second time")
{
    // The panel sits at (320, 48) in the window. A click at the panel's own
    // centre must hit dead ahead: correcting for the offset here as well as in
    // the UI is the classic double-correction, and its signature is that the
    // centre of the screen stops working.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId ahead = partAt(fixture, world, "Ahead", {0.0, 0.0, -20.0}, {1.0f, 1.0f, 1.0f});

    Editor editor;
    Inspector inspector;
    aimEditor(editor);

    editor.requestPick({400.0f, 300.0f});
    const auto hit = editor.resolvePick(world, inspector);

    REQUIRE(hit.has_value());
    CHECK(hit->instance == ahead);
}

TEST_CASE("clicking empty space clears the selection")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId target = partAt(fixture, world, "Target", {0.0, 0.0, -20.0}, {4.0f, 4.0f, 4.0f});

    Editor editor;
    Inspector inspector;
    aimEditor(editor);

    editor.requestPick({400.0f, 300.0f});
    editor.resolvePick(world, inspector);
    REQUIRE(inspector.selection() == target);

    // The top-left corner, where nothing is.
    editor.requestPick({2.0f, 2.0f});
    const auto miss = editor.resolvePick(world, inspector);

    CHECK_FALSE(miss.has_value());
    CHECK_FALSE(inspector.selection().valid());
}

TEST_CASE("a pick before anything has been rendered does nothing rather than guessing")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId target = partAt(fixture, world, "Target", {0.0, 0.0, -20.0}, {4.0f, 4.0f, 4.0f});

    Editor editor;
    Inspector inspector;
    inspector.select(target);
    editor.setViewport(ViewportRect{0.0f, 0.0f, 800.0f, 600.0f});
    REQUIRE_FALSE(editor.hasCamera());

    editor.requestPick({400.0f, 300.0f});
    const auto hit = editor.resolvePick(world, inspector);

    CHECK_FALSE(hit.has_value());
    // Deliberately still selected: with no image to have clicked on, clearing
    // would be a guess.
    CHECK(inspector.selection() == target);
}

TEST_CASE("resolving with nothing pending is not a pick")
{
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId target = partAt(fixture, world, "Target", {0.0, 0.0, -20.0}, {4.0f, 4.0f, 4.0f});

    Editor editor;
    Inspector inspector;
    inspector.select(target);
    aimEditor(editor);

    CHECK_FALSE(editor.resolvePick(world, inspector).has_value());
    CHECK(inspector.selection() == target);
}

TEST_CASE("a click nearer the top of the viewport picks the higher of two parts")
{
    // The Y flip, which is the other half of the aspect bug: mouse coordinates
    // run down and clip space runs up, and getting it wrong is invisible at the
    // centre and inverted everywhere else.
    app::testing::Fixture fixture;
    scene::World world(fixture.classes, fixture.enums, fixture.atoms, 1234u);
    const core::InstanceId high = partAt(fixture, world, "High", {0.0, 4.0, -20.0}, {3.0f, 3.0f, 3.0f});
    const core::InstanceId low = partAt(fixture, world, "Low", {0.0, -4.0, -20.0}, {3.0f, 3.0f, 3.0f});

    Editor editor;
    Inspector inspector;
    aimEditor(editor);

    editor.requestPick({400.0f, 180.0f});
    const auto upper = editor.resolvePick(world, inspector);
    REQUIRE(upper.has_value());
    CHECK(upper->instance == high);

    editor.requestPick({400.0f, 420.0f});
    const auto lower = editor.resolvePick(world, inspector);
    REQUIRE(lower.has_value());
    CHECK(lower->instance == low);
}

TEST_CASE("an editor opens paused, because a ticking world overwrites what you type into it")
{
    Editor editor;
    CHECK(editor.runState() == luaug::app::RunState::Paused);
    CHECK(editor.allowedTicks(4) == 0);
}

TEST_CASE("playing does not become a second scheduler")
{
    Editor editor;
    editor.setRunState(luaug::app::RunState::Playing);

    // Whatever the frame owed, unchanged -- including the catch-up clamp's
    // maximum and a frame that owed nothing.
    CHECK(editor.allowedTicks(4) == 4);
    CHECK(editor.allowedTicks(1) == 1);
    CHECK(editor.allowedTicks(0) == 0);
}

TEST_CASE("a step is one tick, once")
{
    Editor editor;
    editor.requestStep();

    CHECK(editor.allowedTicks(4) == 1);
    // The next frame owes ticks again and must not take one: a step that
    // repeated while the world stayed paused would be play with extra steps.
    CHECK(editor.allowedTicks(4) == 0);
}

TEST_CASE("a step asked for on a frame that owes nothing is not swallowed")
{
    Editor editor;
    editor.requestStep();

    CHECK(editor.allowedTicks(0) == 0);
    CHECK(editor.allowedTicks(1) == 1);
    CHECK(editor.allowedTicks(1) == 0);
}

TEST_CASE("pausing mid-play stops the world at the next frame")
{
    Editor editor;
    editor.setRunState(luaug::app::RunState::Playing);
    REQUIRE(editor.allowedTicks(2) == 2);

    editor.setRunState(luaug::app::RunState::Paused);
    CHECK(editor.allowedTicks(2) == 0);
}

TEST_CASE("the editor camera is seeded rather than teleported")
{
    Editor editor;
    CHECK_FALSE(editor.cameraAdopted());

    core::CFrameD start;
    start.position = {12.0, 3.0, -40.0};
    editor.adoptCamera(start);

    CHECK(editor.cameraAdopted());
    CHECK(editor.cameraCFrame().position == start.position);
}

TEST_CASE("driving does nothing until a camera has been adopted")
{
    Editor editor;
    const core::CFrameD before = editor.cameraCFrame();

    editor.driveCamera({50.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, 1.0f);

    CHECK(editor.cameraCFrame().position == before.position);
}

TEST_CASE("the game takes its camera back when the world plays")
{
    Editor editor;
    core::CFrameD start;
    start.position = {0.0, 0.0, 0.0};
    editor.adoptCamera(start);
    editor.setRunState(luaug::app::RunState::Playing);

    editor.driveCamera({100.0f, 100.0f}, {1.0f, 1.0f, 1.0f}, 1.0f);

    // Not moved and not turned: while the world is playing the script owns the
    // camera, and an editor still writing it would be two authors for one
    // transform.
    CHECK(editor.cameraCFrame().position == start.position);
}

TEST_CASE("flying forward moves along the camera's own look direction")
{
    Editor editor;
    editor.adoptCamera(core::CFrameD{});
    editor.setCameraSpeed(10.0f);

    // One second of forward, from the identity rotation, which looks down -Z.
    editor.driveCamera({}, {0.0f, 0.0f, 1.0f}, 1.0f);
    CHECK(editor.cameraCFrame().position.z < -9.0);
    CHECK(editor.cameraCFrame().position.x == doctest::Approx(0.0));

    // Turn a quarter turn and the same key goes somewhere else. This is the
    // case that fails when movement is done in world axes rather than the
    // camera's.
    Editor turned;
    turned.adoptCamera(core::CFrameD{});
    turned.setCameraSpeed(10.0f);
    turned.driveCamera({static_cast<float>(-1.5708 / 0.0032), 0.0f}, {}, 0.0f);
    turned.driveCamera({}, {0.0f, 0.0f, 1.0f}, 1.0f);
    CHECK(std::abs(turned.cameraCFrame().position.x) > 9.0);
}

TEST_CASE("pitch stops short of the pole, where a fly camera spins on its own")
{
    Editor editor;
    editor.adoptCamera(core::CFrameD{});

    // Far more than a quarter turn of upward mouse movement, twice, so a clamp
    // that only holds for one frame does not pass.
    for (int i = 0; i < 8; ++i)
        editor.driveCamera({0.0f, -1000.0f}, {}, 0.016f);

    // Straight up would put the look direction's Y at 1. The clamp keeps it
    // just below, which is what stops yaw and look from becoming one axis.
    const core::Mat3& basis = editor.cameraCFrame().rotation;
    const double lookY = -static_cast<double>(basis.m[2][1]);
    CHECK(lookY < 1.0);
    CHECK(lookY > 0.99);
}

TEST_CASE("camera speed refuses a value that makes the camera unusable")
{
    Editor editor;

    editor.setCameraSpeed(0.0f);
    CHECK(editor.cameraSpeed() > 0.0f);

    editor.setCameraSpeed(-5.0f);
    CHECK(editor.cameraSpeed() > 0.0f);
}
