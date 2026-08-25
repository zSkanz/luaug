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
        const physics::BodyHandle handle{nextBody++, 1};
        created.push_back(Created{handle, desc});
        live.push_back(handle);
        return handle;
    }

    void destroyBody(physics::WorldHandle, physics::BodyHandle handle) override
    {
        destroyed.push_back(handle);
        live.erase(std::remove(live.begin(), live.end(), handle), live.end());
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

    void updateBody(physics::WorldHandle, physics::BodyHandle handle, const physics::BodyDesc& desc) override
    {
        rebuilt.push_back(Created{handle, desc});
    }

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
    int steps = 0;
    f32 lastDt = 0.0f;
    bool worldDestroyed = false;
    int charactersDestroyed = 0;
    core::Vec3 gravity{0.0f, 0.0f, 0.0f};
    core::DVec3 origin{};
    int originPushes = 0;

    std::vector<Created> created;
    std::vector<Created> rebuilt;
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
    CHECK(mirror.backend.created[0].desc.shape.points.size() == 4);
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
    CHECK(shape.points.size() == 4);
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
    Mirror mirror;
    const core::NameAtom content = mirror.fixture.world.atoms().intern("asset://models/rock.glb");
    mirror.sync.setCollisionPoints(content, {core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{1.0f, 0.0f, 0.0f},
                                             core::Vec3{0.0f, 1.0f, 0.0f}, core::Vec3{0.0f, 0.0f, 1.0f}});

    const core::InstanceId id = mirror.part("Rock");
    MeshPartComponent mesh;
    mesh.meshContent = content;
    mesh.collisionFidelity = 0;
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
