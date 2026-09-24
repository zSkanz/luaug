// The Box2D backend of `IPhysics2D` (ADR 0008).
//
// **One thread, on purpose.** Box2D runs its solver on worker tasks only when
// it is handed a task system; handed none, it steps on the calling thread, in
// an order that is a function of the world. That is the determinism the rest of
// the engine is built on (R10, ADR 0083), and a 2D world's bodies are few
// enough that the threads would buy little. Box2D brings its own sine and
// cosine for the same reason.
#include "luaug/physics/backends.h"
#include "luaug/physics/physics2d.h"

#include <algorithm>
#include <array>
#include <box2d/box2d.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace luaug::physics {
namespace {

[[nodiscard]] b2Vec2 toBox(core::Vec2 v) noexcept
{
    return b2Vec2{v.x, v.y};
}

[[nodiscard]] core::Vec2 fromBox(b2Vec2 v) noexcept
{
    return core::Vec2{v.x, v.y};
}

[[nodiscard]] void* packUser(u64 value) noexcept
{
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(value));
}

[[nodiscard]] u64 unpackUser(void* value) noexcept
{
    return static_cast<u64>(reinterpret_cast<std::uintptr_t>(value));
}

struct BodyRecord
{
    b2BodyId id = b2_nullBodyId;
    u32 generation = 0;
    bool alive = false;
    CollisionGroup2D group = kDefaultCollisionGroup2D;
};

struct WorldRecord
{
    b2WorldId id = b2_nullWorldId;
    u32 generation = 0;
    bool alive = false;
    std::vector<BodyRecord> bodies;
    std::vector<u32> freeBodies;
    std::vector<Contact2D> contacts;
    // Which groups each group collides with: a bit per group, all set.
    std::array<u64, kMaxCollisionGroups2D> collides{};

    [[nodiscard]] b2Filter filterFor(CollisionGroup2D group) const noexcept
    {
        b2Filter filter = b2DefaultFilter();
        const u32 slot = std::min<u32>(group, kMaxCollisionGroups2D - 1);
        filter.categoryBits = u64{1} << slot;
        filter.maskBits = collides[slot];
        return filter;
    }
};

class Box2DPhysics final : public IPhysics2D
{
public:
    World2DHandle createWorld(const World2DDesc& desc) override
    {
        b2WorldDef def = b2DefaultWorldDef();
        def.gravity = toBox(desc.gravity);
        u32 index = 0;
        if (!m_freeWorlds.empty()) {
            index = m_freeWorlds.back();
            m_freeWorlds.pop_back();
        }
        else {
            index = static_cast<u32>(m_worlds.size());
            m_worlds.emplace_back();
        }
        WorldRecord& record = m_worlds[index];
        record = WorldRecord{};
        record.id = b2CreateWorld(&def);
        record.generation = ++m_generation;
        record.alive = true;
        record.collides.fill(~u64{0});
        return World2DHandle{index, record.generation};
    }

    void destroyWorld(World2DHandle world) override
    {
        WorldRecord* record = resolve(world);
        if (record == nullptr)
            return;
        b2DestroyWorld(record->id);
        record->alive = false;
        record->bodies.clear();
        record->contacts.clear();
        m_freeWorlds.push_back(world.index);
    }

    void setGravity(World2DHandle world, core::Vec2 gravity) override
    {
        if (WorldRecord* record = resolve(world); record != nullptr)
            b2World_SetGravity(record->id, toBox(gravity));
    }

    void step(World2DHandle world, f32 fixedDt) override
    {
        WorldRecord* record = resolve(world);
        if (record == nullptr)
            return;
        // Four sub-steps: Box2D's own recommendation, and what makes a stack
        // of boxes stand at a sixtieth of a second.
        b2World_Step(record->id, fixedDt, 4);

        record->contacts.clear();
        const b2ContactEvents touches = b2World_GetContactEvents(record->id);
        for (int at = 0; at < touches.beginCount; ++at)
            push(*record, Contact2D::Kind::Begin, touches.beginEvents[at].shapeIdA, touches.beginEvents[at].shapeIdB,
                 false);
        for (int at = 0; at < touches.endCount; ++at)
            push(*record, Contact2D::Kind::End, touches.endEvents[at].shapeIdA, touches.endEvents[at].shapeIdB, false);
        const b2SensorEvents sensors = b2World_GetSensorEvents(record->id);
        for (int at = 0; at < sensors.beginCount; ++at)
            push(*record, Contact2D::Kind::Begin, sensors.beginEvents[at].sensorShapeId,
                 sensors.beginEvents[at].visitorShapeId, true);
        for (int at = 0; at < sensors.endCount; ++at)
            push(*record, Contact2D::Kind::End, sensors.endEvents[at].sensorShapeId,
                 sensors.endEvents[at].visitorShapeId, true);
    }

    Body2DHandle createBody(World2DHandle world, const Body2DDesc& desc) override
    {
        WorldRecord* record = resolve(world);
        if (record == nullptr)
            return {};

        b2BodyDef def = b2DefaultBodyDef();
        def.type = desc.motion == Motion2D::Static      ? b2_staticBody
                   : desc.motion == Motion2D::Kinematic ? b2_kinematicBody
                                                        : b2_dynamicBody;
        def.position = toBox(desc.position);
        def.rotation = b2MakeRot(desc.angle);
        def.linearVelocity = toBox(desc.linearVelocity);
        def.angularVelocity = desc.angularVelocity;
        def.linearDamping = desc.linearDamping;
        def.angularDamping = desc.angularDamping;
        def.gravityScale = desc.gravityScale;
        def.fixedRotation = desc.fixedRotation;
        def.isBullet = desc.bullet;
        def.userData = packUser(desc.userData);
        const b2BodyId body = b2CreateBody(record->id, &def);
        if (!b2Body_IsValid(body))
            return {};

        b2ShapeDef shape = b2DefaultShapeDef();
        shape.userData = packUser(desc.userData);
        shape.density = desc.density;
        shape.material.friction = desc.friction;
        shape.material.restitution = desc.restitution;
        shape.isSensor = desc.sensor;
        shape.enableSensorEvents = true;
        shape.enableContactEvents = true;
        shape.filter = record->filterFor(desc.group);
        if (!addShape(body, shape, desc.shape)) {
            b2DestroyBody(body);
            return {};
        }

        u32 index = 0;
        if (!record->freeBodies.empty()) {
            index = record->freeBodies.back();
            record->freeBodies.pop_back();
        }
        else {
            index = static_cast<u32>(record->bodies.size());
            record->bodies.emplace_back();
        }
        BodyRecord& held = record->bodies[index];
        held.id = body;
        held.generation = ++m_generation;
        held.alive = true;
        held.group = desc.group;
        return Body2DHandle{index, held.generation};
    }

    void destroyBody(World2DHandle world, Body2DHandle body) override
    {
        WorldRecord* record = resolve(world);
        BodyRecord* held = record != nullptr ? resolve(*record, body) : nullptr;
        if (held == nullptr)
            return;
        b2DestroyBody(held->id);
        held->alive = false;
        record->freeBodies.push_back(body.index);
    }

    void setBodyTransform(World2DHandle world, Body2DHandle body, core::Vec2 position, f32 angle) override
    {
        if (const BodyRecord* held = bodyOf(world, body); held != nullptr)
            b2Body_SetTransform(held->id, toBox(position), b2MakeRot(angle));
    }

    void moveKinematic(World2DHandle world, Body2DHandle body, core::Vec2 position, f32 angle, f32 fixedDt) override
    {
        if (const BodyRecord* held = bodyOf(world, body); held != nullptr) {
            b2Transform target;
            target.p = toBox(position);
            target.q = b2MakeRot(angle);
            b2Body_SetTargetTransform(held->id, target, fixedDt);
        }
    }

    void setVelocity(World2DHandle world, Body2DHandle body, core::Vec2 linear, f32 angular) override
    {
        if (const BodyRecord* held = bodyOf(world, body); held != nullptr) {
            b2Body_SetLinearVelocity(held->id, toBox(linear));
            b2Body_SetAngularVelocity(held->id, angular);
        }
    }

    void applyImpulse(World2DHandle world, Body2DHandle body, core::Vec2 impulse) override
    {
        if (const BodyRecord* held = bodyOf(world, body); held != nullptr)
            b2Body_ApplyLinearImpulseToCenter(held->id, toBox(impulse), true);
    }

    [[nodiscard]] Body2DState bodyState(World2DHandle world, Body2DHandle body) const override
    {
        Body2DState state;
        const BodyRecord* held = bodyOf(world, body);
        if (held == nullptr)
            return state;
        state.position = fromBox(b2Body_GetPosition(held->id));
        state.angle = b2Rot_GetAngle(b2Body_GetRotation(held->id));
        state.linearVelocity = fromBox(b2Body_GetLinearVelocity(held->id));
        state.angularVelocity = b2Body_GetAngularVelocity(held->id);
        state.awake = b2Body_IsAwake(held->id);
        return state;
    }

    [[nodiscard]] std::span<const Contact2D> contacts(World2DHandle world) const override
    {
        const WorldRecord* record = resolve(world);
        return record != nullptr ? std::span<const Contact2D>(record->contacts) : std::span<const Contact2D>{};
    }

    [[nodiscard]] std::optional<Raycast2DHit> raycast(World2DHandle world, core::Vec2 origin, core::Vec2 translation,
                                                      u64 ignored) const override
    {
        const WorldRecord* record = resolve(world);
        if (record == nullptr)
            return std::nullopt;
        b2QueryFilter filter = b2DefaultQueryFilter();
        filter.maskBits = ~ignored;
        const b2RayResult result = b2World_CastRayClosest(record->id, toBox(origin), toBox(translation), filter);
        if (!result.hit || !b2Shape_IsValid(result.shapeId))
            return std::nullopt;
        Raycast2DHit hit;
        hit.userData = unpackUser(b2Shape_GetUserData(result.shapeId));
        hit.point = fromBox(result.point);
        hit.normal = fromBox(result.normal);
        hit.fraction = result.fraction;
        return hit;
    }

    void setGroupsCollidable(World2DHandle world, CollisionGroup2D a, CollisionGroup2D b, bool collidable) override
    {
        WorldRecord* record = resolve(world);
        if (record == nullptr || a >= kMaxCollisionGroups2D || b >= kMaxCollisionGroups2D)
            return;
        const auto set = [&](CollisionGroup2D row, CollisionGroup2D column) {
            if (collidable)
                record->collides[row] |= u64{1} << column;
            else
                record->collides[row] &= ~(u64{1} << column);
        };
        set(a, b);
        set(b, a);
        // Every body already made takes the new rule, in body order.
        for (const BodyRecord& held : record->bodies) {
            if (!held.alive || (held.group != a && held.group != b))
                continue;
            std::array<b2ShapeId, 4> shapes{};
            const int count = b2Body_GetShapes(held.id, shapes.data(), static_cast<int>(shapes.size()));
            for (int at = 0; at < count; ++at)
                b2Shape_SetFilter(shapes[static_cast<std::size_t>(at)], record->filterFor(held.group));
        }
    }

private:
    [[nodiscard]] static bool addShape(b2BodyId body, const b2ShapeDef& def, const Shape2DDesc& shape)
    {
        const b2Rot rotation = b2MakeRot(shape.angle);
        switch (shape.type) {
        case Shape2DType::Box: {
            const b2Polygon box = b2MakeOffsetBox(std::max(shape.halfExtents.x, 0.005f),
                                                  std::max(shape.halfExtents.y, 0.005f), toBox(shape.offset), rotation);
            return b2Shape_IsValid(b2CreatePolygonShape(body, &def, &box));
        }
        case Shape2DType::Circle: {
            b2Circle circle;
            circle.center = toBox(shape.offset);
            circle.radius = std::max(shape.radius, 0.005f);
            return b2Shape_IsValid(b2CreateCircleShape(body, &def, &circle));
        }
        case Shape2DType::Capsule: {
            const b2Vec2 along = b2RotateVector(rotation, b2Vec2{0.0f, std::max(shape.halfExtents.y, 0.0f)});
            b2Capsule capsule;
            capsule.center1 = b2Add(toBox(shape.offset), along);
            capsule.center2 = b2Sub(toBox(shape.offset), along);
            capsule.radius = std::max(shape.radius, 0.005f);
            return b2Shape_IsValid(b2CreateCapsuleShape(body, &def, &capsule));
        }
        case Shape2DType::Polygon: {
            std::vector<b2Vec2> points;
            points.reserve(shape.points.size());
            for (const core::Vec2 point : shape.points)
                points.push_back(toBox(point));
            const b2Hull hull = b2ComputeHull(points.data(), static_cast<int>(std::min<std::size_t>(points.size(), 8)));
            if (hull.count == 0)
                return false;
            const b2Polygon polygon = b2MakePolygon(&hull, 0.0f);
            return b2Shape_IsValid(b2CreatePolygonShape(body, &def, &polygon));
        }
        case Shape2DType::Chain: {
            // An open chain's first and last points are ghosts: they shape the
            // collision at its ends and are not themselves a segment.
            if (shape.points.size() < 4)
                return false;
            std::vector<b2Vec2> points;
            points.reserve(shape.points.size());
            for (const core::Vec2 point : shape.points)
                points.push_back(toBox(point));
            b2ChainDef chain = b2DefaultChainDef();
            chain.userData = def.userData;
            chain.points = points.data();
            chain.count = static_cast<int>(points.size());
            chain.materials = &def.material;
            chain.materialCount = 1;
            chain.filter = def.filter;
            chain.enableSensorEvents = true;
            return B2_IS_NON_NULL(b2CreateChain(body, &chain));
        }
        }
        return false;
    }

    static void push(WorldRecord& record, Contact2D::Kind kind, b2ShapeId a, b2ShapeId b, bool sensor)
    {
        // A shape destroyed this step still reports its end; there is nothing
        // left to say whose it was.
        if (!b2Shape_IsValid(a) || !b2Shape_IsValid(b))
            return;
        Contact2D contact;
        contact.kind = kind;
        contact.userDataA = unpackUser(b2Shape_GetUserData(a));
        contact.userDataB = unpackUser(b2Shape_GetUserData(b));
        contact.sensor = sensor;
        record.contacts.push_back(contact);
    }

    [[nodiscard]] WorldRecord* resolve(World2DHandle world) noexcept
    {
        if (world.index >= m_worlds.size())
            return nullptr;
        WorldRecord& record = m_worlds[world.index];
        return record.alive && record.generation == world.generation ? &record : nullptr;
    }

    [[nodiscard]] const WorldRecord* resolve(World2DHandle world) const noexcept
    {
        if (world.index >= m_worlds.size())
            return nullptr;
        const WorldRecord& record = m_worlds[world.index];
        return record.alive && record.generation == world.generation ? &record : nullptr;
    }

    [[nodiscard]] static BodyRecord* resolve(WorldRecord& record, Body2DHandle body) noexcept
    {
        if (body.index >= record.bodies.size())
            return nullptr;
        BodyRecord& held = record.bodies[body.index];
        return held.alive && held.generation == body.generation ? &held : nullptr;
    }

    [[nodiscard]] const BodyRecord* bodyOf(World2DHandle world, Body2DHandle body) const noexcept
    {
        const WorldRecord* record = resolve(world);
        if (record == nullptr || body.index >= record->bodies.size())
            return nullptr;
        const BodyRecord& held = record->bodies[body.index];
        return held.alive && held.generation == body.generation ? &held : nullptr;
    }

    std::vector<WorldRecord> m_worlds;
    std::vector<u32> m_freeWorlds;
    u32 m_generation = 0;
};

} // namespace

Physics2DResult createBox2DPhysics()
{
    return std::make_unique<Box2DPhysics>();
}

} // namespace luaug::physics
