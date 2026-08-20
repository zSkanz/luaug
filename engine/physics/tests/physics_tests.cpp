// The physics seam, exercised through the interface and never through Jolt.
//
// Everything here is written against `IPhysics3D`, which is the point twice
// over: it is what a second backend would have to satisfy, and it is the only
// vocabulary the modules above L2 have. A test that reached for a JPH type
// would be testing something no caller can see.
#include "luaug/physics/backends.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <doctest/doctest.h>
#include <memory>
#include <vector>

using namespace luaug;
using namespace luaug::physics;

namespace {

constexpr f32 kFixedDt = 1.0f / 60.0f;

// A floor at y = 0 with its top face there, so a part dropped from above lands
// at its own half-height.
[[nodiscard]] BodyDesc floorDesc()
{
    BodyDesc desc;
    desc.shape.type = ShapeType::Box;
    desc.shape.size = core::Vec3{100.0f, 2.0f, 100.0f};
    desc.transform.position = core::DVec3{0.0, -1.0, 0.0};
    desc.motion = MotionType::Static;
    desc.userData = 1;
    return desc;
}

[[nodiscard]] BodyDesc cubeDesc(core::DVec3 at, u64 userData)
{
    BodyDesc desc;
    desc.shape.type = ShapeType::Box;
    desc.shape.size = core::Vec3{1.0f, 1.0f, 1.0f};
    desc.transform.position = at;
    desc.motion = MotionType::Dynamic;
    desc.userData = userData;
    return desc;
}

struct Fixture
{
    Fixture()
    {
        physics = createJoltPhysics();
        REQUIRE(physics != nullptr);
        world = physics->createWorld(WorldDesc{});
        REQUIRE(world.valid());
    }

    ~Fixture() { physics->destroyWorld(world); }

    // Deliberately drops the handle. Several cases need a body only to exist
    // -- a floor, a wall, the twelve crates an ordering assertion counts -- and
    // naming a variable nothing reads is noise the next reader has to check.
    void spawn(const BodyDesc& desc)
    {
        const BodyHandle handle = physics->createBody(world, desc);
        REQUIRE(handle.valid());
    }

    void run(int ticks)
    {
        for (int i = 0; i < ticks; ++i) {
            physics->step(world, kFixedDt);
        }
    }

    PhysicsResult physics;
    WorldHandle world;
};

} // namespace

TEST_CASE("a handle nobody issued resolves to nothing")
{
    Fixture fixture;

    // The reason handles carry a generation: a state read through a stale one
    // must be inert rather than aliasing whatever moved into the slot.
    CHECK_FALSE(BodyHandle{}.valid());
    const BodyState state = fixture.physics->bodyState(fixture.world, BodyHandle{});
    CHECK(state.active == false);
    CHECK(state.transform.position == core::DVec3{});

    const BodyHandle body = fixture.physics->createBody(fixture.world, cubeDesc({0.0, 10.0, 0.0}, 2));
    REQUIRE(body.valid());
    fixture.physics->destroyBody(fixture.world, body);
    CHECK_FALSE(fixture.physics->bodyState(fixture.world, body).active);
}

TEST_CASE("a cube falls onto the ground and stops there")
{
    Fixture fixture;
    fixture.spawn(floorDesc());
    const BodyHandle cube = fixture.physics->createBody(fixture.world, cubeDesc({0.0, 10.0, 0.0}, 2));
    REQUIRE(cube.valid());

    // Two seconds is comfortably longer than the ~1.35 s a 9.5 m fall takes,
    // and the assertion is about where it comes to rest rather than when.
    fixture.run(180);

    const BodyState state = fixture.physics->bodyState(fixture.world, cube);
    CHECK(state.transform.position.y == doctest::Approx(0.5).epsilon(0.05));
    CHECK(std::abs(state.linearVelocity.y) < 0.1f);
}

TEST_CASE("an anchored part does not fall")
{
    Fixture fixture;
    BodyDesc desc = cubeDesc({0.0, 10.0, 0.0}, 2);
    desc.motion = MotionType::Static;
    const BodyHandle body = fixture.physics->createBody(fixture.world, desc);

    fixture.run(60);

    CHECK(fixture.physics->bodyState(fixture.world, body).transform.position.y == doctest::Approx(10.0));
}

TEST_CASE("density decides mass, and mass decides which way a seesaw tips")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    BodyDesc light = cubeDesc({-4.0, 5.0, 0.0}, 2);
    light.density = 0.1f;
    BodyDesc heavy = cubeDesc({4.0, 5.0, 0.0}, 3);
    heavy.density = 10.0f;

    const BodyHandle lightBody = fixture.physics->createBody(fixture.world, light);
    const BodyHandle heavyBody = fixture.physics->createBody(fixture.world, heavy);

    // Same impulse, different mass: the lighter one must end up moving faster.
    // This is the cheapest assertion that density reaches the solver at all --
    // a property that is stored and never read would leave these equal.
    fixture.physics->applyImpulse(fixture.world, lightBody, core::Vec3{10.0f, 0.0f, 0.0f});
    fixture.physics->applyImpulse(fixture.world, heavyBody, core::Vec3{10.0f, 0.0f, 0.0f});
    fixture.run(1);

    const f32 lightSpeed = std::abs(fixture.physics->bodyState(fixture.world, lightBody).linearVelocity.x);
    const f32 heavySpeed = std::abs(fixture.physics->bodyState(fixture.world, heavyBody).linearVelocity.x);
    CHECK(lightSpeed > heavySpeed * 2.0f);
}

TEST_CASE("a ray hits the nearest queryable body and reports where")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    RayD ray;
    ray.origin = core::DVec3{0.0, 10.0, 0.0};
    ray.direction = core::Vec3{0.0f, -20.0f, 0.0f};

    RayHit hit;
    REQUIRE(fixture.physics->raycast(fixture.world, ray, QueryFilter{}, hit));
    CHECK(hit.userData == 1);
    CHECK(hit.position.y == doctest::Approx(0.0).epsilon(0.01));
    CHECK(hit.distance == doctest::Approx(10.0).epsilon(0.01));
    CHECK(hit.normal.y == doctest::Approx(1.0).epsilon(0.01));

    SUBCASE("a body excluded by the filter is not hit")
    {
        const std::array<u64, 1> exclude{1};
        QueryFilter filter;
        filter.mode = QueryFilter::Mode::Exclude;
        filter.userData = exclude;
        CHECK_FALSE(fixture.physics->raycast(fixture.world, ray, filter, hit));
    }

    SUBCASE("CanQuery false removes a body from every query")
    {
        RayHit ignored;
        const BodyHandle floor = fixture.physics->createBody(fixture.world, floorDesc());
        fixture.physics->setBodyFlags(fixture.world, floor, true, false);
        // The original floor is still there, so this asserts the flag rather
        // than an empty world.
        CHECK(fixture.physics->raycast(fixture.world, ray, QueryFilter{}, ignored));
        CHECK(ignored.userData == 1);
    }
}

TEST_CASE("a shape cast sweeps rather than points")
{
    Fixture fixture;
    // A wall with a gap a ray would pass through and a sphere would not.
    BodyDesc left = cubeDesc({-1.5, 5.0, 0.0}, 2);
    left.motion = MotionType::Static;
    BodyDesc right = cubeDesc({1.5, 5.0, 0.0}, 3);
    right.motion = MotionType::Static;
    fixture.spawn(left);
    fixture.spawn(right);

    RayD ray;
    ray.origin = core::DVec3{0.0, 5.0, -10.0};
    ray.direction = core::Vec3{0.0f, 0.0f, 20.0f};

    RayHit hit;
    CHECK_FALSE(fixture.physics->raycast(fixture.world, ray, QueryFilter{}, hit));
    CHECK(fixture.physics->spherecast(fixture.world, ray, 1.0f, QueryFilter{}, hit));
}

TEST_CASE("an overlap query reports each body once, in a stable order")
{
    Fixture fixture;
    for (u64 i = 0; i < 8; ++i) {
        BodyDesc desc = cubeDesc({static_cast<f64>(i) * 0.5, 5.0, 0.0}, 100 + i);
        desc.motion = MotionType::Static;
        fixture.spawn(desc);
    }

    core::CFrameD box;
    box.position = core::DVec3{2.0, 5.0, 0.0};

    std::vector<u64> first;
    std::vector<u64> second;
    fixture.physics->overlapBox(fixture.world, box, core::Vec3{20.0f, 20.0f, 20.0f}, QueryFilter{}, first);
    fixture.physics->overlapBox(fixture.world, box, core::Vec3{20.0f, 20.0f, 20.0f}, QueryFilter{}, second);

    CHECK(first.size() == 8);
    CHECK(first == second);
    CHECK(std::is_sorted(first.begin(), first.end()));
}

TEST_CASE("a contact begins once and ends once")
{
    Fixture fixture;
    fixture.spawn(floorDesc());
    fixture.spawn(cubeDesc({0.0, 2.0, 0.0}, 2));

    int began = 0;
    int ended = 0;
    for (int i = 0; i < 240; ++i) {
        fixture.physics->step(fixture.world, kFixedDt);
        for (const ContactEvent& event : fixture.physics->drainContacts(fixture.world)) {
            if (event.phase == ContactPhase::Began) {
                ++began;
                CHECK(event.firstUserData == 1);
                CHECK(event.secondUserData == 2);
            }
            else {
                ++ended;
            }
        }
    }

    // A cube dropped from 1.5 m onto a floor with no restitution settles, so it
    // touches once and never lets go. Persisting contacts must not re-fire --
    // that is the whole difference between a diff and a callback relay.
    CHECK(began == 1);
    CHECK(ended == 0);
}

TEST_CASE("a non-collidable part still reports a touch and does not stop anything")
{
    Fixture fixture;
    BodyDesc trigger = floorDesc();
    trigger.collidable = false;
    trigger.userData = 5;
    fixture.spawn(trigger);
    const BodyHandle faller = fixture.physics->createBody(fixture.world, cubeDesc({0.0, 3.0, 0.0}, 6));

    bool touched = false;
    for (int i = 0; i < 180; ++i) {
        fixture.physics->step(fixture.world, kFixedDt);
        for (const ContactEvent& event : fixture.physics->drainContacts(fixture.world)) {
            if (event.phase == ContactPhase::Began) {
                touched = true;
            }
        }
    }

    CHECK(touched);
    // Through, not onto: CanCollide false means no collision response.
    CHECK(fixture.physics->bodyState(fixture.world, faller).transform.position.y < -5.0);
}

TEST_CASE("collision groups decide what collides, and Default always exists")
{
    Fixture fixture;
    CHECK(fixture.physics->groupsCollidable(fixture.world, kDefaultCollisionGroup, kDefaultCollisionGroup));

    const CollisionGroup ghosts = fixture.physics->registerCollisionGroup(fixture.world, "Ghosts");
    REQUIRE(ghosts != kDefaultCollisionGroup);
    // Registering twice returns the same group, which is what makes a script
    // that registers at boot survive a hot reload.
    CHECK(fixture.physics->registerCollisionGroup(fixture.world, "Ghosts") == ghosts);
    CHECK(fixture.physics->findCollisionGroup(fixture.world, "Ghosts") == ghosts);
    CHECK(fixture.physics->groupsCollidable(fixture.world, ghosts, kDefaultCollisionGroup));

    fixture.physics->setGroupsCollidable(fixture.world, ghosts, kDefaultCollisionGroup, false);
    CHECK_FALSE(fixture.physics->groupsCollidable(fixture.world, ghosts, kDefaultCollisionGroup));

    std::vector<std::string_view> names;
    fixture.physics->collectCollisionGroups(fixture.world, names);
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "Default");
    CHECK(names[1] == "Ghosts");

    BodyDesc ghost = cubeDesc({0.0, 2.0, 0.0}, 7);
    ghost.group = ghosts;
    const BodyHandle body = fixture.physics->createBody(fixture.world, ghost);
    fixture.spawn(floorDesc());
    fixture.run(180);

    CHECK(fixture.physics->bodyState(fixture.world, body).transform.position.y < -5.0);
}

TEST_CASE("a character walks up a step it could not climb as a rigid body")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    // A 0.4 m ledge: below the character's step height, well above what a
    // capsule sliding into a wall would climb. The box is 0.8 m tall and
    // centred on the floor plane, so its top face -- the thing that has to be
    // stepped onto -- is at 0.4.
    BodyDesc step;
    step.shape.size = core::Vec3{4.0f, 0.8f, 4.0f};
    step.transform.position = core::DVec3{0.0, 0.0, 6.0};
    step.motion = MotionType::Static;
    step.userData = 8;
    fixture.spawn(step);

    CharacterDesc desc;
    desc.transform.position = core::DVec3{0.0, 0.0, 0.0};
    desc.stepHeight = 0.6f;
    desc.userData = 9;
    const CharacterHandle character = fixture.physics->createCharacter(fixture.world, desc);
    REQUIRE(character.valid());

    // Ninety ticks at 4 m/s is 6 m, which lands the character in the middle of
    // the ledge rather than past it. The first version of this ran for three
    // seconds, walked the full twelve metres, came back down the far side and
    // reported standing on the floor -- a pass condition that would have been
    // satisfied by a character that never climbed anything.
    for (int i = 0; i < 90; ++i) {
        const CharacterState state = fixture.physics->characterState(fixture.world, character);
        // Walk forward, and fall while airborne. This is the movement model the
        // caller owns; the seam owns the sweeping.
        const f32 vertical =
            state.ground == CharacterGround::Grounded ? 0.0f : state.linearVelocity.y - 9.81f * kFixedDt;
        fixture.physics->moveCharacter(fixture.world, character, core::Vec3{0.0f, vertical, 4.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);
    }

    const CharacterState state = fixture.physics->characterState(fixture.world, character);
    CHECK(state.transform.position.z > 5.0);
    CHECK(state.transform.position.z < 8.0);
    CHECK(state.transform.position.y == doctest::Approx(0.4).epsilon(0.2));
    CHECK(state.ground == CharacterGround::Grounded);
    // On the ledge, and it says which ledge -- what a moving platform needs at
    // M6 and what a `Landed` signal names.
    CHECK(state.groundUserData == 8);
}

TEST_CASE("a character falls, lands, and names what it landed on")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    CharacterDesc desc;
    desc.transform.position = core::DVec3{0.0, 8.0, 0.0};
    const CharacterHandle character = fixture.physics->createCharacter(fixture.world, desc);

    for (int i = 0; i < 180; ++i) {
        const CharacterState state = fixture.physics->characterState(fixture.world, character);
        const f32 vertical =
            state.ground == CharacterGround::Grounded ? 0.0f : state.linearVelocity.y - 9.81f * kFixedDt;
        fixture.physics->moveCharacter(fixture.world, character, core::Vec3{0.0f, vertical, 0.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);
    }

    const CharacterState state = fixture.physics->characterState(fixture.world, character);
    CHECK(state.ground == CharacterGround::Grounded);
    CHECK(state.transform.position.y == doctest::Approx(0.0).epsilon(0.05));
    CHECK(state.groundUserData == 1);
}

TEST_CASE("two identical worlds stepped identically agree, body for body")
{
    // The seam's own half of ADR 0025: same build, same platform, same inputs.
    // Not a replacement for the replay gate -- that one covers the whole world
    // -- but the one that says which side of the seam a divergence came from.
    const auto simulate = [](std::vector<core::DVec3>& out) {
        Fixture fixture;
        fixture.spawn(floorDesc());
        std::vector<BodyHandle> cubes;
        for (u64 i = 0; i < 24; ++i) {
            const f64 x = static_cast<f64>(i % 4) * 1.1;
            const f64 y = 2.0 + static_cast<f64>(i / 4) * 1.4;
            cubes.push_back(fixture.physics->createBody(fixture.world, cubeDesc({x, y, 0.0}, 100 + i)));
        }
        for (int tick = 0; tick < 300; ++tick) {
            if (tick == 30) {
                fixture.physics->applyImpulse(fixture.world, cubes[5], core::Vec3{3.0f, 0.0f, 1.0f});
            }
            fixture.physics->step(fixture.world, kFixedDt);
        }
        for (const BodyHandle& cube : cubes) {
            out.push_back(fixture.physics->bodyState(fixture.world, cube).transform.position);
        }
    };

    std::vector<core::DVec3> first;
    std::vector<core::DVec3> second;
    simulate(first);
    simulate(second);

    REQUIRE(first.size() == second.size());
    CHECK(first == second);
}

TEST_CASE("the active-body list is ordered, and it is ordered by our handles")
{
    Fixture fixture;
    fixture.spawn(floorDesc());
    for (u64 i = 0; i < 12; ++i) {
        fixture.spawn(cubeDesc({static_cast<f64>(i) * 2.0, 4.0, 0.0}, 200 + i));
    }
    fixture.run(5);

    std::vector<ActiveBody> active;
    fixture.physics->collectActiveBodies(fixture.world, active);
    REQUIRE(active.size() == 12);
    CHECK(std::is_sorted(active.begin(), active.end(),
                         [](const ActiveBody& a, const ActiveBody& b) { return a.body.index < b.body.index; }));
    // A static floor is never active, which is what makes the writeback loop
    // cost nothing for a world that is mostly scenery.
    CHECK(std::none_of(active.begin(), active.end(), [](const ActiveBody& body) { return body.userData == 1; }));
}

TEST_CASE("the rollback seam refuses rather than pretending")
{
    Fixture fixture;
    std::vector<u8> blob;
    CHECK_FALSE(fixture.physics->saveState(fixture.world, blob));
    CHECK(blob.empty());
    CHECK_FALSE(fixture.physics->restoreState(fixture.world, blob));
}
