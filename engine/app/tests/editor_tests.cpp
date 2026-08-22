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
