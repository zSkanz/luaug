#include "luaug/scene/physics_sync_2d.h"

#include "luaug/scene/tile_outline.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace luaug::scene {
namespace {

// The instance id is the body's user data, as it is for the 3D mirror.
[[nodiscard]] u64 packInstance(core::InstanceId id) noexcept
{
    return (static_cast<u64>(id.generation) << 32) | id.index;
}

[[nodiscard]] core::InstanceId unpackInstance(u64 packed) noexcept
{
    return core::InstanceId{static_cast<u32>(packed & 0xffffffffu), static_cast<u32>(packed >> 32)};
}

constexpr f32 DegreesPerRadian = 180.0f / std::numbers::pi_v<f32>;

[[nodiscard]] physics::Shape2DDesc shapeOf(const Part2DComponent& part)
{
    // `Enum.Shape2D`: Box, Circle, Capsule. A size that is not positive is
    // refused by the property, so the halves below are never negative -- but a
    // capsule shorter than it is wide is a circle, and is built as one.
    physics::Shape2DDesc shape;
    const f32 halfWidth = part.size.x * 0.5f;
    const f32 halfHeight = part.size.y * 0.5f;
    switch (part.shape) {
    case 1:
        shape.type = physics::Shape2DType::Circle;
        shape.radius = std::min(halfWidth, halfHeight);
        break;
    case 2:
        shape.type = physics::Shape2DType::Capsule;
        shape.radius = halfWidth;
        shape.halfExtents = core::Vec2{0.0f, std::max(0.0f, halfHeight - halfWidth)};
        if (shape.halfExtents.y <= 0.0f)
            shape.type = physics::Shape2DType::Circle;
        break;
    default:
        shape.type = physics::Shape2DType::Box;
        shape.halfExtents = core::Vec2{halfWidth, halfHeight};
        break;
    }
    return shape;
}

} // namespace

PhysicsSync2D::PhysicsSync2D(World& world, physics::IPhysics2D& backend) : m_scene(world), m_backend(backend)
{
    physics::World2DDesc desc;
    desc.gravity = m_gravity;
    m_world = m_backend.createWorld(desc);
}

PhysicsSync2D::~PhysicsSync2D()
{
    if (m_world.valid())
        m_backend.destroyWorld(m_world);
}

core::u16 PhysicsSync2D::groupOf(core::NameAtom name) const noexcept
{
    // A group past the 64 a 2D world can filter by collides as `Default`
    // rather than as somebody else's group.
    const u16 group = m_scene.collisionGroups().find(name);
    if (group == CollisionGroups::kInvalid || group >= physics::kMaxCollisionGroups2D)
        return CollisionGroups::kDefault;
    return group;
}

void PhysicsSync2D::applyScene(f32 fixedDt)
{
    const CollisionGroups& groups = m_scene.collisionGroups();
    if (groups.revision() != m_groupsRevision) {
        m_groupsRevision = groups.revision();
        const u32 count = std::min<u32>(groups.count(), physics::kMaxCollisionGroups2D);
        for (u32 a = 0; a < count; ++a) {
            for (u32 b = a; b < count; ++b) {
                m_backend.setGroupsCollidable(m_world, static_cast<u16>(a), static_cast<u16>(b),
                                              groups.collidable(static_cast<u16>(a), static_cast<u16>(b)));
            }
        }
    }

    // In the world means under `Workspace`, as it does for a 3D part.
    const auto inWorld = [this](core::InstanceId id) {
        return m_workspace.valid() && m_scene.isAncestorOf(m_workspace, id);
    };

    for (auto& [packed, record] : m_parts)
        record.seen = false;
    m_scene.parts2d().forEach([&](core::InstanceId id, Part2DComponent& part) {
        if (!inWorld(id))
            return;
        const u64 packed = packInstance(id);
        PartRecord& record = m_parts[packed];
        record.seen = true;

        Shape shape;
        shape.size = part.size;
        shape.shape = part.shape;
        shape.anchored = part.anchored;
        shape.canCollide = part.canCollide;
        shape.sensor = part.sensor;
        shape.density = part.density;
        shape.friction = part.friction;
        shape.elasticity = part.elasticity;
        shape.fixedRotation = part.fixedRotation;
        shape.gravityScale = part.gravityScale;
        shape.group = groupOf(part.collisionGroup);

        // Anchored ground is static until a script moves it, and kinematic
        // from then on, so what stands on a moving platform is carried.
        const bool moved =
            record.body.valid() && (part.position != record.position || part.rotation != record.rotation);
        shape.moving = record.shape.moving || (shape.anchored && record.shape.anchored && moved);

        if (!record.body.valid() || !(shape == record.shape)) {
            if (record.body.valid())
                m_backend.destroyBody(m_world, record.body);
            physics::Body2DDesc desc;
            desc.shape = shapeOf(part);
            desc.motion = !shape.anchored ? physics::Motion2D::Dynamic
                          : shape.moving  ? physics::Motion2D::Kinematic
                                          : physics::Motion2D::Static;
            desc.position = part.position;
            desc.angle = part.rotation / DegreesPerRadian;
            if (!shape.anchored) {
                desc.linearVelocity = part.velocity;
                desc.angularVelocity = part.angularVelocity / DegreesPerRadian;
            }
            desc.density = part.density;
            desc.friction = part.friction;
            desc.restitution = part.elasticity;
            desc.gravityScale = part.gravityScale;
            desc.fixedRotation = part.fixedRotation;
            desc.sensor = part.sensor;
            desc.collides = part.canCollide;
            desc.group = shape.group;
            desc.userData = packed;
            record.body = m_backend.createBody(m_world, desc);
            record.shape = shape;
        }
        else if (shape.anchored) {
            if (moved) {
                m_backend.moveKinematic(m_world, record.body, part.position, part.rotation / DegreesPerRadian, fixedDt);
            }
            else if (shape.moving) {
                // Still this tick: a kinematic body keeps the velocity it was
                // last given, and would drift off without being stopped.
                m_backend.setVelocity(m_world, record.body, core::Vec2{0.0f, 0.0f}, 0.0f);
            }
        }
        else {
            if (moved)
                m_backend.setBodyTransform(m_world, record.body, part.position, part.rotation / DegreesPerRadian);
            if (part.velocity != record.velocity || part.angularVelocity != record.angularVelocity)
                m_backend.setVelocity(m_world, record.body, part.velocity, part.angularVelocity / DegreesPerRadian);
        }
        if (part.pendingImpulse != core::Vec2{0.0f, 0.0f}) {
            if (!shape.anchored)
                m_backend.applyImpulse(m_world, record.body, part.pendingImpulse);
            part.pendingImpulse = core::Vec2{0.0f, 0.0f};
        }
        record.position = part.position;
        record.rotation = part.rotation;
        record.velocity = part.velocity;
        record.angularVelocity = part.angularVelocity;
    });
    for (auto at = m_parts.begin(); at != m_parts.end();) {
        if (at->second.seen) {
            ++at;
            continue;
        }
        if (at->second.body.valid())
            m_backend.destroyBody(m_world, at->second.body);
        at = m_parts.erase(at);
    }

    const bool restored = m_scene.restores() != m_restores;
    m_restores = m_scene.restores();
    for (auto& [packed, record] : m_tilemaps) {
        record.seen = false;
        if (restored)
            record.built = false;
    }
    m_scene.tilemaps2d().forEach([&](core::InstanceId id, Tilemap2DComponent& tilemap) {
        if (!inWorld(id))
            return;
        const u64 packed = packInstance(id);
        TilemapRecord& record = m_tilemaps[packed];
        const u16 group = groupOf(tilemap.collisionGroup);
        const bool current =
            record.built && (record.revision == tilemap.revision && record.position == tilemap.position &&
                             record.cellSize == tilemap.cellSize && record.collides == tilemap.collides &&
                             record.friction == tilemap.friction && record.group == group);
        record.seen = true;
        if (current)
            return;
        for (const physics::Body2DHandle body : record.bodies)
            m_backend.destroyBody(m_world, body);
        record.bodies.clear();
        record.revision = tilemap.revision;
        record.position = tilemap.position;
        record.cellSize = tilemap.cellSize;
        record.collides = tilemap.collides;
        record.friction = tilemap.friction;
        record.group = group;
        record.built = true;
        if (!tilemap.collides)
            return;
        // One static body per solid region's outline, in the outline's order.
        for (const std::vector<core::Vec2>& loop : tileOutlines(tilemap)) {
            physics::Body2DDesc desc;
            desc.motion = physics::Motion2D::Static;
            desc.position = tilemap.position;
            desc.shape.type = physics::Shape2DType::Chain;
            desc.shape.loop = true;
            desc.shape.points.reserve(loop.size());
            for (const core::Vec2 corner : loop)
                desc.shape.points.push_back(core::Vec2{corner.x * tilemap.cellSize, corner.y * tilemap.cellSize});
            desc.friction = tilemap.friction;
            desc.group = group;
            desc.userData = packed;
            const physics::Body2DHandle body = m_backend.createBody(m_world, desc);
            if (body.valid())
                record.bodies.push_back(body);
        }
    });
    for (auto at = m_tilemaps.begin(); at != m_tilemaps.end();) {
        if (at->second.seen) {
            ++at;
            continue;
        }
        for (const physics::Body2DHandle body : at->second.bodies)
            m_backend.destroyBody(m_world, body);
        at = m_tilemaps.erase(at);
    }
}

void PhysicsSync2D::writeBack()
{
    // Quietly, straight into the component: the solver moving a part is not a
    // script setting its position, and a signal per falling part per tick is
    // the cost nobody asked for.
    for (auto& [packed, record] : m_parts) {
        if (record.shape.anchored || !record.body.valid())
            continue;
        Part2DComponent* part = m_scene.parts2d().find(unpackInstance(packed));
        if (part == nullptr)
            continue;
        const physics::Body2DState state = m_backend.bodyState(m_world, record.body);
        part->position = state.position;
        part->rotation = state.angle * DegreesPerRadian;
        part->velocity = state.linearVelocity;
        part->angularVelocity = state.angularVelocity * DegreesPerRadian;
        record.position = part->position;
        record.rotation = part->rotation;
        record.velocity = part->velocity;
        record.angularVelocity = part->angularVelocity;
    }
}

void PhysicsSync2D::publishContacts()
{
    const core::NameAtom touched = m_scene.atoms().intern("Touched");
    const core::NameAtom touchEnded = m_scene.atoms().intern("TouchEnded");

    // Both ways, as the 3D mirror does: `Touched` is a fact about each side. A
    // tilemap has no signal of its own, so only the part's side is raised
    // where a part meets the ground.
    for (const physics::Contact2D& contact : m_backend.contacts(m_world)) {
        const core::InstanceId first = unpackInstance(contact.userDataA);
        const core::InstanceId second = unpackInstance(contact.userDataB);
        if (!m_scene.alive(first) || !m_scene.alive(second))
            continue;
        Change change;
        change.kind = ChangeKind::InstanceEvent;
        change.name = contact.kind == physics::Contact2D::Kind::Begin ? touched : touchEnded;
        if (m_scene.parts2d().find(first) != nullptr) {
            change.subject = first;
            change.other = second;
            m_scene.changes().push(change);
        }
        if (m_scene.parts2d().find(second) != nullptr) {
            change.subject = second;
            change.other = first;
            m_scene.changes().push(change);
        }
    }
}

void PhysicsSync2D::step(f64 fixedDt)
{
    if (!m_world.valid() || !m_workspace.valid())
        return;
    // A world with nothing on the plane costs two empty walks and no step.
    if (m_scene.parts2d().size() == 0 && m_scene.tilemaps2d().size() == 0 && m_parts.empty() && m_tilemaps.empty())
        return;

    const auto dt = static_cast<f32>(fixedDt);
    applyScene(dt);
    // The plane's gravity is the workspace's, less the part of it that points
    // along the axis the plane does not have.
    if (const WorkspaceComponent* workspace = m_scene.workspaces().find(m_workspace); workspace != nullptr) {
        const core::Vec2 gravity{workspace->gravity.x, workspace->gravity.y};
        if (gravity != m_gravity) {
            m_gravity = gravity;
            m_backend.setGravity(m_world, gravity);
        }
    }
    m_backend.step(m_world, dt);
    writeBack();
    publishContacts();
}

std::optional<PhysicsSync2D::Hit> PhysicsSync2D::raycast(core::Vec2 origin, core::Vec2 direction,
                                                         const physics::Raycast2DFilter& filter) const
{
    if (!m_world.valid())
        return std::nullopt;
    const std::optional<physics::Raycast2DHit> hit = m_backend.raycast(m_world, origin, direction, filter);
    if (!hit.has_value())
        return std::nullopt;
    const core::InstanceId instance = unpackInstance(hit->userData);
    if (!m_scene.alive(instance))
        return std::nullopt;
    const f64 x = static_cast<f64>(direction.x);
    const f64 y = static_cast<f64>(direction.y);
    const f64 reach = std::sqrt(x * x + y * y);
    return Hit{instance, hit->point, hit->normal, reach * static_cast<f64>(hit->fraction)};
}

u64 PhysicsSync2D::userDataOf(core::InstanceId id) noexcept
{
    return packInstance(id);
}

core::usize PhysicsSync2D::bodyCount() const noexcept
{
    core::usize count = 0;
    for (const auto& [packed, record] : m_parts)
        count += record.body.valid() ? 1 : 0;
    for (const auto& [packed, record] : m_tilemaps)
        count += record.bodies.size();
    return count;
}

} // namespace luaug::scene
