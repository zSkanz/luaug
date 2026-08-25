// Where the world is watched from.
//
// A streamed world materialises around a focus and around nothing else, so the
// focus set is the difference between a world and an empty one -- and D098 is
// what happens when that set is empty by default: a project somebody has just
// made gets a streaming grid, registers no focus, and shows nothing at all,
// with both halves reporting success.
#include "luaug/app/streaming_host.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>

#include "inspector_fixture.h"

using namespace luaug;
using luaug::app::StreamingHost;

namespace {

struct WorldFixture
{
    app::testing::Fixture fixture;
    scene::World world{fixture.classes, fixture.enums, fixture.atoms, 1234u};
    core::InstanceId workspace;

    WorldFixture()
    {
        workspace = world.create(fixture.workspaceClass);
        world.setName(workspace, fixture.atoms.intern("Workspace"));
        world.workspaces().add(workspace, scene::WorkspaceComponent{});
    }

    core::InstanceId cameraAt(core::DVec3 position)
    {
        const core::InstanceId id = world.create(fixture.cameraClass);
        world.setName(id, fixture.atoms.intern("MainCamera"));
        scene::CameraComponent camera;
        camera.cframe.position = position;
        world.cameras().add(id, camera);
        (void)world.setParent(id, workspace);
        return id;
    }

    core::InstanceId partAt(core::DVec3 position)
    {
        const core::InstanceId id = world.create(fixture.partClass);
        world.setName(id, fixture.atoms.intern("Character"));
        scene::PartComponent part;
        part.cframe.position = position;
        world.parts().add(id, part);
        (void)world.setParent(id, workspace);
        return id;
    }
};

} // namespace

TEST_CASE("a world that registers no focus is watched from its camera")
{
    // **The whole of D098.** `StreamingService:AddFocus` is the only thing that
    // fills the focus set, and `partitionProject` runs for any non-editor run
    // with a scene -- so a project with a camera and two parts in it got a grid
    // and never loaded a cell of it. The log said the scene was partitioned,
    // which reads as success, and the world said nothing at all.
    WorldFixture world;
    world.cameraAt(core::DVec3{10.0, 4.0, -6.0});
    world.world.workspaces().find(world.workspace)->currentCamera =
        world.world.findFirstChild(world.workspace, world.fixture.atoms.intern("MainCamera"));

    StreamingHost host;
    host.setWorld(&world.world, world.workspace);

    const std::vector<asset::StreamingFocus> foci = host.collectFoci();
    REQUIRE(foci.size() == 1);
    CHECK(foci[0].position.x == doctest::Approx(10.0));
    CHECK(foci[0].position.y == doctest::Approx(4.0));
    CHECK(foci[0].position.z == doctest::Approx(-6.0));
}

TEST_CASE("a world that registers a focus is not also watched from its camera")
{
    // The fallback is narrowed to exactly the broken case. A game that streams
    // around its character must not also stream around wherever a spectator
    // camera happens to be -- that is cells loaded for nobody, and it would be a
    // change to what every existing project does.
    WorldFixture world;
    world.cameraAt(core::DVec3{500.0, 0.0, 0.0});
    world.world.workspaces().find(world.workspace)->currentCamera =
        world.world.findFirstChild(world.workspace, world.fixture.atoms.intern("MainCamera"));

    const core::InstanceId character = world.partAt(core::DVec3{1.0, 2.0, 3.0});
    world.world.streamingFoci().push_back(character);

    StreamingHost host;
    host.setWorld(&world.world, world.workspace);

    const std::vector<asset::StreamingFocus> foci = host.collectFoci();
    REQUIRE(foci.size() == 1);
    CHECK(foci[0].position.x == doctest::Approx(1.0));
    CHECK(foci[0].position.z == doctest::Approx(3.0));
}

TEST_CASE("a world with no camera and no focus is watched from nowhere")
{
    // Which is the honest answer rather than the origin. A focus at {0,0,0} that
    // nobody asked for would load whatever cell happens to sit there, and a
    // world that streams one arbitrary cell is harder to diagnose than one that
    // streams none.
    WorldFixture world;

    StreamingHost host;
    host.setWorld(&world.world, world.workspace);

    CHECK(host.collectFoci().empty());
}

TEST_CASE("a camera that has been deleted is not a focus")
{
    // `CurrentCamera` is a REFERENCE, and deleting the camera leaves the
    // property naming a dead id -- the same shape as D100, which produced a
    // rainbow instead of a world three separate times. Here it would be a focus
    // reading a component that is not there.
    WorldFixture world;
    const core::InstanceId camera = world.cameraAt(core::DVec3{10.0, 4.0, -6.0});
    world.world.workspaces().find(world.workspace)->currentCamera = camera;

    REQUIRE(world.world.destroy(camera));
    world.world.retireDestroyed();

    StreamingHost host;
    host.setWorld(&world.world, world.workspace);

    CHECK(host.collectFoci().empty());
}
