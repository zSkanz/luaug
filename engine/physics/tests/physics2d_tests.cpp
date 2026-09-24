// The 2D physics seam over its one backend, Box2D (ADR 0008).
//
// What a 2D game needs of it, each on its own: a body lands and rests where it
// should, what touches is reported and what stops touching too, a sensor sees
// without pushing, a ray finds the ground, a group told not to collide falls
// through, a moving platform carries what stands on it -- and two worlds built
// the same way are the same world bit for bit, which is the property the
// engine's replays rest on (R10).
#include "luaug/physics/backends.h"
#include "luaug/physics/physics2d.h"

#include <cstring>
#include <doctest/doctest.h>
#include <vector>

#if LUAUG_PHYSICS_BOX2D

using namespace luaug;
using namespace luaug::physics;

namespace {

constexpr f32 Tick = 1.0f / 60.0f;

// A floor 40 m wide whose top is at y = 0.
Body2DHandle floorOf(IPhysics2D& physics, World2DHandle world, u64 userData = 1)
{
    Body2DDesc floor;
    floor.motion = Motion2D::Static;
    floor.shape.type = Shape2DType::Box;
    floor.shape.halfExtents = core::Vec2{20.0f, 0.5f};
    floor.position = core::Vec2{0.0f, -0.5f};
    floor.userData = userData;
    return physics.createBody(world, floor);
}

Body2DHandle crateAt(IPhysics2D& physics, World2DHandle world, core::Vec2 at, u64 userData = 2)
{
    Body2DDesc crate;
    crate.shape.type = Shape2DType::Box;
    crate.shape.halfExtents = core::Vec2{0.5f, 0.5f};
    crate.position = at;
    crate.userData = userData;
    return physics.createBody(world, crate);
}

void run(IPhysics2D& physics, World2DHandle world, int ticks)
{
    for (int at = 0; at < ticks; ++at)
        physics.step(world, Tick);
}

} // namespace

TEST_CASE("a box dropped on the floor comes to rest on it")
{
    const Physics2DResult physics = createBox2DPhysics();
    const World2DHandle world = physics->createWorld({});
    (void)floorOf(*physics, world);
    const Body2DHandle crate = crateAt(*physics, world, core::Vec2{0.0f, 5.0f});
    run(*physics, world, 180);

    const Body2DState state = physics->bodyState(world, crate);
    CHECK(state.position.y == doctest::Approx(0.5).epsilon(0.02));
    CHECK(state.position.x == doctest::Approx(0.0).epsilon(0.001));
    CHECK(static_cast<double>(state.linearVelocity.y) == doctest::Approx(0.0).epsilon(0.05));
}

TEST_CASE("landing and leaving are reported, with whose bodies they were")
{
    const Physics2DResult physics = createBox2DPhysics();
    const World2DHandle world = physics->createWorld({});
    (void)floorOf(*physics, world, 10);
    const Body2DHandle crate = crateAt(*physics, world, core::Vec2{0.0f, 2.0f}, 20);

    bool landed = false;
    for (int at = 0; at < 120 && !landed; ++at) {
        physics->step(world, Tick);
        for (const Contact2D& contact : physics->contacts(world)) {
            if (contact.kind == Contact2D::Kind::Begin && !contact.sensor) {
                landed = true;
                const bool pair = (contact.userDataA == 10 && contact.userDataB == 20) ||
                                  (contact.userDataA == 20 && contact.userDataB == 10);
                CHECK(pair);
            }
        }
    }
    REQUIRE(landed);

    // A jump: it leaves the floor, and that is reported too.
    physics->applyImpulse(world, crate, core::Vec2{0.0f, 8.0f});
    bool left = false;
    for (int at = 0; at < 30 && !left; ++at) {
        physics->step(world, Tick);
        for (const Contact2D& contact : physics->contacts(world))
            left = left || (contact.kind == Contact2D::Kind::End && !contact.sensor);
    }
    CHECK(left);
}

TEST_CASE("a sensor sees what enters it and pushes nothing")
{
    const Physics2DResult physics = createBox2DPhysics();
    const World2DHandle world = physics->createWorld({});
    (void)floorOf(*physics, world);
    Body2DDesc zone;
    zone.motion = Motion2D::Static;
    zone.sensor = true;
    zone.shape.halfExtents = core::Vec2{1.0f, 1.0f};
    zone.position = core::Vec2{0.0f, 2.0f};
    zone.userData = 30;
    (void)physics->createBody(world, zone);
    const Body2DHandle crate = crateAt(*physics, world, core::Vec2{0.0f, 6.0f}, 40);

    bool entered = false;
    for (int at = 0; at < 120; ++at) {
        physics->step(world, Tick);
        for (const Contact2D& contact : physics->contacts(world))
            entered = entered || (contact.sensor && contact.userDataA == 30 && contact.userDataB == 40);
    }
    CHECK(entered);
    // It fell straight through the zone to the floor.
    CHECK(physics->bodyState(world, crate).position.y == doctest::Approx(0.5).epsilon(0.02));
}

TEST_CASE("a ray down finds the floor, and says where and which way it faces")
{
    const Physics2DResult physics = createBox2DPhysics();
    const World2DHandle world = physics->createWorld({});
    (void)floorOf(*physics, world, 77);
    physics->step(world, Tick);

    const std::optional<Raycast2DHit> hit = physics->raycast(world, core::Vec2{3.0f, 10.0f}, core::Vec2{0.0f, -20.0f});
    REQUIRE(hit.has_value());
    CHECK(hit->userData == 77);
    CHECK(hit->point.y == doctest::Approx(0.0).epsilon(0.001));
    CHECK(hit->normal.y == doctest::Approx(1.0).epsilon(0.001));
    CHECK(hit->fraction == doctest::Approx(0.5).epsilon(0.001));

    // Past it, nothing.
    CHECK_FALSE(physics->raycast(world, core::Vec2{30.0f, 10.0f}, core::Vec2{0.0f, -20.0f}).has_value());
}

TEST_CASE("two groups told not to collide pass through each other")
{
    const Physics2DResult physics = createBox2DPhysics();
    const World2DHandle world = physics->createWorld({});
    (void)floorOf(*physics, world);
    Body2DDesc ghost;
    ghost.shape.halfExtents = core::Vec2{0.5f, 0.5f};
    ghost.position = core::Vec2{0.0f, 3.0f};
    ghost.group = 3;
    const Body2DHandle body = physics->createBody(world, ghost);
    physics->setGroupsCollidable(world, 3, kDefaultCollisionGroup2D, false);
    run(*physics, world, 120);
    CHECK(physics->bodyState(world, body).position.y < -2.0f);
}

TEST_CASE("a moving platform carries what stands on it")
{
    const Physics2DResult physics = createBox2DPhysics();
    const World2DHandle world = physics->createWorld({});
    Body2DDesc platform;
    platform.motion = Motion2D::Kinematic;
    platform.shape.halfExtents = core::Vec2{3.0f, 0.25f};
    platform.position = core::Vec2{0.0f, 0.0f};
    const Body2DHandle lift = physics->createBody(world, platform);
    Body2DDesc rider;
    rider.shape.halfExtents = core::Vec2{0.4f, 0.4f};
    rider.position = core::Vec2{0.0f, 0.7f};
    rider.friction = 1.0f;
    const Body2DHandle box = physics->createBody(world, rider);
    run(*physics, world, 30);

    // Two metres to the right over a second.
    for (int at = 1; at <= 60; ++at) {
        const f32 x = 2.0f * static_cast<f32>(at) / 60.0f;
        physics->moveKinematic(world, lift, core::Vec2{x, 0.0f}, 0.0f, Tick);
        physics->step(world, Tick);
    }
    CHECK(physics->bodyState(world, lift).position.x == doctest::Approx(2.0).epsilon(0.01));
    // Carried, not left behind: the platform set off at two metres a second
    // from rest, and friction takes a moment to bring the box up to it, so it
    // is a little behind -- and still on top, three metres either side.
    const Body2DState carried = physics->bodyState(world, box);
    CHECK(carried.position.x > 1.4f);
    CHECK(carried.position.y > 0.5f);
}

TEST_CASE("ground laid as a chain holds a box, and a capsule slides along it")
{
    const Physics2DResult physics = createBox2DPhysics();
    const World2DHandle world = physics->createWorld({});
    Body2DDesc ground;
    ground.motion = Motion2D::Static;
    ground.shape.type = Shape2DType::Chain;
    // Ghost, three segments along y = 0, ghost.
    ground.shape.points = {core::Vec2{12.0f, 0.0f}, core::Vec2{10.0f, 0.0f}, core::Vec2{0.0f, 0.0f},
                           core::Vec2{-10.0f, 0.0f}, core::Vec2{-12.0f, 0.0f}};
    REQUIRE(physics->createBody(world, ground).valid());
    const Body2DHandle crate = crateAt(*physics, world, core::Vec2{1.0f, 3.0f});
    Body2DDesc walker;
    walker.shape.type = Shape2DType::Capsule;
    walker.shape.radius = 0.4f;
    walker.shape.halfExtents = core::Vec2{0.0f, 0.5f};
    walker.fixedRotation = true;
    walker.position = core::Vec2{-4.0f, 2.0f};
    walker.linearVelocity = core::Vec2{3.0f, 0.0f};
    walker.friction = 0.0f;
    const Body2DHandle capsule = physics->createBody(world, walker);
    run(*physics, world, 120);

    CHECK(physics->bodyState(world, crate).position.y == doctest::Approx(0.5).epsilon(0.02));
    const Body2DState slid = physics->bodyState(world, capsule);
    CHECK(slid.position.y == doctest::Approx(0.9).epsilon(0.05));
    CHECK(slid.angle == doctest::Approx(0.0));
}

TEST_CASE("two worlds built the same way are the same world, bit for bit")
{
    // A tumbling pile: the case where an order depending on anything but the
    // calls made would show first.
    const auto pile = [] {
        const Physics2DResult physics = createBox2DPhysics();
        const World2DHandle world = physics->createWorld({});
        (void)floorOf(*physics, world);
        std::vector<Body2DHandle> bodies;
        for (int row = 0; row < 8; ++row) {
            for (int column = 0; column < 5; ++column) {
                Body2DDesc desc;
                desc.shape.type = (row + column) % 2 == 0 ? Shape2DType::Box : Shape2DType::Circle;
                desc.shape.halfExtents = core::Vec2{0.45f, 0.45f};
                desc.shape.radius = 0.45f;
                desc.position = core::Vec2{static_cast<f32>(column) * 1.1f + static_cast<f32>(row) * 0.13f,
                                           1.0f + static_cast<f32>(row) * 1.05f};
                desc.angle = static_cast<f32>(row * column) * 0.07f;
                bodies.push_back(physics->createBody(world, desc));
            }
        }
        run(*physics, world, 300);
        std::vector<Body2DState> states;
        for (const Body2DHandle body : bodies)
            states.push_back(physics->bodyState(world, body));
        return states;
    };
    const std::vector<Body2DState> first = pile();
    const std::vector<Body2DState> second = pile();
    REQUIRE(first.size() == second.size());
    for (std::size_t at = 0; at < first.size(); ++at) {
        CAPTURE(at);
        CHECK(std::memcmp(&first[at].position, &second[at].position, sizeof(core::Vec2)) == 0);
        CHECK(std::memcmp(&first[at].angle, &second[at].angle, sizeof(f32)) == 0);
    }
}

#endif
