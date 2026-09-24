// The 2D physics seam (ADR 0008, post-v1 phase 3).
//
// **A seam of its own, beside `IPhysics3D` and not inside it.** A 2D world is
// not a 3D world with a flat axis: its bodies turn about one angle, its shapes
// are polygons and circles, and its solver is a different library (Box2D, where
// the 3D one is Jolt). One interface for both would be every method taking a
// dimension argument and every backend refusing half of them. What the two
// share is the shape of the contract -- opaque generational handles, a world
// stepped by a fixed tick the caller owns, contacts reported after the step in
// a deterministic order -- and that is kept identical on purpose, so the scene
// mirror for one reads like the mirror for the other.
//
// No Box2D type appears here (R17): the backend has no public header, and is
// reached only through `createBox2DPhysics` in `backends.h`.
#pragma once

#include "luaug/core/math.h"
#include "luaug/core/types.h"

#include <optional>
#include <span>
#include <vector>

namespace luaug::physics {

using core::f32;
using core::u16;
using core::u32;
using core::u64;

struct World2DHandle
{
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }
    [[nodiscard]] constexpr bool operator==(const World2DHandle&) const noexcept = default;
};

struct Body2DHandle
{
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }
    [[nodiscard]] constexpr bool operator==(const Body2DHandle&) const noexcept = default;
};

enum class Motion2D : core::u8
{
    Static,
    Kinematic,
    Dynamic,
};

enum class Shape2DType : core::u8
{
    // A rectangle `halfExtents` either side of the body's origin.
    Box,
    // A disc of `radius`.
    Circle,
    // A stadium: a segment `halfExtents.y` either side of the origin along the
    // body's y, swept by `radius`. What a character stands on, because its
    // rounded foot slides over the seam between two boxes instead of catching.
    Capsule,
    // A convex polygon of up to eight `points`, in the body's frame.
    Polygon,
    // An open chain of `points` with one-sided collision: ground laid out as a
    // line, the shape a tilemap's surface becomes.
    Chain,
};

// The most groups a 2D world can hold: Box2D filters by a 64-bit category and
// mask, one bit per group.
inline constexpr u32 kMaxCollisionGroups2D = 64;
using CollisionGroup2D = u16;
inline constexpr CollisionGroup2D kDefaultCollisionGroup2D = 0;

struct Shape2DDesc
{
    Shape2DType type = Shape2DType::Box;
    core::Vec2 halfExtents{0.5f, 0.5f};
    f32 radius = 0.5f;
    std::vector<core::Vec2> points;
    // Where the shape sits in the body's frame.
    core::Vec2 offset{0.0f, 0.0f};
    f32 angle = 0.0f;
    // A chain that closes on itself: the outline of a solid region, collided
    // from outside. Counter-clockwise around the solid, so its outside is the
    // segments' right. Needs at least three points; an open chain four.
    bool loop = false;
};

struct Body2DDesc
{
    Shape2DDesc shape;
    Motion2D motion = Motion2D::Dynamic;
    core::Vec2 position{0.0f, 0.0f};
    // Radians, counter-clockwise.
    f32 angle = 0.0f;
    core::Vec2 linearVelocity{0.0f, 0.0f};
    f32 angularVelocity = 0.0f;
    f32 density = 1.0f;
    f32 friction = 0.3f;
    f32 restitution = 0.0f;
    f32 gravityScale = 1.0f;
    f32 linearDamping = 0.0f;
    f32 angularDamping = 0.0f;
    // A body that does not turn, whatever hits it: a character.
    bool fixedRotation = false;
    // Reports what overlaps it and pushes nothing.
    bool sensor = false;
    // False collides with nothing at all, and the body still moves: a part
    // that falls through the world, as a `CanCollide = false` part does.
    bool collides = true;
    // Continuous collision against other moving bodies, for something small
    // and fast; every body already has it against static ones.
    bool bullet = false;
    CollisionGroup2D group = kDefaultCollisionGroup2D;
    // Handed back in contacts and raycast hits, so the caller learns which of
    // its own objects was hit without a lookup of its own.
    u64 userData = 0;
};

struct Body2DState
{
    core::Vec2 position{0.0f, 0.0f};
    f32 angle = 0.0f;
    core::Vec2 linearVelocity{0.0f, 0.0f};
    f32 angularVelocity = 0.0f;
    bool awake = true;
};

struct World2DDesc
{
    core::Vec2 gravity{0.0f, -9.81f};
};

// One contact event from the last step. `Begin` is two shapes starting to touch
// or a shape entering a sensor, `End` the reverse.
struct Contact2D
{
    enum class Kind : core::u8
    {
        Begin,
        End,
    };
    Kind kind = Kind::Begin;
    u64 userDataA = 0;
    u64 userDataB = 0;
    // A sensor overlap rather than a touch: A is the sensor.
    bool sensor = false;
};

// What a ray may hit. The default hits everything but sensors.
struct Raycast2DFilter
{
    // True: only bodies whose user data `userData` lists. False: every body
    // but those.
    bool include = false;
    std::span<const u64> userData;
    // Cast as a body of this group would collide: bodies of a group it does
    // not collide with are passed through. Empty casts as every group.
    std::optional<CollisionGroup2D> group;
};

struct Raycast2DHit
{
    u64 userData = 0;
    core::Vec2 point{0.0f, 0.0f};
    core::Vec2 normal{0.0f, 0.0f};
    // Of the ray's length, from 0 at its origin to 1 at its end.
    f32 fraction = 0.0f;
};

class IPhysics2D
{
public:
    virtual ~IPhysics2D() = default;

    [[nodiscard]] virtual World2DHandle createWorld(const World2DDesc& desc) = 0;
    virtual void destroyWorld(World2DHandle world) = 0;
    virtual void setGravity(World2DHandle world, core::Vec2 gravity) = 0;

    // One fixed step. The caller owns the accumulator (R10).
    virtual void step(World2DHandle world, f32 fixedDt) = 0;

    [[nodiscard]] virtual Body2DHandle createBody(World2DHandle world, const Body2DDesc& desc) = 0;
    virtual void destroyBody(World2DHandle world, Body2DHandle body) = 0;
    // A teleport: the body is put there, with its velocity kept.
    virtual void setBodyTransform(World2DHandle world, Body2DHandle body, core::Vec2 position, f32 angle) = 0;
    // A kinematic body moved to a target over one step, with the velocity that
    // gets it there, so what stands on it is carried rather than left behind.
    virtual void moveKinematic(World2DHandle world, Body2DHandle body, core::Vec2 position, f32 angle, f32 fixedDt) = 0;
    virtual void setVelocity(World2DHandle world, Body2DHandle body, core::Vec2 linear, f32 angular) = 0;
    virtual void applyImpulse(World2DHandle world, Body2DHandle body, core::Vec2 impulse) = 0;
    [[nodiscard]] virtual Body2DState bodyState(World2DHandle world, Body2DHandle body) const = 0;

    // What began and ended touching in the last step, in an order that is a
    // function of the world rather than of threads.
    [[nodiscard]] virtual std::span<const Contact2D> contacts(World2DHandle world) const = 0;

    // The first body the segment from `origin` to `origin + translation` hits,
    // sensors aside. Two hits at one distance resolve to the lower user data,
    // so the answer does not hang on the tree's traversal order (R10).
    [[nodiscard]] virtual std::optional<Raycast2DHit> raycast(World2DHandle world, core::Vec2 origin,
                                                              core::Vec2 translation,
                                                              const Raycast2DFilter& filter = {}) const = 0;

    // Whether bodies of two groups collide. Every pair does until told
    // otherwise; a body created after the change sees it, and one created before
    // is refiltered.
    virtual void setGroupsCollidable(World2DHandle world, CollisionGroup2D a, CollisionGroup2D b, bool collidable) = 0;
};

} // namespace luaug::physics
