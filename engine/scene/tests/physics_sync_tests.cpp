// The mirror, tested against a fake simulation.
//
// The point of a seam is that the thing above it can be tested without the
// thing below, and this is that test: `FakePhysics` records what it was asked
// to do and answers with whatever the case wants it to. What is under test is
// the mirror's own logic -- when a body is created, when it is rebuilt rather
// than adjusted, which direction a write crossed in, and how a contact becomes
// a deferred fact -- none of which needs a solver, and all of which a solver
// would make slower and less exact to assert.
#include "luaug/scene/physics_sync.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <string>
#include <vector>

#include "scene_fixture.h"

using namespace luaug;
using namespace luaug::scene;

namespace {

// Records every call and answers from state the test sets. Deliberately not a
// mini-solver: a fake that simulated would be a second implementation to keep
// correct, and the case that wants a solver is `engine/physics`'s own suite.
class FakePhysics final : public physics::IPhysics3D
{
public:
    struct Created
    {
        physics::BodyHandle handle;
        physics::BodyDesc desc;
        // **A COPY of the hull, because the span is not ours to keep.**
        // `ShapeDesc::points` is documented as outliving the call and no
        // longer, and the vector behind it belongs to the mirror -- which
        // replaces it whenever a mesh loads. Two cases here used to read
        // `desc.shape.points` after the fact, which happened to work only
        // because nothing had replaced it yet.
        std::vector<core::Vec3> points;
    };

    [[nodiscard]] physics::WorldHandle createWorld(const physics::WorldDesc&) override
    {
        return physics::WorldHandle{0, 1};
    }
    void destroyWorld(physics::WorldHandle) override { worldDestroyed = true; }
    void setGravity(physics::WorldHandle, core::Vec3 value) override { gravity = value; }

    // Recorded rather than acted on: what these cases assert is that the mirror
    // pushes the origin down exactly once per change, which a real solver would
    // hide behind a thousand body moves.
    void setWorldOrigin(physics::WorldHandle, core::DVec3 value) override
    {
        origin = value;
        originPushes += 1;
    }

    [[nodiscard]] core::DVec3 worldOrigin(physics::WorldHandle) const override { return origin; }

    [[nodiscard]] physics::BodyHandle createBody(physics::WorldHandle, const physics::BodyDesc& desc) override
    {
        // **The span is not kept**, for the reason the update below gives: its
        // documented lifetime is this call, and the vector behind it belongs to
        // whoever is mirroring.
        physics::BodyDesc kept = desc;
        std::vector<core::Vec3> points(desc.shape.points.begin(), desc.shape.points.end());
        kept.shape.points = {};
        if (refuseCreates) {
            // The attempt is still recorded, because what a case wants to count
            // is how many times the mirror ASKED.
            created.push_back(Created{physics::BodyHandle{}, kept, std::move(points)});
            return {};
        }
        const physics::BodyHandle handle{nextBody++, 1};
        created.push_back(Created{handle, kept, std::move(points)});
        live.push_back(handle);
        calls.emplace_back("createBody", handle.index);
        return handle;
    }

    // Set by a case that wants to see what the mirror does when the backend
    // will not make a body -- a degenerate hull is the real one.
    bool refuseCreates = false;

    void destroyBody(physics::WorldHandle, physics::BodyHandle handle) override
    {
        destroyed.push_back(handle);
        calls.emplace_back("destroyBody", handle.index);
        live.erase(std::remove(live.begin(), live.end(), handle), live.end());
    }

    // --- Constraints ----------------------------------------------------------
    //
    // Recorded, and recorded into `calls` as well as into their own list: the
    // contract this mirror has to keep is an ORDER -- constraints retired before
    // the bodies they hold -- and a per-kind list cannot show an interleaving.

    struct MadeConstraint
    {
        physics::ConstraintHandle handle;
        physics::ConstraintDesc desc;
    };

    [[nodiscard]] physics::ConstraintHandle createConstraint(physics::WorldHandle,
                                                             const physics::ConstraintDesc& desc) override
    {
        const physics::ConstraintHandle handle{nextConstraint++, 1};
        constraints.push_back(MadeConstraint{handle, desc});
        liveConstraints.push_back(handle);
        calls.emplace_back("createConstraint", handle.index);
        return handle;
    }

    void destroyConstraint(physics::WorldHandle, physics::ConstraintHandle handle) override
    {
        constraintsDestroyed.push_back(handle);
        calls.emplace_back("destroyConstraint", handle.index);
        liveConstraints.erase(std::remove(liveConstraints.begin(), liveConstraints.end(), handle),
                              liveConstraints.end());
    }

    void setConstraintEnabled(physics::WorldHandle, physics::ConstraintHandle handle, bool enabled) override
    {
        constraintEnables.emplace_back(handle, enabled);
    }

    void updateConstraint(physics::WorldHandle, physics::ConstraintHandle handle,
                          const physics::ConstraintDesc& desc) override
    {
        constraintsUpdated.push_back(MadeConstraint{handle, desc});
    }

    [[nodiscard]] physics::ConstraintState constraintState(physics::WorldHandle,
                                                           physics::ConstraintHandle) const override
    {
        return {};
    }

    void setBodyTransform(physics::WorldHandle, physics::BodyHandle handle, const core::CFrameD& transform) override
    {
        transforms.emplace_back(handle, transform);
    }

    void setBodyVelocity(physics::WorldHandle, physics::BodyHandle, core::Vec3, core::Vec3) override {}

    void applyImpulse(physics::WorldHandle, physics::BodyHandle handle, core::Vec3 impulse) override
    {
        impulses.emplace_back(handle, impulse);
    }

    // **The span is not kept**, because the caller's contract says it may not
    // outlive the call and this fake used to store the whole desc. A test that
    // read `rebuilt.back().desc.shape.points` afterwards was reading a span into
    // whatever the mirror had at the time.
    bool updateBody(physics::WorldHandle, physics::BodyHandle handle, const physics::BodyDesc& desc) override
    {
        physics::BodyDesc kept = desc;
        std::vector<core::Vec3> points(desc.shape.points.begin(), desc.shape.points.end());
        kept.shape.points = {};
        rebuilt.push_back(Created{handle, kept, std::move(points)});
        return !refuseUpdates;
    }

    // Set by a case that wants to see what the mirror does when the backend
    // will not take a description -- a degenerate hull is the real one.
    bool refuseUpdates = false;

    void setBodyMaterial(physics::WorldHandle, physics::BodyHandle, f32 friction, f32 restitution) override
    {
        materials.emplace_back(friction, restitution);
    }

    void setBodyFlags(physics::WorldHandle, physics::BodyHandle, bool collidable, bool queryable) override
    {
        flags.emplace_back(collidable, queryable);
    }

    void setBodyGroup(physics::WorldHandle, physics::BodyHandle, physics::CollisionGroup group) override
    {
        groups.push_back(group);
    }

    [[nodiscard]] physics::BodyState bodyState(physics::WorldHandle, physics::BodyHandle) const override { return {}; }

    void collectActiveBodies(physics::WorldHandle, std::vector<physics::ActiveBody>& out) const override
    {
        out.insert(out.end(), active.begin(), active.end());
    }

    void step(physics::WorldHandle, f32 dt) override
    {
        ++steps;
        lastDt = dt;
    }

    [[nodiscard]] std::span<const physics::ContactEvent> drainContacts(physics::WorldHandle) override
    {
        return contacts;
    }

    [[nodiscard]] physics::StepTimings lastStepTimings(physics::WorldHandle) const override { return {}; }

    [[nodiscard]] bool raycast(physics::WorldHandle, const physics::RayD&, const physics::QueryFilter&,
                               physics::RayHit&) const override
    {
        return false;
    }

    [[nodiscard]] bool spherecast(physics::WorldHandle, const physics::RayD&, f32, const physics::QueryFilter&,
                                  physics::RayHit&) const override
    {
        return false;
    }

    void overlapBox(physics::WorldHandle, const core::CFrameD&, core::Vec3, const physics::QueryFilter&,
                    std::vector<u64>&) const override
    {}

    [[nodiscard]] physics::CharacterHandle createCharacter(physics::WorldHandle,
                                                           const physics::CharacterDesc& desc) override
    {
        characters.push_back(desc);
        return physics::CharacterHandle{nextCharacter++, 1};
    }

    void destroyCharacter(physics::WorldHandle, physics::CharacterHandle) override { ++charactersDestroyed; }

    void moveCharacter(physics::WorldHandle, physics::CharacterHandle, core::Vec3 velocity, f32) override
    {
        moves.push_back(velocity);
    }

    void setCharacterTransform(physics::WorldHandle, physics::CharacterHandle, const core::CFrameD&) override {}

    [[nodiscard]] physics::CharacterState characterState(physics::WorldHandle, physics::CharacterHandle) const override
    {
        return characterAnswer;
    }

    [[nodiscard]] physics::CollisionGroup registerCollisionGroup(physics::WorldHandle, std::string_view name) override
    {
        registeredGroups.emplace_back(name);
        return static_cast<physics::CollisionGroup>(registeredGroups.size() - 1);
    }

    [[nodiscard]] physics::CollisionGroup findCollisionGroup(physics::WorldHandle, std::string_view) const override
    {
        return 0;
    }

    void setGroupsCollidable(physics::WorldHandle, physics::CollisionGroup, physics::CollisionGroup, bool) override {}

    [[nodiscard]] bool groupsCollidable(physics::WorldHandle, physics::CollisionGroup,
                                        physics::CollisionGroup) const override
    {
        return true;
    }

    void collectCollisionGroups(physics::WorldHandle, std::vector<std::string_view>&) const override {}

    [[nodiscard]] bool saveState(physics::WorldHandle, std::vector<u8>&) const override { return false; }
    [[nodiscard]] bool restoreState(physics::WorldHandle, std::span<const u8>) override { return false; }
    void debugDraw(physics::WorldHandle, physics::IDebugDrawSink&) override {}

    u32 nextBody = 1;
    u32 nextCharacter = 1;
    u32 nextConstraint = 1;
    int steps = 0;
    f32 lastDt = 0.0f;
    bool worldDestroyed = false;
    int charactersDestroyed = 0;
    core::Vec3 gravity{0.0f, 0.0f, 0.0f};
    core::DVec3 origin{};
    int originPushes = 0;

    std::vector<Created> created;
    std::vector<Created> rebuilt;
    std::vector<MadeConstraint> constraints;
    std::vector<MadeConstraint> constraintsUpdated;
    std::vector<physics::ConstraintHandle> constraintsDestroyed;
    std::vector<physics::ConstraintHandle> liveConstraints;
    std::vector<std::pair<physics::ConstraintHandle, bool>> constraintEnables;
    // Every lifecycle call in the order it happened, which is the only way to
    // assert an interleaving -- "constraints retired before bodies" is a claim
    // about sequence and a per-kind list cannot make it.
    std::vector<std::pair<std::string, u32>> calls;
    std::vector<physics::BodyHandle> destroyed;
    std::vector<physics::BodyHandle> live;
    std::vector<std::pair<physics::BodyHandle, core::CFrameD>> transforms;
    std::vector<std::pair<physics::BodyHandle, core::Vec3>> impulses;
    std::vector<std::pair<f32, f32>> materials;
    std::vector<std::pair<bool, bool>> flags;
    std::vector<physics::CollisionGroup> groups;
    std::vector<std::string> registeredGroups;
    std::vector<physics::CharacterDesc> characters;
    std::vector<core::Vec3> moves;

    std::vector<physics::ActiveBody> active;
    std::vector<physics::ContactEvent> contacts;
    physics::CharacterState characterAnswer;
};

// The scene fixture's hierarchy predates the physics components, and its own
// storage hook attaches only `PartComponent`. Adding the rigid body here rather
// than teaching the fixture keeps the fixture describing the M2 surface it was
// written for.
struct Mirror
{
    testing::Fixture fixture;
    FakePhysics backend;
    core::InstanceId workspace;
    PhysicsSync sync{fixture.world, backend};

    Mirror()
    {
        workspace = fixture.folder("Workspace");
        sync.setWorkspace(workspace);
    }

    [[nodiscard]] core::InstanceId part(std::string_view name, core::DVec3 at = {})
    {
        const core::InstanceId id = fixture.part(name);
        PartComponent* component = fixture.world.parts().find(id);
        component->cframe.position = at;
        RigidBodyComponent body;
        body.collisionGroup = fixture.world.collisionGroups().nameAt(CollisionGroups::kDefault);
        fixture.world.rigidBodies().add(id, body);
        REQUIRE(fixture.world.setParent(id, workspace) == std::nullopt);
        return id;
    }

    [[nodiscard]] RigidBodyComponent& body(core::InstanceId id) { return *fixture.world.rigidBodies().find(id); }
    [[nodiscard]] PartComponent& transform(core::InstanceId id) { return *fixture.world.parts().find(id); }

    void step() { sync.step(1.0 / 60.0); }
};

} // namespace

TEST_CASE("a part under Workspace becomes a body and a part outside it does not")
{
    Mirror mirror;
    const core::InstanceId inside = mirror.part("Inside");

    // Created with a rigid body but never parented: in the tree's terms it is
    // not in the world, and the mirror's contract is that the tree decides.
    const core::InstanceId outside = mirror.fixture.part("Outside");
    mirror.fixture.world.rigidBodies().add(outside, RigidBodyComponent{});

    mirror.step();

    CHECK(mirror.backend.created.size() == 1);
    CHECK(mirror.backend.created[0].desc.userData != 0);
    CHECK(mirror.sync.instanceOf(mirror.backend.created[0].desc.userData) == inside);
    CHECK(mirror.sync.bodyCount() == 1);
}

TEST_CASE("a part taken out of the world loses its body")
{
    Mirror mirror;
    const core::InstanceId id = mirror.part("Crate");
    mirror.step();
    REQUIRE(mirror.backend.created.size() == 1);

    REQUIRE(mirror.fixture.world.setParent(id, core::InstanceId{}) == std::nullopt);
    mirror.step();

    CHECK(mirror.backend.destroyed.size() == 1);
    CHECK(mirror.sync.bodyCount() == 0);

    SUBCASE("and gets a new one when it comes back")
    {
        REQUIRE(mirror.fixture.world.setParent(id, mirror.workspace) == std::nullopt);
        mirror.step();
        CHECK(mirror.backend.created.size() == 2);
    }
}

TEST_CASE("a destroyed part loses its body")
{
    Mirror mirror;
    const core::InstanceId id = mirror.part("Crate");
    mirror.step();

    REQUIRE(mirror.fixture.world.destroy(id));
    mirror.fixture.world.retireDestroyed();
    mirror.step();

    CHECK(mirror.backend.destroyed.size() == 1);
    CHECK(mirror.sync.bodyCount() == 0);
}

TEST_CASE("Anchored decides the motion type, and changing it rebuilds")
{
    Mirror mirror;
    const core::InstanceId id = mirror.part("Floor");
    mirror.body(id).anchored = true;
    mirror.step();

    REQUIRE(mirror.backend.created.size() == 1);
    CHECK(mirror.backend.created[0].desc.motion == physics::MotionType::Static);

    mirror.body(id).anchored = false;
    mirror.step();

    // A rebuild, not a setter: an anchored part is a different kind of body,
    // not a very heavy one.
    REQUIRE(mirror.backend.rebuilt.size() == 1);
    CHECK(mirror.backend.rebuilt[0].desc.motion == physics::MotionType::Dynamic);
    CHECK(mirror.backend.created.size() == 1);
}

TEST_CASE("friction and restitution are adjusted rather than rebuilt")
{
    Mirror mirror;
    const core::InstanceId id = mirror.part("Ice");
    mirror.step();

    mirror.body(id).friction = 0.01f;
    mirror.body(id).restitution = 0.9f;
    mirror.step();

    REQUIRE(mirror.backend.materials.size() == 1);
    CHECK(mirror.backend.materials[0].first == doctest::Approx(0.01));
    CHECK(mirror.backend.materials[0].second == doctest::Approx(0.9));
    CHECK(mirror.backend.rebuilt.empty());
}

TEST_CASE("a script's CFrame write reaches the body and the mirror's own does not")
{
    Mirror mirror;
    const core::InstanceId id = mirror.part("Crate");
    mirror.step();
    CHECK(mirror.backend.transforms.empty());

    SUBCASE("a script write is pushed down")
    {
        mirror.transform(id).cframe.position = core::DVec3{5.0, 0.0, 0.0};
        mirror.step();
        REQUIRE(mirror.backend.transforms.size() == 1);
        CHECK(mirror.backend.transforms[0].second.position.x == doctest::Approx(5.0));
    }

    SUBCASE("the mirror's own writeback is not pushed back down")
    {
        // This is the loop the `written` snapshot exists to break: the mirror
        // writes a position into the component, and the next tick must not read
        // it back as a script's teleport.
        physics::ActiveBody moved;
        moved.userData = mirror.sync.userDataOf(id);
        moved.body = mirror.backend.created[0].handle;
        moved.state.transform.position = core::DVec3{0.0, 3.0, 0.0};
        moved.state.active = true;
        mirror.backend.active.push_back(moved);

        mirror.step();
        CHECK(mirror.transform(id).cframe.position.y == doctest::Approx(3.0));

        mirror.step();
        CHECK(mirror.backend.transforms.empty());
    }
}

TEST_CASE("ApplyImpulse is queued and drained exactly once")
{
    Mirror mirror;
    const core::InstanceId id = mirror.part("Crate");
    mirror.step();

    mirror.body(id).pendingImpulse = core::Vec3{0.0f, 10.0f, 0.0f};
    mirror.step();
    REQUIRE(mirror.backend.impulses.size() == 1);
    CHECK(mirror.backend.impulses[0].second.y == doctest::Approx(10.0));

    // Drained: a second tick with nothing new applies nothing, which is the
    // difference between an impulse and a force.
    mirror.step();
    CHECK(mirror.backend.impulses.size() == 1);
    CHECK(mirror.body(id).pendingImpulse.y == doctest::Approx(0.0));
}

TEST_CASE("velocity and sleep state are written back")
{
    Mirror mirror;
    const core::InstanceId id = mirror.part("Crate");
    mirror.step();

    physics::ActiveBody moving;
    moving.userData = mirror.sync.userDataOf(id);
    moving.body = mirror.backend.created[0].handle;
    moving.state.linearVelocity = core::Vec3{1.0f, -2.0f, 0.0f};
    moving.state.active = true;
    mirror.backend.active.push_back(moving);
    mirror.step();

    CHECK(mirror.body(id).linearVelocity.y == doctest::Approx(-2.0));
    CHECK(mirror.body(id).active);

    // Gone from the active list means asleep, and a body that is asleep is not
    // still moving at the speed it had when it stopped being reported.
    mirror.backend.active.clear();
    mirror.step();
    CHECK_FALSE(mirror.body(id).active);
    CHECK(mirror.body(id).linearVelocity.y == doctest::Approx(0.0));
}

TEST_CASE("a contact becomes two deferred facts, one for each part")
{
    Mirror mirror;
    const core::InstanceId first = mirror.part("A");
    const core::InstanceId second = mirror.part("B");
    mirror.step();
    // Drop whatever parenting enqueued: this case is about what the contact
    // produces, not about what building the scene did.
    [[maybe_unused]] const auto ignored = mirror.fixture.world.changes().take();

    physics::ContactEvent event;
    event.phase = physics::ContactPhase::Began;
    event.firstUserData = mirror.sync.userDataOf(first);
    event.secondUserData = mirror.sync.userDataOf(second);
    mirror.backend.contacts.push_back(event);
    mirror.step();

    const std::span<const Change> changes = mirror.fixture.world.changes().take();
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].kind == ChangeKind::InstanceEvent);
    CHECK(changes[0].subject == first);
    CHECK(changes[0].other == second);
    CHECK(changes[1].subject == second);
    CHECK(changes[1].other == first);
    // Both name `Touched`, because a script connects to one side of the pair
    // without knowing which side it is.
    CHECK(mirror.fixture.world.atoms().text(changes[0].name) == "Touched");
    CHECK(mirror.fixture.world.atoms().text(changes[1].name) == "Touched");
}

TEST_CASE("a contact naming a destroyed part produces nothing")
{
    Mirror mirror;
    const core::InstanceId first = mirror.part("A");
    const core::InstanceId second = mirror.part("B");
    mirror.step();

    physics::ContactEvent event;
    event.phase = physics::ContactPhase::Ended;
    event.firstUserData = mirror.sync.userDataOf(first);
    event.secondUserData = mirror.sync.userDataOf(second);
    mirror.backend.contacts.push_back(event);

    REQUIRE(mirror.fixture.world.destroy(second));
    mirror.fixture.world.retireDestroyed();
    [[maybe_unused]] const auto ignored = mirror.fixture.world.changes().take();
    mirror.step();

    // The handle no longer resolves, so there is nothing to hand a handler.
    CHECK(mirror.fixture.world.changes().size() == 0);
}

TEST_CASE("gravity comes from Workspace and reaches the simulation")
{
    Mirror mirror;
    // The fixture's Workspace is a Folder, so give it the component the real
    // one carries.
    mirror.fixture.world.workspaces().add(mirror.workspace, WorkspaceComponent{});
    mirror.fixture.world.workspaces().find(mirror.workspace)->gravity = core::Vec3{0.0f, -1.62f, 0.0f};
    mirror.step();
    CHECK(mirror.backend.gravity.y == doctest::Approx(-1.62));
}

TEST_CASE("the mirror does nothing at all without a Workspace")
{
    Mirror mirror;
    const core::InstanceId id = mirror.part("Crate");
    mirror.sync.setWorkspace(core::InstanceId{});
    mirror.step();

    // The M4.5 shape: an unresolved service id renders a plausible-looking
    // nothing. Here it must be a visible nothing -- no bodies, no steps -- so a
    // host that forgets to hand the id over fails a test rather than shipping a
    // world where nothing falls.
    CHECK(mirror.backend.created.empty());
    CHECK(mirror.backend.steps == 0);
    CHECK(id.valid());
}

TEST_CASE("the mirror pushes an origin change down exactly once")
{
    Mirror mirror;
    (void)mirror.part("Crate", core::DVec3{0.0, 5.0, 0.0});
    mirror.sync.step(1.0 / 60.0);

    CHECK(mirror.sync.origin() == core::DVec3{});
    CHECK(mirror.backend.originPushes == 0);
    CHECK(mirror.sync.rebaseCount() == 0);

    mirror.sync.setOrigin(core::DVec3{4096.0, 0.0, -4096.0});
    CHECK(mirror.backend.origin == core::DVec3{4096.0, 0.0, -4096.0});
    CHECK(mirror.backend.originPushes == 1);
    CHECK(mirror.sync.rebaseCount() == 1);

    // Setting the same origin again is not a rebase. Without this, a host that
    // pushed the current origin every frame would move every body in the world
    // every frame -- and the counter is what would say so.
    mirror.sync.setOrigin(core::DVec3{4096.0, 0.0, -4096.0});
    CHECK(mirror.backend.originPushes == 1);
    CHECK(mirror.sync.rebaseCount() == 1);
}

TEST_CASE("a rebase does not make the mirror re-push every transform")
{
    Mirror mirror;
    (void)mirror.part("Crate", core::DVec3{0.0, 5.0, 0.0});
    mirror.sync.step(1.0 / 60.0);

    const std::size_t before = mirror.backend.transforms.size();
    mirror.sync.setOrigin(core::DVec3{4096.0, 0.0, 0.0});
    mirror.sync.step(1.0 / 60.0);

    // The mirror's record of what it last pushed is in ABSOLUTE coordinates, so
    // a rebase leaves it valid. If it had been stored in the solver's space,
    // every part in the world would look like a script write on the next tick
    // and six thousand transforms would go back down for nothing.
    CHECK(mirror.backend.transforms.size() == before);
}

TEST_CASE("the rebase policy is a box around the origin, not a sphere")
{
    Mirror mirror;

    CHECK_FALSE(mirror.sync.shouldRebase(core::DVec3{}));
    CHECK_FALSE(mirror.sync.shouldRebase(core::DVec3{3999.0, 0.0, 0.0}));
    CHECK(mirror.sync.shouldRebase(core::DVec3{4001.0, 0.0, 0.0}));
    CHECK(mirror.sync.shouldRebase(core::DVec3{0.0, 0.0, -4001.0}));

    // A box rather than a sphere, so a focus travelling along one axis rebases
    // at the same distance whichever axis it is. On a sphere the diagonal would
    // reach 6.9 km before triggering.
    CHECK_FALSE(mirror.sync.shouldRebase(core::DVec3{3999.0, 0.0, 3999.0}));

    // And the tolerance follows the origin rather than the world's zero.
    mirror.sync.setOrigin(core::DVec3{100000.0, 0.0, 0.0});
    CHECK_FALSE(mirror.sync.shouldRebase(core::DVec3{100000.0, 0.0, 0.0}));
    CHECK(mirror.sync.shouldRebase(core::DVec3{}));
}

// ---------------------------------------------------------------------------
// `MeshPart.CollisionFidelity` (roadmap M7: it stops being `Inert`).

TEST_CASE("a MeshPart collides as its bounding box until its points arrive")
{
    // The state of every `MeshPart` in the frame before its file finishes
    // loading. A body with no shape for one frame is a body that falls through
    // the floor, so the box is the fallback rather than nothing.
    Mirror mirror;
    const core::InstanceId id = mirror.part("Rock");
    MeshPartComponent mesh;
    mesh.meshContent = mirror.fixture.world.atoms().intern("asset://models/rock.glb");
    // `Hull`, not `Box`: the caller asked for the geometry.
    mesh.collisionFidelity = 1;
    mirror.fixture.world.meshParts().add(id, mesh);
    mirror.step();

    REQUIRE(mirror.backend.created.size() == 1);
    CHECK(mirror.backend.created[0].desc.shape.type == physics::ShapeType::Box);
}

TEST_CASE("a MeshPart collides as a hull once its points have been handed over")
{
    Mirror mirror;
    const core::NameAtom content = mirror.fixture.world.atoms().intern("asset://models/rock.glb");

    // A tetrahedron: the fewest points a hull can be made of, which is also the
    // threshold the mirror checks before it believes it has geometry.
    mirror.sync.setCollisionPoints(content, {core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{1.0f, 0.0f, 0.0f},
                                             core::Vec3{0.0f, 1.0f, 0.0f}, core::Vec3{0.0f, 0.0f, 1.0f}});
    CHECK(mirror.sync.collisionMeshCount() == 1);

    const core::InstanceId id = mirror.part("Rock");
    MeshPartComponent mesh;
    mesh.meshContent = content;
    mesh.collisionFidelity = 1;
    mirror.fixture.world.meshParts().add(id, mesh);
    mirror.step();

    REQUIRE(mirror.backend.created.size() == 1);
    CHECK(mirror.backend.created[0].desc.shape.type == physics::ShapeType::ConvexHull);
    CHECK(mirror.backend.created[0].points.size() == 4);
}

TEST_CASE("a hull is scaled by Size over MeshSize, like the mesh on screen")
{
    // The collision shape and the drawn shape are the same shape or they are a
    // lie. `MeshSize` is what the mesh measures as authored; `Size` is what the
    // part is; the hull's points are in the mesh's own space, so the ratio is
    // what carries one into the other.
    Mirror mirror;
    const core::NameAtom content = mirror.fixture.world.atoms().intern("asset://models/rock.glb");
    mirror.sync.setCollisionPoints(content, {core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{1.0f, 0.0f, 0.0f},
                                             core::Vec3{0.0f, 1.0f, 0.0f}, core::Vec3{0.0f, 0.0f, 1.0f}});

    const core::InstanceId id = mirror.part("Rock");
    mirror.transform(id).size = core::Vec3{4.0f, 6.0f, 8.0f};
    MeshPartComponent mesh;
    mesh.meshContent = content;
    mesh.collisionFidelity = 1;
    mesh.meshSize = core::Vec3{2.0f, 2.0f, 4.0f};
    mirror.fixture.world.meshParts().add(id, mesh);
    mirror.step();

    REQUIRE(mirror.backend.created.size() == 1);
    const physics::ShapeDesc& shape = mirror.backend.created[0].desc.shape;
    CHECK(shape.type == physics::ShapeType::ConvexHull);
    // A factor rather than pre-scaled points: the points are SHARED across every
    // part naming this file, and scaling them here would be a copy per body.
    CHECK(mirror.backend.created[0].points.size() == 4);
    CHECK(shape.pointScale.x == 2.0f);
    CHECK(shape.pointScale.y == 3.0f);
    CHECK(shape.pointScale.z == 2.0f);
}

TEST_CASE("a MeshPart nobody resized hands the backend an identity scale")
{
    // Both default to one, so every scene written before `MeshSize` existed
    // builds exactly the hull it did then.
    Mirror mirror;
    const core::NameAtom content = mirror.fixture.world.atoms().intern("asset://models/rock.glb");
    mirror.sync.setCollisionPoints(content, {core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{1.0f, 0.0f, 0.0f},
                                             core::Vec3{0.0f, 1.0f, 0.0f}, core::Vec3{0.0f, 0.0f, 1.0f}});

    const core::InstanceId id = mirror.part("Rock");
    MeshPartComponent mesh;
    mesh.meshContent = content;
    mesh.collisionFidelity = 1;
    mirror.fixture.world.meshParts().add(id, mesh);
    mirror.step();

    REQUIRE(mirror.backend.created.size() == 1);
    const physics::ShapeDesc& shape = mirror.backend.created[0].desc.shape;
    CHECK(shape.pointScale.x == 1.0f);
    CHECK(shape.pointScale.y == 1.0f);
    CHECK(shape.pointScale.z == 1.0f);
}

TEST_CASE("CollisionFidelity Box is honoured exactly, points or no points")
{
    // The one fidelity that names a shape rather than an accuracy, and it means
    // what it says: a caller who asked for the bounding box gets it even when
    // the mesh's geometry is sitting right there.
    //
    // **This case said `Box` in its title and wrote 0** (D146), which is
    // `Default`. The test and the code held the same wrong belief about which
    // item was which, so the suite agreed with the defect and went on agreeing
    // -- which is how one constant produced two opposite faults and neither was
    // caught by a test written specifically about it.
    Mirror mirror;
    const core::NameAtom content = mirror.fixture.world.atoms().intern("asset://models/rock.glb");
    mirror.sync.setCollisionPoints(content, {core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{1.0f, 0.0f, 0.0f},
                                             core::Vec3{0.0f, 1.0f, 0.0f}, core::Vec3{0.0f, 0.0f, 1.0f}});

    const core::InstanceId id = mirror.part("Rock");
    MeshPartComponent mesh;
    mesh.meshContent = content;
    mesh.collisionFidelity = 2; // Enum.CollisionFidelity.Box
    mirror.fixture.world.meshParts().add(id, mesh);
    mirror.step();

    REQUIRE(mirror.backend.created.size() == 1);
    CHECK(mirror.backend.created[0].desc.shape.type == physics::ShapeType::Box);
}

TEST_CASE("a hull too degenerate to be one falls back to the box")
{
    // Three points is a triangle, not a solid. Jolt would refuse it and the body
    // would have no shape at all -- which is the part falling through the world.
    Mirror mirror;
    const core::NameAtom content = mirror.fixture.world.atoms().intern("asset://models/flat.glb");
    mirror.sync.setCollisionPoints(
        content, {core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{1.0f, 0.0f, 0.0f}, core::Vec3{0.0f, 1.0f, 0.0f}});

    const core::InstanceId id = mirror.part("Flat");
    MeshPartComponent mesh;
    mesh.meshContent = content;
    mesh.collisionFidelity = 1;
    mirror.fixture.world.meshParts().add(id, mesh);
    mirror.step();

    REQUIRE(mirror.backend.created.size() == 1);
    CHECK(mirror.backend.created[0].desc.shape.type == physics::ShapeType::Box);
}

// --- Attachment and Bone -----------------------------------------------------

namespace {

// A `SkeletonHost` that answers about one rig, so the mirror's bone path can be
// exercised without a renderer, a mesh file or a GPU.
class FakeSkeleton final : public SkeletonHost
{
public:
    [[nodiscard]] u32 jointCount(core::InstanceId) const override { return 2; }

    [[nodiscard]] i32 findJoint(core::InstanceId, std::string_view name) const override
    {
        ++lookups;
        if (name == "Root")
            return 0;
        if (name == "Hand")
            return 1;
        return -1;
    }

    [[nodiscard]] i32 jointParent(core::InstanceId, u32 joint) const override { return joint == 0 ? -1 : 0; }

    [[nodiscard]] std::string_view jointName(core::InstanceId, u32 joint) const override
    {
        return joint == 0 ? "Root" : "Hand";
    }

    [[nodiscard]] bool jointModel(core::InstanceId, u32 joint, core::CFrameD& out) const override
    {
        if (joint >= 2)
            return false;
        // The hand is two metres up the mesh's own Y, the root at its origin.
        out = core::CFrameD{};
        if (joint == 1)
            out.position = core::DVec3{0.0, 2.0, 0.0};
        return true;
    }

    void setJointOverride(core::InstanceId, u32, const core::CFrameD&) override {}
    void clearJointOverrides(core::InstanceId) override {}
    void commitOverrides() override { ++commits; }

    mutable int lookups = 0;
    int commits = 0;
};

} // namespace

TEST_CASE("an attachment follows the part it is on")
{
    Mirror mirror;
    const core::InstanceId part = mirror.part("Anchor", core::DVec3{10.0, 0.0, 0.0});

    const core::InstanceId socket = mirror.fixture.world.create(mirror.fixture.schema.attachmentClass);
    REQUIRE(mirror.fixture.world.setParent(socket, part) == std::nullopt);
    mirror.fixture.world.attachments().find(socket)->cframe.position = core::DVec3{0.0, 1.0, 0.0};

    mirror.step();

    const AttachmentComponent* resolved = mirror.fixture.world.attachments().find(socket);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->worldCFrame.position.x == doctest::Approx(10.0));
    CHECK(resolved->worldCFrame.position.y == doctest::Approx(1.0));

    // And it follows: move the part, step, and the socket is where the part put
    // it rather than where it was.
    mirror.transform(part).cframe.position = core::DVec3{20.0, 5.0, 0.0};
    mirror.step();
    CHECK(mirror.fixture.world.attachments().find(socket)->worldCFrame.position.x == doctest::Approx(20.0));
    CHECK(mirror.fixture.world.attachments().find(socket)->worldCFrame.position.y == doctest::Approx(6.0));
}

TEST_CASE("an attachment on a WELDED part is not one frame behind it")
{
    // The reason attachment resolution is interleaved with weld resolution
    // through the same memo rather than run as a pass after it.
    Mirror mirror;
    const core::InstanceId anchor = mirror.part("Anchor", core::DVec3{0.0, 0.0, 0.0});
    const core::InstanceId driven = mirror.part("Driven", core::DVec3{0.0, 0.0, 0.0});

    const core::InstanceId weld = mirror.fixture.world.create(mirror.fixture.schema.weldClass);
    REQUIRE(mirror.fixture.world.setParent(weld, mirror.workspace) == std::nullopt);
    WeldComponent& component = *mirror.fixture.world.welds().find(weld);
    component.part0 = anchor;
    component.part1 = driven;
    component.c0.position = core::DVec3{5.0, 0.0, 0.0};

    const core::InstanceId socket = mirror.fixture.world.create(mirror.fixture.schema.attachmentClass);
    REQUIRE(mirror.fixture.world.setParent(socket, driven) == std::nullopt);

    mirror.transform(anchor).cframe.position = core::DVec3{100.0, 0.0, 0.0};
    mirror.step();

    // The weld put the driven part at 105 this tick, and the socket is there --
    // not at wherever the part was before the weld ran.
    CHECK(mirror.transform(driven).cframe.position.x == doctest::Approx(105.0));
    CHECK(mirror.fixture.world.attachments().find(socket)->worldCFrame.position.x == doctest::Approx(105.0));
}

TEST_CASE("a bone follows its joint, and an unknown one follows the part")
{
    Mirror mirror;
    FakeSkeleton skeleton;
    mirror.sync.setSkeleton(&skeleton);

    const core::InstanceId character = mirror.part("Character", core::DVec3{0.0, 0.0, 50.0});
    mirror.fixture.world.meshParts().add(character, MeshPartComponent{});

    const core::InstanceId hand = mirror.fixture.world.create(mirror.fixture.schema.attachmentClass);
    REQUIRE(mirror.fixture.world.setParent(hand, character) == std::nullopt);
    mirror.fixture.world.attachments().find(hand)->jointName = mirror.fixture.world.atoms().intern("Hand");

    const core::InstanceId nowhere = mirror.fixture.world.create(mirror.fixture.schema.attachmentClass);
    REQUIRE(mirror.fixture.world.setParent(nowhere, character) == std::nullopt);
    mirror.fixture.world.attachments().find(nowhere)->jointName = mirror.fixture.world.atoms().intern("Tail");

    mirror.step();

    // The hand joint is two up the mesh's own Y, and the mesh is at z = 50.
    const AttachmentComponent* resolvedHand = mirror.fixture.world.attachments().find(hand);
    CHECK(resolvedHand->jointIndex == 1);
    CHECK(resolvedHand->worldCFrame.position.y == doctest::Approx(2.0));
    CHECK(resolvedHand->worldCFrame.position.z == doctest::Approx(50.0));

    // **A joint the rig does not have puts the bone on the character**, not at
    // the world origin. The failure a renamed joint should produce is a sword in
    // the wrong place, not a sword in another country.
    const AttachmentComponent* resolvedNowhere = mirror.fixture.world.attachments().find(nowhere);
    CHECK(resolvedNowhere->jointIndex == -1);
    CHECK(resolvedNowhere->worldCFrame.position.z == doctest::Approx(50.0));
    CHECK(resolvedNowhere->worldCFrame.position.y == doctest::Approx(0.0));
}

TEST_CASE("a bone resolves its joint name once and then holds the index")
{
    // A name is looked up against the rig; an index survives the rig reloading.
    // Looking the name up every tick would be a string compare per bone per
    // frame for an answer that does not change.
    Mirror mirror;
    FakeSkeleton skeleton;
    mirror.sync.setSkeleton(&skeleton);

    const core::InstanceId character = mirror.part("Character");
    mirror.fixture.world.meshParts().add(character, MeshPartComponent{});
    const core::InstanceId hand = mirror.fixture.world.create(mirror.fixture.schema.attachmentClass);
    REQUIRE(mirror.fixture.world.setParent(hand, character) == std::nullopt);
    mirror.fixture.world.attachments().find(hand)->jointName = mirror.fixture.world.atoms().intern("Hand");

    mirror.step();
    const int afterFirst = skeleton.lookups;
    CHECK(afterFirst == 1);
    mirror.step();
    mirror.step();
    CHECK(skeleton.lookups == afterFirst);
}

TEST_CASE("the pose is committed once per tick, after everything that moves a joint")
{
    Mirror mirror;
    FakeSkeleton skeleton;
    mirror.sync.setSkeleton(&skeleton);

    mirror.step();
    CHECK(skeleton.commits == 1);
    mirror.step();
    CHECK(skeleton.commits == 2);
}

TEST_CASE("a sword welded to the hand of a welded character settles in one tick")
{
    // **The chain the anchor path exists for**, and the one that a naive
    // implementation gets one frame wrong: the sword is welded to a bone, the
    // bone is on a character, and the character is itself welded to a moving
    // platform. All three have to settle in the tick the platform moved, not
    // one behind it.
    //
    // The weld pool is walked in creation order, and the SWORD's weld is
    // created first here on purpose -- so it is reached before the character's
    // and has to pull that one forward itself.
    Mirror mirror;
    FakeSkeleton skeleton;
    mirror.sync.setSkeleton(&skeleton);

    const core::InstanceId platform = mirror.part("Platform");
    const core::InstanceId character = mirror.part("Character");
    mirror.fixture.world.meshParts().add(character, MeshPartComponent{});
    const core::InstanceId sword = mirror.part("Sword");

    const core::InstanceId hand = mirror.fixture.world.create(mirror.fixture.schema.attachmentClass);
    REQUIRE(mirror.fixture.world.setParent(hand, character) == std::nullopt);
    mirror.fixture.world.attachments().find(hand)->jointName = mirror.fixture.world.atoms().intern("Hand");

    const core::InstanceId swordWeld = mirror.fixture.world.create(mirror.fixture.schema.weldClass);
    REQUIRE(mirror.fixture.world.setParent(swordWeld, mirror.workspace) == std::nullopt);
    WeldComponent& toHand = *mirror.fixture.world.welds().find(swordWeld);
    toHand.part0 = hand;
    toHand.part1 = sword;

    const core::InstanceId characterWeld = mirror.fixture.world.create(mirror.fixture.schema.weldClass);
    REQUIRE(mirror.fixture.world.setParent(characterWeld, mirror.workspace) == std::nullopt);
    WeldComponent& toPlatform = *mirror.fixture.world.welds().find(characterWeld);
    toPlatform.part0 = platform;
    toPlatform.part1 = character;

    mirror.transform(platform).cframe.position = core::DVec3{300.0, 0.0, 0.0};
    mirror.step();

    // One tick: the platform is at 300, the character went with it, and the
    // sword is at the character's hand two metres up.
    CHECK(mirror.transform(character).cframe.position.x == doctest::Approx(300.0));
    CHECK(mirror.transform(sword).cframe.position.x == doctest::Approx(300.0));
    CHECK(mirror.transform(sword).cframe.position.y == doctest::Approx(2.0));
}

TEST_CASE("a weld may be anchored to an attachment, and driven only by a part")
{
    // What welds a sword to a hand: the attachment is a `Bone`, the bone follows
    // a joint, and the sword follows the bone.
    Mirror mirror;
    FakeSkeleton skeleton;
    mirror.sync.setSkeleton(&skeleton);

    const core::InstanceId character = mirror.part("Character", core::DVec3{7.0, 0.0, 0.0});
    mirror.fixture.world.meshParts().add(character, MeshPartComponent{});
    const core::InstanceId hand = mirror.fixture.world.create(mirror.fixture.schema.attachmentClass);
    REQUIRE(mirror.fixture.world.setParent(hand, character) == std::nullopt);
    mirror.fixture.world.attachments().find(hand)->jointName = mirror.fixture.world.atoms().intern("Hand");

    const core::InstanceId sword = mirror.part("Sword");
    const core::InstanceId weld = mirror.fixture.world.create(mirror.fixture.schema.weldClass);
    REQUIRE(mirror.fixture.world.setParent(weld, mirror.workspace) == std::nullopt);
    WeldComponent& component = *mirror.fixture.world.welds().find(weld);
    component.part0 = hand;
    component.part1 = sword;

    mirror.step();

    // The hand joint is two up the mesh's Y and the character is at x = 7, so
    // that is where the sword is.
    CHECK(mirror.transform(sword).cframe.position.x == doctest::Approx(7.0));
    CHECK(mirror.transform(sword).cframe.position.y == doctest::Approx(2.0));
}

// --- Constraints --------------------------------------------------------------

namespace {

// A part with an attachment on it, which is the pair every constraint needs.
struct Jointed
{
    core::InstanceId part;
    core::InstanceId attachment;
};

[[nodiscard]] Jointed jointedPart(Mirror& mirror, std::string_view name, core::DVec3 at = {})
{
    Jointed out;
    out.part = mirror.part(name, at);
    out.attachment = mirror.fixture.world.create(mirror.fixture.schema.attachmentClass);
    REQUIRE(mirror.fixture.world.setParent(out.attachment, out.part) == std::nullopt);
    return out;
}

[[nodiscard]] core::InstanceId hinge(Mirror& mirror, const Jointed& a, const Jointed& b)
{
    const core::InstanceId id = mirror.fixture.world.create(mirror.fixture.schema.constraintClass);
    REQUIRE(mirror.fixture.world.setParent(id, mirror.workspace) == std::nullopt);
    ConstraintComponent& component = *mirror.fixture.world.constraints().find(id);
    component.kind = static_cast<i32>(physics::ConstraintType::Hinge);
    component.attachment0 = a.attachment;
    component.attachment1 = b.attachment;
    return id;
}

} // namespace

TEST_CASE("a constraint reaches the backend once, with both bodies")
{
    Mirror mirror;
    const Jointed frame = jointedPart(mirror, "Frame");
    const Jointed door = jointedPart(mirror, "Door", core::DVec3{1.0, 0.0, 0.0});
    (void)hinge(mirror, frame, door);

    mirror.step();

    REQUIRE(mirror.backend.constraints.size() == 1);
    CHECK(mirror.backend.constraints[0].desc.type == physics::ConstraintType::Hinge);
    // The two BODIES, through the parts the attachments sit on.
    CHECK(mirror.backend.constraints[0].desc.first == mirror.backend.created[0].handle);
    CHECK(mirror.backend.constraints[0].desc.second == mirror.backend.created[1].handle);

    // And a second tick creates nothing: the mirror can tell a change from a
    // no-op without a dirty flag on state the world hashes.
    mirror.step();
    CHECK(mirror.backend.constraints.size() == 1);
}

TEST_CASE("constraints are created after every body, in constraint pool order")
{
    // **The reason this is a second walk.** Folding it into the body walk would
    // create a joint before the body at its other end existed; creating that
    // body from here instead would put BODY creation order in the constraint
    // pool's order, and two scenes differing only in the order somebody added
    // joints would then simulate differently (R10).
    Mirror mirror;
    const Jointed a = jointedPart(mirror, "A");
    const Jointed b = jointedPart(mirror, "B", core::DVec3{1.0, 0.0, 0.0});
    const Jointed c = jointedPart(mirror, "C", core::DVec3{2.0, 0.0, 0.0});
    (void)hinge(mirror, a, b);
    (void)hinge(mirror, b, c);

    mirror.step();

    // Every body, then every constraint.
    std::size_t lastBody = 0;
    std::size_t firstConstraint = mirror.backend.calls.size();
    for (std::size_t index = 0; index < mirror.backend.calls.size(); ++index) {
        if (mirror.backend.calls[index].first == "createBody")
            lastBody = index;
        if (mirror.backend.calls[index].first == "createConstraint" && index < firstConstraint)
            firstConstraint = index;
    }
    CHECK(mirror.backend.constraints.size() == 2);
    CHECK(lastBody < firstConstraint);
}

TEST_CASE("a constraint is retired BEFORE the bodies it holds")
{
    // A joint holding a body that is gone is a dangling pointer inside the
    // solver, and it is silent. The backend drops a body's joints itself, so a
    // sweep that retired bodies first would destroy those joints twice from
    // here.
    Mirror mirror;
    const Jointed a = jointedPart(mirror, "A");
    const Jointed b = jointedPart(mirror, "B", core::DVec3{1.0, 0.0, 0.0});
    (void)hinge(mirror, a, b);
    mirror.step();
    REQUIRE(mirror.backend.constraints.size() == 1);

    mirror.backend.calls.clear();
    // Both parts leave the world at once, which is what destroying a model does.
    REQUIRE(mirror.fixture.world.setParent(a.part, core::InstanceId{}) == std::nullopt);
    REQUIRE(mirror.fixture.world.setParent(b.part, core::InstanceId{}) == std::nullopt);
    mirror.step();

    std::size_t destroyedConstraint = mirror.backend.calls.size();
    std::size_t firstDestroyedBody = mirror.backend.calls.size();
    for (std::size_t index = 0; index < mirror.backend.calls.size(); ++index) {
        if (mirror.backend.calls[index].first == "destroyConstraint" && index < destroyedConstraint)
            destroyedConstraint = index;
        if (mirror.backend.calls[index].first == "destroyBody" && index < firstDestroyedBody)
            firstDestroyedBody = index;
    }
    CHECK(mirror.backend.constraintsDestroyed.size() == 1);
    CHECK(destroyedConstraint < firstDestroyedBody);
}

TEST_CASE("a joint whose ends a script has not finished assigning is not built")
{
    // Not an error: it is what every script that sets two properties on two
    // lines briefly produces.
    Mirror mirror;
    const Jointed a = jointedPart(mirror, "A");

    const core::InstanceId id = mirror.fixture.world.create(mirror.fixture.schema.constraintClass);
    REQUIRE(mirror.fixture.world.setParent(id, mirror.workspace) == std::nullopt);
    mirror.fixture.world.constraints().find(id)->attachment0 = a.attachment;

    mirror.step();
    CHECK(mirror.backend.constraints.empty());

    // And it builds the moment the other end arrives.
    const Jointed b = jointedPart(mirror, "B", core::DVec3{1.0, 0.0, 0.0});
    mirror.fixture.world.constraints().find(id)->attachment1 = b.attachment;
    mirror.step();
    CHECK(mirror.backend.constraints.size() == 1);
}

TEST_CASE("a joint whose two ends are on one part is refused")
{
    Mirror mirror;
    const Jointed a = jointedPart(mirror, "A");
    const core::InstanceId second = mirror.fixture.world.create(mirror.fixture.schema.attachmentClass);
    REQUIRE(mirror.fixture.world.setParent(second, a.part) == std::nullopt);

    const core::InstanceId id = mirror.fixture.world.create(mirror.fixture.schema.constraintClass);
    REQUIRE(mirror.fixture.world.setParent(id, mirror.workspace) == std::nullopt);
    ConstraintComponent& component = *mirror.fixture.world.constraints().find(id);
    component.attachment0 = a.attachment;
    component.attachment1 = second;

    mirror.step();
    // One body joined to itself divides by an infinite mass in the solver.
    CHECK(mirror.backend.constraints.empty());
}

TEST_CASE("a limit change is an update rather than a rebuild")
{
    // Rebuilding would move the constraint to the end of the solve order, and a
    // door whose limit somebody nudged would simulate differently afterwards.
    Mirror mirror;
    const Jointed frame = jointedPart(mirror, "Frame");
    const Jointed door = jointedPart(mirror, "Door", core::DVec3{1.0, 0.0, 0.0});
    const core::InstanceId id = hinge(mirror, frame, door);
    mirror.step();
    REQUIRE(mirror.backend.constraints.size() == 1);

    ConstraintComponent& component = *mirror.fixture.world.constraints().find(id);
    component.limitsEnabled = true;
    component.limitLow = -1.0f;
    component.limitHigh = 1.0f;
    mirror.step();

    CHECK(mirror.backend.constraints.size() == 1);
    CHECK(mirror.backend.constraintsDestroyed.empty());
    REQUIRE(mirror.backend.constraintsUpdated.size() == 1);
    CHECK(mirror.backend.constraintsUpdated[0].desc.limitHigh == 1.0f);
}

TEST_CASE("changing what the joint IS rebuilds it")
{
    Mirror mirror;
    const Jointed a = jointedPart(mirror, "A");
    const Jointed b = jointedPart(mirror, "B", core::DVec3{1.0, 0.0, 0.0});
    const core::InstanceId id = hinge(mirror, a, b);
    mirror.step();
    REQUIRE(mirror.backend.constraints.size() == 1);

    // A ball socket that gains limits stops being a point joint and becomes a
    // swing-twist -- a different solver joint, not a point joint with
    // corrections bolted on.
    mirror.fixture.world.constraints().find(id)->kind = static_cast<i32>(physics::ConstraintType::SwingTwist);
    mirror.step();

    CHECK(mirror.backend.constraints.size() == 2);
    CHECK(mirror.backend.constraintsDestroyed.size() == 1);
    CHECK(mirror.backend.constraints[1].desc.type == physics::ConstraintType::SwingTwist);
}

TEST_CASE("disabling a joint does not destroy it")
{
    Mirror mirror;
    const Jointed a = jointedPart(mirror, "A");
    const Jointed b = jointedPart(mirror, "B", core::DVec3{1.0, 0.0, 0.0});
    const core::InstanceId id = hinge(mirror, a, b);
    mirror.step();

    mirror.fixture.world.constraints().find(id)->enabled = false;
    mirror.step();

    // Its place in the solve order is the one it was created with: a ragdoll
    // that switched itself off and on would otherwise simulate differently.
    CHECK(mirror.backend.constraintsDestroyed.empty());
    REQUIRE(mirror.backend.constraintEnables.size() == 1);
    CHECK_FALSE(mirror.backend.constraintEnables[0].second);
}

TEST_CASE("a constraint cannot reach a CharacterBody")
{
    // `CharacterVirtual` is swept rather than solved (the M5 finding), so there
    // is no body for a joint to hold.
    Mirror mirror;
    const Jointed a = jointedPart(mirror, "A");
    const Jointed walker = jointedPart(mirror, "Walker", core::DVec3{1.0, 0.0, 0.0});
    mirror.fixture.world.characterBodies().add(walker.part, CharacterBodyComponent{});

    const core::InstanceId id = mirror.fixture.world.create(mirror.fixture.schema.constraintClass);
    REQUIRE(mirror.fixture.world.setParent(id, mirror.workspace) == std::nullopt);
    ConstraintComponent& component = *mirror.fixture.world.constraints().find(id);
    component.attachment0 = a.attachment;
    component.attachment1 = walker.attachment;

    mirror.step();
    CHECK(mirror.backend.constraints.empty());
}

TEST_CASE("the joint frames are in each body's own space")
{
    // Which is what the seam asks for, and what makes a floating-origin rebase
    // cost nothing: the bodies move and the frames do not.
    Mirror mirror;
    const Jointed frame = jointedPart(mirror, "Frame", core::DVec3{100.0, 0.0, 0.0});
    const Jointed door = jointedPart(mirror, "Door", core::DVec3{101.0, 0.0, 0.0});
    mirror.fixture.world.attachments().find(frame.attachment)->cframe.position = core::DVec3{0.5, 0.0, 0.0};
    mirror.fixture.world.attachments().find(door.attachment)->cframe.position = core::DVec3{-0.5, 0.0, 0.0};
    (void)hinge(mirror, frame, door);

    mirror.step();

    REQUIRE(mirror.backend.constraints.size() == 1);
    // Half a metre, not a hundred and a half: local, not world.
    CHECK(mirror.backend.constraints[0].desc.firstFrame.position.x == doctest::Approx(0.5));
    CHECK(mirror.backend.constraints[0].desc.secondFrame.position.x == doctest::Approx(-0.5));
}

// --- Ragdoll ------------------------------------------------------------------

namespace {

// A ragdoll parented to a mesh, with one limb: a part, and a bone on it naming
// the joint that limb IS.
struct Rag
{
    core::InstanceId meshPart;
    core::InstanceId ragdoll;
    core::InstanceId limb;
    core::InstanceId bone;
};

[[nodiscard]] Rag ragdollOn(Mirror& mirror, core::DVec3 characterAt, core::DVec3 limbAt)
{
    Rag out;
    out.meshPart = mirror.part("Character", characterAt);
    mirror.fixture.world.meshParts().add(out.meshPart, MeshPartComponent{});

    out.ragdoll = mirror.fixture.world.create(mirror.fixture.schema.ragdollClass);
    REQUIRE(mirror.fixture.world.setParent(out.ragdoll, out.meshPart) == std::nullopt);

    out.limb = mirror.part("Limb", limbAt);
    REQUIRE(mirror.fixture.world.setParent(out.limb, out.ragdoll) == std::nullopt);

    out.bone = mirror.fixture.world.create(mirror.fixture.schema.attachmentClass);
    REQUIRE(mirror.fixture.world.setParent(out.bone, out.limb) == std::nullopt);
    return out;
}

// Records the overrides it is handed, so a test can say WHICH joint was driven
// and to where.
class RecordingSkeleton final : public SkeletonHost
{
public:
    [[nodiscard]] u32 jointCount(core::InstanceId) const override { return 2; }
    [[nodiscard]] i32 findJoint(core::InstanceId, std::string_view name) const override
    {
        return name == "Hand" ? 1 : (name == "Root" ? 0 : -1);
    }
    [[nodiscard]] i32 jointParent(core::InstanceId, u32 joint) const override { return joint == 0 ? -1 : 0; }
    [[nodiscard]] std::string_view jointName(core::InstanceId, u32 joint) const override
    {
        return joint == 0 ? "Root" : "Hand";
    }
    [[nodiscard]] bool jointModel(core::InstanceId, u32 joint, core::CFrameD& out) const override
    {
        if (joint >= 2)
            return false;
        out = core::CFrameD{};
        if (joint == 1)
            out.position = core::DVec3{0.0, 2.0, 0.0};
        return true;
    }

    void setJointOverride(core::InstanceId meshPart, u32 joint, const core::CFrameD& model) override
    {
        overrides.push_back({meshPart, joint, model});
    }
    void clearJointOverrides(core::InstanceId) override {}
    void commitOverrides() override
    {
        // What the commit SAW, kept after the clear. An empty `overrides` after
        // a tick is what "nothing drove" and "the commit consumed them" BOTH
        // look like, and no test can tell those apart without this.
        committed = overrides;
        ++commits;
        overrides.clear();
    }

    struct Written
    {
        core::InstanceId meshPart;
        u32 joint = 0;
        core::CFrameD model;
    };
    std::vector<Written> overrides;
    std::vector<Written> committed;
    int commits = 0;
};

} // namespace

TEST_CASE("a disabled ragdoll drives nothing")
{
    Mirror mirror;
    RecordingSkeleton skeleton;
    mirror.sync.setSkeleton(&skeleton);

    const Rag rag = ragdollOn(mirror, {0.0, 0.0, 0.0}, {0.0, 5.0, 0.0});
    mirror.fixture.world.attachments().find(rag.bone)->jointName = mirror.fixture.world.atoms().intern("Hand");

    mirror.step();
    // Off is a character animated by clips with some parts sitting there doing
    // nothing, which is what it has to be -- a ragdoll that drove while disabled
    // would make building one impossible to author.
    CHECK(skeleton.committed.empty());
}

TEST_CASE("an enabled ragdoll writes where its limb ended up, in the mesh's own space")
{
    Mirror mirror;
    RecordingSkeleton skeleton;
    mirror.sync.setSkeleton(&skeleton);

    // The character at x = 100 and its limb three metres above it. What the pose
    // wants is the limb relative to the CHARACTER, which is (0, 3, 0) -- not the
    // world position, and not the character's.
    const Rag rag = ragdollOn(mirror, {100.0, 0.0, 0.0}, {100.0, 3.0, 0.0});
    mirror.fixture.world.attachments().find(rag.bone)->jointName = mirror.fixture.world.atoms().intern("Hand");
    mirror.fixture.world.ragdolls().find(rag.ragdoll)->enabled = true;

    mirror.step();

    REQUIRE(skeleton.committed.size() == 1);
    CHECK(skeleton.committed[0].meshPart == rag.meshPart);
    CHECK(skeleton.committed[0].joint == 1);
    CHECK(skeleton.committed[0].model.position.x == doctest::Approx(0.0));
    CHECK(skeleton.committed[0].model.position.y == doctest::Approx(3.0));
}

TEST_CASE("a bone whose joint did not resolve drives nothing")
{
    // Better than driving joint zero: a limb whose joint an artist renamed
    // should do nothing rather than fold the character in half at the root.
    Mirror mirror;
    RecordingSkeleton skeleton;
    mirror.sync.setSkeleton(&skeleton);

    const Rag rag = ragdollOn(mirror, {0.0, 0.0, 0.0}, {0.0, 5.0, 0.0});
    mirror.fixture.world.attachments().find(rag.bone)->jointName = mirror.fixture.world.atoms().intern("Tail");
    mirror.fixture.world.ragdolls().find(rag.ragdoll)->enabled = true;

    mirror.step();
    CHECK(skeleton.committed.empty());
}

TEST_CASE("a ragdoll drives only the bones under it")
{
    // It owns nothing, so what it drives is what is UNDER it -- which is what
    // makes two ragdolls in one scene two characters rather than one confused
    // one.
    Mirror mirror;
    RecordingSkeleton skeleton;
    mirror.sync.setSkeleton(&skeleton);

    const Rag rag = ragdollOn(mirror, {0.0, 0.0, 0.0}, {0.0, 3.0, 0.0});
    mirror.fixture.world.attachments().find(rag.bone)->jointName = mirror.fixture.world.atoms().intern("Hand");
    mirror.fixture.world.ragdolls().find(rag.ragdoll)->enabled = true;

    // A bone on the character itself, outside the ragdoll. It resolves to a
    // joint and must not be driven by it.
    const core::InstanceId loose = mirror.fixture.world.create(mirror.fixture.schema.attachmentClass);
    REQUIRE(mirror.fixture.world.setParent(loose, rag.meshPart) == std::nullopt);
    mirror.fixture.world.attachments().find(loose)->jointName = mirror.fixture.world.atoms().intern("Root");

    mirror.step();

    REQUIRE(skeleton.committed.size() == 1);
    CHECK(skeleton.committed[0].joint == 1);
}

TEST_CASE("the ragdoll drive runs before the pose is committed")
{
    // Which is what makes the joints nobody simulates ride along on the ones
    // somebody does. Committed first, the overrides would land on a palette
    // already built and take a frame to appear.
    Mirror mirror;
    RecordingSkeleton skeleton;
    mirror.sync.setSkeleton(&skeleton);

    const Rag rag = ragdollOn(mirror, {0.0, 0.0, 0.0}, {0.0, 3.0, 0.0});
    mirror.fixture.world.attachments().find(rag.bone)->jointName = mirror.fixture.world.atoms().intern("Hand");
    mirror.fixture.world.ragdolls().find(rag.ragdoll)->enabled = true;

    mirror.step();
    // The commit SAW the override, which is the whole claim -- an empty list on
    // its own would equally mean nothing ever drove.
    CHECK(skeleton.commits == 1);
    REQUIRE(skeleton.committed.size() == 1);
    CHECK(skeleton.overrides.empty());
}

TEST_CASE("Blend carries the driven joint part of the way, and 0 leaves the clip alone")
{
    // **A POSE blend, not a solver one.** The limb has fallen exactly as far at
    // 0.5 as at 1 -- nothing here touches the simulation -- and what changes is
    // only how far the drawn joint is carried towards it. That is what a stumble
    // is, and ramping it is what stops going down being a one-frame snap.
    Mirror mirror;
    RecordingSkeleton skeleton;
    mirror.sync.setSkeleton(&skeleton);

    // The animated `Hand` is at y = 2 in the mesh's space; the limb ends up at
    // y = 4. Halfway between them is 3, and no other blend gives that number.
    const Rag rag = ragdollOn(mirror, {0.0, 0.0, 0.0}, {0.0, 4.0, 0.0});
    mirror.fixture.world.attachments().find(rag.bone)->jointName = mirror.fixture.world.atoms().intern("Hand");
    RagdollComponent* ragdoll = mirror.fixture.world.ragdolls().find(rag.ragdoll);
    ragdoll->enabled = true;
    ragdoll->blend = 0.5f;

    mirror.step();
    REQUIRE(skeleton.committed.size() == 1);
    CHECK(skeleton.committed[0].model.position.y == doctest::Approx(3.0));

    // At zero the joint is written at the ANIMATION's own value rather than not
    // written at all. Those are different: a joint that stopped being written
    // would keep whatever the last tick left in the palette, so a ramp back to 0
    // would freeze the character instead of handing it back to the clip.
    ragdoll->blend = 0.0f;
    mirror.step();
    REQUIRE(skeleton.committed.size() == 1);
    CHECK(skeleton.committed[0].model.position.y == doctest::Approx(2.0));
}

// --- The mirror with nothing ticking (S5.10) ---------------------------------

TEST_CASE("mirror creates the bodies the tree describes and advances nothing")
{
    // **What an editor's collision view is drawn from.** A paused world never
    // calls `step`, so the backend holds no bodies at all, and a wireframe of a
    // world with no bodies in it is an empty picture that looks exactly like a
    // working one.
    Mirror mirror;
    const core::InstanceId crate = mirror.part("Crate", {0.0, 4.0, 0.0});

    mirror.sync.mirror();
    CHECK(mirror.backend.live.size() == 1);
    CHECK(mirror.backend.steps == 0);
    // And the tree is untouched: a view that wrote back would move a part an
    // author is in the middle of placing.
    CHECK(mirror.transform(crate).cframe.position.y == doctest::Approx(4.0));
}

TEST_CASE("mirror retires what the tree no longer has")
{
    // Mark and sweep, exactly as a tick does it -- so an editor that deletes a
    // part does not leave a collider drawn where it used to be.
    Mirror mirror;
    const core::InstanceId crate = mirror.part("Crate");
    mirror.sync.mirror();
    REQUIRE(mirror.backend.live.size() == 1);

    mirror.fixture.world.destroy(crate);
    mirror.sync.mirror();
    CHECK(mirror.backend.live.empty());
}

TEST_CASE("mirror raises no contact, because nobody is playing")
{
    // The reason this is not `step(0)`. A zero-length step still runs the
    // solver, the character controllers and the contact diff -- so it would fire
    // `Touched` in the editor, which is a handler running in a world nobody has
    // pressed play on.
    Mirror mirror;
    const core::InstanceId first = mirror.part("A");
    const core::InstanceId second = mirror.part("B");
    mirror.sync.mirror();
    [[maybe_unused]] const auto ignored = mirror.fixture.world.changes().take();

    physics::ContactEvent event;
    event.phase = physics::ContactPhase::Began;
    event.firstUserData = mirror.sync.userDataOf(first);
    event.secondUserData = mirror.sync.userDataOf(second);
    mirror.backend.contacts.push_back(event);

    mirror.sync.mirror();
    CHECK(mirror.fixture.world.changes().take().empty());
}

// --- What the mirror remembers about a refusal -------------------------------

TEST_CASE("a body the backend refuses is asked for once, not once a tick")
{
    // **The retry storm.** A refusal used to zero the record, so the next tick
    // saw an empty slot and asked again -- for ever, once per tick, burning a
    // body generation each time and saying nothing at all. Nobody noticed
    // because nothing in the engine refused a body until a hull could arrive
    // late enough to be degenerate.
    Mirror mirror;
    mirror.backend.refuseCreates = true;
    (void)mirror.part("Crate");

    mirror.step();
    REQUIRE(mirror.backend.created.size() == 1);
    CHECK(mirror.sync.bodyCount() == 0);

    for (int tick = 0; tick < 20; ++tick)
        mirror.step();

    // One attempt, twenty ticks later.
    CHECK(mirror.backend.created.size() == 1);
    CHECK(mirror.sync.bodyCount() == 0);
}

TEST_CASE("a refused body is asked for again when the description changes")
{
    // The other half, and the half that makes remembering safe rather than
    // final: a hull whose points have since arrived, or a part somebody
    // resized, is a different question and gets asked.
    Mirror mirror;
    mirror.backend.refuseCreates = true;
    const core::InstanceId id = mirror.part("Crate");

    mirror.step();
    REQUIRE(mirror.backend.created.size() == 1);
    mirror.step();
    REQUIRE(mirror.backend.created.size() == 1);

    mirror.backend.refuseCreates = false;
    mirror.fixture.world.parts().find(id)->size = core::Vec3{2.0f, 2.0f, 2.0f};
    mirror.step();

    CHECK(mirror.backend.created.size() == 2);
    CHECK(mirror.sync.bodyCount() == 1);
}

TEST_CASE("a record that remembers a refusal is not destroyed as though it held a body")
{
    // `retireUnseen` destroys anything with a generation and no `seen`. A
    // refusal keeps this instance's generation and has no body, so without the
    // `live` flag the sweep hands the backend a handle naming nothing and takes
    // the count below zero.
    Mirror mirror;
    mirror.backend.refuseCreates = true;
    const core::InstanceId id = mirror.part("Crate");
    mirror.step();
    REQUIRE(mirror.sync.bodyCount() == 0);

    REQUIRE(mirror.fixture.world.setParent(id, core::InstanceId{}) == std::nullopt);
    mirror.step();

    CHECK(mirror.backend.destroyed.empty());
    CHECK(mirror.sync.bodyCount() == 0);
}

TEST_CASE("a constraint on a part with no body is not made")
{
    // `bodyHandleOf` is private and reached through the constraint walk, which
    // is the only caller and the one that matters: a record remembering a
    // refusal carries this instance's generation with no body behind it, and a
    // joint handed that handle would hold a dangling pointer inside the solver.
    Mirror mirror;
    mirror.backend.refuseCreates = true;
    (void)mirror.part("Anchor");
    (void)mirror.part("Hanging");
    mirror.step();

    CHECK(mirror.backend.constraints.empty());
}

TEST_CASE("a hull is rebuilt when the points behind it change")
{
    // **The one that async mesh loading depends on.** Two hulls with the same
    // type and the same size are not the same hull if the geometry underneath
    // them was replaced -- a mesh finishing its load, or a hot reload swapping
    // one. `sameShape` compared type and size and nothing else, so the first
    // hull a `MeshPart` got was the hull it kept for ever.
    //
    // The spans are deliberately not compared: comparing them would mean
    // keeping one, and `ShapeDesc::points` may not outlive the create call.
    Mirror mirror;
    const core::InstanceId id = mirror.part("Rock");
    MeshPartComponent rock;
    rock.meshContent = mirror.fixture.world.atoms().intern("asset://models/rock.glb");
    rock.collisionFidelity = 1;
    mirror.fixture.world.meshParts().add(id, rock);

    const std::vector<core::Vec3> first{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    mirror.sync.setCollisionPoints(mirror.fixture.world.atoms().intern("asset://models/rock.glb"), first);
    mirror.step();
    REQUIRE(mirror.backend.created.size() == 1);
    CHECK(mirror.backend.created[0].desc.shape.type == physics::ShapeType::ConvexHull);
    const core::usize rebuiltBefore = mirror.backend.rebuilt.size();

    // The same COUNT and the same size, so nothing but the revision can tell
    // these apart -- which is the case a span comparison would also have caught
    // and a type-and-size comparison cannot.
    const std::vector<core::Vec3> second{
        {0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {0.0f, 0.0f, 2.0f}};
    mirror.sync.setCollisionPoints(mirror.fixture.world.atoms().intern("asset://models/rock.glb"), second);
    mirror.step();

    CHECK(mirror.backend.rebuilt.size() == rebuiltBefore + 1);

    // And it settles: an unchanged cloud is not a reason to rebuild anything.
    const core::usize after = mirror.backend.rebuilt.size();
    mirror.step();
    mirror.step();
    CHECK(mirror.backend.rebuilt.size() == after);
}

TEST_CASE("a refused rebuild costs one attempt, and the body it had stays")
{
    // What the mirror does when the backend will not take a description.
    //
    // **The refusal is the fake's, and deliberately so.** The obvious real case
    // would be a degenerate hull, but Jolt's `ConvexHullBuilder` accepts a
    // coplanar quad and gives it a thickness -- so writing this against real
    // geometry would be writing it against a refusal that does not happen. What
    // is under test is the MIRROR's rule: a body the backend will not rebuild
    // stays what it was, and the record keeps what was ASKED for, which is what
    // stops it being asked again on every tick for ever.
    Mirror mirror;
    const core::InstanceId id = mirror.part("Slab");
    MeshPartComponent slab;
    slab.meshContent = mirror.fixture.world.atoms().intern("asset://models/slab.glb");
    slab.collisionFidelity = 1;
    mirror.fixture.world.meshParts().add(id, slab);
    mirror.step();
    REQUIRE(mirror.backend.created.size() == 1);

    mirror.backend.refuseUpdates = true;
    const std::vector<core::Vec3> flat{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    mirror.sync.setCollisionPoints(mirror.fixture.world.atoms().intern("asset://models/slab.glb"), flat);
    mirror.step();
    const core::usize attempts = mirror.backend.rebuilt.size();
    CHECK(attempts == 1);

    for (int tick = 0; tick < 20; ++tick)
        mirror.step();
    CHECK(mirror.backend.rebuilt.size() == attempts);

    // The body is still there. It is the old shape, which is the honest
    // outcome: the alternative to a stale hull is no hull at all.
    CHECK(mirror.sync.bodyCount() == 1);
    CHECK(mirror.backend.destroyed.empty());
    (void)id;
}

// --- Which shape a fidelity asks for (D146, S6.2) ----------------------------
//
// **One constant, two defects, in opposite directions.** The mirror short-
// circuited on item ZERO -- `Default`, whose whole meaning is "the engine
// chooses" and whose documented choice is a hull -- so every `MeshPart` nobody
// touched collided as its bounding box while the enum promised the geometry, and
// the one person who explicitly asked for `Box` got a hull instead.
//
// Neither is visible in a screenshot and neither raises. What they produce is a
// character walking on a box a metre outside a rock, or falling into a hull it
// should have stood on.

namespace {

// A `MeshPart` with points loaded, so the hull branch has something to reach.
[[nodiscard]] core::InstanceId meshWithPoints(Mirror& mirror, i32 fidelity)
{
    const core::InstanceId id = mirror.part("Rock");
    MeshPartComponent mesh;
    mesh.meshContent = mirror.fixture.world.atoms().intern("asset://models/rock.gltf");
    mesh.collisionFidelity = fidelity;
    mirror.fixture.world.meshParts().add(id, mesh);

    // Four points is the minimum a hull needs, which is what the mirror checks.
    const std::vector<core::Vec3> points{
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    mirror.sync.setCollisionPoints(mesh.meshContent, points);
    return id;
}

[[nodiscard]] physics::ShapeType shapeOfLast(const Mirror& mirror)
{
    REQUIRE_FALSE(mirror.backend.created.empty());
    return mirror.backend.created.back().desc.shape.type;
}

} // namespace

TEST_CASE("Default asks the engine to choose, and the engine chooses the hull")
{
    // The item's own doc: "The engine chooses. In this release that is `Hull`."
    // A box here is the enum saying one thing and the solver doing another, for
    // every mesh nobody ever set the property on -- which is nearly all of them.
    Mirror mirror;
    (void)meshWithPoints(mirror, 0);
    mirror.step();
    CHECK(shapeOfLast(mirror) == physics::ShapeType::ConvexHull);
}

TEST_CASE("Box asks for the bounding box and gets it")
{
    Mirror mirror;
    (void)meshWithPoints(mirror, 2);
    mirror.step();
    CHECK(shapeOfLast(mirror) == physics::ShapeType::Box);
}

TEST_CASE("Hull asks for the hull and gets it")
{
    Mirror mirror;
    (void)meshWithPoints(mirror, 1);
    mirror.step();
    CHECK(shapeOfLast(mirror) == physics::ShapeType::ConvexHull);
}

TEST_CASE("Precise asks for the triangles and gets the hull, which the enum says it will")
{
    // Not silent and not a lie: `Enum.CollisionFidelity.Precise` documents that
    // this release collides against a hull and reads back `Precise`. A
    // triangle-mesh collider is a different shape class with different rules --
    // it cannot be dynamic -- and it is asset-pipeline work.
    Mirror mirror;
    const core::InstanceId rock = meshWithPoints(mirror, 3);
    mirror.step();
    CHECK(shapeOfLast(mirror) == physics::ShapeType::ConvexHull);
    // And the property reads back what was written, which is the half that makes
    // it honest rather than merely unimplemented.
    CHECK(mirror.fixture.world.meshParts().find(rock)->collisionFidelity == 3);
}

TEST_CASE("a mesh whose points have not arrived falls back to the box, whatever it asked for")
{
    // The frame before a file finishes loading. A body with no shape for one
    // frame is a body that falls through the floor.
    Mirror mirror;
    const core::InstanceId id = mirror.part("Rock");
    MeshPartComponent mesh;
    mesh.meshContent = mirror.fixture.world.atoms().intern("asset://models/absent.gltf");
    mesh.collisionFidelity = 1;
    mirror.fixture.world.meshParts().add(id, mesh);

    mirror.step();
    CHECK(shapeOfLast(mirror) == physics::ShapeType::Box);
}
