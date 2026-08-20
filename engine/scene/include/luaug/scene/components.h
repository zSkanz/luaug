// Class-specific state, stored as POD components (architecture.md §4).
//
// Every field here backs a property the IDL declares, and the generated
// accessors are the only things that read or write them. They are plain structs
// with no invariants of their own on purpose: `World::snapshot` is a per-pool
// copy, and a component with a constructor, a pointer or a heap allocation
// would make that a traversal instead (ADR 0016).
//
// A class arrives with the milestone that gives it behaviour, and so does its
// component.
//
// **Why the render module's components live in `scene`'s header (M4).**
// `World` holds one `ComponentPool<T>` member per component type and has no
// extension point for a higher module to add its own. A component is data with
// no invariants, so the alternative -- a type-erased pool registry keyed by type
// id -- would rework the ECS core to buy an indirection on every access, and it
// would buy it for exactly the reason architecture.md §2 rule 3 already
// tolerates: `scene` never *includes* `render`, and it does not here either. It
// stores five more POD structs and interprets none of them.
//
// The cost is that this file grows with every module that owns classes, and the
// day that becomes the problem -- physics at M5 is the next candidate -- the
// type-erased registry is the answer. Recorded so that day is a decision rather
// than a discovery.
//
// M5 came and did not spend it. `RigidBodyComponent` and
// `CharacterBodyComponent` are scene's own, not the physics module's: physics
// sits at L2, BELOW scene, so it could not register a class into the scene
// registry even if it wanted to. What crosses the seam is a body handle held by
// the glue, and neither module holds one of the other's types. The registry is
// still the answer for the day a module ABOVE scene brings a third batch.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/types.h"
#include "luaug/scene/types.h"

namespace luaug::scene {

// `BasePart`'s structural half (M2 brief, Decision 6). The physics half --
// Anchored, CanCollide, Friction and the rest -- arrives in M5 with a
// simulation that can mean something by it.
//
// `Position` and `Orientation` are NOT stored: they are views of `cframe`,
// derived on read and folded back on write, because storing them alongside
// would create two sources of truth that a `.prefab.luau` could set to
// contradict each other.
struct PartComponent
{
    core::CFrameD cframe;
    core::Vec3 size{1.0f, 1.0f, 1.0f};
    core::Color3 color{1.0f, 1.0f, 1.0f};
    f32 transparency = 0.0f;
    // `Enum.PartShape`'s value. Stored as the raw item value rather than as an
    // enum class so that the generated accessor needs no per-enum C++ type.
    i32 shape = 0;
};

// `MeshPart`'s geometry. The renderer resolves the URN to a loaded mesh and
// keeps that mapping on its own side, because a `MeshHandle` is a GPU resource
// and `scene` has no business holding one.
struct MeshPartComponent
{
    // `asset://models/x.glb` or another Content URN, interned rather than held
    // as a string: this header's opening contract is that a component is
    // trivially copyable, because `World::snapshot` is a per-pool memcpy and a
    // heap allocation would make it a traversal (ADR 0016). Interning also
    // matches how `Instance.Name` is stored.
    //
    // Kept as the script wrote it rather than resolved, so reading the property
    // back gives what was written even when the file failed to load.
    core::NameAtom meshContent;

    // `Enum.CollisionFidelity`'s value, stored raw. Read by the physics mirror
    // rather than by the renderer, which is why it sits with the geometry it
    // approximates rather than with the rigid body that uses it.
    i32 collisionFidelity = 0;
};

// `BasePart`'s physical half (M5). Separate from `PartComponent` rather than
// folded into it because the two have different readers: `render::extract`
// walks every part every frame and wants the transform and the look, and the
// physics mirror walks the bodies and wants this. One pool each keeps each
// walk over the fields it uses.
//
// Attached by the same hook as `PartComponent` -- a `BasePart` has both or
// neither -- and holding no handle of any kind. The mapping from an instance to
// a simulation body lives in the glue above both modules, because `scene` (L3)
// must not learn what a body is and `physics` (L2) must not learn what an
// instance is.
struct RigidBodyComponent
{
    // Written by the SCRIPT and read by the mirror.
    bool anchored = false;
    bool canCollide = true;
    bool canQuery = true;
    // The group's name, interned. Resolved to a simulation group by the glue,
    // which is the only party that knows the group table exists.
    core::NameAtom collisionGroup;
    f32 friction = 0.3f;
    f32 restitution = 0.0f;
    f32 density = 1.0f;

    // Written by the MIRROR and read by scripts. Read-only in the API for the
    // reason the property's Doc gives: an assignment would be an impulse with
    // the mass divided out, and `ApplyImpulse` is that operation named.
    core::Vec3 linearVelocity{0.0f, 0.0f, 0.0f};
    core::Vec3 angularVelocity{0.0f, 0.0f, 0.0f};

    // `ApplyImpulse` accumulates here and the mirror drains it at the start of
    // the next tick. A queue rather than an immediate call because a script may
    // run at any point in the frame and the solver may not be interrupted --
    // and because summing impulses is exactly what applying them one after
    // another would do anyway.
    core::Vec3 pendingImpulse{0.0f, 0.0f, 0.0f};

    // Whether the simulation is still moving this body, written by the mirror.
    // Not a property -- nothing in api-design.md exposes it -- and hashed
    // anyway, because two runs that agree on every position while disagreeing
    // about which bodies are asleep are one nudge away from disagreeing on
    // everything (architecture.md §9: the hash covers physics state).
    bool active = false;
};

// `CharacterBody`'s own state (M5). The three tuning numbers a script sets, the
// two facts the controller reports, and the command for the next tick.
//
// The command is stored rather than dispatched because `Move` and `Jump` are
// called from script code that may run at any point in the frame, and the
// controller may only be advanced inside the sim tick. It is simulation state
// like any other and hashes with the rest of the world.
struct CharacterBodyComponent
{
    f32 walkSpeed = 16.0f;
    f32 jumpSpeed = 8.0f;
    f32 maxSlopeAngle = 46.0f;
    f32 autoStepHeight = 0.5f;

    // Reported by the controller after each tick.
    bool grounded = false;
    // `Enum.CharacterState`'s value, stored raw for the same reason
    // `PartComponent::shape` is.
    i32 state = 1;
    // What the character was standing on last tick, so `Landed` can name it and
    // so the transition can be detected without a second flag.
    core::InstanceId groundPart;

    // The command for the next tick. Cleared by the mirror once consumed, so a
    // character told nothing stops -- which is what `Move`'s Doc promises.
    core::Vec3 moveDirection{0.0f, 0.0f, 0.0f};
    bool jumpRequested = false;
    // Carried across ticks because a controller owns its own vertical velocity:
    // gravity integrates here rather than in the solver, since a
    // `CharacterVirtual` is not a body the solver knows about.
    f32 verticalVelocity = 0.0f;
};

// `Camera`. Everything here is what a projection matrix needs and nothing more:
// the viewport is the renderer's, not the camera's.
struct CameraComponent
{
    core::CFrameD cframe;
    // Degrees, vertical. Stored as authored so a read gives back the write.
    f32 fieldOfView = 70.0f;
    f32 nearPlane = 0.1f;
    f32 farPlane = 5000.0f;
};

struct PointLightComponent
{
    core::Color3 color{1.0f, 1.0f, 1.0f};
    f32 brightness = 1.0f;
    f32 range = 16.0f;
    // Stored and reported faithfully; this release casts shadows from the sun
    // alone (M4 brief, Decision 10). A property that round-trips is honest; one
    // that silently reads back false would not be.
    bool shadows = false;
};

struct SpotLightComponent
{
    core::Color3 color{1.0f, 1.0f, 1.0f};
    f32 brightness = 1.0f;
    f32 range = 16.0f;
    // Full cone width in degrees.
    f32 angle = 45.0f;
    bool shadows = false;
};

// The `Lighting` service's own state. `SunDirection` is deliberately absent: it
// is derived from `clockTime` and `geographicLatitude` on read, so there is one
// source of truth and a replay cannot drift from the run it replays (R10).
struct LightingComponent
{
    // Hours, 0 to 24, wrapping.
    f32 clockTime = 12.0f;
    f32 geographicLatitude = 0.0f;
    core::Color3 ambient{0.15f, 0.16f, 0.2f};
    f32 brightness = 2.0f;
    core::Color3 fogColor{0.6f, 0.7f, 0.85f};
    f32 fogStart = 200.0f;
    // Equal to or below `fogStart` means no fog at all, which is how fog is
    // turned off without a second flag to keep in sync.
    f32 fogEnd = 0.0f;
};

// `Workspace`'s own state. One field, and it is a reference rather than a
// camera: the camera is an ordinary instance under the tree, and this says which
// one the renderer looks through.
struct WorkspaceComponent
{
    core::InstanceId currentCamera;
    // SI and signed, so the default points down (api-design.md §2.1).
    core::Vec3 gravity{0.0f, -9.81f, 0.0f};
};

// `PVInstance`'s own state, and therefore attached to every `BasePart`, `Model`
// and `Camera` in the world.
//
// One field, and it is what gives `PivotTo` a meaning `CFrame = target` does not
// already have. Without it the pivot is always the object's centre, `PivotTo` is
// an assignment under a longer name, and nothing can hinge about an edge.
//
// **It is not a centre of mass.** Jolt has its own notion of where a body turns
// about, and joining the two would make hinging a door change its dynamics --
// which is a defect that would take a milestone to notice. M5 must keep them
// apart.
struct PVComponent
{
    core::CFrameD pivotOffset;
};

struct ModelComponent
{
    core::InstanceId primaryPart;
};

struct ScriptComponent
{
    bool enabled = true;
};

} // namespace luaug::scene
