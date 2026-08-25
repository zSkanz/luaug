// The vocabulary of the physics seam (architecture.md §2, ADR 0007, ADR 0023).
//
// Everything here is POD that `core` can express. No backend type appears --
// not in a field, not in an enum value, not behind a pointer (R17) -- and
// nothing above L2 has to know that Jolt exists to talk about a body.
//
// The one deliberate omission is any notion of an *Instance*. A body carries an
// opaque `userData` the caller chooses and the physics module never interprets;
// the glue above stores an `InstanceId` in it. That is what lets `scene` own
// the tree and `physics` own the simulation without either learning the other's
// vocabulary -- and it is why a contact event reports two `userData` values
// rather than two things this module would have to name.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <span>

namespace luaug::physics {

using core::f32;
using core::f64;
using core::u16;
using core::u32;
using core::u64;
using core::u8;

// Handles are index + generation for the same reason `core::InstanceId` is: a
// slot that is freed and refilled must hand out a handle that compares unequal
// to the one that pointed at the old occupant, so a stale reference is
// detectable rather than silently aliasing whatever moved in.
struct WorldHandle
{
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }
    [[nodiscard]] constexpr bool operator==(const WorldHandle&) const noexcept = default;
};

struct BodyHandle
{
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }
    [[nodiscard]] constexpr bool operator==(const BodyHandle&) const noexcept = default;
};

struct CharacterHandle
{
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }
    [[nodiscard]] constexpr bool operator==(const CharacterHandle&) const noexcept = default;
};

// An `Anchored` part is Static, and that is not the same thing as a dynamic
// body with an enormous mass: a static body is not in the solver's island graph
// at all. Kinematic exists for the M6 case a moving platform needs -- driven by
// a script, pushing dynamic bodies, unmoved by them.
enum class MotionType : u8
{
    Static,
    Kinematic,
    Dynamic,
};

// The shapes `Enum.PartShape` names, plus the convex hull a `MeshPart` collapses
// to. There is no triangle mesh: that is asset-pipeline work (M7), and a shape
// this enum cannot express is a shape the caller must approximate knowingly
// rather than one this module silently substitutes.
enum class ShapeType : u8
{
    Box,
    Sphere,
    Capsule,
    Cylinder,
    ConvexHull,
};

// Sized in the same units `BasePart.Size` is: `size` is the FULL extent, never
// a half-extent. Getting that wrong is a factor of two that looks plausible in
// every screenshot, which is why the field is not called `extents`.
struct ShapeDesc
{
    ShapeType type = ShapeType::Box;

    // Box and Wedge: the full box. Sphere: `size.x` is the diameter. Capsule
    // and Cylinder: `size.x` is the diameter and `size.y` the full height,
    // caps included, which is how `BasePart.Size` describes one.
    core::Vec3 size{1.0f, 1.0f, 1.0f};

    // ConvexHull only. Points are in the part's local space and the span must
    // outlive the `createBody` call and no longer.
    std::span<const core::Vec3> points;

    // ConvexHull only: what to multiply each point by on the way in.
    //
    // A factor rather than pre-scaled points, because the points are shared --
    // every `MeshPart` naming one file collides against one cached point cloud,
    // and scaling in `scene` would mean a copy per body per resize. The backend
    // already copies each point into its own array to build the hull, so this
    // rides along for nothing.
    core::Vec3 pointScale{1.0f, 1.0f, 1.0f};
};

// A collision group is an index into the world's collidability matrix.
// Group 0 is "Default" and always exists.
using CollisionGroup = u16;

inline constexpr CollisionGroup kDefaultCollisionGroup = 0;

// The most groups a world can hold. Ten bits of the object layer, which is what
// leaves room for the moving/non-moving split the broad phase needs.
inline constexpr u32 kMaxCollisionGroups = 1024;

struct BodyDesc
{
    ShapeDesc shape;
    core::CFrameD transform;
    MotionType motion = MotionType::Dynamic;

    // `BasePart.Friction`, `.Restitution`, `.Density`. Density is what gives a
    // body its mass, together with the shape's volume -- there is no `Mass`
    // property, because a mass that disagreed with the size and density would
    // be a third source of truth.
    f32 friction = 0.3f;
    f32 restitution = 0.0f;
    f32 density = 1.0f;

    // `CanCollide`. A non-colliding body still reports contacts -- that is what
    // makes a trigger volume work, and it matches what a `Touched` on a
    // pass-through part means.
    bool collidable = true;

    // `CanQuery`. Excluded from raycasts, shapecasts and overlaps, and from
    // nothing else.
    bool queryable = true;

    CollisionGroup group = kDefaultCollisionGroup;

    // Opaque to this module. The glue stores an instance identity here.
    u64 userData = 0;
};

// Everything the writeback loop reads, in one call: reading a transform, then a
// velocity, then an awake flag would take three virtual calls and three
// lookups per body per tick, and there are a thousand of them.
struct BodyState
{
    core::CFrameD transform;
    core::Vec3 linearVelocity{0.0f, 0.0f, 0.0f};
    core::Vec3 angularVelocity{0.0f, 0.0f, 0.0f};
    // False for a body the solver has put to sleep, and for every static body.
    bool active = false;
};

// A body whose state changed this step, reported in a stable order the caller
// may rely on (R10). Jolt's own active-body list is explicitly documented as
// unordered under a multi-threaded job system (Docs/Architecture.md:807), so
// this is sorted before it is handed over and the sort is part of the contract
// rather than an implementation detail.
struct ActiveBody
{
    BodyHandle body;
    u64 userData = 0;
    BodyState state;
};

enum class ContactPhase : u8
{
    Began,
    Ended,
};

// One contact between two bodies, deduplicated to one event per pair per step.
// `first` and `second` are ordered by the backend so that the same pair always
// reports in the same order, which is what lets the glue diff two ticks' worth
// of contacts without sorting them again.
struct ContactEvent
{
    ContactPhase phase = ContactPhase::Began;
    BodyHandle first;
    BodyHandle second;
    u64 firstUserData = 0;
    u64 secondUserData = 0;
};

// f64 origin because a ray is cast from a world position; f32 direction because
// a direction is an extent and f32 is still exact at that scale (ADR 0014).
// `direction` is NOT normalised: its length is the ray's length, which is how
// `Workspace:Raycast(origin, direction)` reads in api-design.md §2.1.
struct RayD
{
    core::DVec3 origin;
    core::Vec3 direction{0.0f, 0.0f, -1.0f};
};

struct RayHit
{
    BodyHandle body;
    u64 userData = 0;
    core::DVec3 position;
    core::Vec3 normal{0.0f, 1.0f, 0.0f};
    // Along the ray, in metres.
    f32 distance = 0.0f;
};

// `RaycastParams` in the API, and the same filter serves every query. Exclude
// and include are the two modes `Enum.RaycastFilterType` names; the list holds
// `userData` values because that is the only identity this module has.
struct QueryFilter
{
    enum class Mode : u8
    {
        Exclude,
        Include,
    };

    Mode mode = Mode::Exclude;
    std::span<const u64> userData;

    // Empty means every group. A named group filters to bodies in it.
    bool filterGroup = false;
    CollisionGroup group = kDefaultCollisionGroup;
};

// A capsule that walks. Not a rigid body: Jolt's character sweeps its own shape
// and resolves its own contacts, which is what lets it climb a step without the
// solver deciding the capsule should tip over instead.
struct CharacterDesc
{
    core::CFrameD transform;
    // Full height including the caps, and the diameter -- authored the way
    // `BasePart.Size` is, so a 2 x 5 x 2 part and a character of the same size
    // occupy the same volume.
    f32 height = 5.0f;
    f32 diameter = 2.0f;

    // Degrees. A slope steeper than this is a wall.
    f32 maxSlopeAngle = 46.0f;
    // The tallest ledge the character walks over rather than into.
    f32 stepHeight = 0.5f;

    f32 mass = 80.0f;
    CollisionGroup group = kDefaultCollisionGroup;
    u64 userData = 0;
};

// `Enum.CharacterState` has exactly these two items (api-design.md §2.3), so
// this enum is that enum rather than a superset of it.
enum class CharacterGround : u8
{
    Grounded,
    Airborne,
};

struct CharacterState
{
    core::CFrameD transform;
    core::Vec3 linearVelocity{0.0f, 0.0f, 0.0f};
    CharacterGround ground = CharacterGround::Airborne;
    // The surface under the character's feet, or an invalid handle when
    // airborne. This is what a moving platform needs at M6 and what a
    // `Landed` signal names.
    BodyHandle groundBody;
    u64 groundUserData = 0;
    core::Vec3 groundNormal{0.0f, 1.0f, 0.0f};
};

// Seconds spent inside the last `step`, and nothing else.
//
// The roadmap asks the M5 budget to be broken down as broadphase / narrowphase
// / solver, "because one number says a budget was missed and three say which
// stage missed it". Jolt does not expose those three: the split exists only
// inside its own profiler, which is a compile-time feature (JPH_PROFILE_ENABLED
// or an external-measurement class it calls from EVERY scope in the library)
// that dumps to a file rather than answering a query. Turning it on would tax
// every configuration, shipping included, for a number the gate reads once.
//
// So the breakdown the budget records is the one that exists at this seam and
// measures stages that are really separable: applying the scene's writes,
// stepping, and writing the result back. The bench reports those three; this
// struct is the middle one, and it is the backend's to fill because it is the
// only party that knows where its own update begins and ends.
struct StepTimings
{
    f64 step = 0.0;
};

struct WorldDesc
{
    core::Vec3 gravity{0.0f, -9.81f, 0.0f};
    // The number of bodies the world is sized for. Jolt allocates up front; a
    // world that grows past this refuses to create rather than reallocating
    // under a running simulation.
    u32 maxBodies = 16384;
};

// The wireframe Jolt can draw of what it thinks the world looks like -- which
// is the only picture that can disagree with the rendered one, and therefore
// the only one worth having (roadmap M5, "Jolt debug-draw bridge").
//
// A sink rather than a returned buffer: the drawing is a walk over shapes the
// backend already holds, and copying it into a vector so the caller can walk it
// again would double the cost of a debug view.
class IDebugDrawSink
{
public:
    virtual ~IDebugDrawSink() = default;

    // World space, f64 -- the sink rebases. Colour is 0xRRGGBB.
    virtual void line(core::DVec3 from, core::DVec3 to, u32 color) = 0;
};

} // namespace luaug::physics
