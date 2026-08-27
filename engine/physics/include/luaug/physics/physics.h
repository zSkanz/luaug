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

    // --- The floating origin (ADR 0014, architecture.md §10) ------------------
    //
    // Every position on this interface is ABSOLUTE, in the f64 world
    // coordinates `scene` stores and a script reads. The origin is what the
    // backend subtracts to get the f32 space it simulates in, and moving it is
    // how a world stays precise ten thousand kilometres out.
    //
    // **Per world and never a global**, which ADR 0014 says twice: a future
    // server running several simulation regions gives each its own origin, and
    // nothing about this interface would change.
    //
    // A rebase moves every resident body and character by the negative of the
    // delta, preserving velocity, so **nothing moves in absolute terms**. The
    // caller decides WHEN -- the trigger is a focus leaving the tolerance, which
    // is streaming's business rather than the solver's -- and the caller must do
    // it at a safe point, because a rebase mid-tick would move the world under a
    // solver that is halfway through it.
    virtual void setWorldOrigin(WorldHandle world, core::DVec3 origin) = 0;
    [[nodiscard]] virtual core::DVec3 worldOrigin(WorldHandle world) const = 0;

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
    // False when the backend refused the new description and the body is
    // therefore still the one it was: a degenerate hull that no convex builder
    // can close is the case that reaches this, and it used to be silent.
    //
    // **The caller records what it ATTEMPTED either way**, so a refusal costs
    // one attempt rather than one per tick -- and `ShapeDesc::pointsRevision` is
    // what makes the next genuinely different description try again.
    [[nodiscard]] virtual bool updateBody(WorldHandle world, BodyHandle body, const BodyDesc& desc) = 0;

    // **Rewrites a rectangle of a `HeightField` body's heights in place**
    // (ADR 0066), which is the only in-place shape edit this seam has and the
    // reason a height field is worth being its own kind.
    //
    // `updateBody` above is `RemoveBody` + `DestroyBody` + `CreateAndAddBody`:
    // a new Jolt body, a broadphase removal and insertion, and every constraint
    // on it rebuilt. This changes the samples of the shape that is already
    // there -- same shape object, same underlying body, nothing to re-insert --
    // which is what makes dragging a brush across ground affordable at all.
    //
    // **`x`, `z`, `sizeX` and `sizeZ` must be multiples of the body's
    // `heightBlockSize`**, and the rectangle must lie inside the grid. That is
    // Jolt's rule rather than this seam's: it asserts on a misaligned start, so
    // a caller grows a brush's affected area outward to a block boundary before
    // calling. A description this cannot honour returns false and changes
    // nothing.
    //
    // `heights` is `sizeX * sizeZ` samples in row order, and like every other
    // span in this module it must outlive the call and no longer.
    [[nodiscard]] virtual bool updateHeightField(WorldHandle world, BodyHandle body, u32 x, u32 z, u32 sizeX, u32 sizeZ,
                                                 std::span<const float> heights) = 0;

    // Friction, restitution, collidability, queryability and group, none of
    // which need the body rebuilt.
    virtual void setBodyMaterial(WorldHandle world, BodyHandle body, f32 friction, f32 restitution) = 0;
    virtual void setBodyFlags(WorldHandle world, BodyHandle body, bool collidable, bool queryable) = 0;
    virtual void setBodyGroup(WorldHandle world, BodyHandle body, CollisionGroup group) = 0;

    [[nodiscard]] virtual BodyState bodyState(WorldHandle world, BodyHandle body) const = 0;

    // Every body the last `step` moved, in a stable order (see `ActiveBody`).
    // Appends; the caller owns clearing.
    virtual void collectActiveBodies(WorldHandle world, std::vector<ActiveBody>& out) const = 0;

    // --- Constraints ---------------------------------------------------------
    //
    // The body lifecycle, mirrored, and with two ordering rules that are part of
    // the contract rather than of one backend:
    //
    //   * **A constraint is destroyed before either body it holds.** A joint
    //     referencing a body that is gone is a dangling pointer inside the
    //     solver, and it is silent -- so `destroyBody` drops the constraints on
    //     that body itself, and a caller sweeping both must retire constraints
    //     first anyway or it will destroy them twice.
    //   * **`updateBody` keeps the constraints on the body.** Changing a shape
    //     or a motion type rebuilds the body underneath; the constraints are
    //     rebuilt with it, in their original creation order, so a resized limb
    //     stays attached and the solve order does not change.
    //
    // Creation order is the solve order in every backend worth having, so a
    // caller that needs determinism must create constraints in a stable order
    // (R10). This interface does not sort them: it cannot know what order the
    // caller meant.

    // An invalid handle when either body is invalid, when both name the same
    // body, or when the world is full.
    [[nodiscard]] virtual ConstraintHandle createConstraint(WorldHandle world, const ConstraintDesc& desc) = 0;
    virtual void destroyConstraint(WorldHandle world, ConstraintHandle constraint) = 0;

    // A disabled constraint stays in the world and holds nothing, which is what
    // a ragdoll turning itself off and on again needs -- rebuilding the joints
    // would change their order and, with it, the simulation.
    virtual void setConstraintEnabled(WorldHandle world, ConstraintHandle constraint, bool enabled) = 0;

    // Limits, motor and `collideConnected` may all change; the two bodies and
    // the type may not. A caller that needs those rebuilds.
    virtual void updateConstraint(WorldHandle world, ConstraintHandle constraint, const ConstraintDesc& desc) = 0;

    [[nodiscard]] virtual ConstraintState constraintState(WorldHandle world, ConstraintHandle constraint) const = 0;

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
