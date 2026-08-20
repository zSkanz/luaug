// The 3D physics seam (architecture.md §2, ADR 0007).
//
// One implementation exists in v1 and the interface is still virtual, because
// ADR 0023 makes a backend a build-time selection and architecture.md §2 names
// the second one already: Box3D, when its double precision and cross-platform
// determinism reach 1.0. The seam is also what R17 rests on -- a Jolt type
// cannot reach the Luau bindings if the only physics header those bindings can
// include is this one.
//
// Multi-world by design, and the reason is written down (architecture.md §10):
// a physics world is never a global, so a future dedicated server can run
// several. v1 creates exactly one, and every call still names which.
//
// **Threading.** Every method here is called from the simulation thread and
// from nowhere else. The backend may use worker threads inside `step`; nothing
// it does there is visible until `step` returns, and no callback it makes on a
// worker thread reaches a caller (see `drainContacts`).
#pragma once

#include "luaug/core/error.h"
#include "luaug/physics/types.h"

#include <span>
#include <string_view>
#include <vector>

namespace luaug::physics {

class IPhysics3D
{
public:
    virtual ~IPhysics3D() = default;

    // --- Worlds --------------------------------------------------------------

    [[nodiscard]] virtual WorldHandle createWorld(const WorldDesc& desc) = 0;
    virtual void destroyWorld(WorldHandle world) = 0;
    virtual void setGravity(WorldHandle world, core::Vec3 gravity) = 0;

    // --- Bodies --------------------------------------------------------------

    // An invalid handle when the world is full or the shape is degenerate. The
    // caller raises the key; this module has no catalog.
    [[nodiscard]] virtual BodyHandle createBody(WorldHandle world, const BodyDesc& desc) = 0;
    virtual void destroyBody(WorldHandle world, BodyHandle body) = 0;

    // Teleports. Velocity is preserved, which is what the floating-origin
    // rebase at M7 needs and what a script assigning `CFrame` expects.
    virtual void setBodyTransform(WorldHandle world, BodyHandle body, const core::CFrameD& transform) = 0;
    virtual void setBodyVelocity(WorldHandle world, BodyHandle body, core::Vec3 linear, core::Vec3 angular) = 0;
    virtual void applyImpulse(WorldHandle world, BodyHandle body, core::Vec3 impulse) = 0;

    // Changing the motion type or the shape is a recreate on Jolt's side, so it
    // is one call rather than a setter per field: a body that must be rebuilt
    // keeps its handle, its user data and its place in the world.
    virtual void updateBody(WorldHandle world, BodyHandle body, const BodyDesc& desc) = 0;

    // Friction, restitution, collidability, queryability and group, none of
    // which need the body rebuilt.
    virtual void setBodyMaterial(WorldHandle world, BodyHandle body, f32 friction, f32 restitution) = 0;
    virtual void setBodyFlags(WorldHandle world, BodyHandle body, bool collidable, bool queryable) = 0;
    virtual void setBodyGroup(WorldHandle world, BodyHandle body, CollisionGroup group) = 0;

    [[nodiscard]] virtual BodyState bodyState(WorldHandle world, BodyHandle body) const = 0;

    // Every body the last `step` moved, in a stable order (see `ActiveBody`).
    // Appends; the caller owns clearing.
    virtual void collectActiveBodies(WorldHandle world, std::vector<ActiveBody>& out) const = 0;

    // --- Simulation ----------------------------------------------------------

    // One fixed step. `fixedDt` is the sim tick, never a wall-clock delta
    // (R10): the caller owns the accumulator and this owns the integration.
    virtual void step(WorldHandle world, f32 fixedDt) = 0;

    // The contacts that began or ended during the last `step`, in a stable
    // order. Valid until the next `step` on this world.
    //
    // Deliberately a drain rather than a callback: a backend's contact listener
    // runs inside its own update and, once a job system is wired at M7, on a
    // worker thread. Nothing there may touch the world or decide an order, so
    // the backend buffers and this hands the buffer over afterwards.
    [[nodiscard]] virtual std::span<const ContactEvent> drainContacts(WorldHandle world) = 0;

    [[nodiscard]] virtual StepTimings lastStepTimings(WorldHandle world) const = 0;

    // --- Queries -------------------------------------------------------------

    // The closest hit, or false. Ties are broken deterministically by the
    // backend, because a query whose answer depends on traversal order is a
    // replay divergence waiting for a body count to change (R10).
    [[nodiscard]] virtual bool raycast(WorldHandle world, const RayD& ray, const QueryFilter& filter,
                                       RayHit& outHit) const = 0;

    [[nodiscard]] virtual bool spherecast(WorldHandle world, const RayD& ray, f32 radius, const QueryFilter& filter,
                                          RayHit& outHit) const = 0;

    // Every body overlapping the oriented box, by user data, in a stable order.
    // Appends.
    virtual void overlapBox(WorldHandle world, const core::CFrameD& transform, core::Vec3 size,
                            const QueryFilter& filter, std::vector<u64>& out) const = 0;

    // --- Characters ----------------------------------------------------------

    [[nodiscard]] virtual CharacterHandle createCharacter(WorldHandle world, const CharacterDesc& desc) = 0;
    virtual void destroyCharacter(WorldHandle world, CharacterHandle character) = 0;

    // `velocity` is the character's desired velocity for this step, in world
    // space, gravity included -- the caller owns the movement model and this
    // owns the sweeping. Called once per sim tick per character.
    virtual void moveCharacter(WorldHandle world, CharacterHandle character, core::Vec3 velocity, f32 fixedDt) = 0;
    virtual void setCharacterTransform(WorldHandle world, CharacterHandle character,
                                       const core::CFrameD& transform) = 0;
    [[nodiscard]] virtual CharacterState characterState(WorldHandle world, CharacterHandle character) const = 0;

    // --- Collision groups ----------------------------------------------------

    // Returns the existing group when the name is already registered, which is
    // what makes `RegisterCollisionGroup` idempotent across a hot reload. An
    // invalid group (0xffff) when the table is full.
    [[nodiscard]] virtual CollisionGroup registerCollisionGroup(WorldHandle world, std::string_view name) = 0;
    [[nodiscard]] virtual CollisionGroup findCollisionGroup(WorldHandle world, std::string_view name) const = 0;
    virtual void setGroupsCollidable(WorldHandle world, CollisionGroup a, CollisionGroup b, bool collidable) = 0;
    [[nodiscard]] virtual bool groupsCollidable(WorldHandle world, CollisionGroup a, CollisionGroup b) const = 0;
    // Registration order, which is deterministic; never a hash order (R10).
    virtual void collectCollisionGroups(WorldHandle world, std::vector<std::string_view>& out) const = 0;

    // --- The rollback-oriented seam (ADR 0016) -------------------------------
    //
    // Declared and unimplemented, and that is the decision rather than an
    // omission: v1 does not do rollback (ADR 0016 calls these foundations, not
    // rollback), and a half-written snapshot that silently dropped contact
    // caches would be worse than a refusal. A backend that cannot do it returns
    // false; nothing in v1 calls these.
    [[nodiscard]] virtual bool saveState(WorldHandle world, std::vector<u8>& out) const = 0;
    [[nodiscard]] virtual bool restoreState(WorldHandle world, std::span<const u8> blob) = 0;

    // --- Debug ---------------------------------------------------------------

    // Draws the shapes the simulation actually holds. Does nothing when the
    // build compiled the backend's debug renderer out.
    virtual void debugDraw(WorldHandle world, IDebugDrawSink& sink) = 0;
};

} // namespace luaug::physics
