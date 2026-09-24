// The 2D mirror (the 2D layer, post-v1 phase 3) and the tile outline it
// builds ground from.
//
// Against a recording backend, as `physics_sync_tests.cpp` does for the 3D
// mirror: what is asserted is the mirror's own logic -- which bodies exist,
// when one is rebuilt, what the solver's answers become. That Box2D then does
// the right thing with them is `physics2d_tests.cpp` and the conformance spec.

#include "luaug/scene/physics_sync_2d.h"
#include "luaug/scene/tile_outline.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <ostream>
#include <vector>

#include "scene_fixture.h"

namespace luaug::scene {
namespace {

using physics::Body2DDesc;
using physics::Body2DHandle;
using physics::Body2DState;
using physics::World2DHandle;

class FakePhysics2D final : public physics::IPhysics2D
{
public:
    struct Body
    {
        Body2DDesc desc;
        Body2DState state;
        bool alive = true;
    };
    std::vector<Body> bodies;
    std::vector<physics::Contact2D> pending;
    std::vector<core::Vec2> impulses;
    core::Vec2 gravity{0.0f, -9.81f};
    int steps = 0;

    World2DHandle createWorld(const physics::World2DDesc& desc) override
    {
        gravity = desc.gravity;
        return World2DHandle{0, 1};
    }
    void destroyWorld(World2DHandle) override {}
    void setGravity(World2DHandle, core::Vec2 value) override { gravity = value; }
    void step(World2DHandle, core::f32) override { ++steps; }
    Body2DHandle createBody(World2DHandle, const Body2DDesc& desc) override
    {
        Body body;
        body.desc = desc;
        body.state.position = desc.position;
        body.state.angle = desc.angle;
        bodies.push_back(body);
        return Body2DHandle{static_cast<core::u32>(bodies.size() - 1), 1};
    }
    void destroyBody(World2DHandle, Body2DHandle body) override { bodies[body.index].alive = false; }
    void setBodyTransform(World2DHandle, Body2DHandle body, core::Vec2 position, core::f32 angle) override
    {
        bodies[body.index].state.position = position;
        bodies[body.index].state.angle = angle;
    }
    void moveKinematic(World2DHandle, Body2DHandle body, core::Vec2 position, core::f32 angle, core::f32) override
    {
        bodies[body.index].state.position = position;
        bodies[body.index].state.angle = angle;
    }
    void setVelocity(World2DHandle, Body2DHandle body, core::Vec2 linear, core::f32 angular) override
    {
        bodies[body.index].state.linearVelocity = linear;
        bodies[body.index].state.angularVelocity = angular;
    }
    void applyImpulse(World2DHandle, Body2DHandle, core::Vec2 impulse) override { impulses.push_back(impulse); }
    [[nodiscard]] Body2DState bodyState(World2DHandle, Body2DHandle body) const override
    {
        return bodies[body.index].state;
    }
    [[nodiscard]] std::span<const physics::Contact2D> contacts(World2DHandle) const override { return pending; }
    [[nodiscard]] std::optional<physics::Raycast2DHit> raycast(World2DHandle, core::Vec2, core::Vec2,
                                                               const physics::Raycast2DFilter&) const override
    {
        return std::nullopt;
    }
    void setGroupsCollidable(World2DHandle, physics::CollisionGroup2D, physics::CollisionGroup2D, bool) override {}

    [[nodiscard]] int alive() const
    {
        int count = 0;
        for (const Body& body : bodies)
            count += body.alive ? 1 : 0;
        return count;
    }
};

struct Mirror
{
    testing::Fixture fixture;
    FakePhysics2D backend;
    core::InstanceId workspace;
    PhysicsSync2D sync{fixture.world, backend};

    Mirror()
    {
        workspace = fixture.folder("Workspace");
        sync.setWorkspace(workspace);
    }

    // The fixture has no `Part2D` class; the mirror reads only the pool, so a
    // folder carrying the component is a part as far as it can tell.
    [[nodiscard]] core::InstanceId part(core::Vec2 at, bool anchored = false)
    {
        const core::InstanceId id = fixture.folder("Part2D");
        Part2DComponent part;
        part.position = at;
        part.anchored = anchored;
        fixture.world.parts2d().add(id, part);
        REQUIRE(fixture.world.setParent(id, workspace) == std::nullopt);
        return id;
    }

    [[nodiscard]] core::InstanceId tilemap()
    {
        const core::InstanceId id = fixture.folder("Tilemap2D");
        fixture.world.tilemaps2d().add(id, Tilemap2DComponent{});
        REQUIRE(fixture.world.setParent(id, workspace) == std::nullopt);
        return id;
    }

    void step() { sync.step(1.0 / 60.0); }
};

[[nodiscard]] Tilemap2DComponent paint(std::initializer_list<std::pair<core::i32, core::i32>> cells)
{
    Tilemap2DComponent tilemap;
    for (const auto& [x, y] : cells)
        (void)tilemap.setCell(x, y, 1);
    return tilemap;
}

} // namespace

TEST_CASE("one tile is one square loop, counter-clockwise")
{
    const auto loops = tileOutlines(paint({{0, 0}}));
    REQUIRE(loops.size() == 1);
    REQUIRE(loops[0].size() == 4);
    // Twice the signed area: positive going counter-clockwise.
    core::f32 area = 0.0f;
    for (std::size_t at = 0; at < loops[0].size(); ++at) {
        const core::Vec2 a = loops[0][at];
        const core::Vec2 b = loops[0][(at + 1) % loops[0].size()];
        area += a.x * b.y - b.x * a.y;
    }
    CHECK(area == doctest::Approx(2.0));
}

TEST_CASE("a floor of tiles is one loop with no corner along its top")
{
    // Forty tiles across two 16-cell blocks and a negative one: the seams
    // between blocks must not leave a corner behind either.
    Tilemap2DComponent tilemap;
    for (core::i32 x = -8; x < 32; ++x)
        (void)tilemap.setCell(x, 0, 1);
    const auto loops = tileOutlines(tilemap);
    REQUIRE(loops.size() == 1);
    CHECK(loops[0].size() == 4);
}

TEST_CASE("an L shape has six corners and a hole is its own loop")
{
    const auto shape = tileOutlines(paint({{0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, 2}}));
    REQUIRE(shape.size() == 1);
    CHECK(shape[0].size() == 6);

    // A ring of eight around an empty middle: the outside and the hole.
    const auto ring = tileOutlines(paint({{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}, {0, 2}, {1, 2}, {2, 2}}));
    CHECK(ring.size() == 2);
}

TEST_CASE("two regions that touch only at a corner get a loop each")
{
    const auto loops = tileOutlines(paint({{0, 0}, {1, 1}}));
    REQUIRE(loops.size() == 2);
    CHECK(loops[0].size() == 4);
    CHECK(loops[1].size() == 4);
}

TEST_CASE("the outline is the same however the map was painted")
{
    const auto forwards = tileOutlines(paint({{0, 0}, {1, 0}, {5, 3}, {-20, 7}}));
    const auto backwards = tileOutlines(paint({{-20, 7}, {5, 3}, {1, 0}, {0, 0}}));
    REQUIRE(forwards.size() == backwards.size());
    for (std::size_t at = 0; at < forwards.size(); ++at)
        CHECK(forwards[at] == backwards[at]);
}

TEST_CASE("a Part2D under Workspace is a body, and out of it is none")
{
    Mirror mirror;
    const core::InstanceId falling = mirror.part(core::Vec2{0.0f, 5.0f});
    const core::InstanceId ground = mirror.part(core::Vec2{0.0f, 0.0f}, true);
    mirror.step();
    REQUIRE(mirror.backend.alive() == 2);
    // In pool order, which is creation order.
    CHECK(mirror.backend.bodies[0].desc.userData == PhysicsSync2D::userDataOf(falling));
    CHECK(mirror.backend.bodies[0].desc.motion == physics::Motion2D::Dynamic);
    CHECK(mirror.backend.bodies[1].desc.motion == physics::Motion2D::Static);

    REQUIRE(mirror.fixture.world.setParent(ground, core::InstanceId{}) == std::nullopt);
    mirror.step();
    CHECK(mirror.backend.alive() == 1);
    CHECK(mirror.sync.bodyCount() == 1);
}

TEST_CASE("the solver's answer is written back, and a script's write is pushed down")
{
    Mirror mirror;
    const core::InstanceId id = mirror.part(core::Vec2{0.0f, 5.0f});
    mirror.step();
    FakePhysics2D::Body& body = mirror.backend.bodies[0];
    body.state.position = core::Vec2{1.0f, 4.0f};
    body.state.linearVelocity = core::Vec2{0.0f, -2.0f};
    mirror.step();
    Part2DComponent& part = *mirror.fixture.world.parts2d().find(id);
    CHECK(part.position == core::Vec2{1.0f, 4.0f});
    CHECK(part.velocity == core::Vec2{0.0f, -2.0f});

    part.position = core::Vec2{-3.0f, 9.0f};
    part.pendingImpulse = core::Vec2{2.0f, 0.0f};
    mirror.step();
    CHECK(body.state.position == core::Vec2{-3.0f, 9.0f});
    REQUIRE(mirror.backend.impulses.size() == 1);
    CHECK(part.pendingImpulse == core::Vec2{0.0f, 0.0f});
    // Nothing was rebuilt for either.
    CHECK(mirror.backend.bodies.size() == 1);
}

TEST_CASE("an anchored part a script moves becomes kinematic")
{
    Mirror mirror;
    const core::InstanceId id = mirror.part(core::Vec2{0.0f, 0.0f}, true);
    mirror.step();
    REQUIRE(mirror.backend.bodies.back().desc.motion == physics::Motion2D::Static);
    mirror.fixture.world.parts2d().find(id)->position = core::Vec2{1.0f, 0.0f};
    mirror.step();
    CHECK(mirror.backend.bodies.back().desc.motion == physics::Motion2D::Kinematic);
    CHECK(mirror.backend.alive() == 1);
}

TEST_CASE("a tilemap is rebuilt when a cell changes and only then")
{
    Mirror mirror;
    const core::InstanceId id = mirror.tilemap();
    Tilemap2DComponent& tilemap = *mirror.fixture.world.tilemaps2d().find(id);
    for (core::i32 x = 0; x < 10; ++x)
        (void)tilemap.setCell(x, 0, 1);
    tilemap.revision += 1;
    mirror.step();
    REQUIRE(mirror.backend.alive() == 1);
    CHECK(mirror.backend.bodies[0].desc.shape.type == physics::Shape2DType::Chain);
    CHECK(mirror.backend.bodies[0].desc.shape.loop);

    mirror.step();
    CHECK(mirror.backend.bodies.size() == 1);

    // A gap in the middle: two regions, two loops.
    (void)tilemap.setCell(5, 0, 0);
    tilemap.revision += 1;
    mirror.step();
    CHECK(mirror.backend.alive() == 2);

    tilemap.collides = false;
    mirror.step();
    CHECK(mirror.backend.alive() == 0);
}

TEST_CASE("after a restore, a revision the mirror has seen is not trusted")
{
    // An undo puts a tilemap's revision back, and the next edit can arrive at
    // the number the mirror built from before -- with different cells.
    Mirror mirror;
    const core::InstanceId id = mirror.tilemap();
    mirror.step();
    const WorldSnapshot before = mirror.fixture.world.snapshot();

    Tilemap2DComponent* tilemap = mirror.fixture.world.tilemaps2d().find(id);
    (void)tilemap->setCell(0, 0, 1);
    tilemap->revision += 1;
    mirror.step();
    REQUIRE(mirror.backend.alive() == 1);

    mirror.fixture.world.restore(before);
    tilemap = mirror.fixture.world.tilemaps2d().find(id);
    (void)tilemap->setCell(0, 0, 1);
    (void)tilemap->setCell(5, 0, 1);
    tilemap->revision += 1;
    mirror.step();
    // Two separate tiles: two loops, not the one built before the undo.
    CHECK(mirror.backend.alive() == 2);
}

TEST_CASE("a contact is Touched on each part, and a tilemap raises nothing of its own")
{
    Mirror mirror;
    const core::InstanceId part = mirror.part(core::Vec2{0.0f, 1.0f});
    const core::InstanceId ground = mirror.tilemap();
    mirror.step();
    mirror.fixture.world.changes().clear();

    mirror.backend.pending.push_back(physics::Contact2D{
        physics::Contact2D::Kind::Begin, PhysicsSync2D::userDataOf(part), PhysicsSync2D::userDataOf(ground), false});
    mirror.step();
    std::vector<Change> touches;
    for (const Change& change : mirror.fixture.world.changes().take()) {
        if (change.kind == ChangeKind::InstanceEvent)
            touches.push_back(change);
    }
    REQUIRE(touches.size() == 1);
    CHECK(touches[0].subject == part);
    CHECK(touches[0].other == ground);
    CHECK(mirror.fixture.world.atoms().text(touches[0].name) == "Touched");
}

TEST_CASE("the plane's gravity is the workspace's x and y")
{
    Mirror mirror;
    WorkspaceComponent workspace;
    workspace.gravity = core::Vec3{1.0f, -20.0f, 7.0f};
    mirror.fixture.world.workspaces().add(mirror.workspace, workspace);
    (void)mirror.part(core::Vec2{0.0f, 0.0f});
    mirror.step();
    CHECK(mirror.backend.gravity == core::Vec2{1.0f, -20.0f});
}

TEST_CASE("on a replica the authority moves a part: its body is kinematic and nothing is written back")
{
    Mirror mirror;
    mirror.fixture.world.engineState().networkTopology = NetworkTopology::Replica;
    const core::InstanceId id = mirror.part(core::Vec2{0.0f, 5.0f});
    mirror.step();
    REQUIRE(mirror.backend.alive() == 1);
    CHECK(mirror.backend.bodies[0].desc.motion == physics::Motion2D::Kinematic);

    // Whatever the solver holds, the component keeps what the wire said.
    mirror.backend.bodies[0].state.position = core::Vec2{9.0f, 9.0f};
    mirror.step();
    CHECK((mirror.fixture.world.parts2d().find(id)->position == core::Vec2{0.0f, 5.0f}));

    // And a snapshot moving it drives the body there.
    mirror.fixture.world.parts2d().find(id)->position = core::Vec2{1.0f, 4.0f};
    mirror.step();
    CHECK((mirror.backend.bodies[0].state.position == core::Vec2{1.0f, 4.0f}));
    CHECK(mirror.backend.bodies.size() == 1);
}

TEST_CASE("a world with nothing on the plane never steps it")
{
    Mirror mirror;
    mirror.step();
    CHECK(mirror.backend.steps == 0);
}

} // namespace luaug::scene
