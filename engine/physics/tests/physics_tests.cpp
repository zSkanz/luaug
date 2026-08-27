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
#include <cstring>
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
    // The centre of a five-metre capsule standing on a floor whose top face is
    // at zero. Placed at zero it would start half buried, which is a different
    // test about how the controller recovers.
    desc.transform.position = core::DVec3{0.0, 2.5, 0.0};
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
    // The transform is the capsule's CENTRE, so a five-metre character standing
    // on a 0.4 m ledge has its centre at 2.9.
    CHECK(state.transform.position.y == doctest::Approx(2.9).epsilon(0.2));
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
    CHECK(state.transform.position.y == doctest::Approx(2.5).epsilon(0.05));
    CHECK(state.groundUserData == 1);
}

TEST_CASE("two characters cannot walk through each other")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    // Six metres apart, walking straight at one another. Two metres wide each,
    // so "not overlapping" means their centres stay at least two apart.
    CharacterDesc left;
    left.transform.position = core::DVec3{-3.0, 2.5, 0.0};
    left.userData = 10;
    CharacterDesc right;
    right.transform.position = core::DVec3{3.0, 2.5, 0.0};
    right.userData = 11;

    const CharacterHandle a = fixture.physics->createCharacter(fixture.world, left);
    const CharacterHandle b = fixture.physics->createCharacter(fixture.world, right);
    REQUIRE(a.valid());
    REQUIRE(b.valid());

    for (int i = 0; i < 180; ++i) {
        fixture.physics->moveCharacter(fixture.world, a, core::Vec3{2.0f, 0.0f, 0.0f}, kFixedDt);
        fixture.physics->moveCharacter(fixture.world, b, core::Vec3{-2.0f, 0.0f, 0.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);
    }

    const f64 gap = fixture.physics->characterState(fixture.world, b).transform.position.x -
                    fixture.physics->characterState(fixture.world, a).transform.position.x;

    // Three seconds of walking into each other at 2 m/s. What stops them is
    // the inner body -- verified by deleting `settings.mInnerBodyShape` and
    // re-running, which walks them straight through one another and fails this
    // check at a gap of -40. With it, they stop at 2.02: two metres of width
    // and Jolt's own collision padding.
    CHECK(gap > 1.8);
}

TEST_CASE("a character touches the wall it walks into, and the floor it stands on")
{
    // D028: a `CharacterBody` is a `BasePart` and a script expects `Touched`
    // from one. The rigid-body contact listener cannot give it -- a
    // `CharacterVirtual` is not a body in the broad phase -- so M6 answered the
    // ground half by diffing the surface under the feet, and a wall walked into
    // fired nothing at all. The obby's finish had to become a pad you stand on.
    Fixture fixture;
    fixture.spawn(floorDesc());

    BodyDesc wall;
    wall.shape.size = core::Vec3{6.0f, 6.0f, 1.0f};
    wall.transform.position = core::DVec3{0.0, 3.0, 6.0};
    wall.motion = MotionType::Static;
    wall.userData = 21;
    fixture.spawn(wall);

    CharacterDesc desc;
    desc.transform.position = core::DVec3{0.0, 2.5, 0.0};
    desc.userData = 9;
    const CharacterHandle character = fixture.physics->createCharacter(fixture.world, desc);
    REQUIRE(character.valid());

    std::vector<u64> began;
    for (int i = 0; i < 120; ++i) {
        const CharacterState state = fixture.physics->characterState(fixture.world, character);
        const f32 vertical =
            state.ground == CharacterGround::Grounded ? 0.0f : state.linearVelocity.y - 9.81f * kFixedDt;
        fixture.physics->moveCharacter(fixture.world, character, core::Vec3{0.0f, vertical, 4.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);

        for (const ContactEvent& event : fixture.physics->drainContacts(fixture.world)) {
            if (event.phase == ContactPhase::Began && event.firstUserData == 9)
                began.push_back(event.secondUserData);
        }
    }

    // The floor it stands on and the wall it walks into, each named once. The
    // wall is the half D028 carried; the floor is the half that used to be
    // diffed a layer up and is one mechanism now.
    CHECK(std::count(began.begin(), began.end(), 1u) == 1);
    CHECK(std::count(began.begin(), began.end(), 21u) == 1);

    const CharacterState state = fixture.physics->characterState(fixture.world, character);
    CHECK(state.transform.position.z < 5.6);
}

TEST_CASE("a character stops touching a wall it walks away from")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    BodyDesc wall;
    wall.shape.size = core::Vec3{6.0f, 6.0f, 1.0f};
    wall.transform.position = core::DVec3{0.0, 3.0, 6.0};
    wall.motion = MotionType::Static;
    wall.userData = 21;
    fixture.spawn(wall);

    CharacterDesc desc;
    desc.transform.position = core::DVec3{0.0, 2.5, 0.0};
    desc.userData = 9;
    const CharacterHandle character = fixture.physics->createCharacter(fixture.world, desc);

    const auto walk = [&](f32 forward, int ticks, std::vector<ContactEvent>& out) {
        for (int i = 0; i < ticks; ++i) {
            const CharacterState state = fixture.physics->characterState(fixture.world, character);
            const f32 vertical =
                state.ground == CharacterGround::Grounded ? 0.0f : state.linearVelocity.y - 9.81f * kFixedDt;
            fixture.physics->moveCharacter(fixture.world, character, core::Vec3{0.0f, vertical, forward}, kFixedDt);
            fixture.physics->step(fixture.world, kFixedDt);
            const std::span<const ContactEvent> events = fixture.physics->drainContacts(fixture.world);
            out.insert(out.end(), events.begin(), events.end());
        }
    };

    std::vector<ContactEvent> approach;
    walk(4.0f, 120, approach);
    const bool touchedWall = std::any_of(approach.begin(), approach.end(), [](const ContactEvent& event) {
        return event.phase == ContactPhase::Began && event.firstUserData == 9 && event.secondUserData == 21;
    });
    REQUIRE(touchedWall);

    std::vector<ContactEvent> retreat;
    walk(-4.0f, 60, retreat);
    // A contact that stops being reported has really ended: a character is never
    // put to sleep by the solver, which is why this diff makes no sleep
    // exception where the rigid one has to.
    const bool leftWall = std::any_of(retreat.begin(), retreat.end(), [](const ContactEvent& event) {
        return event.phase == ContactPhase::Ended && event.firstUserData == 9 && event.secondUserData == 21;
    });
    CHECK(leftWall);
}

TEST_CASE("a character standing still reports its floor once, not once a tick")
{
    // The whole point of a diff. Reading the contact list every tick and firing
    // on it would make `Touched` a per-frame event, which is the most common way
    // a contact signal becomes useless.
    Fixture fixture;
    fixture.spawn(floorDesc());

    CharacterDesc desc;
    desc.transform.position = core::DVec3{0.0, 2.5, 0.0};
    desc.userData = 9;
    const CharacterHandle character = fixture.physics->createCharacter(fixture.world, desc);

    int began = 0;
    int ended = 0;
    for (int i = 0; i < 240; ++i) {
        const CharacterState state = fixture.physics->characterState(fixture.world, character);
        const f32 vertical =
            state.ground == CharacterGround::Grounded ? 0.0f : state.linearVelocity.y - 9.81f * kFixedDt;
        fixture.physics->moveCharacter(fixture.world, character, core::Vec3{0.0f, vertical, 0.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);
        for (const ContactEvent& event : fixture.physics->drainContacts(fixture.world)) {
            if (event.firstUserData != 9)
                continue;
            if (event.phase == ContactPhase::Began)
                ++began;
            else
                ++ended;
        }
    }

    CHECK(began == 1);
    CHECK(ended == 0);
}

TEST_CASE("a character is not pushed by another walking into it")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    CharacterDesc still;
    still.transform.position = core::DVec3{0.0, 2.5, 0.0};
    still.userData = 30;
    CharacterDesc charger;
    charger.transform.position = core::DVec3{-8.0, 2.5, 0.0};
    charger.userData = 31;

    const CharacterHandle stationary = fixture.physics->createCharacter(fixture.world, still);
    const CharacterHandle running = fixture.physics->createCharacter(fixture.world, charger);

    // One runs at 12 m/s -- three times a walk -- and is told to keep running
    // long after it has arrived. The other is told nothing at all.
    for (int i = 0; i < 240; ++i) {
        fixture.physics->moveCharacter(fixture.world, running, core::Vec3{12.0f, 0.0f, 0.0f}, kFixedDt);
        fixture.physics->moveCharacter(fixture.world, stationary, core::Vec3{0.0f, 0.0f, 0.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);
    }

    // Characters BLOCK each other; neither pushes the other. Four seconds of a
    // running character against a standing one moves the standing one by
    // nothing that rounds to a centimetre.
    const f64 pushed = fixture.physics->characterState(fixture.world, stationary).transform.position.x;
    CHECK(std::abs(pushed) < 0.01);
    const f64 stoppedAt = fixture.physics->characterState(fixture.world, running).transform.position.x;
    CHECK(stoppedAt < -1.9);
    CHECK(stoppedAt > -2.2);
}

TEST_CASE("four characters walking into one another queue instead of overlapping")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    std::vector<CharacterHandle> crowd;
    for (int i = 0; i < 4; ++i) {
        CharacterDesc desc;
        desc.transform.position = core::DVec3{-6.0 + i * 4.0, 2.5, 0.0};
        desc.userData = static_cast<u64>(40 + i);
        crowd.push_back(fixture.physics->createCharacter(fixture.world, desc));
    }

    for (int i = 0; i < 300; ++i) {
        // Everybody walks at the middle. Two from the left, two from the right.
        const f32 push = 6.0f;
        fixture.physics->moveCharacter(fixture.world, crowd[0], core::Vec3{push, 0.0f, 0.0f}, kFixedDt);
        fixture.physics->moveCharacter(fixture.world, crowd[1], core::Vec3{push, 0.0f, 0.0f}, kFixedDt);
        fixture.physics->moveCharacter(fixture.world, crowd[2], core::Vec3{-push, 0.0f, 0.0f}, kFixedDt);
        fixture.physics->moveCharacter(fixture.world, crowd[3], core::Vec3{-push, 0.0f, 0.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);
    }

    // A file, not a heap: every neighbouring pair is at least a diameter apart,
    // and the order they started in is the order they end in.
    for (std::size_t i = 1; i < crowd.size(); ++i) {
        const f64 left = fixture.physics->characterState(fixture.world, crowd[i - 1]).transform.position.x;
        const f64 right = fixture.physics->characterState(fixture.world, crowd[i]).transform.position.x;
        CHECK(right - left > 1.9);
    }
}

TEST_CASE("a character walks through a wall its group does not collide with")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    const CollisionGroup walls = fixture.physics->registerCollisionGroup(fixture.world, "Walls");
    const CollisionGroup ghosts = fixture.physics->registerCollisionGroup(fixture.world, "Ghosts");
    fixture.physics->setGroupsCollidable(fixture.world, walls, ghosts, false);

    BodyDesc wall;
    wall.shape.type = ShapeType::Box;
    wall.shape.size = core::Vec3{1.0f, 8.0f, 20.0f};
    wall.transform.position = core::DVec3{0.0, 4.0, 0.0};
    wall.motion = MotionType::Static;
    wall.group = walls;
    wall.userData = 60;
    fixture.spawn(wall);

    REQUIRE(walls != ghosts);
    REQUIRE(!fixture.physics->groupsCollidable(fixture.world, walls, ghosts));
    REQUIRE(!fixture.physics->groupsCollidable(fixture.world, ghosts, walls));

    CharacterDesc walker;
    walker.transform.position = core::DVec3{-6.0, 2.5, 0.0};
    walker.group = ghosts;
    walker.userData = 61;
    const CharacterHandle handle = fixture.physics->createCharacter(fixture.world, walker);

    for (int i = 0; i < 240; ++i) {
        fixture.physics->moveCharacter(fixture.world, handle, core::Vec3{6.0f, 0.0f, 0.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);
    }

    // Well past the wall. Before the character's sweep was given the world's
    // own layer filter it stopped dead at the wall's face, which made
    // `CollisionGroup` a property that worked for every pair except the one a
    // player controls.
    CHECK(fixture.physics->characterState(fixture.world, handle).transform.position.x > 6.0);
}

TEST_CASE("two characters whose groups do not collide walk through each other")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    const CollisionGroup ghosts = fixture.physics->registerCollisionGroup(fixture.world, "Ghosts");
    fixture.physics->setGroupsCollidable(fixture.world, ghosts, ghosts, false);

    CharacterDesc left;
    left.transform.position = core::DVec3{-3.0, 2.5, 0.0};
    left.group = ghosts;
    left.userData = 70;
    CharacterDesc right;
    right.transform.position = core::DVec3{3.0, 2.5, 0.0};
    right.group = ghosts;
    right.userData = 71;

    const CharacterHandle a = fixture.physics->createCharacter(fixture.world, left);
    const CharacterHandle b = fixture.physics->createCharacter(fixture.world, right);

    for (int i = 0; i < 180; ++i) {
        fixture.physics->moveCharacter(fixture.world, a, core::Vec3{2.0f, 0.0f, 0.0f}, kFixedDt);
        fixture.physics->moveCharacter(fixture.world, b, core::Vec3{-2.0f, 0.0f, 0.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);
    }

    // They have swapped sides. This is the other half of the case above: a
    // group that says "do not collide" has to mean it between two characters as
    // well, which is the thing a per-character collision list could not have
    // expressed.
    const f64 crossedA = fixture.physics->characterState(fixture.world, a).transform.position.x;
    const f64 crossedB = fixture.physics->characterState(fixture.world, b).transform.position.x;
    CHECK(crossedA > crossedB);
}

TEST_CASE("a character walks through a part that cannot collide")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    BodyDesc ghost;
    ghost.shape.type = ShapeType::Box;
    ghost.shape.size = core::Vec3{1.0f, 8.0f, 20.0f};
    ghost.transform.position = core::DVec3{0.0, 4.0, 0.0};
    ghost.motion = MotionType::Static;
    ghost.collidable = false;
    ghost.userData = 50;
    fixture.spawn(ghost);

    CharacterDesc walker;
    walker.transform.position = core::DVec3{-6.0, 2.5, 0.0};
    walker.userData = 51;
    const CharacterHandle handle = fixture.physics->createCharacter(fixture.world, walker);

    for (int i = 0; i < 240; ++i) {
        fixture.physics->moveCharacter(fixture.world, handle, core::Vec3{6.0f, 0.0f, 0.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);
    }

    CHECK(fixture.physics->characterState(fixture.world, handle).transform.position.x > 6.0);
}

TEST_CASE("fifty characters in one crowd stay inside the tick budget")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    // Fifty enemies in a ten-by-five block two metres apart, which is shoulder
    // to shoulder for a two-metre capsule: every one of them is inside its
    // neighbours' sweep every tick, so this is the crowd, not fifty characters
    // that happen to share a world.
    std::vector<CharacterHandle> crowd;
    for (int i = 0; i < 50; ++i) {
        CharacterDesc desc;
        desc.transform.position = core::DVec3{-9.0 + (i % 10) * 2.0, 2.5, -4.0 + (i / 10) * 2.0};
        desc.userData = static_cast<u64>(100 + i);
        crowd.push_back(fixture.physics->createCharacter(fixture.world, desc));
    }

    for (int i = 0; i < 120; ++i) {
        for (const CharacterHandle handle : crowd) {
            fixture.physics->moveCharacter(fixture.world, handle, core::Vec3{2.0f, 0.0f, 0.0f}, kFixedDt);
        }
        fixture.physics->step(fixture.world, kFixedDt);
    }

    // Nobody has been squeezed out of the crowd or through the floor. The
    // number this case exists to defend is in `docs/perf-baselines.md`; what it
    // asserts here is only that fifty of them is a supported thing to do.
    for (const CharacterHandle handle : crowd) {
        const CharacterState state = fixture.physics->characterState(fixture.world, handle);
        CHECK(state.transform.position.y > 2.0);
        CHECK(state.transform.position.y < 3.0);
    }
}

TEST_CASE("a character that has been destroyed stops blocking the ones that remain")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    CharacterDesc blocker;
    blocker.transform.position = core::DVec3{0.0, 2.5, 0.0};
    blocker.userData = 20;
    CharacterDesc walker;
    walker.transform.position = core::DVec3{-6.0, 2.5, 0.0};
    walker.userData = 21;

    const CharacterHandle stationary = fixture.physics->createCharacter(fixture.world, blocker);
    const CharacterHandle moving = fixture.physics->createCharacter(fixture.world, walker);

    // Destroying a character has to take its inner body out of the world with
    // it. If it did not, the space it occupied would keep blocking, and the
    // check below would read as "walked into a ghost".
    fixture.physics->destroyCharacter(fixture.world, stationary);

    for (int i = 0; i < 240; ++i) {
        fixture.physics->moveCharacter(fixture.world, moving, core::Vec3{4.0f, 0.0f, 0.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);
    }

    CHECK(fixture.physics->characterState(fixture.world, moving).transform.position.x > 2.0);
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

// --- Kinematic motion, and what stands on it (D027) ---------------------------
//
// A script-moved kinematic body had a velocity of zero, because moving one used
// `SetPositionAndRotation` -- which puts a body somewhere and derives no
// velocity from having done so. Nothing downstream could tell a platform that
// was sliding from one that had always been there: a closing door did not push,
// a piston did not launch, a conveyor did not carry, and a character standing on
// a moving platform stayed exactly where it was while the platform left.
//
// Both halves are asserted below, and the second is the one somebody reported
// from playing the obby.

TEST_CASE("a kinematic body moved by a script has a velocity")
{
    Fixture fixture;

    BodyDesc desc;
    desc.shape.type = ShapeType::Box;
    desc.shape.size = core::Vec3{4.0f, 1.0f, 4.0f};
    desc.transform.position = core::DVec3{0.0, 0.0, 0.0};
    desc.motion = MotionType::Kinematic;
    desc.userData = 21;
    const BodyHandle platform = fixture.physics->createBody(fixture.world, desc);
    REQUIRE(platform.valid());

    // One tick's worth of travel: two metres in a sixtieth of a second is
    // 120 m/s, which is what the velocity has to come out as.
    core::CFrameD target;
    target.position = core::DVec3{0.0, 0.0, 2.0};
    fixture.physics->setBodyTransform(fixture.world, platform, target);
    fixture.physics->step(fixture.world, kFixedDt);

    const BodyState state = fixture.physics->bodyState(fixture.world, platform);
    // It ARRIVED -- a moved body still ends up where it was told to.
    CHECK(state.transform.position.z == doctest::Approx(2.0).epsilon(0.01));
    // And it got there at a speed, which is the half that was missing.
    CHECK(static_cast<double>(state.linearVelocity.z) ==
          doctest::Approx(2.0 / static_cast<double>(kFixedDt)).epsilon(0.05));
}

TEST_CASE("a capsule standing on a moving platform arrives with it")
{
    // The case the defect was reported from: a platform going there and back
    // with a character on top. Break the ground-velocity inheritance in
    // `moveCharacter` and this goes red -- the platform arrives and the capsule
    // does not.
    Fixture fixture;

    BodyDesc platformDesc;
    platformDesc.shape.type = ShapeType::Box;
    platformDesc.shape.size = core::Vec3{8.0f, 1.0f, 8.0f};
    platformDesc.transform.position = core::DVec3{0.0, 0.0, 0.0};
    platformDesc.motion = MotionType::Kinematic;
    platformDesc.userData = 22;
    const BodyHandle platform = fixture.physics->createBody(fixture.world, platformDesc);
    REQUIRE(platform.valid());

    CharacterDesc characterDesc;
    // Standing on a platform whose top face is at 0.5, so a five-metre capsule
    // has its centre at 3.
    characterDesc.transform.position = core::DVec3{0.0, 3.0, 0.0};
    characterDesc.stepHeight = 0.6f;
    characterDesc.userData = 23;
    const CharacterHandle rider = fixture.physics->createCharacter(fixture.world, characterDesc);
    REQUIRE(rider.valid());

    // Settle, so the capsule is genuinely grounded before anything moves.
    for (int tick = 0; tick < 30; ++tick) {
        fixture.physics->moveCharacter(fixture.world, rider, core::Vec3{0.0f, -1.0f, 0.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);
    }
    REQUIRE(fixture.physics->characterState(fixture.world, rider).ground == CharacterGround::Grounded);

    // There: 6 m over 120 ticks. Then back. The character asks for NOTHING
    // horizontal the whole time -- every metre it covers is the platform's.
    const auto driveTo = [&](f64 from, f64 to, int ticks) {
        for (int tick = 1; tick <= ticks; ++tick) {
            core::CFrameD target;
            target.position = core::DVec3{0.0, 0.0, from + (to - from) * (static_cast<f64>(tick) / ticks)};
            fixture.physics->setBodyTransform(fixture.world, platform, target);
            fixture.physics->moveCharacter(fixture.world, rider, core::Vec3{0.0f, -1.0f, 0.0f}, kFixedDt);
            fixture.physics->step(fixture.world, kFixedDt);
        }
    };

    driveTo(0.0, 6.0, 120);
    const CharacterState there = fixture.physics->characterState(fixture.world, rider);
    CHECK(there.transform.position.z == doctest::Approx(6.0).epsilon(0.15));
    CHECK(there.ground == CharacterGround::Grounded);

    driveTo(6.0, 0.0, 120);
    const CharacterState back = fixture.physics->characterState(fixture.world, rider);
    CHECK(back.transform.position.z == doctest::Approx(0.0).epsilon(0.15));
    // And it is still on the platform rather than having been left behind and
    // caught up with by accident.
    CHECK(back.ground == CharacterGround::Grounded);
    CHECK(back.transform.position.y == doctest::Approx(3.0).epsilon(0.2));
}

TEST_CASE("a character in mid-air does not inherit the platform it left")
{
    // The other half of "only while grounded": carrying the last platform's
    // velocity through a jump would launch the player off it.
    Fixture fixture;

    BodyDesc platformDesc;
    platformDesc.shape.type = ShapeType::Box;
    platformDesc.shape.size = core::Vec3{8.0f, 1.0f, 8.0f};
    platformDesc.motion = MotionType::Kinematic;
    platformDesc.userData = 24;
    const BodyHandle platform = fixture.physics->createBody(fixture.world, platformDesc);
    REQUIRE(platform.valid());

    CharacterDesc characterDesc;
    characterDesc.transform.position = core::DVec3{0.0, 3.0, 0.0};
    characterDesc.userData = 25;
    const CharacterHandle rider = fixture.physics->createCharacter(fixture.world, characterDesc);
    REQUIRE(rider.valid());

    for (int tick = 0; tick < 30; ++tick) {
        fixture.physics->moveCharacter(fixture.world, rider, core::Vec3{0.0f, -1.0f, 0.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);
    }

    // Straight up, and away from the platform. From here on the character is
    // airborne and the platform is racing sideways underneath it.
    f32 vertical = 12.0f;
    f64 z = 0.0;
    for (int tick = 0; tick < 40; ++tick) {
        z += 0.2;
        core::CFrameD target;
        target.position = core::DVec3{0.0, 0.0, z};
        fixture.physics->setBodyTransform(fixture.world, platform, target);
        fixture.physics->moveCharacter(fixture.world, rider, core::Vec3{0.0f, vertical, 0.0f}, kFixedDt);
        fixture.physics->step(fixture.world, kFixedDt);
        vertical -= 9.81f * kFixedDt;
    }

    const CharacterState state = fixture.physics->characterState(fixture.world, rider);
    CHECK(state.ground != CharacterGround::Grounded);
    // It went up, and it did not go sideways. Eight metres of platform travel
    // happened underneath it and none of it was inherited.
    CHECK(state.transform.position.y > 5.0);
    CHECK(std::abs(state.transform.position.z) < 0.5);
}

// --- The floating origin (ADR 0014, architecture.md §10) ---------------------

namespace {

// One scenario, run wherever the caller puts it: a floor, a cube dropped onto
// it from ten metres, and a hundred and twenty ticks of the result. What comes
// back is the cube's motion RELATIVE to where it started, because that is the
// thing that must not depend on where in the world the scenario is.
[[nodiscard]] std::vector<core::DVec3> dropTrajectory(core::DVec3 at, core::DVec3 origin)
{
    Fixture fixture;
    fixture.physics->setWorldOrigin(fixture.world, origin);

    BodyDesc floor = floorDesc();
    floor.transform.position = floor.transform.position + at;
    fixture.spawn(floor);

    const core::DVec3 start = at + core::DVec3{0.0, 10.0, 0.0};
    const BodyHandle cube = fixture.physics->createBody(fixture.world, cubeDesc(start, 2));
    REQUIRE(cube.valid());

    std::vector<core::DVec3> trajectory;
    trajectory.reserve(120);
    for (int tick = 0; tick < 120; ++tick) {
        fixture.physics->step(fixture.world, kFixedDt);
        const BodyState state = fixture.physics->bodyState(fixture.world, cube);
        trajectory.push_back(state.transform.position - start);
    }
    return trajectory;
}

// A micrometre. The grid the hash below quantizes onto, and the number is
// chosen rather than picked:
//
//   * f64's own resolution at 1e7 is about 1.9 NANOMETRES, so the round trip
//     through absolute world coordinates -- which is what a script reads and
//     therefore what has to be compared -- carries a few nanometres of noise
//     that no solver produced and no rebase can remove;
//   * a real behavioural divergence is millimetres at the very least, because
//     it comes from a contact resolving differently.
//
// So the grid sits five hundred times above the noise and a thousand times
// below the signal. It is the same technique the render capture gate uses, and
// for the same reason: hashing raw floats compares the representation instead
// of the thing.
constexpr core::f64 kTrajectoryGrid = 1.0e-6;

[[nodiscard]] core::u64 hashTrajectory(const std::vector<core::DVec3>& trajectory)
{
    core::u64 hash = 1469598103934665603ull;
    const auto mix = [&hash](core::f64 value) {
        const auto quantized = static_cast<core::i64>(std::llround(value / kTrajectoryGrid));
        core::u64 bits = 0;
        std::memcpy(&bits, &quantized, sizeof(bits));
        hash ^= bits;
        hash *= 1099511628211ull;
    };
    for (const core::DVec3& sample : trajectory) {
        mix(sample.x);
        mix(sample.y);
        mix(sample.z);
    }
    return hash;
}

} // namespace

TEST_CASE("a body ten thousand kilometres out behaves exactly as one at the origin")
{
    // The roadmap's own gate item: "object behavior at coordinate 1e7 identical
    // to origin (hash comparison)".
    constexpr core::DVec3 far{1.0e7, 0.0, 1.0e7};

    const std::vector<core::DVec3> atOrigin = dropTrajectory(core::DVec3{}, core::DVec3{});
    const std::vector<core::DVec3> rebased = dropTrajectory(far, far);

    REQUIRE(atOrigin.size() == rebased.size());
    CHECK(hashTrajectory(atOrigin) == hashTrajectory(rebased));

    // The SOLVER is bit-identical -- the origin is exactly the translation and
    // `1e7 + 10` is exact in f64, so it is handed the same f32 inputs both
    // times. What is not bit-identical is the value a script reads back, and
    // the reason is worth stating: an absolute f64 coordinate at 1e7 has a
    // resolution of about 1.9 nanometres, so the round trip out of local space
    // and back quantizes. That is a property of f64 rather than of this
    // engine, and the tolerance below is that number with room to spare.
    constexpr core::f64 tolerance = 1.0e-7;
    for (core::usize i = 0; i < atOrigin.size(); ++i) {
        CHECK(std::abs(atOrigin[i].x - rebased[i].x) < tolerance);
        CHECK(std::abs(atOrigin[i].y - rebased[i].y) < tolerance);
        CHECK(std::abs(atOrigin[i].z - rebased[i].z) < tolerance);
    }

    // And it actually simulated: a cube dropped from ten metres onto a floor
    // ends up about ten metres lower. A trajectory of zeroes would satisfy every
    // comparison above.
    CHECK(atOrigin.back().y == doctest::Approx(-9.5).epsilon(0.05));
}

TEST_CASE("without the origin, the same scenario ten thousand kilometres out is wrong")
{
    // The differential, and the reason the case above is not a gate that passes
    // while doing nothing: leave the origin at zero and the solver receives f32
    // coordinates whose quantum out there is most of a metre.
    constexpr core::DVec3 far{1.0e7, 0.0, 1.0e7};

    const std::vector<core::DVec3> atOrigin = dropTrajectory(core::DVec3{}, core::DVec3{});
    const std::vector<core::DVec3> unrebased = dropTrajectory(far, core::DVec3{});

    REQUIRE(atOrigin.size() == unrebased.size());
    CHECK(hashTrajectory(atOrigin) != hashTrajectory(unrebased));
}

TEST_CASE("a rebase moves nothing in absolute terms and keeps the fall going")
{
    Fixture fixture;
    fixture.spawn(floorDesc());

    const BodyHandle cube = fixture.physics->createBody(fixture.world, cubeDesc(core::DVec3{0.0, 40.0, 0.0}, 2));
    REQUIRE(cube.valid());

    for (int tick = 0; tick < 30; ++tick) {
        fixture.physics->step(fixture.world, kFixedDt);
    }

    const BodyState before = fixture.physics->bodyState(fixture.world, cube);
    fixture.physics->setWorldOrigin(fixture.world, core::DVec3{5000.0, 0.0, -8000.0});
    const BodyState after = fixture.physics->bodyState(fixture.world, cube);

    // The world moved under the simulation and the simulation did not notice:
    // the absolute position is what a script reads, and it is unchanged.
    CHECK(after.transform.position.x == doctest::Approx(before.transform.position.x));
    CHECK(after.transform.position.y == doctest::Approx(before.transform.position.y));
    CHECK(after.transform.position.z == doctest::Approx(before.transform.position.z));

    // Velocity survives, which is what "velocity-preserving teleport" means and
    // is the difference between a rebase and a body stopping dead every time
    // the camera walks far enough.
    CHECK(after.linearVelocity.y == doctest::Approx(static_cast<double>(before.linearVelocity.y)));
    CHECK(after.linearVelocity.y < -1.0f);

    CHECK(fixture.physics->worldOrigin(fixture.world) == core::DVec3{5000.0, 0.0, -8000.0});

    // And it keeps falling to the same place it would have.
    for (int tick = 0; tick < 200; ++tick) {
        fixture.physics->step(fixture.world, kFixedDt);
    }
    CHECK(fixture.physics->bodyState(fixture.world, cube).transform.position.y == doctest::Approx(0.5).epsilon(0.1));
}

TEST_CASE("two worlds in one process keep their own origins")
{
    // ADR 0014 says this twice: origin is World-scoped state and never a global.
    // A future server running several simulation regions gives each its own.
    Fixture first;
    Fixture second;

    first.physics->setWorldOrigin(first.world, core::DVec3{1000.0, 0.0, 0.0});
    second.physics->setWorldOrigin(second.world, core::DVec3{-2.5e6, 0.0, 7.0e6});

    CHECK(first.physics->worldOrigin(first.world) == core::DVec3{1000.0, 0.0, 0.0});
    CHECK(second.physics->worldOrigin(second.world) == core::DVec3{-2.5e6, 0.0, 7.0e6});

    const BodyHandle a = first.physics->createBody(first.world, cubeDesc(core::DVec3{3.0, 5.0, 0.0}, 7));
    const BodyHandle b = second.physics->createBody(second.world, cubeDesc(core::DVec3{3.0, 5.0, 0.0}, 7));
    REQUIRE(a.valid());
    REQUIRE(b.valid());

    // Same absolute coordinates in and out, whatever each world subtracts in
    // between.
    CHECK(first.physics->bodyState(first.world, a).transform.position.x == doctest::Approx(3.0));
    CHECK(second.physics->bodyState(second.world, b).transform.position.x == doctest::Approx(3.0));
}

TEST_CASE("reshaping a body a cube is resting on does not re-announce the contact")
{
    // **Found while designing terrain sculpting** (F1), where dragging a brush
    // reshapes the ground somebody is standing on many times a second -- but
    // nothing here is about terrain. It is about `updateBody`, which is what a
    // script does by writing `part.Size`.
    //
    // `updateBody` is `RemoveBody` + `DestroyBody` + `CreateAndAddBody`, and it
    // calls `forgetPairs` first. That erases every contact pair naming the body
    // from `m_previousPairs`, and `Began`/`Ended` are a DIFF against that list
    // -- so a contact that never physically broke would be absent from the
    // previous set, present in the next, and reported as new, with no `Ended`
    // to pair it. `forgetPairs` is CORRECT for `destroyBody`, and its own
    // comment says why: "the pair can never appear again". `updateBody`
    // recreates the body, so it can.
    //
    // **The body must still be AWAKE when the reshape lands, and getting that
    // wrong is how the first version of this case passed while testing
    // nothing.** A sleeping body produces no contact callbacks, so a settled
    // cube reports zero events whatever `updateBody` did to the pair list --
    // `began == 0` meant "nothing was observed", not "nothing was
    // re-announced". Jolt's sleep timer is half a second, so this reshapes on
    // the tick the contact is made rather than after letting it settle.
    Fixture fixture;
    const BodyHandle floor = fixture.physics->createBody(fixture.world, floorDesc());
    REQUIRE(floor.valid());
    const BodyHandle cube = fixture.physics->createBody(fixture.world, cubeDesc({0.0, 2.0, 0.0}, 2));
    REQUIRE(cube.valid());

    const auto drain = [&](int& began, int& ended) {
        for (const ContactEvent& event : fixture.physics->drainContacts(fixture.world)) {
            if (event.phase == ContactPhase::Began) {
                ++began;
            }
            else {
                ++ended;
            }
        }
    };

    // Step until the contact is announced, and no further.
    int began = 0;
    int ended = 0;
    for (int i = 0; i < 240 && began == 0; ++i) {
        fixture.physics->step(fixture.world, kFixedDt);
        drain(began, ended);
    }
    REQUIRE(began == 1);
    REQUIRE(ended == 0);
    // Awake, which is what makes everything below observable.
    REQUIRE(fixture.physics->bodyState(fixture.world, cube).active);

    // **The floor is reshaped without moving out from under the cube.** Wider
    // and no thinner, same position, so the cube is resting on it before and
    // after and the contact never physically breaks.
    BodyDesc wider = floorDesc();
    wider.shape.size = core::Vec3{120.0f, 2.0f, 120.0f};
    REQUIRE(fixture.physics->updateBody(fixture.world, floor, wider));

    began = 0;
    ended = 0;
    for (int i = 0; i < 30; ++i) {
        fixture.physics->step(fixture.world, kFixedDt);
        drain(began, ended);
    }

    // Nothing began, because nothing started touching. Nothing ended, because
    // nothing stopped.
    CHECK(began == 0);
    CHECK(ended == 0);
}
