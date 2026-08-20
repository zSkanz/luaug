#include "luaug/scene/physics_sync.h"

#include "luaug/scene/world.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace luaug::scene {
namespace {

// The instance id IS the body's user data. Eight bytes each way, no table, and
// a query hit resolves back to an instance by decoding rather than by looking
// up -- with the liveness check the world already knows how to do.
[[nodiscard]] u64 packInstance(core::InstanceId id) noexcept
{
    return (static_cast<u64>(id.generation) << 32) | id.index;
}

[[nodiscard]] core::InstanceId unpackInstance(u64 packed) noexcept
{
    return core::InstanceId{static_cast<u32>(packed & 0xffffffffu), static_cast<u32>(packed >> 32)};
}

[[nodiscard]] physics::ShapeType shapeForPartShape(i32 shape) noexcept
{
    // `Enum.PartShape`: Block, Ball, Cylinder, Capsule, Wedge. A wedge collides
    // as its bounding box in this release -- the renderer has no wedge either,
    // so nothing yet disagrees about what one looks like.
    switch (shape) {
    case 1:
        return physics::ShapeType::Sphere;
    case 2:
        return physics::ShapeType::Cylinder;
    case 3:
        return physics::ShapeType::Capsule;
    default:
        break;
    }
    return physics::ShapeType::Box;
}

[[nodiscard]] bool sameShape(const physics::ShapeDesc& a, const physics::ShapeDesc& b) noexcept
{
    return a.type == b.type && a.size == b.size;
}

} // namespace

PhysicsSync::PhysicsSync(World& world, physics::IPhysics3D& backend) : m_scene(world), m_backend(backend)
{
    physics::WorldDesc desc;
    m_world = m_backend.createWorld(desc);
}

PhysicsSync::~PhysicsSync()
{
    if (m_world.valid())
        m_backend.destroyWorld(m_world);
}

core::InstanceId PhysicsSync::instanceOf(u64 userData) const noexcept
{
    const core::InstanceId id = unpackInstance(userData);
    return m_scene.alive(id) ? id : core::InstanceId{};
}

u64 PhysicsSync::userDataOf(core::InstanceId id) const noexcept
{
    return packInstance(id);
}

bool PhysicsSync::inWorld(core::InstanceId id) const
{
    // Under `Workspace`, which is what "in the world" means (api-design.md
    // §2.1). A part parented to a Folder under Workspace is in; one parented to
    // nil or to a service that is not Workspace is not.
    if (!m_workspace.valid())
        return false;

    // Answered for the PARENT and remembered for one tick, because the walk is
    // O(depth) and ten thousand parts in one folder ask the identical question
    // ten thousand times. A single-entry memo rather than a map: the parts of a
    // scene arrive in pool order, which is creation order, which groups them by
    // parent for free.
    const core::InstanceId parent = m_scene.parentOf(id);
    if (parent == m_lastParent)
        return m_lastParentInWorld;

    m_lastParent = parent;
    m_lastParentInWorld = parent == m_workspace || m_scene.isAncestorOf(m_workspace, parent);
    return m_lastParentInWorld;
}

physics::ShapeDesc PhysicsSync::shapeOf(core::InstanceId id, const PartComponent& part) const
{
    physics::ShapeDesc shape;
    shape.size = part.size;

    // A `MeshPart` collides as the box or hull its CollisionFidelity asks for.
    // There is no hull yet -- the mesh's points live in the render module,
    // which sits above this one -- so every fidelity collides as the bounding
    // box and the property reads back what was written. That is a defect with a
    // milestone (M7's asset pipeline), not a silent substitution.
    if (const MeshPartComponent* mesh = m_scene.meshParts().find(id); mesh != nullptr) {
        shape.type = physics::ShapeType::Box;
        return shape;
    }
    shape.type = shapeForPartShape(part.shape);
    return shape;
}

physics::BodyDesc PhysicsSync::descOf(core::InstanceId id, const PartComponent& part,
                                      const RigidBodyComponent& body) const
{
    physics::BodyDesc desc;
    desc.shape = shapeOf(id, part);
    desc.transform = part.cframe;
    // A part an active weld drives is Kinematic: it still collides and still
    // pushes what it runs into, and the solver does not move it -- which is what
    // "driven, not simulated" means (roadmap M5).
    desc.motion = isDriven(id)    ? physics::MotionType::Kinematic
                  : body.anchored ? physics::MotionType::Static
                                  : physics::MotionType::Dynamic;
    desc.friction = body.friction;
    desc.restitution = body.restitution;
    desc.density = body.density;
    desc.collidable = body.canCollide;
    desc.queryable = body.canQuery;
    const u16 group = m_scene.collisionGroups().find(body.collisionGroup);
    desc.group = group == CollisionGroups::kInvalid ? CollisionGroups::kDefault : group;
    desc.userData = packInstance(id);
    return desc;
}

void PhysicsSync::syncCollisionGroups()
{
    const CollisionGroups& groups = m_scene.collisionGroups();
    if (groups.revision() == m_groupRevision)
        return;
    m_groupRevision = groups.revision();

    // Pushed wholesale rather than diffed. The table changes when a script
    // registers a group or flips a pair -- twice in a game's life -- and a
    // hundred-entry rewrite then is cheaper to be right about than a diff that
    // runs every tick.
    for (u32 index = 0; index < groups.count(); ++index) {
        const std::string_view name = m_scene.atoms().text(groups.nameAt(static_cast<u16>(index)));
        [[maybe_unused]] const physics::CollisionGroup registered = m_backend.registerCollisionGroup(m_world, name);
    }
    for (u32 a = 0; a < groups.count(); ++a) {
        for (u32 b = a; b < groups.count(); ++b) {
            m_backend.setGroupsCollidable(m_world, static_cast<physics::CollisionGroup>(a),
                                          static_cast<physics::CollisionGroup>(b),
                                          groups.collidable(static_cast<u16>(a), static_cast<u16>(b)));
        }
    }
}

void PhysicsSync::applyBody(core::InstanceId id, PartComponent& part, RigidBodyComponent& body)
{
    if (id.index >= m_bodies.size())
        m_bodies.resize(static_cast<usize>(id.index) + 1);

    BodyRecord& record = m_bodies[id.index];
    const physics::BodyDesc desc = descOf(id, part, body);

    if (record.generation != id.generation) {
        // The slot is empty, or it holds a body that belonged to a different
        // instance in the same slot. Either way this instance has no body yet.
        if (record.generation != 0) {
            m_backend.destroyBody(m_world, record.handle);
            --m_bodyCount;
        }

        const physics::BodyHandle handle = m_backend.createBody(m_world, desc);
        if (!handle.valid()) {
            record = BodyRecord{};
            return;
        }
        record.generation = id.generation;
        record.handle = handle;
        record.shape = desc.shape;
        record.motion = desc.motion;
        record.collidable = desc.collidable;
        record.queryable = desc.queryable;
        record.group = desc.group;
        record.friction = desc.friction;
        record.restitution = desc.restitution;
        record.density = desc.density;
        record.written = part.cframe;
        record.seen = true;
        ++m_bodyCount;
        return;
    }

    record.seen = true;

    // A shape, motion type or density change is a rebuild on the backend's
    // side, so it is one call rather than four setters -- and the handle
    // survives it, because everything above holds one.
    if (!sameShape(record.shape, desc.shape) || record.motion != desc.motion || record.density != desc.density) {
        m_backend.updateBody(m_world, record.handle, desc);
        record.shape = desc.shape;
        record.motion = desc.motion;
        record.density = desc.density;
        record.friction = desc.friction;
        record.restitution = desc.restitution;
        record.collidable = desc.collidable;
        record.queryable = desc.queryable;
        record.group = desc.group;
        record.written = part.cframe;
    }
    else {
        if (record.friction != desc.friction || record.restitution != desc.restitution) {
            m_backend.setBodyMaterial(m_world, record.handle, desc.friction, desc.restitution);
            record.friction = desc.friction;
            record.restitution = desc.restitution;
        }
        if (record.collidable != desc.collidable || record.queryable != desc.queryable) {
            m_backend.setBodyFlags(m_world, record.handle, desc.collidable, desc.queryable);
            record.collidable = desc.collidable;
            record.queryable = desc.queryable;
        }
        if (record.group != desc.group) {
            m_backend.setBodyGroup(m_world, record.handle, desc.group);
            record.group = desc.group;
        }

        // The one place a script's write is told apart from the mirror's own:
        // the component differs from what this mirror last wrote into it, so
        // something else did. No dirty flag on either side.
        if (!(part.cframe == record.written)) {
            m_backend.setBodyTransform(m_world, record.handle, part.cframe);
            record.written = part.cframe;
        }
    }

    if (!(body.pendingImpulse == core::Vec3{0.0f, 0.0f, 0.0f})) {
        m_backend.applyImpulse(m_world, record.handle, body.pendingImpulse);
        body.pendingImpulse = core::Vec3{0.0f, 0.0f, 0.0f};
    }
}

void PhysicsSync::applyCharacter(core::InstanceId id, PartComponent& part, RigidBodyComponent& body,
                                 CharacterBodyComponent& character, f32 fixedDt)
{
    const u64 key = packInstance(id);
    const u16 sceneGroup = m_scene.collisionGroups().find(body.collisionGroup);
    const physics::CollisionGroup group = sceneGroup == CollisionGroups::kInvalid
                                              ? CollisionGroups::kDefault
                                              : static_cast<physics::CollisionGroup>(sceneGroup);

    auto found = m_characters.find(key);
    if (found == m_characters.end() || found->second.height != part.size.y ||
        found->second.diameter != std::max(part.size.x, part.size.z) ||
        found->second.maxSlopeAngle != character.maxSlopeAngle ||
        found->second.stepHeight != character.autoStepHeight || found->second.group != group) {
        // Rebuilt rather than adjusted: a controller's capsule, slope limit and
        // step height are settings it is constructed with, and a character that
        // changes size mid-stride is a rare enough event to pay for.
        if (found != m_characters.end()) {
            m_backend.destroyCharacter(m_world, found->second.handle);
            m_characters.erase(found);
        }

        physics::CharacterDesc desc;
        desc.transform = part.cframe;
        desc.height = part.size.y;
        desc.diameter = std::max(part.size.x, part.size.z);
        desc.maxSlopeAngle = character.maxSlopeAngle;
        desc.stepHeight = character.autoStepHeight;
        desc.group = group;
        desc.userData = key;

        CharacterRecord record;
        record.handle = m_backend.createCharacter(m_world, desc);
        record.height = desc.height;
        record.diameter = desc.diameter;
        record.maxSlopeAngle = desc.maxSlopeAngle;
        record.stepHeight = desc.stepHeight;
        record.group = group;
        record.written = part.cframe;
        record.seen = true;
        if (!record.handle.valid())
            return;
        found = m_characters.emplace(key, record).first;
    }

    CharacterRecord& record = found->second;
    record.seen = true;

    if (!(part.cframe == record.written)) {
        m_backend.setCharacterTransform(m_world, record.handle, part.cframe);
        record.written = part.cframe;
    }

    // The movement model is the caller's and the sweeping is the backend's.
    // Gravity integrates here rather than in the solver because a character
    // controller is not a body the solver knows about.
    const core::Vec3 gravity = m_scene.workspaces().find(m_workspace) != nullptr
                                   ? m_scene.workspaces().find(m_workspace)->gravity
                                   : core::Vec3{0.0f, -9.81f, 0.0f};

    const physics::CharacterState state = m_backend.characterState(m_world, record.handle);
    const bool grounded = state.ground == physics::CharacterGround::Grounded;

    if (grounded && character.verticalVelocity <= 0.0f)
        character.verticalVelocity = 0.0f;
    character.verticalVelocity += gravity.y * fixedDt;

    if (character.jumpRequested) {
        // Ignored in mid-air rather than queued: a jump that fires the moment
        // you land is a jump you did not ask for (the method's Doc).
        if (grounded)
            character.verticalVelocity = character.jumpSpeed;
        character.jumpRequested = false;
    }

    // Horizontal only -- vertical movement is gravity's and Jump's -- and
    // scaled rather than normalised, so a shorter direction walks slower.
    const core::Vec3 horizontal{character.moveDirection.x * character.walkSpeed, 0.0f,
                                character.moveDirection.z * character.walkSpeed};
    const core::Vec3 velocity{horizontal.x, character.verticalVelocity, horizontal.z};
    m_backend.moveCharacter(m_world, record.handle, velocity, fixedDt);

    // Cleared once consumed: a character told nothing stops, which is what
    // `Move`'s Doc promises.
    character.moveDirection = core::Vec3{0.0f, 0.0f, 0.0f};
}

void PhysicsSync::applyScene()
{
    syncCollisionGroups();

    // Cleared every tick: a part reparented between two ticks must not be
    // answered from the previous one's memo.
    m_lastParent = core::InstanceId{};
    m_lastParentInWorld = false;

    for (BodyRecord& record : m_bodies)
        record.seen = false;
    for (auto& entry : m_characters)
        entry.second.seen = false;

    const f32 fixedDt = static_cast<f32>(m_scene.engineState().fixedTimestep);

    // The pool walk is dense and in slot order, which is a pure function of the
    // operation sequence -- so the order bodies are created in, and therefore
    // the order the backend assigns its own ids in, is deterministic (R10).
    m_scene.rigidBodies().forEach([&](core::InstanceId id, RigidBodyComponent& body) {
        PartComponent* part = m_scene.parts().find(id);
        if (part == nullptr || !inWorld(id))
            return;

        if (CharacterBodyComponent* character = m_scene.characterBodies().find(id); character != nullptr) {
            applyCharacter(id, *part, body, *character, fixedDt);
            return;
        }
        applyBody(id, *part, body);
    });

    retireUnseen();
}

bool PhysicsSync::isDriven(core::InstanceId id) const
{
    return std::find(m_drivenParts.begin(), m_drivenParts.end(), id) != m_drivenParts.end();
}

// Welds resolve AFTER the step and the writeback, which is the defined point in
// the tick the roadmap asks for. Before it, a driven part would follow where its
// anchor was last tick and lag by a frame; after it, it follows where the anchor
// ended up this one.
//
// The order within the pass is dependency order, not pool order: `resolveWeld`
// resolves whatever its anchor hangs from before it resolves itself, so a chain
// lands correctly however the pool holds it. Cycles cannot occur -- the property
// setters refuse the write that would create one -- and the recursion is bounded
// by the weld count regardless.
void PhysicsSync::resolveWelds()
{
    m_resolvedWelds.clear();
    m_drivenParts.clear();

    // Slot order, which is a pure function of the operation sequence. It decides
    // nothing about the result, because dependency order does -- what it decides
    // is that two runs walk the same list.
    m_scene.welds().forEach([&](core::InstanceId weldId, WeldComponent& weld) { resolveWeld(weldId, weld); });
}

void PhysicsSync::resolveWeld(core::InstanceId weldId, WeldComponent& weld)
{
    if (std::find(m_resolvedWelds.begin(), m_resolvedWelds.end(), weldId) != m_resolvedWelds.end())
        return;
    m_resolvedWelds.push_back(weldId);

    if (!weld.enabled || !m_scene.alive(weld.part0) || !m_scene.alive(weld.part1))
        return;

    PartComponent* anchor = m_scene.parts().find(weld.part0);
    PartComponent* driven = m_scene.parts().find(weld.part1);
    if (anchor == nullptr || driven == nullptr)
        return;

    // The anchor may itself be driven by another weld. Resolve that one first,
    // so a chain settles in one pass instead of lagging one link per tick.
    m_scene.welds().forEach([&](core::InstanceId otherId, WeldComponent& other) {
        if (otherId != weldId && other.enabled && other.part1 == weld.part0)
            resolveWeld(otherId, other);
    });

    // A constraint reads the relationship off the world the first time it holds;
    // a weld was told it. `C1` is what carries it either way, so the resolver
    // has one formula.
    if (weld.captures && !weld.captured) {
        weld.c1 = core::inverse(driven->cframe) * (anchor->cframe * weld.c0);
        weld.captured = true;
    }

    driven->cframe = (anchor->cframe * weld.c0) * core::inverse(weld.c1);
    m_drivenParts.push_back(weld.part1);
}

void PhysicsSync::retireUnseen()
{
    for (BodyRecord& record : m_bodies) {
        if (record.generation == 0 || record.seen)
            continue;
        m_backend.destroyBody(m_world, record.handle);
        record = BodyRecord{};
        --m_bodyCount;
    }
    for (auto it = m_characters.begin(); it != m_characters.end();) {
        if (it->second.seen) {
            ++it;
            continue;
        }
        m_backend.destroyCharacter(m_world, it->second.handle);
        it = m_characters.erase(it);
    }
}

void PhysicsSync::writeBack()
{
    m_active.clear();
    m_backend.collectActiveBodies(m_world, m_active);

    for (const physics::ActiveBody& active : m_active) {
        const core::InstanceId id = unpackInstance(active.userData);
        if (!m_scene.alive(id))
            continue;

        if (id.index >= m_bodies.size() || m_bodies[id.index].generation != id.generation)
            continue;
        BodyRecord& record = m_bodies[id.index];

        // The QUIET write: straight into the component, with the changed set
        // reaching a listener only if one exists (architecture.md §4). Ten
        // thousand moving parts enqueue nothing while nobody is watching.
        if (PartComponent* part = m_scene.parts().find(id); part != nullptr) {
            part->cframe = active.state.transform;
            record.written = active.state.transform;
        }
        if (RigidBodyComponent* body = m_scene.rigidBodies().find(id); body != nullptr) {
            body->linearVelocity = active.state.linearVelocity;
            body->angularVelocity = active.state.angularVelocity;
            body->active = active.state.active;
        }
    }

    // A body that just went to sleep is no longer in the active list, and its
    // component still says it is moving. One pass over the records fixes that
    // and costs nothing for a world where nothing is asleep.
    // Over what the SCENE says is still moving rather than over every record: a
    // body that just went to sleep has left the active list while its component
    // still says it is moving, and a world where nothing is asleep pays for
    // nothing.
    m_scene.rigidBodies().forEach([&](core::InstanceId id, RigidBodyComponent& body) {
        if (!body.active)
            return;
        const u64 packed = packInstance(id);
        const bool stillActive = std::any_of(m_active.begin(), m_active.end(), [&](const physics::ActiveBody& active) {
            return active.userData == packed;
        });
        if (!stillActive) {
            body.active = false;
            body.linearVelocity = core::Vec3{0.0f, 0.0f, 0.0f};
            body.angularVelocity = core::Vec3{0.0f, 0.0f, 0.0f};
        }
    });

    writeCharacters();
}

void PhysicsSync::writeCharacters()
{
    for (auto& entry : m_characters) {
        const core::InstanceId id = unpackInstance(entry.first);
        CharacterBodyComponent* character = m_scene.characterBodies().find(id);
        PartComponent* part = m_scene.parts().find(id);
        if (character == nullptr || part == nullptr)
            continue;

        const physics::CharacterState state = m_backend.characterState(m_world, entry.second.handle);
        part->cframe = state.transform;
        entry.second.written = state.transform;

        const bool grounded = state.ground == physics::CharacterGround::Grounded;
        const core::InstanceId ground =
            state.groundUserData == 0 ? core::InstanceId{} : instanceOf(state.groundUserData);

        // Landing is a transition, not a state: airborne last tick and grounded
        // now. Reading the flag alone would fire it every tick a character
        // stands still.
        if (grounded && !character->grounded) {
            Change change;
            change.kind = ChangeKind::InstanceEvent;
            change.subject = id;
            change.other = ground;
            change.name = m_scene.atoms().intern("Landed");
            m_scene.changes().push(change);
        }

        character->grounded = grounded;
        character->state = grounded ? 0 : 1;
        character->groundPart = ground;
        if (RigidBodyComponent* body = m_scene.rigidBodies().find(id); body != nullptr) {
            body->linearVelocity = state.linearVelocity;
            body->active = true;
        }
    }
}

void PhysicsSync::publishContacts()
{
    const core::NameAtom touched = m_scene.atoms().intern("Touched");
    const core::NameAtom touchEnded = m_scene.atoms().intern("TouchEnded");

    // Both directions, because `Touched` is a fact about each part and a script
    // connects to one of them without knowing which side of the pair it is.
    for (const physics::ContactEvent& event : m_backend.drainContacts(m_world)) {
        const core::InstanceId first = unpackInstance(event.firstUserData);
        const core::InstanceId second = unpackInstance(event.secondUserData);
        if (!m_scene.alive(first) || !m_scene.alive(second))
            continue;

        const core::NameAtom name = event.phase == physics::ContactPhase::Began ? touched : touchEnded;

        Change change;
        change.kind = ChangeKind::InstanceEvent;
        change.name = name;

        change.subject = first;
        change.other = second;
        m_scene.changes().push(change);

        change.subject = second;
        change.other = first;
        m_scene.changes().push(change);
    }
}

void PhysicsSync::step(f64 fixedDt)
{
    if (!m_world.valid())
        return;

    // `Workspace` is handed in by the host rather than looked up here: `scene`
    // has no notion of the DataModel root, and the host already resolves it for
    // the renderer. An invalid id means no world root, which means nothing has
    // a body -- and that is exactly the M4.5 defect shape, so the host has a
    // test asserting it resolved (`world_host_tests.cpp`).
    if (!m_workspace.valid())
        return;

    const auto begin = std::chrono::steady_clock::now();
    applyScene();
    const auto applied = std::chrono::steady_clock::now();

    if (const WorkspaceComponent* workspace = m_scene.workspaces().find(m_workspace); workspace != nullptr)
        m_backend.setGravity(m_world, workspace->gravity);

    m_backend.step(m_world, static_cast<f32>(fixedDt));
    const auto stepped = std::chrono::steady_clock::now();

    writeBack();
    // After the writeback, so a driven part follows where its anchor ENDED UP
    // this tick rather than where it was at the start of it.
    resolveWelds();
    publishContacts();
    const auto end = std::chrono::steady_clock::now();

    m_timings.apply = std::chrono::duration<f64>(applied - begin).count();
    m_timings.step = std::chrono::duration<f64>(stepped - applied).count();
    m_timings.writeback = std::chrono::duration<f64>(end - stepped).count();
}

} // namespace luaug::scene
