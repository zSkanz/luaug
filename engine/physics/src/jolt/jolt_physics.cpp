// The Jolt backend (ADR 0007, ADR 0023).
//
// The whole backend is one translation unit and it has no public header, which
// is what keeps R17 mechanical rather than aspirational: no module above L2 can
// include a JPH type because there is no file of ours to include that names
// one.
//
// Three things in here exist because of R10 rather than because Jolt needs
// them, and upstream's own documentation is why (Docs/Architecture.md:804-807):
// contact callbacks arrive in a non-deterministic order, the active-body list
// is in a non-deterministic order, and a query's hits arrive in a
// non-deterministic order. All three are true today only in the sense that they
// WILL be true when M7 wires a multi-threaded job system -- with the
// single-threaded one they happen to be stable. Writing the sorts now is what
// stops M7 from being the milestone that discovers a thousand recorded traces
// are worthless.
//
// Jolt requires Jolt.h before any other Jolt header -- its own headers say so
// and none of them include it -- so this block is exempt from include sorting.
// clang-format off
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Collision/CollisionCollector.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRendererSimple.h>
#endif
// clang-format on

#include "luaug/core/error.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/physics/backends.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace luaug::physics {
namespace {

using core::f32;
using core::f64;
using core::u16;
using core::u32;
using core::u64;
using core::u8;
using core::usize;

// --- Layer encoding ---------------------------------------------------------
//
// One object layer per (collision group, moving) pair: `layer = group * 2 +
// moving`. Ten bits of group and one of motion fit inside the 16-bit object
// layer Jolt is built with, which is what caps a world at
// `kMaxCollisionGroups`.
//
// The split by motion is not ours; it is what lets the broad phase keep static
// geometry in a tree it never rebuilds. Everything else about which pairs
// collide is a runtime matrix, because `PhysicsService:RegisterCollisionGroup`
// is a call a script makes after the world exists.

constexpr JPH::BroadPhaseLayer kBroadPhaseNonMoving(0);
constexpr JPH::BroadPhaseLayer kBroadPhaseMoving(1);
constexpr JPH::uint kBroadPhaseLayerCount = 2;

[[nodiscard]] JPH::ObjectLayer encodeLayer(CollisionGroup group, bool moving) noexcept
{
    return static_cast<JPH::ObjectLayer>((static_cast<u32>(group) << 1) | (moving ? 1u : 0u));
}

[[nodiscard]] CollisionGroup decodeGroup(JPH::ObjectLayer layer) noexcept
{
    return static_cast<CollisionGroup>(static_cast<u32>(layer) >> 1);
}

[[nodiscard]] bool decodeMoving(JPH::ObjectLayer layer) noexcept
{
    return (static_cast<u32>(layer) & 1u) != 0u;
}

// The collidability matrix, dense and square, one byte per pair. A world with
// the four groups a game actually registers costs sixteen bytes; the bound is
// what stops a script from asking for a megabyte by looping.
class CollisionMatrix
{
public:
    CollisionMatrix()
    {
        m_names.emplace_back("Default");
        m_collidable.assign(1, 1);
    }

    [[nodiscard]] u32 count() const noexcept { return static_cast<u32>(m_names.size()); }

    [[nodiscard]] CollisionGroup find(std::string_view name) const noexcept
    {
        for (usize i = 0; i < m_names.size(); ++i) {
            if (m_names[i] == name) {
                return static_cast<CollisionGroup>(i);
            }
        }
        return kInvalidGroup;
    }

    [[nodiscard]] CollisionGroup add(std::string_view name)
    {
        const CollisionGroup existing = find(name);
        if (existing != kInvalidGroup) {
            return existing;
        }
        if (m_names.size() >= kMaxCollisionGroups) {
            return kInvalidGroup;
        }

        const u32 previous = count();
        const u32 next = previous + 1;
        std::vector<u8> grown(static_cast<usize>(next) * next, 1);
        for (u32 row = 0; row < previous; ++row) {
            for (u32 column = 0; column < previous; ++column) {
                grown[static_cast<usize>(row) * next + column] =
                    m_collidable[static_cast<usize>(row) * previous + column];
            }
        }
        m_collidable.swap(grown);
        m_names.emplace_back(name);
        return static_cast<CollisionGroup>(previous);
    }

    void setCollidable(CollisionGroup a, CollisionGroup b, bool collidable) noexcept
    {
        if (a >= count() || b >= count()) {
            return;
        }
        m_collidable[static_cast<usize>(a) * count() + b] = collidable ? 1u : 0u;
        m_collidable[static_cast<usize>(b) * count() + a] = collidable ? 1u : 0u;
    }

    [[nodiscard]] bool collidable(CollisionGroup a, CollisionGroup b) const noexcept
    {
        if (a >= count() || b >= count()) {
            return false;
        }
        return m_collidable[static_cast<usize>(a) * count() + b] != 0u;
    }

    void collectNames(std::vector<std::string_view>& out) const
    {
        for (const std::string& name : m_names) {
            out.emplace_back(name);
        }
    }

    static constexpr CollisionGroup kInvalidGroup = 0xffffu;

private:
    std::vector<std::string> m_names;
    std::vector<u8> m_collidable;
};

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
{
public:
    [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override { return kBroadPhaseLayerCount; }

    [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return decodeMoving(layer) ? kBroadPhaseMoving : kBroadPhaseNonMoving;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    [[nodiscard]] const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == kBroadPhaseMoving ? "moving" : "static";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
    {
        // Static against static is the pair the broad-phase split exists to
        // skip: two things that never move cannot begin to overlap.
        return decodeMoving(layer) || broadPhase == kBroadPhaseMoving;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter
{
public:
    explicit ObjectPairFilter(const CollisionMatrix& matrix) : m_matrix(matrix) {}

    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        if (!decodeMoving(a) && !decodeMoving(b)) {
            return false;
        }
        return m_matrix.collidable(decodeGroup(a), decodeGroup(b));
    }

private:
    const CollisionMatrix& m_matrix;
};

// --- Records ----------------------------------------------------------------

struct BodyRecord
{
    JPH::BodyID id;
    u32 generation = 0;
    bool alive = false;
    bool collidable = true;
    bool queryable = true;
    MotionType motion = MotionType::Dynamic;
    CollisionGroup group = kDefaultCollisionGroup;
    u64 userData = 0;
};

// A live joint, plus everything needed to rebuild it: `updateBody` destroys and
// recreates the Jolt body underneath, which would dangle every constraint on it.
struct ConstraintRecord
{
    JPH::Ref<JPH::TwoBodyConstraint> constraint;
    u32 generation = 0;
    bool alive = false;
    // Kept because a rebuild needs them and because retiring a body has to find
    // the joints that name it.
    BodyHandle first;
    BodyHandle second;
    ConstraintDesc desc;
};

struct CharacterRecord
{
    JPH::Ref<JPH::CharacterVirtual> character;
    u32 generation = 0;
    bool alive = false;
    f32 stepHeight = 0.5f;
    u64 userData = 0;
    // The layer the character sweeps the world as -- its own group, moving.
    // Kept because `ExtendedUpdate` needs it every tick and the settings that
    // carried it are gone by then.
    JPH::ObjectLayer layer = 0;
};

// A kinematic body's target for this tick, waiting for the delta that turns it
// into a velocity. See `setBodyTransform`.
struct PendingMove
{
    JPH::BodyID id;
    JPH::RVec3 position;
    JPH::Quat rotation;
};

// A contacting pair, keyed by our own handles rather than by Jolt's body ids.
// Packed so a pair is one comparison and one sort key -- and so the ordering is
// ours, which is what makes it survive a job system that reports the same pair
// in either order (Docs/Architecture.md:806).
struct ContactPair
{
    u64 first = 0;
    u64 second = 0;

    [[nodiscard]] constexpr bool operator==(const ContactPair&) const noexcept = default;
    [[nodiscard]] constexpr bool operator<(const ContactPair& other) const noexcept
    {
        return first != other.first ? first < other.first : second < other.second;
    }
};

// One contact between a CHARACTER and something else, for the tick.
//
// Held apart from `ContactPair` rather than folded into it, and the reason is
// not tidiness: a `CharacterVirtual` is not a body, so the two sides are handles
// from different spaces, and the sleep exception the rigid diff makes must not
// apply here -- a character is never put to sleep by the solver and its inner
// body is created with `mAllowSleeping = false` (`CharacterVirtual.cpp:146`), so
// a contact that stops being reported really has ended.
//
// The character is always the FIRST side, which is what makes the pair
// canonical without a swap.
struct CharacterPair
{
    u64 character = 0;
    u64 other = 0;
    // The other side is another character's inner body rather than an ordinary
    // one, so its handle is a `CharacterHandle` and resolves through a different
    // table. Part of the key, because the two spaces can collide numerically.
    bool otherIsCharacter = false;

    [[nodiscard]] constexpr bool operator==(const CharacterPair&) const noexcept = default;
    [[nodiscard]] constexpr bool operator<(const CharacterPair& rhs) const noexcept
    {
        if (character != rhs.character)
            return character < rhs.character;
        if (other != rhs.other)
            return other < rhs.other;
        return static_cast<int>(otherIsCharacter) < static_cast<int>(rhs.otherIsCharacter);
    }
};

[[nodiscard]] constexpr u64 packHandle(BodyHandle handle) noexcept
{
    return (static_cast<u64>(handle.generation) << 32) | handle.index;
}

[[nodiscard]] constexpr u64 packHandle(CharacterHandle handle) noexcept
{
    return (static_cast<u64>(handle.generation) << 32) | handle.index;
}

[[nodiscard]] constexpr CharacterHandle unpackCharacter(u64 packed) noexcept
{
    return CharacterHandle{static_cast<u32>(packed & 0xffffffffu), static_cast<u32>(packed >> 32)};
}

[[nodiscard]] constexpr BodyHandle unpackHandle(u64 packed) noexcept
{
    return BodyHandle{static_cast<u32>(packed & 0xffffffffu), static_cast<u32>(packed >> 32)};
}

// --- Conversions ------------------------------------------------------------
//
// f64 in, f32 out. This is architecture.md §10's split and not a shortcut:
// world precision is f64 in `scene` plus a floating origin, and physics runs in
// the rebased f32 space.
//
// **M7 made the origin real, and the two functions below are now the WRONG ones
// to call from inside a world.** They narrow against an origin of zero, which is
// only correct for a world that has never rebased; `JoltWorld::toLocal` and
// `::toWorld` are the pair that subtracts and adds the world's own origin, and
// every call site inside the world uses those. These stay because the debug
// bridge -- which is handed a sink and not a world -- needs a raw pair, and
// because a raycast's DIRECTION is a displacement rather than a position and
// must never be shifted.

[[nodiscard]] JPH::Vec3 toJolt(core::Vec3 v) noexcept
{
    return JPH::Vec3(v.x, v.y, v.z);
}

[[nodiscard]] JPH::RVec3 toJoltPosition(core::DVec3 v) noexcept
{
    return JPH::RVec3(static_cast<f32>(v.x), static_cast<f32>(v.y), static_cast<f32>(v.z));
}

[[nodiscard]] core::Vec3 fromJolt(JPH::Vec3Arg v) noexcept
{
    return core::Vec3{v.GetX(), v.GetY(), v.GetZ()};
}

[[nodiscard]] core::DVec3 fromJoltPosition(JPH::RVec3Arg v) noexcept
{
    return core::DVec3{static_cast<f64>(v.GetX()), static_cast<f64>(v.GetY()), static_cast<f64>(v.GetZ())};
}

[[nodiscard]] JPH::Quat toJolt(const core::Mat3& rotation) noexcept
{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 1.0f;
    core::toQuaternion(rotation, x, y, z, w);
    return JPH::Quat(x, y, z, w).Normalized();
}

[[nodiscard]] core::Mat3 fromJolt(JPH::QuatArg q) noexcept
{
    return core::fromQuaternion(q.GetX(), q.GetY(), q.GetZ(), q.GetW());
}

// --- Shapes -----------------------------------------------------------------

[[nodiscard]] JPH::ShapeRefC buildShape(const ShapeDesc& desc)
{
    // Half-extents, because `size` is the full extent everywhere in this engine
    // and Jolt takes halves. Clamped away from zero: a zero-sized shape is a
    // degenerate hull Jolt refuses, and a script writing `Size = Vector3.zero`
    // must produce a very small part rather than an error the frame cannot
    // recover from.
    constexpr f32 kMinHalfExtent = 0.005f;
    const f32 hx = std::max(desc.size.x * 0.5f, kMinHalfExtent);
    const f32 hy = std::max(desc.size.y * 0.5f, kMinHalfExtent);
    const f32 hz = std::max(desc.size.z * 0.5f, kMinHalfExtent);

    switch (desc.type) {
    case ShapeType::Box: {
        // Jolt's box carries a convex radius that must fit inside the box; the
        // default 0.05 makes a part thinner than 10 cm fail to build.
        const f32 convexRadius = std::min({hx, hy, hz, JPH::cDefaultConvexRadius});
        return JPH::ShapeRefC(new JPH::BoxShape(JPH::Vec3(hx, hy, hz), convexRadius));
    }
    case ShapeType::Sphere:
        return JPH::ShapeRefC(new JPH::SphereShape(std::max({hx, hy, hz})));
    case ShapeType::Capsule: {
        const f32 radius = std::max(hx, hz);
        // The cylindrical part only: Jolt's half-height excludes the caps,
        // while `Size.y` includes them.
        const f32 halfCylinder = std::max(hy - radius, kMinHalfExtent);
        return JPH::ShapeRefC(new JPH::CapsuleShape(halfCylinder, radius));
    }
    case ShapeType::Cylinder: {
        const f32 radius = std::max(hx, hz);
        const f32 convexRadius = std::min({radius, hy, JPH::cDefaultConvexRadius});
        return JPH::ShapeRefC(new JPH::CylinderShape(hy, radius, convexRadius));
    }
    case ShapeType::ConvexHull: {
        if (desc.points.size() < 4) {
            return {};
        }
        JPH::Array<JPH::Vec3> points;
        points.reserve(desc.points.size());
        for (const core::Vec3& point : desc.points) {
            points.push_back(toJolt(
                core::Vec3{point.x * desc.pointScale.x, point.y * desc.pointScale.y, point.z * desc.pointScale.z}));
        }
        JPH::ConvexHullShapeSettings settings(points);
        settings.SetEmbedded();
        const JPH::ShapeSettings::ShapeResult result = settings.Create();
        if (result.HasError()) {
            return {};
        }
        return result.Get();
    }
    }
    return {};
}

// --- Deterministic collectors -----------------------------------------------
//
// Jolt's own closest-hit collector keeps the FIRST of two hits at an equal
// fraction, and which one arrives first is traversal order. These keep the one
// with the lower body id instead, so a tie -- two coincident surfaces, a ray
// down a seam -- answers the same way on every run (R10).

class ClosestRayCollector final : public JPH::CastRayCollector
{
public:
    void AddHit(const JPH::RayCastResult& result) override
    {
        if (!has || result.mFraction < hit.mFraction ||
            (result.mFraction == hit.mFraction &&
             result.mBodyID.GetIndexAndSequenceNumber() < hit.mBodyID.GetIndexAndSequenceNumber())) {
            hit = result;
            has = true;
            UpdateEarlyOutFraction(result.mFraction);
        }
    }

    JPH::RayCastResult hit;
    bool has = false;
};

class ClosestShapeCollector final : public JPH::CastShapeCollector
{
public:
    void AddHit(const JPH::ShapeCastResult& result) override
    {
        if (!has || result.mFraction < hit.mFraction ||
            (result.mFraction == hit.mFraction &&
             result.mBodyID2.GetIndexAndSequenceNumber() < hit.mBodyID2.GetIndexAndSequenceNumber())) {
            hit = result;
            has = true;
            UpdateEarlyOutFraction(result.mFraction);
        }
    }

    JPH::ShapeCastResult hit;
    bool has = false;
};

class OverlapCollector final : public JPH::CollideShapeCollector
{
public:
    void AddHit(const JPH::CollideShapeResult& result) override { bodies.push_back(result.mBodyID2); }

    JPH::Array<JPH::BodyID> bodies;
};

// --- The world --------------------------------------------------------------

class JoltWorld;

// Appends to the world's per-step pair buffer and does nothing else. Runs
// inside `PhysicsSystem::Update`, and from M7 on a worker thread -- so it may
// not touch the scene, allocate a script value, or decide an order.
class ContactRecorder final : public JPH::ContactListener
{
public:
    void OnContactAdded(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold&,
                        JPH::ContactSettings&) override
    {
        record(first, second);
    }

    void OnContactPersisted(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold&,
                            JPH::ContactSettings&) override
    {
        record(first, second);
    }

    // **The one place `collideConnected = false` can be implemented.**
    //
    // An upper arm and a lower arm overlap at the elbow by construction, and
    // left colliding they shove each other apart every step -- a ragdoll that
    // vibrates instead of falling. The exclusion is per PAIR and Jolt's object
    // layer matrix is per LAYER, so a collision group cannot express it: two
    // limbs of one character must ignore each other and still collide with the
    // two limbs of the next.
    //
    // Called from Jolt's collision jobs, so it takes the same lock the recorder
    // does -- and returns early on the common case, which is a world with no
    // constraints at all.
    JPH::ValidateResult OnContactValidate(const JPH::Body& first, const JPH::Body& second, JPH::RVec3Arg,
                                          const JPH::CollideShapeResult&) override
    {
        {
            const std::lock_guard<std::mutex> guard(m_mutex);
            if (!m_excluded.empty()) {
                ContactPair pair{first.GetUserData(), second.GetUserData()};
                if (pair.second < pair.first) {
                    std::swap(pair.first, pair.second);
                }
                if (std::binary_search(m_excluded.begin(), m_excluded.end(), pair)) {
                    return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
                }
            }
        }
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    // Sorted on insert, so the validate hook is a binary search rather than a
    // scan -- a ragdoll is a dozen pairs and a crowd of them is hundreds, and
    // this runs once per candidate pair per step.
    void exclude(u64 first, u64 second)
    {
        ContactPair pair{first, second};
        if (pair.second < pair.first) {
            std::swap(pair.first, pair.second);
        }
        const std::lock_guard<std::mutex> guard(m_mutex);
        const auto at = std::lower_bound(m_excluded.begin(), m_excluded.end(), pair);
        // Not deduplicated: two constraints between one pair are two exclusions,
        // and removing one must leave the other standing.
        m_excluded.insert(at, pair);
    }

    void unexclude(u64 first, u64 second)
    {
        ContactPair pair{first, second};
        if (pair.second < pair.first) {
            std::swap(pair.first, pair.second);
        }
        const std::lock_guard<std::mutex> guard(m_mutex);
        const auto at = std::lower_bound(m_excluded.begin(), m_excluded.end(), pair);
        if (at != m_excluded.end() && *at == pair) {
            m_excluded.erase(at);
        }
    }

    void clear()
    {
        // No lock: called between steps, from the simulation thread.
        m_pairs.clear();
    }

    [[nodiscard]] std::vector<ContactPair>& pairs() noexcept { return m_pairs; }

private:
    void record(const JPH::Body& first, const JPH::Body& second)
    {
        ContactPair pair{first.GetUserData(), second.GetUserData()};
        if (pair.second < pair.first) {
            std::swap(pair.first, pair.second);
        }
        const std::lock_guard<std::mutex> guard(m_mutex);
        m_pairs.push_back(pair);
    }

    std::mutex m_mutex;
    std::vector<ContactPair> m_pairs;
    // Sorted, and read under the same lock: the validate hook runs on Jolt's
    // worker threads while nothing may be writing here, but a constraint created
    // between steps writes from the simulation thread and the lock is what makes
    // the two safe against each other.
    std::vector<ContactPair> m_excluded;
};

#ifdef JPH_DEBUG_RENDERER
// Jolt's simple debug renderer draws everything as triangles and lines; we take
// the lines and turn the triangles into their three edges, because the engine's
// debug draw is a wireframe and a filled physics shape would hide the render
// mesh it is there to be compared against.
class DebugBridge final : public JPH::DebugRendererSimple
{
public:
    // The origin is passed in rather than read from a world, because the bridge
    // is handed a sink and never a world -- and a wireframe drawn in local space
    // while everything else is drawn in world space is D011 again, one rebase
    // later.
    DebugBridge(IDebugDrawSink& sink, core::DVec3 origin) : m_sink(sink), m_origin(origin) {}

    void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color) override
    {
        m_sink.line(toWorld(from), toWorld(to), toRgb(color));
    }

    void DrawTriangle(JPH::RVec3Arg v1, JPH::RVec3Arg v2, JPH::RVec3Arg v3, JPH::ColorArg color, ECastShadow) override
    {
        const u32 rgb = toRgb(color);
        m_sink.line(toWorld(v1), toWorld(v2), rgb);
        m_sink.line(toWorld(v2), toWorld(v3), rgb);
        m_sink.line(toWorld(v3), toWorld(v1), rgb);
    }

    void DrawText3D(JPH::RVec3Arg, const JPH::string_view&, JPH::ColorArg, float) override {}

private:
    [[nodiscard]] core::DVec3 toWorld(JPH::RVec3Arg v) const noexcept { return fromJoltPosition(v) + m_origin; }

    [[nodiscard]] static u32 toRgb(JPH::ColorArg color) noexcept
    {
        return (static_cast<u32>(color.r) << 16) | (static_cast<u32>(color.g) << 8) | static_cast<u32>(color.b);
    }

    IDebugDrawSink& m_sink;
    core::DVec3 m_origin;
};
#endif

// The three budgets a world is sized by, all derived from the body count so
// that one number in `WorldDesc` decides them together.
//
// A stack of a thousand crates produces far more contacts than bodies, and a
// buffer that overflows drops contacts -- which reads as parts sinking through
// each other rather than as an error. The temp allocator is derived from the
// same numbers because Jolt allocates its per-step working set from it in ONE
// request: sized independently, the first integration here asked for 30 MB from
// a 16 MB allocator and the process aborted with no message.
[[nodiscard]] JPH::uint bodyBudget(const WorldDesc& desc) noexcept
{
    return std::max<JPH::uint>(desc.maxBodies, 1024);
}

[[nodiscard]] JPH::uint contactBudget(const WorldDesc& desc) noexcept
{
    return bodyBudget(desc) * 2;
}

// **How many threads Jolt solves on, and why it is a constant** (S6.10).
//
// Jolt is deterministic across runs provided the thread count is the same, so
// this number is part of the world hash in the way a physics constant is: change
// it and every recorded trace in `tests/determinism` has to be re-recorded.
// Deriving it from the machine -- `hardware_concurrency`, or the engine job
// pool's worker count -- would make the SAME platform's trace differ between two
// machines, which is the one thing a committed trace cannot survive.
//
// Four rather than one, measured on `win-msvc-dev` at 1,000 and 10,000 bodies:
//
//   physics1k step   1.76 ms -> 0.65 ms
//   churn10k  step   3.73 ms -> 1.85 ms
//   churn10k  worst  174 ms  -> 40 ms
//
// Four rather than eight because the gain is in the solver's own parallelism and
// the tail flattens, and because a fixed count that oversubscribes a small
// machine costs more than the threads it adds. It is a number this project can
// revisit with a measurement and a trace re-record, which is what makes it a
// constant with a name rather than a literal at the call site.
inline constexpr int kPhysicsThreads = 4;

[[nodiscard]] JPH::uint tempBytes(const WorldDesc& desc) noexcept
{
    return 8u * 1024u * 1024u + contactBudget(desc) * 64u + contactBudget(desc) * 512u;
}

class JoltWorld
{
public:
    explicit JoltWorld(const WorldDesc& desc)
        : m_pairFilter(m_matrix), m_temp(tempBytes(desc)),
          m_jobs(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, kPhysicsThreads), m_gravity(desc.gravity),
          m_contactBudget(contactBudget(desc))
    {
        m_system.Init(bodyBudget(desc), 0, contactBudget(desc), contactBudget(desc), m_broadPhaseLayers,
                      m_objectVsBroadPhase, m_pairFilter);
        m_system.SetGravity(toJolt(desc.gravity));
        m_system.SetContactListener(&m_contacts);
    }

    ~JoltWorld()
    {
        JPH::BodyInterface& bodies = m_system.GetBodyInterface();
        for (const BodyRecord& record : m_bodies) {
            if (record.alive) {
                bodies.RemoveBody(record.id);
                bodies.DestroyBody(record.id);
            }
        }
    }

    JoltWorld(const JoltWorld&) = delete;
    JoltWorld& operator=(const JoltWorld&) = delete;

    // --- The floating origin (ADR 0014, architecture.md §10) ------------------
    //
    // Every position crossing this seam is ABSOLUTE f64 -- that is what `scene`
    // stores and what a script reads. Everything inside Jolt is f32 relative to
    // `m_origin`. These two are the whole translation, and they are members
    // rather than free functions precisely so that a call site cannot forget
    // which space it is in: there is no way to reach a body's position without
    // going through one of them.
    //
    // A DIRECTION never passes through here. A ray's direction is a
    // displacement, and shifting it would turn a hundred-metre ray into a
    // hundred-metre ray pointing at the old origin.
    [[nodiscard]] JPH::RVec3 toLocal(core::DVec3 v) const noexcept { return toJoltPosition(v - m_origin); }

    [[nodiscard]] core::DVec3 toWorld(JPH::RVec3Arg v) const noexcept { return fromJoltPosition(v) + m_origin; }

    [[nodiscard]] core::DVec3 origin() const noexcept { return m_origin; }

    // Moves the world under the simulation. Every resident body and character
    // shifts by the negative of the delta, so **nothing moves in absolute
    // terms** -- which is the entire point, and is why the test for this is a
    // hash rather than a picture.
    //
    // Velocities are untouched, and that is not an omission: a teleport that
    // reset them would stop a falling body dead every time the origin moved,
    // which is architecture.md's "velocity-preserving teleport" spelled out.
    // Bodies are NOT activated, because a sleeping body that wakes on a rebase
    // is a world that behaves differently depending on where the camera is.
    void setOrigin(core::DVec3 origin)
    {
        if (origin == m_origin) {
            return;
        }

        const JPH::Vec3 delta = toJolt(core::toVec3(origin - m_origin));
        m_origin = origin;

        JPH::BodyInterface& bodies = m_system.GetBodyInterface();
        for (const BodyRecord& record : m_bodies) {
            if (!record.alive) {
                continue;
            }
            JPH::RVec3 position;
            JPH::Quat rotation;
            bodies.GetPositionAndRotation(record.id, position, rotation);
            bodies.SetPositionAndRotation(record.id, position - delta, rotation, JPH::EActivation::DontActivate);
        }

        // A kinematic body's pending target is a POSITION and shifts with
        // everything else. Missing this would send every moving platform back
        // to where it was before the rebase, once, on the frame it happened.
        for (PendingMove& move : m_kinematicMoves) {
            move.position = move.position - delta;
        }

        for (const CharacterRecord& record : m_characters) {
            if (record.character != nullptr) {
                record.character->SetPosition(record.character->GetPosition() - delta);
            }
        }
    }

    void setGravity(core::Vec3 gravity)
    {
        m_gravity = gravity;
        m_system.SetGravity(toJolt(gravity));
    }

    [[nodiscard]] core::Vec3 gravity() const noexcept { return m_gravity; }

    // --- Bodies ---------------------------------------------------------------

    [[nodiscard]] BodyHandle createBody(const BodyDesc& desc)
    {
        const JPH::ShapeRefC shape = buildShape(desc.shape);
        if (shape == nullptr) {
            return {};
        }

        u32 slot = 0;
        if (!m_freeBodies.empty()) {
            slot = m_freeBodies.back();
            m_freeBodies.pop_back();
        }
        else {
            slot = static_cast<u32>(m_bodies.size());
            m_bodies.emplace_back();
        }

        BodyRecord& record = m_bodies[slot];
        // Generations start at one so a default-constructed handle is invalid,
        // and never wrap to zero for the same reason.
        record.generation = record.generation + 1 == 0 ? 1 : record.generation + 1;

        const BodyHandle handle{slot, record.generation};
        if (!instantiate(record, handle, desc, shape)) {
            record.alive = false;
            m_freeBodies.push_back(slot);
            return {};
        }
        return handle;
    }

    // A shape or motion-type change is a recreate on Jolt's side, and the
    // handle survives it: everything above holds one, and the caller asked for
    // the body to change rather than to be replaced. Velocity is carried across
    // so that resizing a falling part does not stop it in mid-air.
    [[nodiscard]] bool updateBody(BodyHandle handle, const BodyDesc& desc)
    {
        BodyRecord* record = resolve(handle);
        if (record == nullptr) {
            return false;
        }
        const JPH::ShapeRefC shape = buildShape(desc.shape);
        if (shape == nullptr) {
            // **The body stays what it was**, which is the useful answer:
            // replacing a working box with nothing would drop the part through
            // the floor. Reported so the caller can say so once rather than
            // retrying it every tick.
            //
            // What actually reaches here is a shape Jolt's builder rejects or a
            // description it cannot make -- and NOT, as this comment used to
            // claim, a coplanar quad: `ConvexHullBuilder` accepts one and gives
            // it a small thickness. Naming a case that does not happen is worse
            // than naming none, because somebody writes a test around it.
            return false;
        }

        const BodyState previous = bodyState(handle);
        forgetPairs(packHandle(handle));

        JPH::BodyInterface& bodies = m_system.GetBodyInterface();
        bodies.RemoveBody(record->id);
        bodies.DestroyBody(record->id);
        record->id = JPH::BodyID();

        if (!instantiate(*record, handle, desc, shape)) {
            record->alive = false;
            m_freeBodies.push_back(handle.index);
            return false;
        }
        if (desc.motion != MotionType::Static) {
            bodies.SetLinearAndAngularVelocity(record->id, toJolt(previous.linearVelocity),
                                               toJolt(previous.angularVelocity));
        }

        // The Jolt body underneath is a NEW one, so every constraint on it now
        // holds a pointer to the destroyed one. Rebuilt in creation order, so a
        // resized limb stays attached and the solve sequence does not move.
        rebuildConstraintsOn(handle);
        return true;
    }

    void destroyBody(BodyHandle handle)
    {
        BodyRecord* record = resolve(handle);
        if (record == nullptr) {
            return;
        }

        // BEFORE the body goes. A joint holding a body that is gone is a
        // dangling pointer inside the solver, and nothing about it is loud --
        // the interface states this as a contract because a caller sweeping
        // both must not be the only thing that remembers.
        retireConstraintsOn(handle);

        // A destroyed part does not fire TouchEnded -- the instance is gone,
        // and a signal on an instance nobody can reach is a signal nobody can
        // handle. Dropping its pairs here is what makes that true rather than
        // leaving an event referring to a body that no longer exists.
        forgetPairs(packHandle(handle));

        JPH::BodyInterface& bodies = m_system.GetBodyInterface();
        bodies.RemoveBody(record->id);
        bodies.DestroyBody(record->id);
        record->alive = false;
        record->id = JPH::BodyID();
        m_freeBodies.push_back(handle.index);
    }

    void setBodyTransform(BodyHandle handle, const core::CFrameD& transform)
    {
        BodyRecord* record = resolve(handle);
        if (record == nullptr) {
            return;
        }

        // **A kinematic body MOVES; it does not teleport** (D027).
        //
        // `SetPositionAndRotation` puts a body somewhere and derives no velocity
        // from having done so, so every script-moved kinematic body in this
        // engine had a velocity of zero: a closing door did not push, a piston
        // did not launch, a conveyor did not carry, and a character standing on
        // a moving platform stayed where it was while the platform left.
        //
        // `MoveKinematic` is the call that takes a target and a delta and
        // computes the velocity that gets there. It needs the delta, and the
        // only honest delta is the tick the simulation is ABOUT to take -- so
        // the target is recorded here and spent in `step`, where that number is.
        // Reading a wall clock for it would put a wall clock inside the
        // simulation, which is exactly what R10 forbids.
        if (record->motion == MotionType::Kinematic) {
            // Appended, never searched. Two writes to one body in one tick both
            // land and `step` applies them in order, so the last one wins --
            // which is what two property writes in one tick mean anyway.
            //
            // The first version deduplicated here by scanning the list, and that
            // was quadratic: `churn10k` moves six thousand anchored parts a tick
            // and each write walked every pending move before it, which cost
            // four milliseconds a tick that the benchmark found immediately.
            m_kinematicMoves.push_back(
                PendingMove{record->id, toLocal(transform.position), toJolt(transform.rotation)});
            return;
        }

        m_system.GetBodyInterface().SetPositionAndRotation(
            record->id, toLocal(transform.position), toJolt(transform.rotation),
            record->motion == MotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    }

    void setBodyVelocity(BodyHandle handle, core::Vec3 linear, core::Vec3 angular)
    {
        BodyRecord* record = resolve(handle);
        if (record == nullptr || record->motion == MotionType::Static) {
            return;
        }
        m_system.GetBodyInterface().SetLinearAndAngularVelocity(record->id, toJolt(linear), toJolt(angular));
    }

    void applyImpulse(BodyHandle handle, core::Vec3 impulse)
    {
        BodyRecord* record = resolve(handle);
        if (record == nullptr || record->motion != MotionType::Dynamic) {
            return;
        }
        m_system.GetBodyInterface().AddImpulse(record->id, toJolt(impulse));
    }

    void setBodyMaterial(BodyHandle handle, f32 friction, f32 restitution)
    {
        BodyRecord* record = resolve(handle);
        if (record == nullptr) {
            return;
        }
        JPH::BodyInterface& bodies = m_system.GetBodyInterface();
        bodies.SetFriction(record->id, friction);
        bodies.SetRestitution(record->id, restitution);
    }

    void setBodyFlags(BodyHandle handle, bool collidable, bool queryable)
    {
        BodyRecord* record = resolve(handle);
        if (record == nullptr) {
            return;
        }
        record->queryable = queryable;
        if (record->collidable != collidable) {
            record->collidable = collidable;
            m_system.GetBodyInterface().SetIsSensor(record->id, !collidable);
        }
    }

    void setBodyGroup(BodyHandle handle, CollisionGroup group)
    {
        BodyRecord* record = resolve(handle);
        if (record == nullptr || record->group == group) {
            return;
        }
        record->group = group;
        m_system.GetBodyInterface().SetObjectLayer(record->id,
                                                   encodeLayer(group, record->motion != MotionType::Static));
    }

    [[nodiscard]] BodyState bodyState(BodyHandle handle) const
    {
        const BodyRecord* record = resolve(handle);
        BodyState state;
        if (record == nullptr) {
            return state;
        }
        const JPH::BodyInterface& bodies = m_system.GetBodyInterface();
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(record->id, position, rotation);
        state.transform.position = toWorld(position);
        state.transform.rotation = fromJolt(rotation);
        JPH::Vec3 linear;
        JPH::Vec3 angular;
        bodies.GetLinearAndAngularVelocity(record->id, linear, angular);
        state.linearVelocity = fromJolt(linear);
        state.angularVelocity = fromJolt(angular);
        state.active = bodies.IsActive(record->id);
        return state;
    }

    void collectActiveBodies(std::vector<ActiveBody>& out) const
    {
        JPH::BodyIDVector active;
        m_system.GetActiveBodies(JPH::EBodyType::RigidBody, active);

        const usize first = out.size();
        for (const JPH::BodyID& id : active) {
            const u64 packed = m_system.GetBodyInterface().GetUserData(id);
            const BodyHandle handle = unpackHandle(packed);
            const BodyRecord* record = resolve(handle);
            if (record == nullptr) {
                continue;
            }
            out.push_back(ActiveBody{handle, record->userData, bodyState(handle)});
        }

        // Jolt documents this list as unordered under a multi-threaded job
        // system (Docs/Architecture.md:807). Sorting by our own handle makes
        // the order a property of when the caller created the body, which is
        // the scene's deterministic walk.
        std::sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(),
                  [](const ActiveBody& a, const ActiveBody& b) { return packHandle(a.body) < packHandle(b.body); });
    }

    // --- Simulation -----------------------------------------------------------

    void step(f32 fixedDt)
    {
        m_contacts.clear();

        // The kinematic targets a script set this tick, spent against the tick
        // the simulation is about to take (D027). In the order they were
        // written, which is `applyScene`'s pool order and therefore a pure
        // function of the operation sequence (R10).
        if (!m_kinematicMoves.empty()) {
            JPH::BodyInterface& bodies = m_system.GetBodyInterface();
            for (const PendingMove& pending : m_kinematicMoves)
                bodies.MoveKinematic(pending.id, pending.position, pending.rotation, fixedDt);
            m_kinematicMoves.clear();
        }

        const auto begin = std::chrono::steady_clock::now();
        // One collision step per tick. Jolt allows several sub-steps per call;
        // the sim tick already IS the substep grid, and a second, hidden one
        // would make `FixedTimestep` mean two different things.
        const JPH::EPhysicsUpdateError updateError = m_system.Update(fixedDt, 1, &m_temp, &m_jobs);
        reportUpdateError(updateError);
        const auto end = std::chrono::steady_clock::now();
        m_timings.step = std::chrono::duration<f64>(end - begin).count();

        buildContactEvents();
        collectCharacterContacts();
        buildCharacterContactEvents();
    }

    // **What Jolt returned, said out loud, once.**
    //
    // The return value used to be discarded, and Jolt's own assert then fired
    // with "an error occurred during the physics update, see
    // EPhysicsUpdateError for more information" -- a message that tells you to
    // go and look at something the log does not contain. Worse, in a build with
    // asserts off there is no message at all, and every one of these means the
    // same thing: **contacts were silently dropped**, which reads as parts
    // sinking through each other rather than as an error.
    //
    // Once per distinct kind rather than per tick: a full buffer is full on
    // every tick that follows, and a line a frame turns a log into a wall.
    void reportUpdateError(JPH::EPhysicsUpdateError error) noexcept
    {
        if (error == JPH::EPhysicsUpdateError::None)
            return;

        const auto fresh = static_cast<core::u32>(error) & ~m_reportedUpdateErrors;
        if (fresh == 0)
            return;
        m_reportedUpdateErrors |= fresh;

        // Named rather than numbered, and each name says which budget to raise.
        // All three come out of `PhysicsSystem::Init`'s contact arguments, which
        // `contactBudget` derives from `WorldDesc::maxBodies`.
        struct Named
        {
            JPH::EPhysicsUpdateError bit;
            std::string_view text;
        };
        static constexpr std::array<Named, 3> kNames{
            Named{JPH::EPhysicsUpdateError::ManifoldCacheFull, "the manifold cache is full"},
            Named{JPH::EPhysicsUpdateError::BodyPairCacheFull, "the body-pair cache is full"},
            Named{JPH::EPhysicsUpdateError::ContactConstraintsFull, "the contact constraint buffer is full"},
        };
        for (const Named& named : kNames) {
            if ((fresh & static_cast<core::u32>(named.bit)) == 0)
                continue;
            const std::array<core::I18nArg, 2> args{
                core::I18nArg{"reason", named.text},
                core::I18nArg{"budget", static_cast<core::i64>(m_contactBudget)},
            };
            core::log(core::LogLevel::Warn, LUAUG_TR("physics.jolt.warn.update_error"), args);
        }
    }

    [[nodiscard]] std::span<const ContactEvent> contacts() const noexcept { return m_events; }
    [[nodiscard]] StepTimings timings() const noexcept { return m_timings; }

    // --- Queries --------------------------------------------------------------

    [[nodiscard]] bool raycast(const RayD& ray, const QueryFilter& filter, RayHit& outHit) const
    {
        const JPH::RRayCast cast{toLocal(ray.origin), toJolt(ray.direction)};
        const BodyFilterAdapter bodyFilter(*this, filter);
        const LayerFilterAdapter layerFilter(filter);

        ClosestRayCollector collector;
        m_system.GetNarrowPhaseQuery().CastRay(cast, JPH::RayCastSettings{}, collector, JPH::BroadPhaseLayerFilter{},
                                               layerFilter, bodyFilter);
        if (!collector.has) {
            return false;
        }

        const BodyHandle handle = unpackHandle(m_system.GetBodyInterface().GetUserData(collector.hit.mBodyID));
        const BodyRecord* record = resolve(handle);
        if (record == nullptr) {
            return false;
        }

        const JPH::RVec3 point = cast.GetPointOnRay(collector.hit.mFraction);
        outHit.body = handle;
        outHit.userData = record->userData;
        outHit.position = toWorld(point);
        // The fraction is along the ray as given, and the ray's length is the
        // direction's magnitude -- `Workspace:Raycast(origin, direction)` takes
        // an unnormalised direction whose length IS the range.
        outHit.distance = collector.hit.mFraction * core::length(ray.direction);
        outHit.normal = surfaceNormal(collector.hit.mBodyID, collector.hit.mSubShapeID2, point);
        return true;
    }

    [[nodiscard]] bool spherecast(const RayD& ray, f32 radius, const QueryFilter& filter, RayHit& outHit) const
    {
        const JPH::SphereShape sphere(std::max(radius, 0.005f));
        const JPH::RShapeCast cast(&sphere, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(toLocal(ray.origin)),
                                   toJolt(ray.direction));
        const BodyFilterAdapter bodyFilter(*this, filter);
        const LayerFilterAdapter layerFilter(filter);

        ClosestShapeCollector collector;
        m_system.GetNarrowPhaseQuery().CastShape(cast, JPH::ShapeCastSettings{}, JPH::RVec3::sZero(), collector,
                                                 JPH::BroadPhaseLayerFilter{}, layerFilter, bodyFilter);
        if (!collector.has) {
            return false;
        }

        const BodyHandle handle = unpackHandle(m_system.GetBodyInterface().GetUserData(collector.hit.mBodyID2));
        const BodyRecord* record = resolve(handle);
        if (record == nullptr) {
            return false;
        }

        outHit.body = handle;
        outHit.userData = record->userData;
        outHit.position = toWorld(JPH::RVec3(collector.hit.mContactPointOn2));
        outHit.distance = collector.hit.mFraction * core::length(ray.direction);
        const JPH::Vec3 axis = collector.hit.mPenetrationAxis;
        outHit.normal = axis.IsNearZero() ? core::Vec3{0.0f, 1.0f, 0.0f} : fromJolt(-axis.Normalized());
        return true;
    }

    void overlapBox(const core::CFrameD& transform, core::Vec3 size, const QueryFilter& filter,
                    std::vector<u64>& out) const
    {
        const JPH::BoxShape box(JPH::Vec3(std::max(size.x * 0.5f, 0.005f), std::max(size.y * 0.5f, 0.005f),
                                          std::max(size.z * 0.5f, 0.005f)),
                                0.0f);
        const JPH::RMat44 centerOfMass =
            JPH::RMat44::sRotationTranslation(toJolt(transform.rotation), toLocal(transform.position));
        const BodyFilterAdapter bodyFilter(*this, filter);
        const LayerFilterAdapter layerFilter(filter);

        OverlapCollector collector;
        m_system.GetNarrowPhaseQuery().CollideShape(&box, JPH::Vec3::sOne(), centerOfMass, JPH::CollideShapeSettings{},
                                                    JPH::RVec3::sZero(), collector, JPH::BroadPhaseLayerFilter{},
                                                    layerFilter, bodyFilter);

        const usize first = out.size();
        for (const JPH::BodyID& id : collector.bodies) {
            const BodyHandle handle = unpackHandle(m_system.GetBodyInterface().GetUserData(id));
            const BodyRecord* record = resolve(handle);
            if (record != nullptr) {
                out.push_back(record->userData);
            }
        }
        // One entry per body, in an order the caller can rely on: a collide
        // query reports one hit per sub-shape, and its traversal order is
        // explicitly not deterministic (Docs/Architecture.md:805).
        std::sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end());
        out.erase(std::unique(out.begin() + static_cast<std::ptrdiff_t>(first), out.end()), out.end());
    }

    // --- Characters -----------------------------------------------------------

    [[nodiscard]] CharacterHandle createCharacter(const CharacterDesc& desc)
    {
        const f32 radius = std::max(desc.diameter * 0.5f, 0.01f);
        const f32 halfCylinder = std::max(desc.height * 0.5f - radius, 0.01f);

        JPH::CharacterVirtualSettings settings;
        settings.mShape = JPH::ShapeRefC(new JPH::CapsuleShape(halfCylinder, radius));
        settings.mMaxSlopeAngle = JPH::DegreesToRadians(desc.maxSlopeAngle);
        settings.mMass = desc.mass;
        // A character with no inner body is invisible to the simulation: other
        // bodies pass through it, which is not what "a capsule standing on a
        // seesaw" means. The inner body is what makes the character push and be
        // pushed against, and it is why a crate the capsule walks into moves.
        //
        // It is also what makes TWO characters collide, and that is worth
        // stating because the obvious reading of Jolt says they cannot: a
        // `CharacterVirtual` is not a `Body`, and `mCharacterVsCharacterCollision`
        // is null unless somebody sets it (`CharacterVirtual.h:696`). Both true
        // -- and beside the point here, because the thing another character
        // sweeps into is this inner body, which IS a `Body` and is in the
        // broad phase like any other.
        //
        // So `CharacterVsCharacterCollisionSimple` (`CharacterVirtual.h:246`) is
        // deliberately not used. It would be a second, redundant source of the
        // same contact; it is brute force over every registered character where
        // the inner bodies are already indexed by the broad phase; and its
        // `mCharacters` walk has no filter, so it would make character-against-
        // character the one pair in the world that ignores `CollisionGroup`.
        // The tests that hold this down are "two characters cannot walk through
        // each other" and "two characters whose groups do not collide walk
        // through each other" -- both go red if this line is removed.
        settings.mInnerBodyShape = settings.mShape;
        settings.mInnerBodyLayer = encodeLayer(desc.group, true);
        // No shape offset: `transform` is the character's CENTRE, like every
        // other `BasePart`'s, so Jolt's position and the capsule's centre are
        // the same point.
        //
        // The first version put the origin at the feet, on the reasoning that a
        // character stands somewhere. It made `CharacterBody` the one BasePart
        // whose `Position` did not mean the middle of its `Size`, and the
        // debug-draw bridge showed it the first frame it drew: the collider
        // capsule floated a half-height above the part's own box.

        u32 slot = 0;
        if (!m_freeCharacters.empty()) {
            slot = m_freeCharacters.back();
            m_freeCharacters.pop_back();
        }
        else {
            slot = static_cast<u32>(m_characters.size());
            m_characters.emplace_back();
        }

        CharacterRecord& record = m_characters[slot];
        record.generation = record.generation + 1 == 0 ? 1 : record.generation + 1;
        record.alive = true;
        record.stepHeight = desc.stepHeight;
        record.userData = desc.userData;
        record.layer = settings.mInnerBodyLayer;
        record.character = new JPH::CharacterVirtual(&settings, toLocal(desc.transform.position),
                                                     toJolt(desc.transform.rotation), desc.userData, &m_system);

        return CharacterHandle{slot, record.generation};
    }

    void destroyCharacter(CharacterHandle handle)
    {
        CharacterRecord* record = resolve(handle);
        if (record == nullptr) {
            return;
        }
        forgetCharacterPairs(packHandle(handle));
        record->character = nullptr;
        record->alive = false;
        m_freeCharacters.push_back(handle.index);
    }

    void moveCharacter(CharacterHandle handle, core::Vec3 velocity, f32 fixedDt)
    {
        CharacterRecord* record = resolve(handle);
        if (record == nullptr) {
            return;
        }

        // **The character inherits its ground's motion** (D027). Without this a
        // platform slides out from under a player who stays exactly where they
        // were -- the first thing anybody notices about a moving platform, and
        // the second half of the same defect: the velocity below reads zero
        // unless kinematic bodies are MOVED rather than teleported.
        //
        // Only while grounded. A character in mid-air is not standing on
        // anything, and carrying the last platform's velocity through a jump
        // would launch it.
        JPH::Vec3 inherited = JPH::Vec3::sZero();
        if (record->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround)
            inherited = record->character->GetGroundVelocity();

        record->character->SetLinearVelocity(toJolt(velocity) + inherited);

        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        settings.mWalkStairsStepUp = JPH::Vec3(0.0f, record->stepHeight, 0.0f);
        settings.mStickToFloorStepDown = JPH::Vec3(0.0f, -record->stepHeight, 0.0f);

        // The filters are the world's, not the defaults. A default-constructed
        // `ObjectLayerFilter` accepts every layer, which made the character the
        // one thing in the world that ignored `CollisionGroup`: a wall in a
        // group the character's group is set never to collide with still
        // stopped it, and nothing said so. `GetDefaultLayerFilter` asks the same
        // `ObjectPairFilter` every body pair goes through, against the
        // character's own layer.
        record->character->ExtendedUpdate(
            fixedDt, toJolt(m_gravity), settings, m_system.GetDefaultBroadPhaseLayerFilter(record->layer),
            m_system.GetDefaultLayerFilter(record->layer), JPH::BodyFilter{}, JPH::ShapeFilter{}, m_temp);
    }

    void setCharacterTransform(CharacterHandle handle, const core::CFrameD& transform)
    {
        CharacterRecord* record = resolve(handle);
        if (record == nullptr) {
            return;
        }
        record->character->SetPosition(toLocal(transform.position));
        record->character->SetRotation(toJolt(transform.rotation));
    }

    [[nodiscard]] CharacterState characterState(CharacterHandle handle) const
    {
        CharacterState state;
        const CharacterRecord* record = resolve(handle);
        if (record == nullptr) {
            return state;
        }

        state.transform.position = toWorld(record->character->GetPosition());
        state.transform.rotation = fromJolt(record->character->GetRotation());
        state.linearVelocity = fromJolt(record->character->GetLinearVelocity());

        // `Enum.CharacterState` has two items, and Jolt has three: standing on
        // ground too steep to walk on is `OnSteepGround`, which is airborne as
        // far as a jump is concerned and grounded as far as a fall is. It reads
        // as Airborne here, because the property a script branches on is "may I
        // jump".
        const JPH::CharacterBase::EGroundState ground = record->character->GetGroundState();
        state.ground = ground == JPH::CharacterBase::EGroundState::OnGround ? CharacterGround::Grounded
                                                                            : CharacterGround::Airborne;
        state.groundNormal = fromJolt(record->character->GetGroundNormal());

        const JPH::BodyID groundId = record->character->GetGroundBodyID();
        if (!groundId.IsInvalid()) {
            const BodyHandle body = unpackHandle(m_system.GetBodyInterface().GetUserData(groundId));
            const BodyRecord* groundRecord = resolve(body);
            if (groundRecord != nullptr) {
                state.groundBody = body;
                state.groundUserData = groundRecord->userData;
            }
        }
        return state;
    }

    // --- Collision groups -----------------------------------------------------

    [[nodiscard]] CollisionMatrix& matrix() noexcept { return m_matrix; }
    [[nodiscard]] const CollisionMatrix& matrix() const noexcept { return m_matrix; }

    void debugDraw([[maybe_unused]] IDebugDrawSink& sink)
    {
#ifdef JPH_DEBUG_RENDERER
        DebugBridge bridge(sink, m_origin);
        JPH::BodyManager::DrawSettings settings;
        settings.mDrawShape = true;
        settings.mDrawShapeWireframe = true;
        settings.mDrawVelocity = false;
        m_system.DrawBodies(settings, &bridge);
#endif
    }

    [[nodiscard]] const BodyRecord* resolve(BodyHandle handle) const noexcept
    {
        if (handle.index >= m_bodies.size()) {
            return nullptr;
        }
        const BodyRecord& record = m_bodies[handle.index];
        return record.alive && record.generation == handle.generation ? &record : nullptr;
    }

    // --- Constraints ----------------------------------------------------------

    [[nodiscard]] ConstraintHandle createConstraint(const ConstraintDesc& desc)
    {
        // Two bodies, and not the same one twice: Jolt asserts on a self-joint
        // in a debug build and solves nonsense in a release one.
        if (resolve(desc.first) == nullptr || resolve(desc.second) == nullptr || desc.first == desc.second) {
            return {};
        }

        JPH::Ref<JPH::TwoBodyConstraint> built = buildConstraint(desc);
        if (built == nullptr) {
            return {};
        }

        u32 slot = 0;
        if (!m_freeConstraints.empty()) {
            slot = m_freeConstraints.back();
            m_freeConstraints.pop_back();
        }
        else {
            slot = static_cast<u32>(m_constraints.size());
            m_constraints.emplace_back();
        }

        ConstraintRecord& record = m_constraints[slot];
        record.generation = record.generation + 1 == 0 ? 1 : record.generation + 1;
        record.alive = true;
        record.first = desc.first;
        record.second = desc.second;
        record.desc = desc;
        record.constraint = built;
        m_system.AddConstraint(built);
        applyExclusion(desc);
        return ConstraintHandle{slot, record.generation};
    }

    void destroyConstraint(ConstraintHandle handle)
    {
        ConstraintRecord* record = resolve(handle);
        if (record == nullptr) {
            return;
        }
        retireConstraint(*record);
        record->alive = false;
        m_freeConstraints.push_back(handle.index);
    }

    // A body the solver has put to sleep does not notice that the joint holding
    // it changed. It stays exactly where it was, for ever, and the symptom is a
    // constraint that "did not apply" -- which is indistinguishable from a bug
    // in the rebuild until you look at the activation state.
    void wakeBoth(const ConstraintRecord& record)
    {
        JPH::BodyInterface& bodies = m_system.GetBodyInterface();
        if (const BodyRecord* first = resolve(record.first); first != nullptr) {
            bodies.ActivateBody(first->id);
        }
        if (const BodyRecord* second = resolve(record.second); second != nullptr) {
            bodies.ActivateBody(second->id);
        }
    }

    void setConstraintEnabled(ConstraintHandle handle, bool enabled)
    {
        if (ConstraintRecord* record = resolve(handle); record != nullptr) {
            // The constraint stays IN the world. Removing and re-adding it would
            // move it to the end of the solve order, and a ragdoll that toggled
            // itself off and on would simulate differently afterwards.
            record->constraint->SetEnabled(enabled);
            wakeBoth(*record);
        }
    }

    void updateConstraint(ConstraintHandle handle, const ConstraintDesc& desc)
    {
        ConstraintRecord* record = resolve(handle);
        if (record == nullptr) {
            return;
        }
        // The two bodies and the type are what a constraint IS. Changing them is
        // a different joint and the caller rebuilds; swapping them here would
        // change what the solver holds without anything saying so.
        ConstraintDesc next = desc;
        next.type = record->desc.type;
        next.first = record->first;
        next.second = record->second;

        JPH::Ref<JPH::TwoBodyConstraint> built = buildConstraint(next);
        if (built == nullptr) {
            return;
        }
        const bool wasEnabled = record->constraint->GetEnabled();
        m_system.RemoveConstraint(record->constraint);
        dropExclusion(record->desc);
        record->constraint = built;
        record->desc = next;
        built->SetEnabled(wasEnabled);
        m_system.AddConstraint(built);
        applyExclusion(next);
        wakeBoth(*record);
    }

    [[nodiscard]] ConstraintState constraintState(ConstraintHandle handle) const
    {
        ConstraintState state;
        const ConstraintRecord* record = resolve(handle);
        if (record == nullptr) {
            return state;
        }
        state.enabled = record->constraint->GetEnabled();
        state.appliedImpulse = appliedImpulseOf(*record);
        return state;
    }

    // Every constraint that names this body, destroyed. Called BEFORE the body
    // is: a joint holding a body that is gone is a dangling pointer inside the
    // solver, and it is silent.
    void retireConstraintsOn(BodyHandle body)
    {
        for (u32 slot = 0; slot < static_cast<u32>(m_constraints.size()); ++slot) {
            ConstraintRecord& record = m_constraints[slot];
            if (!record.alive || (!(record.first == body) && !(record.second == body))) {
                continue;
            }
            retireConstraint(record);
            record.alive = false;
            m_freeConstraints.push_back(slot);
        }
    }

    // Every constraint on this body, rebuilt against the body that replaced it.
    //
    // In SLOT order, which is creation order, so a rebuilt limb solves in the
    // sequence it always did -- a resize that reordered the solve would change
    // the simulation, which is the kind of silent divergence R10 is about.
    void rebuildConstraintsOn(BodyHandle body)
    {
        for (u32 slot = 0; slot < static_cast<u32>(m_constraints.size()); ++slot) {
            ConstraintRecord& record = m_constraints[slot];
            if (!record.alive || (!(record.first == body) && !(record.second == body))) {
                continue;
            }
            m_system.RemoveConstraint(record.constraint);
            JPH::Ref<JPH::TwoBodyConstraint> built = buildConstraint(record.desc);
            if (built == nullptr) {
                // The body could not be re-joined. Dropped rather than left
                // pointing at the one that was destroyed.
                dropExclusion(record.desc);
                record.constraint = nullptr;
                record.alive = false;
                m_freeConstraints.push_back(slot);
                continue;
            }
            record.constraint = built;
            m_system.AddConstraint(built);
        }
    }

private:
    // The joint frame in WORLD space, from the body's current transform and the
    // frame the caller gave in that body's own space.
    //
    // **This is what sidesteps the centre-of-mass trap.** Jolt's
    // `LocalToBodyCOM` space is relative to the centre of mass and not to the
    // body origin, and a hull MeshPart's two are not the same point -- a joint
    // authored at a shoulder would end up wherever the arm's mass happened to
    // balance. Handing Jolt world space lets IT do that conversion, which it is
    // guaranteed to do consistently with its own solver.
    [[nodiscard]] JPH::RMat44 jointFrame(BodyHandle body, const core::CFrameD& local) const
    {
        const core::CFrameD world = bodyState(body).transform * local;
        return JPH::RMat44::sRotationTranslation(toJolt(world.rotation), toLocal(world.position));
    }

    [[nodiscard]] JPH::Ref<JPH::TwoBodyConstraint> buildConstraint(const ConstraintDesc& desc)
    {
        const BodyRecord* firstRecord = resolve(desc.first);
        const BodyRecord* secondRecord = resolve(desc.second);
        if (firstRecord == nullptr || secondRecord == nullptr) {
            return nullptr;
        }
        // **Both frames BEFORE either lock, and that order is load-bearing.**
        // `jointFrame` reads the body's transform through the LOCKING body
        // interface, so computing one inside the write locks below is a
        // recursive lock on a body this thread already holds -- which is not an
        // error, an assert or a slowdown: the process simply stops, at zero CPU,
        // with no output. It cost one wedged test run to find.
        const JPH::RMat44 frameOne = jointFrame(desc.first, desc.firstFrame);
        const JPH::RMat44 frameTwo = jointFrame(desc.second, desc.secondFrame);

        // **One multi-lock, not two single ones.** Taking two body write locks
        // in a row is a lock-ordering bug: Jolt's own assertion says so
        // ("a lock of same or higher priority was already taken, this can create
        // a deadlock"), and it fired fifty-six times before this was written
        // that way. `BodyLockMultiWrite` sorts the ids and takes them in the one
        // order every caller agrees on.
        const JPH::BodyID ids[2] = {firstRecord->id, secondRecord->id};
        const JPH::BodyLockMultiWrite lock(m_system.GetBodyLockInterface(), ids, 2);
        JPH::Body* first = lock.GetBody(0);
        JPH::Body* second = lock.GetBody(1);
        if (first == nullptr || second == nullptr) {
            return nullptr;
        }

        // X is the joint axis and Y the reference direction a limit is measured
        // from, in every type below. One frame, read one way, whatever the joint
        // then does with it.
        switch (desc.type) {
        case ConstraintType::Fixed: {
            JPH::FixedConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            // The frames as given rather than "wherever they are now": a weld
            // authored to hold two parts a metre apart must hold them a metre
            // apart, and auto-detection would silently substitute their current
            // relative pose for the one that was asked for.
            settings.mAutoDetectPoint = false;
            settings.mPoint1 = frameOne.GetTranslation();
            settings.mAxisX1 = frameOne.GetAxisX();
            settings.mAxisY1 = frameOne.GetAxisY();
            settings.mPoint2 = frameTwo.GetTranslation();
            settings.mAxisX2 = frameTwo.GetAxisX();
            settings.mAxisY2 = frameTwo.GetAxisY();
            return settings.Create(*first, *second);
        }
        case ConstraintType::Point: {
            JPH::PointConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mPoint1 = frameOne.GetTranslation();
            settings.mPoint2 = frameTwo.GetTranslation();
            return settings.Create(*first, *second);
        }
        case ConstraintType::Hinge: {
            JPH::HingeConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mPoint1 = frameOne.GetTranslation();
            settings.mHingeAxis1 = frameOne.GetAxisX();
            settings.mNormalAxis1 = frameOne.GetAxisY();
            settings.mPoint2 = frameTwo.GetTranslation();
            settings.mHingeAxis2 = frameTwo.GetAxisX();
            settings.mNormalAxis2 = frameTwo.GetAxisY();
            if (desc.limitLow <= desc.limitHigh) {
                settings.mLimitsMin = desc.limitLow;
                settings.mLimitsMax = desc.limitHigh;
            }
            applyMotor(settings.mMotorSettings, desc);
            JPH::Ref<JPH::TwoBodyConstraint> made = settings.Create(*first, *second);
            driveMotor(static_cast<JPH::HingeConstraint*>(made.GetPtr()), desc);
            return made;
        }
        case ConstraintType::SwingTwist: {
            JPH::SwingTwistConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mPosition1 = frameOne.GetTranslation();
            settings.mTwistAxis1 = frameOne.GetAxisX();
            settings.mPlaneAxis1 = frameOne.GetAxisY();
            settings.mPosition2 = frameTwo.GetTranslation();
            settings.mTwistAxis2 = frameTwo.GetAxisX();
            settings.mPlaneAxis2 = frameTwo.GetAxisY();
            // One cone rather than an ellipse: a shoulder's two half-angles are
            // rarely different enough to be worth authoring separately, and the
            // ellipse is there in Jolt if a profile ever asks for it.
            settings.mNormalHalfConeAngle = desc.swingLimit;
            settings.mPlaneHalfConeAngle = desc.swingLimit;
            settings.mTwistMinAngle = -desc.twistLimit;
            settings.mTwistMaxAngle = desc.twistLimit;
            return settings.Create(*first, *second);
        }
        case ConstraintType::Slider: {
            JPH::SliderConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mAutoDetectPoint = false;
            settings.mPoint1 = frameOne.GetTranslation();
            settings.mSliderAxis1 = frameOne.GetAxisX();
            settings.mNormalAxis1 = frameOne.GetAxisY();
            settings.mPoint2 = frameTwo.GetTranslation();
            settings.mSliderAxis2 = frameTwo.GetAxisX();
            settings.mNormalAxis2 = frameTwo.GetAxisY();
            if (desc.limitLow <= desc.limitHigh) {
                settings.mLimitsMin = desc.limitLow;
                settings.mLimitsMax = desc.limitHigh;
            }
            applyMotor(settings.mMotorSettings, desc);
            JPH::Ref<JPH::TwoBodyConstraint> made = settings.Create(*first, *second);
            driveMotor(static_cast<JPH::SliderConstraint*>(made.GetPtr()), desc);
            return made;
        }
        case ConstraintType::Distance: {
            JPH::DistanceConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mPoint1 = frameOne.GetTranslation();
            settings.mPoint2 = frameTwo.GetTranslation();
            settings.mMinDistance = desc.minDistance;
            settings.mMaxDistance = desc.maxDistance;
            return settings.Create(*first, *second);
        }
        }
        return nullptr;
    }

    // Honoured by Hinge and Slider, which are the two with something to drive.
    // Ignored by the rest, and the field's own doc says which types read it.
    static void applyMotor(JPH::MotorSettings& settings, const ConstraintDesc& desc)
    {
        if (desc.motor == MotorMode::Off) {
            return;
        }
        settings.SetForceLimit(desc.motorMaxForce);
        settings.SetTorqueLimit(desc.motorMaxForce);
    }

    static void setMotorVelocity(JPH::HingeConstraint* c, f32 target) { c->SetTargetAngularVelocity(target); }
    static void setMotorVelocity(JPH::SliderConstraint* c, f32 target) { c->SetTargetVelocity(target); }
    static void setMotorPosition(JPH::HingeConstraint* c, f32 target) { c->SetTargetAngle(target); }
    static void setMotorPosition(JPH::SliderConstraint* c, f32 target) { c->SetTargetPosition(target); }

    template <typename T>
    static void driveMotor(T* constraint, const ConstraintDesc& desc)
    {
        if (constraint == nullptr || desc.motor == MotorMode::Off) {
            return;
        }
        if (desc.motor == MotorMode::Velocity) {
            constraint->SetMotorState(JPH::EMotorState::Velocity);
            setMotorVelocity(constraint, desc.motorTarget);
            return;
        }
        constraint->SetMotorState(JPH::EMotorState::Position);
        setMotorPosition(constraint, desc.motorTarget);
    }

    // In newton-seconds, and a magnitude rather than a vector: this number is
    // what a breakable joint is a threshold on, and its direction says nothing a
    // caller at this seam can use.
    [[nodiscard]] static f32 appliedImpulseOf(const ConstraintRecord& record)
    {
        const JPH::TwoBodyConstraint* constraint = record.constraint.GetPtr();
        if (constraint == nullptr) {
            return 0.0f;
        }
        switch (record.desc.type) {
        case ConstraintType::Fixed:
            return static_cast<const JPH::FixedConstraint*>(constraint)->GetTotalLambdaPosition().Length();
        case ConstraintType::Point:
            return static_cast<const JPH::PointConstraint*>(constraint)->GetTotalLambdaPosition().Length();
        case ConstraintType::Hinge:
            return static_cast<const JPH::HingeConstraint*>(constraint)->GetTotalLambdaPosition().Length();
        case ConstraintType::SwingTwist:
            return static_cast<const JPH::SwingTwistConstraint*>(constraint)->GetTotalLambdaPosition().Length();
        case ConstraintType::Slider:
            return static_cast<const JPH::SliderConstraint*>(constraint)->GetTotalLambdaPosition().Length();
        case ConstraintType::Distance:
            return std::fabs(static_cast<const JPH::DistanceConstraint*>(constraint)->GetTotalLambdaPosition());
        }
        return 0.0f;
    }

    // Removed from the world, exclusion dropped, reference released. Separate
    // from `destroyConstraint` because retiring a BODY does the same to every
    // joint that names it, and doing it twice is a use-after-free.
    void retireConstraint(ConstraintRecord& record)
    {
        if (record.constraint != nullptr) {
            m_system.RemoveConstraint(record.constraint);
        }
        dropExclusion(record.desc);
        record.constraint = nullptr;
    }

    [[nodiscard]] ConstraintRecord* resolve(ConstraintHandle handle) noexcept
    {
        return const_cast<ConstraintRecord*>(static_cast<const JoltWorld*>(this)->resolve(handle));
    }

    [[nodiscard]] const ConstraintRecord* resolve(ConstraintHandle handle) const noexcept
    {
        if (handle.index >= m_constraints.size()) {
            return nullptr;
        }
        const ConstraintRecord& record = m_constraints[handle.index];
        return record.alive && record.generation == handle.generation ? &record : nullptr;
    }

    void applyExclusion(const ConstraintDesc& desc)
    {
        if (desc.collideConnected) {
            return;
        }
        m_contacts.exclude(packHandle(desc.first), packHandle(desc.second));
    }

    void dropExclusion(const ConstraintDesc& desc)
    {
        if (desc.collideConnected) {
            return;
        }
        m_contacts.unexclude(packHandle(desc.first), packHandle(desc.second));
    }

    [[nodiscard]] BodyRecord* resolve(BodyHandle handle) noexcept
    {
        return const_cast<BodyRecord*>(static_cast<const JoltWorld*>(this)->resolve(handle));
    }

    [[nodiscard]] const CharacterRecord* resolve(CharacterHandle handle) const noexcept
    {
        if (handle.index >= m_characters.size()) {
            return nullptr;
        }
        const CharacterRecord& record = m_characters[handle.index];
        return record.alive && record.generation == handle.generation ? &record : nullptr;
    }

    [[nodiscard]] CharacterRecord* resolve(CharacterHandle handle) noexcept
    {
        return const_cast<CharacterRecord*>(static_cast<const JoltWorld*>(this)->resolve(handle));
    }

    // The half of body creation that both `createBody` and `updateBody` need:
    // everything from the desc onto the record and into Jolt, with the handle
    // already decided.
    [[nodiscard]] bool instantiate(BodyRecord& record, BodyHandle handle, const BodyDesc& desc,
                                   const JPH::ShapeRefC& shape)
    {
        record.alive = true;
        record.collidable = desc.collidable;
        record.queryable = desc.queryable;
        record.motion = desc.motion;
        record.group = desc.group;
        record.userData = desc.userData;

        JPH::BodyCreationSettings settings(shape, toLocal(desc.transform.position), toJolt(desc.transform.rotation),
                                           toJoltMotion(desc.motion),
                                           encodeLayer(desc.group, desc.motion != MotionType::Static));
        settings.mFriction = desc.friction;
        settings.mRestitution = desc.restitution;
        settings.mIsSensor = !desc.collidable;
        settings.mUserData = packHandle(handle);
        // Mass is volume times `BasePart.Density`, and there is no `Mass`
        // property precisely so that the two cannot disagree. `CalculateInertia`
        // takes the mass from here and derives the inertia tensor from the
        // shape, which is the only combination that keeps a dense small part
        // and a light large one behaving differently under a torque.
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = std::max(shape->GetVolume() * std::max(desc.density, 0.0001f), 0.001f);

        JPH::BodyInterface& bodies = m_system.GetBodyInterface();
        record.id = bodies.CreateAndAddBody(settings, desc.motion == MotionType::Static ? JPH::EActivation::DontActivate
                                                                                        : JPH::EActivation::Activate);
        return !record.id.IsInvalid();
    }

    [[nodiscard]] static JPH::EMotionType toJoltMotion(MotionType motion) noexcept
    {
        switch (motion) {
        case MotionType::Static:
            return JPH::EMotionType::Static;
        case MotionType::Kinematic:
            return JPH::EMotionType::Kinematic;
        case MotionType::Dynamic:
            break;
        }
        return JPH::EMotionType::Dynamic;
    }

    [[nodiscard]] core::Vec3 surfaceNormal(const JPH::BodyID& id, const JPH::SubShapeID& subShape,
                                           JPH::RVec3Arg point) const
    {
        const JPH::BodyLockRead lock(m_system.GetBodyLockInterface(), id);
        if (!lock.Succeeded()) {
            return core::Vec3{0.0f, 1.0f, 0.0f};
        }
        return fromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(subShape, point));
    }

    void forgetPairs(u64 packed)
    {
        const auto drop = [packed](const ContactPair& pair) { return pair.first == packed || pair.second == packed; };
        m_previousPairs.erase(std::remove_if(m_previousPairs.begin(), m_previousPairs.end(), drop),
                              m_previousPairs.end());

        // A character standing on the body being destroyed would otherwise carry
        // the contact forever: the pair can never appear again, so the diff can
        // never fire its `TouchEnded`, and `emitCharacter` would refuse it
        // anyway once the record is gone.
        const auto dropCharacter = [packed](const CharacterPair& pair) {
            return !pair.otherIsCharacter && pair.other == packed;
        };
        m_previousCharacterPairs.erase(
            std::remove_if(m_previousCharacterPairs.begin(), m_previousCharacterPairs.end(), dropCharacter),
            m_previousCharacterPairs.end());
    }

    void forgetCharacterPairs(u64 packed)
    {
        const auto drop = [packed](const CharacterPair& pair) {
            return pair.character == packed || (pair.otherIsCharacter && pair.other == packed);
        };
        m_previousCharacterPairs.erase(
            std::remove_if(m_previousCharacterPairs.begin(), m_previousCharacterPairs.end(), drop),
            m_previousCharacterPairs.end());
    }

    // The whole of `Touched`/`TouchEnded`: this tick's contacting pairs against
    // last tick's. A pair that appears fires Began, one that disappears fires
    // Ended, one that stays fires nothing. Built from a diff rather than from
    // Jolt's OnContactRemoved because that callback can arrive for a body the
    // caller has already destroyed, and because the diff is what makes the
    // event order ours.
    void buildContactEvents()
    {
        m_events.clear();

        std::vector<ContactPair>& current = m_contacts.pairs();
        std::sort(current.begin(), current.end());
        current.erase(std::unique(current.begin(), current.end()), current.end());

        m_carried.clear();
        usize i = 0;
        usize j = 0;
        while (i < current.size() || j < m_previousPairs.size()) {
            if (j == m_previousPairs.size() || (i < current.size() && current[i] < m_previousPairs[j])) {
                m_carried.push_back(current[i]);
                emit(ContactPhase::Began, current[i]);
                ++i;
            }
            else if (i == current.size() || m_previousPairs[j] < current[i]) {
                // A pair that stops being reported has not necessarily
                // separated: Jolt stops calling the listener for an island it
                // has put to sleep, so a crate that settles on the floor and
                // dozes off would otherwise fire TouchEnded while still visibly
                // resting on it -- and Touched again the moment anything nudged
                // it. Both bodies asleep means the contact is still there and
                // nobody is looking at it.
                if (asleep(m_previousPairs[j])) {
                    m_carried.push_back(m_previousPairs[j]);
                }
                else {
                    emit(ContactPhase::Ended, m_previousPairs[j]);
                }
                ++j;
            }
            else {
                m_carried.push_back(current[i]);
                ++i;
                ++j;
            }
        }

        m_previousPairs = m_carried;
    }

    // True when neither body of the pair is being simulated -- a static body is
    // never active, and a dynamic one stops being active when the solver puts
    // its island to sleep.
    [[nodiscard]] bool asleep(const ContactPair& pair) const
    {
        const BodyRecord* first = resolve(unpackHandle(pair.first));
        const BodyRecord* second = resolve(unpackHandle(pair.second));
        if (first == nullptr || second == nullptr) {
            return false;
        }
        const JPH::BodyInterface& bodies = m_system.GetBodyInterface();
        return !bodies.IsActive(first->id) && !bodies.IsActive(second->id);
    }

    // **`Touched` for a character's contacts, all of them** (D028).
    //
    // A `CharacterBody` is a `BasePart`, so a script reasonably expects
    // `Touched` from one -- and the rigid-body contact listener cannot give it,
    // because a `CharacterVirtual` is not a body in the broad phase. M6 answered
    // the half an obby needs by diffing the surface under the character's feet
    // in the scene glue, and left a wall walked into firing nothing.
    //
    // This is the whole of it instead, and the ground half moved here with it:
    // two mechanisms for one signal is how the two disagree. `GetActiveContacts`
    // is what the character's own update already collected
    // (`CharacterVirtual.h:511`), so the cost is a walk over a handful of
    // contacts and no extra collision work.
    //
    // The order is `m_characters`' own slot order and then the sort in
    // `buildCharacterContactEvents`, so nothing about how Jolt's job system
    // happened to schedule the sweep reaches the event stream (R10).
    void collectCharacterContacts()
    {
        m_characterPairs.clear();
        const JPH::BodyInterface& bodies = m_system.GetBodyInterface();

        for (usize slot = 0; slot < m_characters.size(); ++slot) {
            const CharacterRecord& record = m_characters[slot];
            if (!record.alive || record.character == nullptr)
                continue;

            const u64 self = packHandle(CharacterHandle{static_cast<u32>(slot), record.generation});
            for (const JPH::CharacterContact& contact : record.character->GetActiveContacts()) {
                // A PREDICTIVE contact is one the sweep found ahead of the
                // character and never reached: `mHadCollision` is what separates
                // "touching" from "about to". A discarded one was refused by the
                // validate callback and never happened at all.
                if (!contact.mHadCollision || contact.mWasDiscarded)
                    continue;
                // Another `CharacterVirtual` directly, which this engine never
                // produces: `mCharacterVsCharacterCollision` is deliberately
                // unset (see `createCharacter`), so character-against-character
                // arrives as the inner BODY below.
                if (contact.mBodyB.IsInvalid())
                    continue;

                CharacterPair pair;
                pair.character = self;
                if (const CharacterHandle peer = characterOfInnerBody(contact.mBodyB); peer.valid()) {
                    // Skip the pair a character makes with its own inner body,
                    // which is a contact with itself and not an event.
                    if (packHandle(peer) == self)
                        continue;
                    pair.other = packHandle(peer);
                    pair.otherIsCharacter = true;
                }
                else {
                    pair.other = bodies.GetUserData(contact.mBodyB);
                }
                m_characterPairs.push_back(pair);
            }
        }
    }

    // Which character owns this body, when the body is a character's inner one.
    //
    // Linear over the character table, which is a handful of entries and is
    // walked only for contacts a character actually has. A map keyed by `BodyID`
    // would be the answer if that stopped being true.
    [[nodiscard]] CharacterHandle characterOfInnerBody(JPH::BodyID id) const noexcept
    {
        for (usize slot = 0; slot < m_characters.size(); ++slot) {
            const CharacterRecord& record = m_characters[slot];
            if (record.alive && record.character != nullptr && record.character->GetInnerBodyID() == id)
                return CharacterHandle{static_cast<u32>(slot), record.generation};
        }
        return CharacterHandle{};
    }

    // The same diff `buildContactEvents` makes, without the sleep exception --
    // see `CharacterPair` for why there is nothing to except.
    void buildCharacterContactEvents()
    {
        std::sort(m_characterPairs.begin(), m_characterPairs.end());
        m_characterPairs.erase(std::unique(m_characterPairs.begin(), m_characterPairs.end()), m_characterPairs.end());

        usize i = 0;
        usize j = 0;
        while (i < m_characterPairs.size() || j < m_previousCharacterPairs.size()) {
            if (j == m_previousCharacterPairs.size() ||
                (i < m_characterPairs.size() && m_characterPairs[i] < m_previousCharacterPairs[j])) {
                emitCharacter(ContactPhase::Began, m_characterPairs[i]);
                ++i;
            }
            else if (i == m_characterPairs.size() || m_previousCharacterPairs[j] < m_characterPairs[i]) {
                emitCharacter(ContactPhase::Ended, m_previousCharacterPairs[j]);
                ++j;
            }
            else {
                ++i;
                ++j;
            }
        }

        m_previousCharacterPairs = m_characterPairs;
    }

    void emitCharacter(ContactPhase phase, const CharacterPair& pair)
    {
        const CharacterHandle character = unpackCharacter(pair.character);
        const CharacterRecord* record = resolve(character);
        if (record == nullptr)
            return;

        // The BODY side of the event is left invalid for a character, because a
        // character does not have one. `firstUserData` and `secondUserData` are
        // filled either way, and they are what the scene glue reads.
        ContactEvent event;
        event.phase = phase;
        event.firstUserData = record->userData;

        if (pair.otherIsCharacter) {
            const CharacterRecord* peer = resolve(unpackCharacter(pair.other));
            if (peer == nullptr)
                return;
            event.secondUserData = peer->userData;
        }
        else {
            const BodyHandle other = unpackHandle(pair.other);
            const BodyRecord* otherRecord = resolve(other);
            if (otherRecord == nullptr)
                return;
            event.second = other;
            event.secondUserData = otherRecord->userData;
        }
        m_events.push_back(event);
    }

    void emit(ContactPhase phase, const ContactPair& pair)
    {
        const BodyHandle first = unpackHandle(pair.first);
        const BodyHandle second = unpackHandle(pair.second);
        const BodyRecord* firstRecord = resolve(first);
        const BodyRecord* secondRecord = resolve(second);
        if (firstRecord == nullptr || secondRecord == nullptr) {
            return;
        }
        m_events.push_back(ContactEvent{phase, first, second, firstRecord->userData, secondRecord->userData});
    }

    // Filters translating a `QueryFilter` into the two things Jolt asks for.
    // Both are stack objects living for the duration of one query.
    class BodyFilterAdapter final : public JPH::BodyFilter
    {
    public:
        BodyFilterAdapter(const JoltWorld& world, const QueryFilter& filter) : m_world(world), m_filter(filter) {}

        [[nodiscard]] bool ShouldCollideLocked(const JPH::Body& body) const override
        {
            const BodyHandle handle = unpackHandle(body.GetUserData());
            const BodyRecord* record = m_world.resolve(handle);
            if (record == nullptr || !record->queryable) {
                return false;
            }

            const bool listed = std::find(m_filter.userData.begin(), m_filter.userData.end(), record->userData) !=
                                m_filter.userData.end();
            return m_filter.mode == QueryFilter::Mode::Exclude ? !listed : listed;
        }

    private:
        const JoltWorld& m_world;
        const QueryFilter& m_filter;
    };

    class LayerFilterAdapter final : public JPH::ObjectLayerFilter
    {
    public:
        explicit LayerFilterAdapter(const QueryFilter& filter) : m_filter(filter) {}

        [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer) const override
        {
            return !m_filter.filterGroup || decodeGroup(layer) == m_filter.group;
        }

    private:
        const QueryFilter& m_filter;
    };

    CollisionMatrix m_matrix;
    BroadPhaseLayers m_broadPhaseLayers;
    ObjectVsBroadPhaseFilter m_objectVsBroadPhase;
    ObjectPairFilter m_pairFilter;
    JPH::TempAllocatorImpl m_temp;
    // **Jolt's own pool, not the engine's** (S6.10, ADR 0064). The engine job
    // pool sizes itself from the machine, and Jolt's determinism is per thread
    // COUNT -- so running the solver on it would make a trace recorded here
    // unreproducible on a machine with a different core count, which is the one
    // property `tests/determinism` exists to hold. A fixed sub-pool of the
    // engine's would be the same threads with more code between them and the
    // same fixed number.
    JPH::JobSystemThreadPool m_jobs;
    core::DVec3 m_origin;
    JPH::PhysicsSystem m_system;
    ContactRecorder m_contacts;

    std::vector<BodyRecord> m_bodies;
    std::vector<u32> m_freeBodies;
    std::vector<CharacterRecord> m_characters;
    std::vector<u32> m_freeCharacters;
    // Slot-indexed and never compacted, exactly as the bodies are: a handle is
    // an index plus a generation, and Jolt solves in the order constraints were
    // added, so a caller that creates them in a stable order gets a stable
    // solve (R10).
    std::vector<ConstraintRecord> m_constraints;
    std::vector<u32> m_freeConstraints;

    std::vector<ContactPair> m_previousPairs;
    // The character half of the same diff (D028).
    std::vector<CharacterPair> m_characterPairs;
    std::vector<CharacterPair> m_previousCharacterPairs;
    // Cleared every `step`; see `setBodyTransform`.
    std::vector<PendingMove> m_kinematicMoves;
    // Scratch for the diff, kept as a member so a tick with ten thousand
    // contacts does not allocate one.
    std::vector<ContactPair> m_carried;
    std::vector<ContactEvent> m_events;
    StepTimings m_timings;
    core::Vec3 m_gravity{0.0f, -9.81f, 0.0f};
    // What `PhysicsSystem::Init` was told, so a report about a full buffer can
    // say how full is full.
    JPH::uint m_contactBudget = 0;
    // Which `EPhysicsUpdateError` bits have already been reported. A full buffer
    // stays full, and a line a tick would bury everything else in the log.
    core::u32 m_reportedUpdateErrors = 0;
};

// --- Process-wide Jolt state ------------------------------------------------
//
// Jolt has three globals that must be set up before any of its types exist and
// torn down after the last of them is gone: the allocator, the factory and the
// type registry. Reference-counted here rather than initialised at static
// construction time, because a static initialiser would run in a test binary
// that never creates a physics world and would leak the factory in a process
// that creates one and destroys it.
// Jolt's own diagnostics, routed through the engine log rather than to a
// console nobody is reading. The text is upstream's and is not translated --
// what carries the i18n key (R3) is the line around it, exactly as the catalog
// reader's own developer diagnostics do.
// The attribute is what lets Clang see that `format` reaches `vsnprintf` from a
// printf-like parameter rather than from a runtime string: without it,
// -Wformat-nonliteral is an error on the Tier-2 build and MSVC says nothing at
// all. The Linux tier found this.
#if defined(__clang__) || defined(__GNUC__)
void traceImpl(const char* format, ...) __attribute__((format(printf, 1, 2)));
#endif

void traceImpl(const char* format, ...)
{
    va_list list;
    va_start(list, format);
    char buffer[1024];
    std::vsnprintf(buffer, sizeof(buffer), format, list);
    va_end(list);
    // `logText` rather than a keyed line: this string is upstream's, and R3's
    // rule is that engine-AUTHORED prose goes through the catalog. Wrapping
    // somebody else's diagnostic in a translated sentence would translate the
    // half nobody reads and leave the half that matters in English.
    core::logText(core::LogLevel::Debug, std::string_view(buffer));
}

#ifdef JPH_ENABLE_ASSERTS
// Returning false means "do not break". An assert here is Jolt telling us we
// misused it -- a degenerate shape, a velocity set on a static body -- and the
// engine that reports it and keeps running is more useful than the one that
// dies, because the report names the call site and the crash would not.
bool assertFailedImpl(const char* expression, const char* message, const char* file, JPH::uint line)
{
    const std::string_view text = message != nullptr ? std::string_view(message) : std::string_view{};
    const std::array<core::I18nArg, 4> args{
        core::I18nArg{"file", std::string_view(file)},
        core::I18nArg{"line", static_cast<core::i64>(line)},
        core::I18nArg{"expression", std::string_view(expression)},
        core::I18nArg{"message", text},
    };
    core::log(core::LogLevel::Error, LUAUG_TR("physics.jolt.err.assert"), args);
    return false;
}
#endif

class JoltRuntime
{
public:
    static void acquire()
    {
        if (s_refs++ == 0) {
            JPH::RegisterDefaultAllocator();
            JPH::Trace = traceImpl;
            JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assertFailedImpl;)
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }
    }

    static void release()
    {
        if (--s_refs == 0) {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }

private:
    static inline int s_refs = 0;
};

class JoltPhysics final : public IPhysics3D
{
public:
    JoltPhysics() { JoltRuntime::acquire(); }
    ~JoltPhysics() override
    {
        m_worlds.clear();
        JoltRuntime::release();
    }

    [[nodiscard]] WorldHandle createWorld(const WorldDesc& desc) override
    {
        u32 slot = 0;
        if (!m_free.empty()) {
            slot = m_free.back();
            m_free.pop_back();
        }
        else {
            slot = static_cast<u32>(m_worlds.size());
            m_worlds.emplace_back();
            m_generations.push_back(0);
        }

        m_generations[slot] = m_generations[slot] + 1 == 0 ? 1 : m_generations[slot] + 1;
        m_worlds[slot] = std::make_unique<JoltWorld>(desc);
        return WorldHandle{slot, m_generations[slot]};
    }

    void destroyWorld(WorldHandle handle) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            m_worlds[handle.index].reset();
            m_free.push_back(handle.index);
        }
    }

    void setGravity(WorldHandle handle, core::Vec3 gravity) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->setGravity(gravity);
        }
    }

    void setWorldOrigin(WorldHandle handle, core::DVec3 origin) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->setOrigin(origin);
        }
    }

    [[nodiscard]] core::DVec3 worldOrigin(WorldHandle handle) const override
    {
        const JoltWorld* world = resolve(handle);
        return world != nullptr ? world->origin() : core::DVec3{};
    }

    [[nodiscard]] BodyHandle createBody(WorldHandle handle, const BodyDesc& desc) override
    {
        JoltWorld* world = resolve(handle);
        return world != nullptr ? world->createBody(desc) : BodyHandle{};
    }

    void destroyBody(WorldHandle handle, BodyHandle body) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->destroyBody(body);
        }
    }

    // --- Constraints ---------------------------------------------------------

    [[nodiscard]] ConstraintHandle createConstraint(WorldHandle handle, const ConstraintDesc& desc) override
    {
        JoltWorld* world = resolve(handle);
        return world != nullptr ? world->createConstraint(desc) : ConstraintHandle{};
    }

    void destroyConstraint(WorldHandle handle, ConstraintHandle constraint) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->destroyConstraint(constraint);
        }
    }

    void setConstraintEnabled(WorldHandle handle, ConstraintHandle constraint, bool enabled) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->setConstraintEnabled(constraint, enabled);
        }
    }

    void updateConstraint(WorldHandle handle, ConstraintHandle constraint, const ConstraintDesc& desc) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->updateConstraint(constraint, desc);
        }
    }

    [[nodiscard]] ConstraintState constraintState(WorldHandle handle, ConstraintHandle constraint) const override
    {
        const JoltWorld* world = resolve(handle);
        return world != nullptr ? world->constraintState(constraint) : ConstraintState{};
    }

    void setBodyTransform(WorldHandle handle, BodyHandle body, const core::CFrameD& transform) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->setBodyTransform(body, transform);
        }
    }

    void setBodyVelocity(WorldHandle handle, BodyHandle body, core::Vec3 linear, core::Vec3 angular) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->setBodyVelocity(body, linear, angular);
        }
    }

    void applyImpulse(WorldHandle handle, BodyHandle body, core::Vec3 impulse) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->applyImpulse(body, impulse);
        }
    }

    bool updateBody(WorldHandle handle, BodyHandle body, const BodyDesc& desc) override
    {
        JoltWorld* world = resolve(handle);
        return world != nullptr && world->updateBody(body, desc);
    }

    void setBodyMaterial(WorldHandle handle, BodyHandle body, f32 friction, f32 restitution) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->setBodyMaterial(body, friction, restitution);
        }
    }

    void setBodyFlags(WorldHandle handle, BodyHandle body, bool collidable, bool queryable) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->setBodyFlags(body, collidable, queryable);
        }
    }

    void setBodyGroup(WorldHandle handle, BodyHandle body, CollisionGroup group) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->setBodyGroup(body, group);
        }
    }

    [[nodiscard]] BodyState bodyState(WorldHandle handle, BodyHandle body) const override
    {
        const JoltWorld* world = resolve(handle);
        return world != nullptr ? world->bodyState(body) : BodyState{};
    }

    void collectActiveBodies(WorldHandle handle, std::vector<ActiveBody>& out) const override
    {
        if (const JoltWorld* world = resolve(handle); world != nullptr) {
            world->collectActiveBodies(out);
        }
    }

    void step(WorldHandle handle, f32 fixedDt) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->step(fixedDt);
        }
    }

    [[nodiscard]] std::span<const ContactEvent> drainContacts(WorldHandle handle) override
    {
        const JoltWorld* world = resolve(handle);
        return world != nullptr ? world->contacts() : std::span<const ContactEvent>{};
    }

    [[nodiscard]] StepTimings lastStepTimings(WorldHandle handle) const override
    {
        const JoltWorld* world = resolve(handle);
        return world != nullptr ? world->timings() : StepTimings{};
    }

    [[nodiscard]] bool raycast(WorldHandle handle, const RayD& ray, const QueryFilter& filter,
                               RayHit& outHit) const override
    {
        const JoltWorld* world = resolve(handle);
        return world != nullptr && world->raycast(ray, filter, outHit);
    }

    [[nodiscard]] bool spherecast(WorldHandle handle, const RayD& ray, f32 radius, const QueryFilter& filter,
                                  RayHit& outHit) const override
    {
        const JoltWorld* world = resolve(handle);
        return world != nullptr && world->spherecast(ray, radius, filter, outHit);
    }

    void overlapBox(WorldHandle handle, const core::CFrameD& transform, core::Vec3 size, const QueryFilter& filter,
                    std::vector<u64>& out) const override
    {
        if (const JoltWorld* world = resolve(handle); world != nullptr) {
            world->overlapBox(transform, size, filter, out);
        }
    }

    [[nodiscard]] CharacterHandle createCharacter(WorldHandle handle, const CharacterDesc& desc) override
    {
        JoltWorld* world = resolve(handle);
        return world != nullptr ? world->createCharacter(desc) : CharacterHandle{};
    }

    void destroyCharacter(WorldHandle handle, CharacterHandle character) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->destroyCharacter(character);
        }
    }

    void moveCharacter(WorldHandle handle, CharacterHandle character, core::Vec3 velocity, f32 fixedDt) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->moveCharacter(character, velocity, fixedDt);
        }
    }

    void setCharacterTransform(WorldHandle handle, CharacterHandle character, const core::CFrameD& transform) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->setCharacterTransform(character, transform);
        }
    }

    [[nodiscard]] CharacterState characterState(WorldHandle handle, CharacterHandle character) const override
    {
        const JoltWorld* world = resolve(handle);
        return world != nullptr ? world->characterState(character) : CharacterState{};
    }

    [[nodiscard]] CollisionGroup registerCollisionGroup(WorldHandle handle, std::string_view name) override
    {
        JoltWorld* world = resolve(handle);
        return world != nullptr ? world->matrix().add(name) : CollisionMatrix::kInvalidGroup;
    }

    [[nodiscard]] CollisionGroup findCollisionGroup(WorldHandle handle, std::string_view name) const override
    {
        const JoltWorld* world = resolve(handle);
        return world != nullptr ? world->matrix().find(name) : CollisionMatrix::kInvalidGroup;
    }

    void setGroupsCollidable(WorldHandle handle, CollisionGroup a, CollisionGroup b, bool collidable) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->matrix().setCollidable(a, b, collidable);
        }
    }

    [[nodiscard]] bool groupsCollidable(WorldHandle handle, CollisionGroup a, CollisionGroup b) const override
    {
        const JoltWorld* world = resolve(handle);
        return world != nullptr && world->matrix().collidable(a, b);
    }

    void collectCollisionGroups(WorldHandle handle, std::vector<std::string_view>& out) const override
    {
        if (const JoltWorld* world = resolve(handle); world != nullptr) {
            world->matrix().collectNames(out);
        }
    }

    [[nodiscard]] bool saveState(WorldHandle, std::vector<u8>&) const override { return false; }
    [[nodiscard]] bool restoreState(WorldHandle, std::span<const u8>) override { return false; }

    void debugDraw(WorldHandle handle, IDebugDrawSink& sink) override
    {
        if (JoltWorld* world = resolve(handle); world != nullptr) {
            world->debugDraw(sink);
        }
    }

private:
    [[nodiscard]] JoltWorld* resolve(WorldHandle handle) noexcept
    {
        if (handle.index >= m_worlds.size() || m_generations[handle.index] != handle.generation) {
            return nullptr;
        }
        return m_worlds[handle.index].get();
    }

    [[nodiscard]] const JoltWorld* resolve(WorldHandle handle) const noexcept
    {
        if (handle.index >= m_worlds.size() || m_generations[handle.index] != handle.generation) {
            return nullptr;
        }
        return m_worlds[handle.index].get();
    }

    std::vector<std::unique_ptr<JoltWorld>> m_worlds;
    std::vector<u32> m_generations;
    std::vector<u32> m_free;
};

} // namespace

PhysicsResult createJoltPhysics(core::EngineError*)
{
    return std::make_unique<JoltPhysics>();
}

} // namespace luaug::physics
