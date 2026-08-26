// Constraints, through the interface and never through Jolt.
//
// The same rule the rest of this module's tests are written under: everything
// here is `IPhysics3D` vocabulary, which is what a second backend would have to
// satisfy and the only vocabulary anything above L2 can see.
//
// **Four of these cover traps rather than features**, and each is silent when
// missed: a constraint surviving `updateBody`, a constraint dropped by
// `destroyBody`, an exclusion that really excludes, and two identically-built
// worlds still agreeing after three hundred steps.
#include "luaug/physics/backends.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <doctest/doctest.h>
#include <memory>
#include <vector>

using namespace luaug;
using namespace luaug::physics;

namespace {

constexpr f32 kFixedDt = 1.0f / 60.0f;
constexpr f32 kPi = 3.14159265358979f;

struct Rig
{
    Rig()
    {
        physics = createJoltPhysics();
        REQUIRE(physics != nullptr);
        world = physics->createWorld(WorldDesc{});
        REQUIRE(world.valid());
    }

    ~Rig() { physics->destroyWorld(world); }

    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    [[nodiscard]] BodyHandle cube(core::DVec3 at, MotionType motion = MotionType::Dynamic, u64 userData = 0)
    {
        BodyDesc desc;
        desc.shape.type = ShapeType::Box;
        desc.shape.size = core::Vec3{1.0f, 1.0f, 1.0f};
        desc.transform.position = at;
        desc.motion = motion;
        desc.userData = userData;
        const BodyHandle handle = physics->createBody(world, desc);
        REQUIRE(handle.valid());
        return handle;
    }

    void step(int count = 1)
    {
        for (int i = 0; i < count; ++i) {
            physics->step(world, kFixedDt);
        }
    }

    [[nodiscard]] core::DVec3 positionOf(BodyHandle body) const
    {
        return physics->bodyState(world, body).transform.position;
    }

    std::unique_ptr<IPhysics3D> physics;
    WorldHandle world;
};

// The two frames meet halfway between the bodies, which is what "attached here"
// means for a pair separated along X.
//
// **The joint's own X axis is turned onto world Z**, and neither the axis nor
// the choice of Z is decoration.
//
// X is the axis a hinge turns about and a twist runs along, and the bodies here
// are offset along X -- so a joint whose X axis pointed at the other body would
// put the second body's centre ON the axis of rotation, where turning it moves
// nothing at all.
//
// Z rather than Y because gravity has to be able to turn it. `applyImpulse` at
// this seam applies a CENTRAL impulse -- at the centre of mass, producing no
// torque -- so nothing in this interface can push a joint round except gravity,
// and gravity turns a horizontal hinge and not a vertical one. Both mistakes
// were made here first, and each produced a test that passed against a limit
// nothing ever reached.
[[nodiscard]] ConstraintDesc jointBetween(BodyHandle first, BodyHandle second, ConstraintType type, f64 halfGap)
{
    // A quarter turn about Y the short way: X goes to +Z, Y stays.
    const core::Mat3 turned = core::rotationY(-kPi * 0.5f);

    ConstraintDesc desc;
    desc.type = type;
    desc.first = first;
    desc.second = second;
    desc.firstFrame.position = core::DVec3{halfGap, 0.0, 0.0};
    desc.firstFrame.rotation = turned;
    desc.secondFrame.position = core::DVec3{-halfGap, 0.0, 0.0};
    desc.secondFrame.rotation = turned;
    return desc;
}

[[nodiscard]] f64 distanceBetween(const Rig& rig, BodyHandle a, BodyHandle b)
{
    const core::DVec3 first = rig.positionOf(a);
    const core::DVec3 second = rig.positionOf(b);
    const core::DVec3 delta{second.x - first.x, second.y - first.y, second.z - first.z};
    return std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
}

} // namespace

TEST_CASE("a fixed constraint holds two bodies together under gravity")
{
    // Fixed rather than Point, because Fixed is the one that conserves a
    // relative POSE: a point joint leaves three rotational degrees, so the
    // hanging body turns about the joint and its centre swings inward -- the
    // joint is still holding and centre-to-centre distance has still changed.
    Rig rig;
    const BodyHandle anchor = rig.cube(core::DVec3{0.0, 10.0, 0.0}, MotionType::Static);
    const BodyHandle hanging = rig.cube(core::DVec3{2.0, 10.0, 0.0});

    const ConstraintHandle joint =
        rig.physics->createConstraint(rig.world, jointBetween(anchor, hanging, ConstraintType::Fixed, 1.0));
    REQUIRE(joint.valid());

    rig.step(120);

    CHECK(distanceBetween(rig, anchor, hanging) == doctest::Approx(2.0).epsilon(0.02));
    // And it did not fall: the anchor is static, so nothing under it moves.
    CHECK(rig.positionOf(hanging).y == doctest::Approx(10.0).epsilon(0.02));
}

TEST_CASE("a point constraint swings but does not let go")
{
    Rig rig;
    const BodyHandle anchor = rig.cube(core::DVec3{0.0, 10.0, 0.0}, MotionType::Static);
    const BodyHandle hanging = rig.cube(core::DVec3{2.0, 10.0, 0.0});

    REQUIRE(
        rig.physics->createConstraint(rig.world, jointBetween(anchor, hanging, ConstraintType::Point, 1.0)).valid());
    rig.step(120);

    // The two joint POINTS stay together, so the centres can never be further
    // apart than the two arms laid end to end -- and the body swings, which is
    // the freedom a point joint is for.
    CHECK(distanceBetween(rig, anchor, hanging) <= 2.05);
    CHECK(distanceBetween(rig, anchor, hanging) > 0.5);
    CHECK(rig.positionOf(hanging).y < 10.0);
}

TEST_CASE("a constraint is refused when it cannot be one")
{
    Rig rig;
    const BodyHandle a = rig.cube(core::DVec3{0.0, 0.0, 0.0});

    // A body joined to itself divides by an infinite mass in a release build and
    // asserts in a debug one, so it is refused here rather than discovered there.
    CHECK_FALSE(rig.physics->createConstraint(rig.world, jointBetween(a, a, ConstraintType::Point, 0.5)).valid());
    CHECK_FALSE(
        rig.physics->createConstraint(rig.world, jointBetween(a, BodyHandle{}, ConstraintType::Point, 0.5)).valid());

    const BodyHandle b = rig.cube(core::DVec3{1.0, 0.0, 0.0});
    const ConstraintHandle joint =
        rig.physics->createConstraint(rig.world, jointBetween(a, b, ConstraintType::Point, 0.5));
    REQUIRE(joint.valid());
    rig.physics->destroyBody(rig.world, b);
    // The body is gone and so is the joint, so the handle no longer resolves --
    // the state a stale handle reports is the default, never a live joint's.
    CHECK_FALSE(rig.physics->constraintState(rig.world, joint).enabled);
}

TEST_CASE("a hinge never leaves its limits, however long gravity pulls")
{
    // The whole reason a limit is a solver constraint rather than a per-frame
    // correction: it is not exceeded and then pulled back, it is not exceeded.
    //
    // A door on a horizontal hinge, a metre and a half of arm, held out sideways
    // and let go. Gravity swings it down and the limit is what stops it -- there
    // is no impulse here because `applyImpulse` is CENTRAL and makes no torque,
    // so gravity is the only thing at this seam that can turn a joint.
    //
    // **`collideConnected` is off, and that is not a convenience.** Two boxes
    // joined at their touching faces are in CONTACT, and the contact solver
    // holds the door up rigidly -- the joint never gets to move, and the case
    // then passes against a limit nothing ever reaches. It cost an afternoon.
    Rig rig;
    const BodyHandle frame = rig.cube(core::DVec3{0.0, 5.0, 0.0}, MotionType::Static);
    const BodyHandle door = rig.cube(core::DVec3{3.0, 5.0, 0.0});

    ConstraintDesc desc = jointBetween(frame, door, ConstraintType::Hinge, 1.5);
    // A quarter turn either way about the joint's own X axis, which is world Z.
    desc.limitLow = -kPi * 0.25f;
    desc.limitHigh = kPi * 0.25f;
    desc.collideConnected = false;
    REQUIRE(rig.physics->createConstraint(rig.world, desc).valid());

    // An arm of 1.5 swung a quarter turn drops its end by sin(45 degrees) of
    // that, and no further.
    constexpr f64 kArm = 1.5;
    constexpr f64 kLowest = 5.0 - kArm * 0.7071;
    bool everSwung = false;

    for (int i = 0; i < 300; ++i) {
        rig.step();
        const core::DVec3 at = rig.positionOf(door);
        // Never past the stop, to within the solver's own softness at a hard
        // stop under momentum -- measured at about five centimetres on a metre
        // and a half of arm, and this allows twice that. It is not room for a
        // missing limit: an UNLIMITED hinge hangs at y = 3.5, four times
        // further past the stop than this tolerance reaches.
        CHECK(at.y > kLowest - 0.1);
        // Still on the hinge: the axis is Z, so z cannot move.
        CHECK(std::abs(at.z) < 0.05);
        if (at.y < 4.5) {
            everSwung = true;
        }
    }

    // It really swung, so the bound above is a limit rather than a description
    // of a door that never moved.
    CHECK(everSwung);
    CHECK(rig.positionOf(door).y == doctest::Approx(kLowest).epsilon(0.05));
}

TEST_CASE("a swing-twist cone holds for ten seconds of being shaken")
{
    // The ragdoll workhorse, and the one whose failure looks like a character
    // slowly coming apart rather than like an error.
    Rig rig;
    const BodyHandle torso = rig.cube(core::DVec3{0.0, 5.0, 0.0}, MotionType::Static);
    const BodyHandle limb = rig.cube(core::DVec3{1.0, 5.0, 0.0});

    ConstraintDesc desc = jointBetween(torso, limb, ConstraintType::SwingTwist, 0.5);
    desc.swingLimit = kPi * 0.25f;
    desc.twistLimit = kPi * 0.1f;
    desc.collideConnected = false;
    REQUIRE(rig.physics->createConstraint(rig.world, desc).valid());

    for (int i = 0; i < 600; ++i) {
        rig.physics->applyImpulse(rig.world, limb, core::Vec3{(i % 2) == 0 ? 20.0f : -20.0f, 15.0f, 12.0f});
        rig.step();
    }

    // Still attached, still the length it was built at. A cone that let go would
    // show up as a limb metres away rather than as a wrong angle.
    CHECK(distanceBetween(rig, torso, limb) == doctest::Approx(1.0).epsilon(0.15));
}

TEST_CASE("collideConnected false really stops the two from pushing each other")
{
    // Two overlapping cubes, which is what an elbow IS: joined, and occupying
    // the same metre of space. Left colliding they shove each other apart every
    // step, and a ragdoll vibrates instead of falling.
    const auto separationAfter = [](bool collide) {
        Rig rig;
        const BodyHandle upper = rig.cube(core::DVec3{0.0, 5.0, 0.0}, MotionType::Static);
        const BodyHandle lower = rig.cube(core::DVec3{0.3, 5.0, 0.0});

        ConstraintDesc desc = jointBetween(upper, lower, ConstraintType::Point, 0.15);
        desc.collideConnected = collide;
        REQUIRE(rig.physics->createConstraint(rig.world, desc).valid());
        rig.step(120);
        return distanceBetween(rig, upper, lower);
    };

    const f64 excluded = separationAfter(false);
    const f64 colliding = separationAfter(true);

    // Excluded, the pair stays at the 0.3 they were built at. Colliding, the
    // contact solver fights the joint and pushes them apart -- the joint wins in
    // the end, but the two answers are not the same and that difference is the
    // whole feature.
    CHECK(excluded == doctest::Approx(0.3).epsilon(0.1));
    CHECK(colliding > excluded + 0.05);
}

TEST_CASE("updateBody re-anchors the constraints on the body it replaced")
{
    // **Trap: `updateBody` destroys and recreates the body underneath**, and a
    // constraint bakes its anchors relative to that body's CENTRE OF MASS. A
    // reshaped body has a different centre of mass, so every joint on it is
    // then anchored at a point that has moved -- silently, by exactly the
    // distance the centre travelled.
    //
    // The shape here is a hull sitting a metre off its own origin, which is what
    // a `MeshPart` is the moment its geometry arrives: the box it collided as
    // was centred and the hull is not. Rebuilding re-derives the anchor from the
    // body's transform and the joint's own local frame, which is where the
    // caller put it and where it stays.
    //
    // (Two weaker versions of this case passed against the defect. A plain
    // box-to-bigger-box resize does not move a centre of mass, and neither
    // does churning the allocator afterwards -- Jolt hands the same `Body`
    // object back off its freelist, so even the dangling pointer resolves.)
    const auto separationAfterReshape = [](bool reshape) {
        Rig rig;
        const BodyHandle anchor = rig.cube(core::DVec3{0.0, 10.0, 0.0}, MotionType::Static);
        const BodyHandle hanging = rig.cube(core::DVec3{3.0, 10.0, 0.0});

        ConstraintDesc desc = jointBetween(anchor, hanging, ConstraintType::Fixed, 1.5);
        desc.collideConnected = false;
        const ConstraintHandle joint = rig.physics->createConstraint(rig.world, desc);
        REQUIRE(joint.valid());
        rig.step(10);

        if (reshape) {
            // A tetrahedron whose points all sit around x = +1, so the hull's
            // centre of mass is a metre from the body origin the joint was
            // authored against.
            static const core::Vec3 kOffsetHull[] = {
                core::Vec3{0.5f, 0.0f, 0.0f},
                core::Vec3{1.5f, 0.0f, 0.0f},
                core::Vec3{1.0f, 1.0f, 0.0f},
                core::Vec3{1.0f, 0.0f, 1.0f},
            };
            BodyDesc reshaped;
            reshaped.shape.type = ShapeType::ConvexHull;
            reshaped.shape.points = kOffsetHull;
            reshaped.transform = rig.physics->bodyState(rig.world, hanging).transform;
            reshaped.motion = MotionType::Dynamic;
            REQUIRE(rig.physics->updateBody(rig.world, hanging, reshaped));
        }

        rig.step(120);
        CHECK(rig.physics->constraintState(rig.world, joint).enabled);
        return distanceBetween(rig, anchor, hanging);
    };

    // Untouched, the fixed joint holds the pair exactly where it was built.
    CHECK(separationAfterReshape(false) == doctest::Approx(3.0).epsilon(0.02));
    // Reshaped, it still does -- the anchor followed the body rather than the
    // body's old centre of mass.
    CHECK(separationAfterReshape(true) == doctest::Approx(3.0).epsilon(0.02));
}

TEST_CASE("destroyBody drops the constraints that named it")
{
    // **Trap: the mirror sweeps bodies and constraints separately.** A joint
    // outliving its body is a dangling pointer inside the solver, so the body
    // lifecycle takes its joints with it and the interface says so.
    Rig rig;
    const BodyHandle anchor = rig.cube(core::DVec3{0.0, 10.0, 0.0}, MotionType::Static);
    const BodyHandle first = rig.cube(core::DVec3{2.0, 10.0, 0.0});
    const BodyHandle second = rig.cube(core::DVec3{4.0, 10.0, 0.0});

    const ConstraintHandle kept =
        rig.physics->createConstraint(rig.world, jointBetween(anchor, first, ConstraintType::Point, 1.0));
    const ConstraintHandle doomed =
        rig.physics->createConstraint(rig.world, jointBetween(first, second, ConstraintType::Point, 1.0));
    REQUIRE(kept.valid());
    REQUIRE(doomed.valid());

    rig.physics->destroyBody(rig.world, second);
    rig.step(60);

    CHECK_FALSE(rig.physics->constraintState(rig.world, doomed).enabled);
    // And the one that did NOT name the destroyed body is untouched, which is
    // what makes this a sweep rather than a reset.
    CHECK(rig.physics->constraintState(rig.world, kept).enabled);
}

TEST_CASE("a disabled constraint holds nothing and then holds again")
{
    Rig rig;
    const BodyHandle anchor = rig.cube(core::DVec3{0.0, 10.0, 0.0}, MotionType::Static);
    const BodyHandle hanging = rig.cube(core::DVec3{2.0, 10.0, 0.0});
    const ConstraintHandle joint =
        rig.physics->createConstraint(rig.world, jointBetween(anchor, hanging, ConstraintType::Point, 1.0));
    REQUIRE(joint.valid());

    rig.physics->setConstraintEnabled(rig.world, joint, false);
    CHECK_FALSE(rig.physics->constraintState(rig.world, joint).enabled);
    rig.step(60);
    // It fell: nothing was holding it.
    CHECK(rig.positionOf(hanging).y < 9.0);

    rig.physics->setConstraintEnabled(rig.world, joint, true);
    CHECK(rig.physics->constraintState(rig.world, joint).enabled);
    const f64 whenReenabled = rig.positionOf(hanging).y;
    rig.step(120);
    // And it stopped falling. The constraint never left the world, so its place
    // in the solve order is the one it was created with.
    CHECK(rig.positionOf(hanging).y > whenReenabled - 3.0);
}

TEST_CASE("a distance constraint keeps a rod a rod")
{
    Rig rig;
    const BodyHandle anchor = rig.cube(core::DVec3{0.0, 10.0, 0.0}, MotionType::Static);
    const BodyHandle bob = rig.cube(core::DVec3{0.0, 7.0, 0.0});

    ConstraintDesc desc;
    desc.type = ConstraintType::Distance;
    desc.first = anchor;
    desc.second = bob;
    // Equal bounds: a rigid rod rather than a rope.
    desc.minDistance = 3.0f;
    desc.maxDistance = 3.0f;
    REQUIRE(rig.physics->createConstraint(rig.world, desc).valid());

    rig.step(180);
    CHECK(distanceBetween(rig, anchor, bob) == doctest::Approx(3.0).epsilon(0.1));
}

TEST_CASE("two worlds built by the same calls agree after three hundred steps")
{
    // The strongest evidence constraints did not break R10. A joint is solved
    // in the order it was added, so anything that reordered creation -- a hash
    // map's iteration, a slot reused out of sequence -- diverges here and
    // nowhere else until a replay fails months later.
    const auto simulate = [] {
        Rig rig;
        std::vector<BodyHandle> chain;
        chain.push_back(rig.cube(core::DVec3{0.0, 12.0, 0.0}, MotionType::Static, 1));
        for (int i = 1; i < 6; ++i) {
            chain.push_back(rig.cube(core::DVec3{static_cast<f64>(i) * 1.5, 12.0, 0.0}, MotionType::Dynamic,
                                     static_cast<u64>(i) + 1));
        }
        for (std::size_t i = 1; i < chain.size(); ++i) {
            ConstraintDesc desc = jointBetween(chain[i - 1], chain[i], ConstraintType::SwingTwist, 0.75);
            desc.swingLimit = kPi * 0.2f;
            desc.twistLimit = kPi * 0.1f;
            desc.collideConnected = false;
            REQUIRE(rig.physics->createConstraint(rig.world, desc).valid());
        }
        rig.step(300);

        std::vector<core::DVec3> out;
        for (const BodyHandle body : chain) {
            out.push_back(rig.positionOf(body));
        }
        return out;
    };

    const std::vector<core::DVec3> first = simulate();
    const std::vector<core::DVec3> second = simulate();

    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        // Bit-identical, not close. A tolerance here would pass the exact drift
        // this case exists to catch.
        CHECK(first[i].x == second[i].x);
        CHECK(first[i].y == second[i].y);
        CHECK(first[i].z == second[i].z);
    }
}

TEST_CASE("a hinge motor drives towards its target")
{
    Rig rig;
    const BodyHandle frame = rig.cube(core::DVec3{0.0, 5.0, 0.0}, MotionType::Static);
    const BodyHandle door = rig.cube(core::DVec3{1.0, 5.0, 0.0});

    ConstraintDesc desc = jointBetween(frame, door, ConstraintType::Hinge, 0.5);
    desc.motor = MotorMode::Velocity;
    desc.motorTarget = 2.0f;
    desc.motorMaxForce = 500.0f;
    // As above: joined at their touching faces, the contact solver holds the
    // door rigid and the motor turns nothing.
    desc.collideConnected = false;
    REQUIRE(rig.physics->createConstraint(rig.world, desc).valid());

    const core::DVec3 before = rig.positionOf(door);
    rig.step(30);
    const core::DVec3 after = rig.positionOf(door);

    // Driven about world Z, so the centre travels in the XY plane. Half a
    // second at two radians per second is most of a radian, which on a
    // half-metre arm is a fifth of a metre and more -- and it is DRIVEN rather
    // than dropped, so it turns the way the motor says and not the way gravity
    // does.
    const f64 dx = after.x - before.x;
    const f64 dy = after.y - before.y;
    CHECK(std::sqrt(dx * dx + dy * dy) > 0.2);
    // Upward: a positive target about +Z lifts the arm, where gravity alone
    // would only ever drop it.
    CHECK(after.y > before.y);
}

TEST_CASE("updateConstraint changes the limits and keeps the joint")
{
    Rig rig;
    const BodyHandle anchor = rig.cube(core::DVec3{0.0, 10.0, 0.0}, MotionType::Static);
    const BodyHandle bob = rig.cube(core::DVec3{0.0, 7.0, 0.0});

    ConstraintDesc desc;
    desc.type = ConstraintType::Distance;
    desc.first = anchor;
    desc.second = bob;
    desc.minDistance = 3.0f;
    desc.maxDistance = 3.0f;
    const ConstraintHandle joint = rig.physics->createConstraint(rig.world, desc);
    REQUIRE(joint.valid());
    rig.step(60);
    CHECK(distanceBetween(rig, anchor, bob) == doctest::Approx(3.0).epsilon(0.1));

    desc.minDistance = 6.0f;
    desc.maxDistance = 6.0f;
    rig.physics->updateConstraint(rig.world, joint, desc);
    rig.step(240);

    CHECK(rig.physics->constraintState(rig.world, joint).enabled);
    CHECK(distanceBetween(rig, anchor, bob) == doctest::Approx(6.0).epsilon(0.15));
}

TEST_CASE("a joint under load reports the impulse holding it")
{
    // What a breakable joint is a threshold on. A joint holding nothing reports
    // nothing, and one holding a falling body reports more.
    Rig rig;
    const BodyHandle anchor = rig.cube(core::DVec3{0.0, 10.0, 0.0}, MotionType::Static);
    const BodyHandle hanging = rig.cube(core::DVec3{0.0, 8.0, 0.0});

    ConstraintDesc desc;
    desc.type = ConstraintType::Distance;
    desc.first = anchor;
    desc.second = hanging;
    desc.minDistance = 2.0f;
    desc.maxDistance = 2.0f;
    const ConstraintHandle joint = rig.physics->createConstraint(rig.world, desc);
    REQUIRE(joint.valid());

    rig.step(60);
    CHECK(rig.physics->constraintState(rig.world, joint).appliedImpulse > 0.0f);
}
