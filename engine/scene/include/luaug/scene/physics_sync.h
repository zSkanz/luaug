// The mirror between the Instance tree and the simulation (architecture.md §2:
// "scene ... built-in systems (transform hierarchy, physics sync via an
// injected `IPhysics3D*`)").
//
// **The tree is the authority and the body is a mirror of it.** A `BasePart`
// under `Workspace` has a body; one taken out of the tree loses it. `scene`
// never learns a body's identity beyond an opaque handle and `physics` never
// learns what an Instance is -- the map between them lives here and nowhere
// else, which is why neither module includes the other's vocabulary.
//
// Writes cross in both directions and each direction has exactly one legal
// point in the frame:
//
//   * **Script to physics** is collected while the script runs and applied at
//     the start of the sim tick. A script that sets `CFrame` mid-frame must not
//     teleport a body the solver is in the middle of.
//   * **Physics to script** happens after the step, as a QUIET write with a
//     batched changed set (architecture.md §4). That is the path the
//     10k-parts benchmark measures; any other route would silently forfeit the
//     equality filter that is worth about a third of it.
//
// Nothing here reads a clock. `step` takes the fixed tick and the caller owns
// the accumulator (R10).
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/types.h"
#include "luaug/physics/physics.h"
#include "luaug/scene/components.h"

#include <unordered_map>
#include <utility>
#include <vector>

namespace luaug::scene {

class World;

class PhysicsSync
{
public:
    PhysicsSync(World& world, physics::IPhysics3D& backend);
    ~PhysicsSync();

    PhysicsSync(const PhysicsSync&) = delete;
    PhysicsSync& operator=(const PhysicsSync&) = delete;

    // `Workspace`, handed in by the host. `scene` has no notion of the
    // DataModel root, and the host already resolves it for the renderer; an
    // invalid id means nothing in this world has a body, which is the shape of
    // the defect that cost M4 a milestone, so the host tests that it resolved.
    void setWorkspace(core::InstanceId workspace) noexcept { m_workspace = workspace; }

    // The points a `MeshPart` with a hull fidelity collides as, keyed by the
    // content id its `MeshContent` names.
    //
    // PUSHED by whoever loaded the mesh rather than pulled from here, because
    // this module is L3 and a mesh file is the asset system's (L2) to read
    // through mounts the app (L6) owns. The app is the only place that can see
    // both, which is the same arrangement the UI's font and image providers use.
    //
    // Positions only, and every one of them: a convex hull builder discards the
    // interior points itself, and a sampled subset would produce a hull SMALLER
    // than the mesh -- which is a character sinking into a rock rather than a
    // slightly wrong shape.
    void setCollisionPoints(core::NameAtom content, std::vector<core::Vec3> points);

    // How many meshes have handed over their points. For a test, and for a
    // person asking why a hull is not a hull.
    [[nodiscard]] usize collisionMeshCount() const noexcept { return m_collisionPoints.size(); }
    [[nodiscard]] core::InstanceId workspace() const noexcept { return m_workspace; }

    // One simulation tick: apply the scene's writes, advance the characters,
    // step, write the results back, and turn the contact diff into deferred
    // signals. Called from the sim tick and from nowhere else.
    void step(f64 fixedDt);

    // --- The floating origin (ADR 0014, architecture.md §10) ------------------
    //
    // The tree stores absolute f64 and always has; what the origin changes is
    // the f32 space the solver works in. Scripts never see it, which is ADR
    // 0014's "rebasing is invisible to Luau" one layer down: a `CFrame` read
    // after a rebase is the same `CFrame` read before it.
    //
    // **Call at a safe point.** A rebase between two ticks is a translation of
    // everything at once; a rebase inside one would move the world under a
    // solver halfway through it.
    void setOrigin(core::DVec3 origin);
    [[nodiscard]] core::DVec3 origin() const noexcept { return m_origin; }

    // How far a focus may drift before the origin should follow it. Four
    // kilometres is architecture.md §10's number: f32 has about 24 bits of
    // mantissa, so at 4 km the quantum is a quarter of a millimetre and a
    // contact still resolves cleanly.
    static constexpr f64 RebaseThreshold = 4000.0;

    // The policy, separated from the mechanism so a test can drive either. True
    // when `focus` has left the tolerance around the current origin; the caller
    // then decides whether this is a safe point.
    [[nodiscard]] bool shouldRebase(core::DVec3 focus, f64 threshold = RebaseThreshold) const noexcept;

    // How many times the origin has moved. A counter rather than a log line:
    // a world that rebases every frame is a world whose threshold has hysteresis
    // it does not have, and that shows up as a number climbing.
    [[nodiscard]] u64 rebaseCount() const noexcept { return m_rebaseCount; }

    // The physics world's own handle, for the query methods the bindings
    // expose. `Workspace:Raycast` is a read of the same world the tick steps.
    [[nodiscard]] physics::WorldHandle worldHandle() const noexcept { return m_world; }
    [[nodiscard]] physics::IPhysics3D& backend() const noexcept { return m_backend; }

    // Resolves a body's opaque user data back to the instance that owns it, for
    // a query result. Invalid when the body is not ours or has been retired.
    [[nodiscard]] core::InstanceId instanceOf(u64 userData) const noexcept;
    // The other direction, for a query filter.
    [[nodiscard]] u64 userDataOf(core::InstanceId id) const noexcept;

    // Seconds inside the last `step`, in the three stages that are separable at
    // this seam (see `physics::StepTimings` for why they are not broadphase,
    // narrowphase and solver).
    struct Timings
    {
        f64 apply = 0.0;
        f64 step = 0.0;
        f64 writeback = 0.0;

        [[nodiscard]] f64 total() const noexcept { return apply + step + writeback; }
    };

    [[nodiscard]] const Timings& timings() const noexcept { return m_timings; }
    [[nodiscard]] usize bodyCount() const noexcept { return m_bodyCount; }

private:
    // Sorted by content atom id. A flat vector rather than a hash map for the
    // reason every other cache in this repository is one: a world has a handful
    // of distinct collision meshes, and a flat array has an order that an
    // unordered container does not (R10).
    std::vector<std::pair<core::NameAtom, std::vector<core::Vec3>>> m_collisionPoints;

    // What the mirror last pushed down for one instance, so that a tick can
    // tell a script's write from its own writeback without either side
    // carrying a dirty flag. `scene` cannot ask the body what it looks like
    // without a virtual call per field per part, and a dirty flag on the
    // component would be mirror bookkeeping in state the world hashes.
    struct BodyRecord
    {
        // Zero means the slot holds no body. Matched against the instance's own
        // generation, so a slot reused by a new instance is not mistaken for the
        // old one's body.
        u32 generation = 0;
        physics::BodyHandle handle;
        // The shape and motion the body was built with. A change to any of them
        // is a rebuild rather than a setter.
        physics::ShapeDesc shape;
        physics::MotionType motion = physics::MotionType::Dynamic;
        bool collidable = true;
        bool queryable = true;
        u16 group = 0;
        f32 friction = 0.3f;
        f32 restitution = 0.0f;
        f32 density = 1.0f;
        // The transform this mirror last wrote INTO the component. A component
        // that differs from it now is a script's write.
        core::CFrameD written;
        // Until which TICK an anchored part stays `Kinematic` (D031). Set when a
        // script writes its `CFrame`; a part past it goes back to `Static`.
        //
        // A tick count and never a duration: R10 forbids a simulation decision
        // that a wall clock could change, and "kinematic for a fifth of a
        // second" would make a body's broadphase layer depend on how fast the
        // machine was running.
        u64 movingUntilTick = 0;
        // Marked each tick during the sweep; anything unmarked afterwards has
        // left the world and its body is destroyed.
        bool seen = false;
    };

    struct CharacterRecord
    {
        physics::CharacterHandle handle;
        f32 height = 5.0f;
        f32 diameter = 2.0f;
        f32 maxSlopeAngle = 46.0f;
        f32 stepHeight = 0.5f;
        u16 group = 0;
        core::CFrameD written;
        bool seen = false;
    };

    void syncCollisionGroups();
    void applyScene();
    void applyBody(core::InstanceId id, PartComponent& part, RigidBodyComponent& body);
    void applyCharacter(core::InstanceId id, PartComponent& part, RigidBodyComponent& body,
                        CharacterBodyComponent& character, f32 fixedDt);
    void retireUnseen();
    void resolveWelds();
    // One weld, and everything it hangs from, resolved once. Returns the
    // driven part's new transform.
    void resolveWeld(core::InstanceId weldId, WeldComponent& weld);
    void writeBack();
    void writeCharacters();
    void publishContacts();

    [[nodiscard]] bool inWorld(core::InstanceId id) const;
    // Whether a part is the driven end of an active weld, which is what makes
    // it kinematic rather than dynamic: it is moved by the weld and by nothing
    // else, so the solver may not integrate it.
    [[nodiscard]] bool isDriven(core::InstanceId id) const;
    [[nodiscard]] physics::ShapeDesc shapeOf(core::InstanceId id, const PartComponent& part) const;
    [[nodiscard]] physics::BodyDesc descOf(core::InstanceId id, const PartComponent& part,
                                           const RigidBodyComponent& body, bool movingAnchored) const;

    World& m_scene;
    physics::IPhysics3D& m_backend;
    physics::WorldHandle m_world;

    // Indexed by the instance's SLOT, not hashed by its id, and that is a
    // measurement rather than a preference: with ten thousand parts the hash
    // map spent 214 ns per body per tick doing nothing, most of it missing
    // cache on a lookup whose key the pool walk already had in hand. A vector
    // indexed by slot is walked in the same ascending order the component pool
    // is, which is what makes it prefetchable.
    //
    // The generation in the record is what makes a stale slot detectable: a
    // slot reused by a different instance has a different generation, and a
    // record whose generation does not match is not that instance's.
    std::vector<BodyRecord> m_bodies;
    // Characters are few and are not on this path, so a map stays a map.
    std::unordered_map<u64, CharacterRecord> m_characters;
    // How many slots in `m_bodies` are live, so `bodyCount` does not walk.
    usize m_bodyCount = 0;

    // Scratch reused across ticks so that a thousand moving bodies allocate
    // nothing per frame.
    std::vector<physics::ActiveBody> m_active;

    // The welds resolved so far this tick, so a chain -- A welded to B, B welded
    // to C -- resolves each link once and in dependency order rather than in
    // whatever order the pool happens to hold them (R10).
    std::vector<core::InstanceId> m_resolvedWelds;
    // The parts an active weld drives, rebuilt each tick before the bodies are
    // applied. A driven part is kinematic; a released one goes back to being
    // whatever `Anchored` says.
    std::vector<core::InstanceId> m_drivenParts;

    // The one-entry memo `inWorld` keeps. Mutable because the question is a
    // read and the answer is a cache; reset every tick so a reparent cannot
    // outlive the frame it happened in.
    mutable core::InstanceId m_lastParent;
    mutable bool m_lastParentInWorld = false;

    u32 m_groupRevision = 0xffffffffu;
    core::InstanceId m_workspace;
    Timings m_timings;
    core::DVec3 m_origin;
    u64 m_rebaseCount = 0;
};

} // namespace luaug::scene
