// World-space UI (F3): where a canvas lands in the world.
//
// A sign that faces the wrong way, reads mirrored, or grows with distance when
// it should not is a bug that reproduces by looking -- so the placements are
// numbers, and these are the numbers.
#include "luaug/app/world_ui.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <cmath>
#include <doctest/doctest.h>

#include "inspector_fixture.h"

using namespace luaug;
using app::CanvasPlacement;

namespace {

[[nodiscard]] bool near(core::Vec3 a, core::Vec3 b, float tolerance = 1e-4f)
{
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}

// A camera at the origin looking down -Z with a 90-degree vertical field, so
// the projection's focal term is one.
[[nodiscard]] render::RenderCamera cameraAtOrigin()
{
    render::RenderCamera camera;
    camera.valid = true;
    camera.view = core::Mat4{};
    camera.projection = core::perspective(1.5707964f, 16.0f / 9.0f, 0.1f, 1000.0f);
    camera.viewProjection = camera.projection;
    return camera;
}

} // namespace

TEST_CASE("a surface on the front face reads left to right from in front of it")
{
    scene::SurfaceGuiComponent gui;
    gui.face = 0; // Front: the face -Z points out of.
    const std::optional<CanvasPlacement> placed =
        app::placeSurface(gui, core::CFrameD{}, core::Vec3{4.0f, 2.0f, 1.0f}, core::DVec3{});
    REQUIRE(placed.has_value());
    CHECK(placed->canvas.x == doctest::Approx(200.0));
    CHECK(placed->canvas.y == doctest::Approx(100.0));
    // A viewer at -Z looking back at it has +X on their LEFT, so the canvas's
    // x runs along -X; its top-left is at +X, +Y, just off the face.
    CHECK(near(placed->right, core::Vec3{-0.02f, 0.0f, 0.0f}));
    CHECK(near(placed->down, core::Vec3{0.0f, -0.02f, 0.0f}));
    CHECK(near(placed->topLeft, core::Vec3{2.0f, 1.0f, -0.501f}));
}

TEST_CASE("every face of a turned part is measured along its own edges")
{
    // Turned a quarter about Y: the part's -Z face now looks along -X.
    core::CFrameD part;
    part.rotation = core::rotationY(1.5707964f);
    part.position = core::DVec3{10.0, 0.0, 0.0};
    const core::Vec3 size{4.0f, 2.0f, 6.0f};

    scene::SurfaceGuiComponent front;
    const std::optional<CanvasPlacement> placedFront = app::placeSurface(front, part, size, core::DVec3{});
    REQUIRE(placedFront.has_value());
    CHECK(placedFront->canvas.x == doctest::Approx(200.0));
    // Its centre is half the depth out along the turned normal.
    const core::Vec3 centre = placedFront->topLeft + placedFront->right * 100.0f + placedFront->down * 50.0f;
    CHECK(near(centre, core::Vec3{7.0f - 0.001f, 0.0f, 0.0f}, 1e-3f));

    scene::SurfaceGuiComponent top;
    top.face = 2;
    const std::optional<CanvasPlacement> placedTop = app::placeSurface(top, part, size, core::DVec3{});
    REQUIRE(placedTop.has_value());
    // The top is width by depth.
    CHECK(placedTop->canvas.x == doctest::Approx(200.0));
    CHECK(placedTop->canvas.y == doctest::Approx(300.0));

    scene::SurfaceGuiComponent right;
    right.face = 4;
    const std::optional<CanvasPlacement> placedRight = app::placeSurface(right, part, size, core::DVec3{});
    REQUIRE(placedRight.has_value());
    CHECK(placedRight->canvas.x == doctest::Approx(300.0));
    CHECK(placedRight->canvas.y == doctest::Approx(100.0));
}

TEST_CASE("a billboard sized in pixels keeps its size on the screen")
{
    const render::RenderCamera camera = cameraAtOrigin();
    scene::BillboardGuiComponent gui;
    gui.size = core::UDim2{core::UDim{0.0f, 100.0f}, core::UDim{0.0f, 50.0f}};

    const std::optional<CanvasPlacement> near10 =
        app::placeBillboard(gui, core::DVec3{0.0, 0.0, -10.0}, camera, core::Vec2{1280.0f, 720.0f});
    const std::optional<CanvasPlacement> near20 =
        app::placeBillboard(gui, core::DVec3{0.0, 0.0, -20.0}, camera, core::Vec2{1280.0f, 720.0f});
    REQUIRE(near10.has_value());
    REQUIRE(near20.has_value());
    CHECK(near10->canvas.x == doctest::Approx(100.0));
    // Twice as far, twice as big in the world: the same size on the screen.
    CHECK(near20->right.x == doctest::Approx(2.0 * static_cast<double>(near10->right.x)));
    // Facing the camera: the canvas's x is the camera's right, its y is down.
    CHECK(near10->right.x > 0.0f);
    CHECK(near10->down.y < 0.0f);
}

TEST_CASE("a billboard sized in metres keeps its size in the world")
{
    const render::RenderCamera camera = cameraAtOrigin();
    scene::BillboardGuiComponent gui;
    gui.size = core::UDim2{core::UDim{4.0f, 0.0f}, core::UDim{1.0f, 0.0f}};
    const std::optional<CanvasPlacement> near10 =
        app::placeBillboard(gui, core::DVec3{0.0, 0.0, -10.0}, camera, core::Vec2{1280.0f, 720.0f});
    const std::optional<CanvasPlacement> near40 =
        app::placeBillboard(gui, core::DVec3{0.0, 0.0, -40.0}, camera, core::Vec2{1280.0f, 720.0f});
    REQUIRE(near10.has_value());
    REQUIRE(near40.has_value());
    CHECK(near10->canvas.x == doctest::Approx(4.0 * static_cast<double>(app::BillboardPixelsPerMetre)));
    CHECK(near10->right.x * near10->canvas.x == doctest::Approx(4.0));
    CHECK(near40->right.x * near40->canvas.x == doctest::Approx(4.0));
}

TEST_CASE("a billboard behind the camera or past its distance is not drawn")
{
    const render::RenderCamera camera = cameraAtOrigin();
    scene::BillboardGuiComponent gui;
    CHECK_FALSE(app::placeBillboard(gui, core::DVec3{0.0, 0.0, 10.0}, camera, core::Vec2{1280.0f, 720.0f}).has_value());
    gui.maxDistance = 50.0f;
    CHECK(app::placeBillboard(gui, core::DVec3{0.0, 0.0, -40.0}, camera, core::Vec2{1280.0f, 720.0f}).has_value());
    CHECK_FALSE(
        app::placeBillboard(gui, core::DVec3{0.0, 0.0, -60.0}, camera, core::Vec2{1280.0f, 720.0f}).has_value());
}

TEST_CASE("a surface's children land on its part, back to front, in the world pass")
{
    app::testing::Fixture fixture;
    scene::World world{fixture.classes, fixture.enums, fixture.atoms, 7u};
    const core::InstanceId workspace = world.create(fixture.workspaceClass);
    world.workspaces().add(workspace, scene::WorkspaceComponent{});

    // Two walls, the far one first in pool order, each with one panel.
    const auto wallWithPanel = [&](core::DVec3 at) {
        const core::InstanceId wall = world.create(fixture.partClass);
        scene::PartComponent part;
        part.cframe.position = at;
        part.size = core::Vec3{4.0f, 2.0f, 0.5f};
        world.parts().add(wall, part);
        (void)world.setParent(wall, workspace);

        const core::InstanceId gui = world.create(fixture.folderClass);
        world.surfaceGuis().add(gui, scene::SurfaceGuiComponent{});
        (void)world.setParent(gui, wall);

        const core::InstanceId panel = world.create(fixture.folderClass);
        scene::UIObjectComponent frame;
        frame.size = core::UDim2{core::UDim{1.0f, 0.0f}, core::UDim{1.0f, 0.0f}};
        frame.backgroundTransparency = 0.0f;
        world.uiObjects().add(panel, frame);
        (void)world.setParent(panel, gui);
        return wall;
    };
    (void)wallWithPanel(core::DVec3{0.0, 0.0, -30.0});
    (void)wallWithPanel(core::DVec3{0.0, 0.0, -10.0});

    render::RenderWorld out;
    out.camera = cameraAtOrigin();
    ui::DrawList scratch;
    app::buildWorldUi(world, workspace, core::InstanceId{}, core::Vec2{1280.0f, 720.0f}, {}, scratch, out);

    // One quad per panel, six corners each, the far one first.
    REQUIRE(out.worldUiVertices.size() == 12);
    CHECK(out.worldUiVertices.front().z < -29.0f);
    CHECK(out.worldUiVertices.back().z > -11.0f);
    REQUIRE_FALSE(out.worldUiRuns.empty());
    CHECK_FALSE(out.worldUiRuns.front().alwaysOnTop);
}

TEST_CASE("the pointer presses a sign in the world, unless it is behind something or seen from behind")
{
    app::testing::Fixture fixture;
    scene::World world{fixture.classes, fixture.enums, fixture.atoms, 7u};
    const core::InstanceId workspace = world.create(fixture.workspaceClass);
    world.workspaces().add(workspace, scene::WorkspaceComponent{});

    // A wall ten metres ahead with a panel over the whole of its near face.
    const core::InstanceId wall = world.create(fixture.partClass);
    scene::PartComponent part;
    part.cframe.position = core::DVec3{0.0, 0.0, -10.0};
    part.size = core::Vec3{4.0f, 2.0f, 0.5f};
    world.parts().add(wall, part);
    (void)world.setParent(wall, workspace);
    const core::InstanceId gui = world.create(fixture.folderClass);
    scene::SurfaceGuiComponent surface;
    surface.face = 1; // Back: the face +Z points out of, towards the camera.
    world.surfaceGuis().add(gui, surface);
    (void)world.setParent(gui, wall);
    const core::InstanceId panel = world.create(fixture.folderClass);
    scene::UIObjectComponent frame;
    frame.size = core::UDim2{core::UDim{1.0f, 0.0f}, core::UDim{1.0f, 0.0f}};
    world.uiObjects().add(panel, frame);
    (void)world.setParent(panel, gui);

    const render::RenderCamera camera = cameraAtOrigin();
    const core::Vec2 viewport{1280.0f, 720.0f};
    const core::Vec2 centre{640.0f, 360.0f};
    core::InstanceId askedToIgnore;
    const app::SolidAlong nothingInTheWay = [&](core::Vec3, core::Vec3, core::InstanceId adornee) {
        askedToIgnore = adornee;
        return std::optional<core::f32>{};
    };

    // Straight ahead: the panel, at the wall's near face.
    const std::optional<app::WorldUiPick> ahead =
        app::pickWorldUi(world, workspace, {}, viewport, camera, centre, nothingInTheWay);
    REQUIRE(ahead.has_value());
    CHECK(ahead->element == panel);
    CHECK(static_cast<double>(ahead->distance) == doctest::Approx(9.749).epsilon(0.001));
    // The part it is printed on is left out of the occlusion question.
    CHECK(askedToIgnore == wall);

    // Off to the side, the ray misses the wall entirely.
    CHECK_FALSE(app::pickWorldUi(world, workspace, {}, viewport, camera, core::Vec2{20.0f, 20.0f}, nothingInTheWay)
                    .has_value());

    // Something solid five metres out hides it...
    const app::SolidAlong crate = [](core::Vec3, core::Vec3, core::InstanceId) {
        return std::optional<core::f32>{5.0f};
    };
    CHECK_FALSE(app::pickWorldUi(world, workspace, {}, viewport, camera, centre, crate).has_value());
    // ...unless it is drawn on top of everything.
    world.surfaceGuis().find(gui)->alwaysOnTop = true;
    CHECK(app::pickWorldUi(world, workspace, {}, viewport, camera, centre, crate).has_value());
    world.surfaceGuis().find(gui)->alwaysOnTop = false;

    // On the far face, the camera sees the canvas's back, and a sign is read
    // from its front.
    world.surfaceGuis().find(gui)->face = 0; // Front: -Z, away from the camera.
    CHECK_FALSE(app::pickWorldUi(world, workspace, {}, viewport, camera, centre, nothingInTheWay).has_value());
}
