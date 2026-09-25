// The mirror between the 2D instances and the 2D simulation (the 2D layer,
// post-v1 phase 3, docs/briefs/p3-2d-kickoff.md).
//
// **`PhysicsSync`'s contract, on the plane.** The tree is the authority and the
// bodies mirror it: a `Part2D` under `Workspace` has a body, a `Tilemap2D` has
// its solid tiles' outline, and one taken out of the tree loses them. A script's
// writes are applied at the start of the tick; the solver's answers are written
// back after it, quietly, straight into the component; what began and stopped
// touching becomes `Touched` and `TouchEnded`, both ways, deferred.
//
// It is a second mirror and not a mode of the first because the two share no
// state: a 3D body and a 2D body never touch. A world may hold both kinds, and
// each is stepped by its own library on the same fixed tick.
//
// A `Constraint2D` is a joint between two of the bodies (ADR 0102), built
// after them and rebuilt whenever either is -- a body rebuilt for a change of
// shape takes its joints with it -- or whenever the joint's own description
// changes. Nothing is adjusted in place, for the reason bodies are not.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/types.h"
#include "luaug/physics/physics2d.h"

#include <map>
#include <optional>
#include <vector>

namespace luaug::scene {

class World;

class PhysicsSync2D
{
public:
    PhysicsSync2D(World& world, physics::IPhysics2D& backend);
    ~PhysicsSync2D();

    PhysicsSync2D(const PhysicsSync2D&) = delete;
    PhysicsSync2D& operator=(const PhysicsSync2D&) = delete;

    // `Workspace`, handed in by the host, as the 3D mirror's is.
    void setWorkspace(core::InstanceId workspace) noexcept { m_workspace = workspace; }

    // One fixed tick: the scene's writes in, the step, the answers back, and
    // the contacts out as signals.
    void step(core::f64 fixedDt);

    struct Hit
    {
        core::InstanceId instance;
        core::Vec2 position{0.0f, 0.0f};
        core::Vec2 normal{0.0f, 0.0f};
        core::f64 distance = 0.0;
    };
    // The first part or tilemap the segment from `origin` along `direction`
    // (its length the reach) hits, sensors aside.
    [[nodiscard]] std::optional<Hit> raycast(core::Vec2 origin, core::Vec2 direction,
                                             const physics::Raycast2DFilter& filter = {}) const;

    // What a body of this instance carries as its user data, for a filter.
    [[nodiscard]] static core::u64 userDataOf(core::InstanceId id) noexcept;

    // Bodies in the simulation now, tilemaps' outlines included.
    [[nodiscard]] core::usize bodyCount() const noexcept;
    // Joints holding now.
    [[nodiscard]] core::usize jointCount() const noexcept;

private:
    // Everything that, changed, makes the body another body: rebuilt rather
    // than adjusted, because a 2D body is cheap and a partial update is where
    // a backend's state and the scene's part ways.
    struct Shape
    {
        core::Vec2 size{0.0f, 0.0f};
        core::i32 shape = 0;
        bool anchored = false;
        bool canCollide = true;
        bool sensor = false;
        core::f32 density = 0.0f;
        core::f32 friction = 0.0f;
        core::f32 elasticity = 0.0f;
        bool fixedRotation = false;
        core::f32 gravityScale = 0.0f;
        core::u16 group = 0;
        // Anchored and moved by a script at least once: kinematic from then on.
        bool moving = false;
        // On a replica, moved by the authority's snapshots: kinematic, driven
        // where the wire put it, as the 3D mirror drives a replicated part.
        bool replicated = false;
        [[nodiscard]] bool operator==(const Shape&) const noexcept = default;
    };

    struct PartRecord
    {
        physics::Body2DHandle body;
        Shape shape;
        // What the component held after the last write-back, so a change is a
        // script's and is pushed down.
        core::Vec2 position{0.0f, 0.0f};
        core::f32 rotation = 0.0f;
        core::Vec2 velocity{0.0f, 0.0f};
        core::f32 angularVelocity = 0.0f;
        bool seen = false;
    };

    struct TilemapRecord
    {
        std::vector<physics::Body2DHandle> bodies;
        core::u64 revision = 0;
        core::Vec2 position{0.0f, 0.0f};
        core::f32 cellSize = 0.0f;
        bool collides = true;
        core::f32 friction = 0.0f;
        core::u16 group = 0;
        bool built = false;
        bool seen = false;
    };

    struct JointRecord
    {
        physics::Joint2DHandle joint;
        // What it was built from: the two bodies' handles and the whole
        // description, so a rebuilt body or an edited property is noticed.
        physics::Joint2DDesc desc;
        bool seen = false;
    };

    void applyScene(core::f32 fixedDt);
    void applyJoints();
    void writeBack();
    void publishContacts();
    [[nodiscard]] core::u16 groupOf(core::NameAtom name) const noexcept;

    World& m_scene;
    physics::IPhysics2D& m_backend;
    physics::World2DHandle m_world;
    core::InstanceId m_workspace;
    core::Vec2 m_gravity{0.0f, -9.81f};
    core::u32 m_groupsRevision = ~core::u32{0};
    // `World::restores` as of the last tick: a restore rewinds every tilemap's
    // revision, so after one no remembered revision is evidence of anything.
    core::u64 m_restores = 0;
    // Keyed by the packed instance id: ordered, so every walk below is in id
    // order and the bodies are created in an order that is a fact about the
    // world (R10).
    std::map<core::u64, PartRecord> m_parts;
    std::map<core::u64, TilemapRecord> m_tilemaps;
    std::map<core::u64, JointRecord> m_joints;
};

} // namespace luaug::scene
