#include "luaug/scene/physics_sync.h"

#include "luaug/asset/terrain_mesher.h"
#include "luaug/asset/voxel_mesher.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace luaug::scene {
namespace {

// The instance id IS the body's user data. Eight bytes each way, no table, and
// a query hit resolves back to an instance by decoding rather than by looking
// up -- with the liveness check the world already knows how to do.
[[nodiscard]] u64 packInstance(core::InstanceId id) noexcept
{
    return (static_cast<u64>(id.generation) << 32) | id.index;
}

[[nodiscard]] core::InstanceId unpackInstance(u64 packed) noexcept
{
    return core::InstanceId{static_cast<u32>(packed & 0xffffffffu), static_cast<u32>(packed >> 32)};
}

[[nodiscard]] physics::ShapeType shapeForPartShape(i32 shape) noexcept
{
    // `Enum.PartShape`: Block, Ball, Cylinder, Capsule, Wedge. A wedge collides
    // as its bounding box in this release -- the renderer has no wedge either,
    // so nothing yet disagrees about what one looks like.
    switch (shape) {
    case 1:
        return physics::ShapeType::Sphere;
    case 2:
        return physics::ShapeType::Cylinder;
    case 3:
        return physics::ShapeType::Capsule;
    default:
        break;
    }
    return physics::ShapeType::Box;
}

// Whether two descriptions ask for the same body.
//
// **`pointsRevision` is here because the geometry can change underneath a hull
// that is otherwise identical** -- a mesh finishing its load, or a hot reload
// replacing one. Without it, a `MeshPart` that came up as a hull and then had
// its points replaced kept the first hull for ever, and the only reason nobody
// hit it is that meshes used to load inside the frame that created the part.
//
// The spans themselves are deliberately not compared. Comparing them would mean
// keeping one, and `ShapeDesc::points` may not outlive the create call.
[[nodiscard]] bool sameShape(const physics::ShapeDesc& a, const physics::ShapeDesc& b) noexcept
{
    return a.type == b.type && a.size == b.size && a.pointScale == b.pointScale &&
           a.pointsRevision == b.pointsRevision && a.geometryRevision == b.geometryRevision &&
           a.heightSampleCount == b.heightSampleCount && a.heightBlockSize == b.heightBlockSize &&
           a.heightMin == b.heightMin && a.heightMax == b.heightMax;
}

// The description as a RECORD may keep it: everything except the spans, whose
// documented lifetime is the call they were handed to.
//
// **Every span, not just `points`** (ADR 0066). `indices` and `heights` arrived
// with the two static shapes under the same rule, and a record that kept either
// would be holding a view into a buffer the next brush stroke replaces -- a
// dangling read rather than a stale one. `geometryRevision` is what lets
// `sameShape` above answer "are these the same triangles" without keeping any
// of them, exactly as `pointsRevision` does for a hull.
[[nodiscard]] physics::ShapeDesc withoutPoints(physics::ShapeDesc shape) noexcept
{
    shape.points = {};
    shape.indices = {};
    shape.heights = {};
    return shape;
}

} // namespace

PhysicsSync::PhysicsSync(World& world, physics::IPhysics3D& backend) : m_scene(world), m_backend(backend)
{
    physics::WorldDesc desc;
    m_world = m_backend.createWorld(desc);
}

PhysicsSync::~PhysicsSync()
{
    if (m_world.valid())
        m_backend.destroyWorld(m_world);
}

core::InstanceId PhysicsSync::instanceOf(u64 userData) const noexcept
{
    const core::InstanceId id = unpackInstance(userData);
    return m_scene.alive(id) ? id : core::InstanceId{};
}

u64 PhysicsSync::userDataOf(core::InstanceId id) const noexcept
{
    return packInstance(id);
}

bool PhysicsSync::inWorld(core::InstanceId id) const
{
    // Under `Workspace`, which is what "in the world" means (api-design.md
    // §2.1). A part parented to a Folder under Workspace is in; one parented to
    // nil or to a service that is not Workspace is not.
    if (!m_workspace.valid())
        return false;

    // Answered for the PARENT and remembered for one tick, because the walk is
    // O(depth) and ten thousand parts in one folder ask the identical question
    // ten thousand times. A single-entry memo rather than a map: the parts of a
    // scene arrive in pool order, which is creation order, which groups them by
    // parent for free.
    const core::InstanceId parent = m_scene.parentOf(id);
    if (parent == m_lastParent)
        return m_lastParentInWorld;

    m_lastParent = parent;
    m_lastParentInWorld = parent == m_workspace || m_scene.isAncestorOf(m_workspace, parent);
    return m_lastParentInWorld;
}

// How many ticks an anchored part stays `Kinematic` after the last write to its
// `CFrame` (D031).
//
// **Hysteresis, and the number is chosen rather than found.** Without it a part
// written every other tick would flip between the two broadphase layers every
// other tick, and each flip is a body rebuild. Twelve ticks covers any driver
// writing at five hertz or faster -- a tween writes every tick, a script nudging
// a platform on a timer is the slow end -- and costs at most a fifth of a second
// of a stopped body sitting in the moving layer, where the only price is that it
// is re-fitted for those twelve ticks.
//
// TICKS and not seconds. A body's broadphase layer must not depend on how fast
// the machine was running (R10).
constexpr u64 kAnchoredMovingTicks = 12;

// `Enum.CollisionFidelity`'s items, by name rather than by the numbers they
// happen to have. The component stores the raw value -- for the reason every
// enum-valued component does, that a generated accessor needs no per-enum C++
// type -- and reading it back against a literal is how D146 happened.
enum class CollisionFidelity : i32
{
    Default = 0,
    Hull = 1,
    Box = 2,
    Precise = 3,
};

physics::ShapeDesc PhysicsSync::shapeOf(core::InstanceId id, const PartComponent& part) const
{
    physics::ShapeDesc shape;
    shape.size = part.size;

    // A `MeshPart` collides as the box or hull its `CollisionFidelity` asks for.
    //
    // `Box` is the fidelity that says "the bounding box" and it is honoured
    // exactly. Every other value asks for the geometry, and the geometry that
    // exists is a CONVEX HULL: a hull is what a rigid-body solver can use
    // directly, and a concave triangle mesh is a different shape class with
    // different rules (it cannot be dynamic) that nobody has asked for.
    //
    // A mesh whose points have not arrived falls back to the box rather than to
    // nothing. That is the state of a `MeshPart` in the frame before its file
    // finishes loading, and a body with no shape for one frame is a body that
    // falls through the floor.
    if (const MeshPartComponent* mesh = m_scene.meshParts().find(id); mesh != nullptr) {
        shape.type = physics::ShapeType::Box;
        // **`Box` is item 2, and this asked for item 0** (D146). Zero is
        // `Default` -- the item whose whole meaning is "the engine chooses", and
        // whose documented choice is a hull -- so every `MeshPart` nobody
        // touched collided as its bounding box while the enum promised the
        // geometry, and the one person who explicitly asked for a box got a
        // hull. Both wrong, in opposite directions, from one constant; and the
        // comment above has described the intended behaviour correctly the whole
        // time.
        if (mesh->collisionFidelity == static_cast<i32>(CollisionFidelity::Box)) {
            return shape;
        }
        const core::NameAtom content = mesh->meshContent;
        const auto at = std::lower_bound(m_collisionPoints.begin(), m_collisionPoints.end(), content,
                                         [](const auto& entry, core::NameAtom key) { return entry.first.id < key.id; });
        if (at != m_collisionPoints.end() && at->first == content && at->second.points.size() >= 4) {
            shape.type = physics::ShapeType::ConvexHull;
            shape.points = at->second.points;
            shape.pointsRevision = at->second.revision;
            // The same `Size / MeshSize` the renderer draws with, so the hull is
            // the shape on screen and not the shape in the file. The box branch
            // above needs no equivalent: it is already `part.size`.
            shape.pointScale = core::Vec3{part.size.x / mesh->meshSize.x, part.size.y / mesh->meshSize.y,
                                          part.size.z / mesh->meshSize.z};
        }
        return shape;
    }
    shape.type = shapeForPartShape(part.shape);
    return shape;
}

physics::BodyDesc PhysicsSync::descOf(core::InstanceId id, const PartComponent& part, const RigidBodyComponent& body,
                                      bool movingAnchored) const
{
    physics::BodyDesc desc;
    desc.shape = shapeOf(id, part);
    desc.transform = part.cframe;
    // A part an active weld drives is Kinematic: it still collides and still
    // pushes what it runs into, and the solver does not move it -- which is what
    // "driven, not simulated" means (roadmap M5).
    //
    // **And so is an anchored part something is currently WRITING** (D031). A
    // moving platform is `Anchored` with a tween on its `CFrame`, and it was
    // classified `Static` -- which is the layer Jolt keeps precisely so that it
    // is never re-fitted. A static body does not move and derives no velocity,
    // so D027's `MoveKinematic` was handed a target and nothing happened: the
    // fix was right and unreachable.
    //
    // Only what is written, and only while it is written. The alternative --
    // `Anchored` meaning kinematic always -- was rejected by the human on cost
    // rather than taste: `jolt_physics.cpp` splits the broadphase into
    // `NonMoving` and `Moving`, and making every floor and wall kinematic would
    // put a world of never-moving bodies into the layer that is updated every
    // tick, to solve a problem two platforms have.
    const bool driven = isDriven(id) || (body.anchored && movingAnchored);
    desc.motion = driven          ? physics::MotionType::Kinematic
                  : body.anchored ? physics::MotionType::Static
                                  : physics::MotionType::Dynamic;
    desc.friction = body.friction;
    desc.restitution = body.restitution;
    desc.density = body.density;
    desc.collidable = body.canCollide;
    desc.queryable = body.canQuery;
    const u16 group = m_scene.collisionGroups().find(body.collisionGroup);
    desc.group = group == CollisionGroups::kInvalid ? CollisionGroups::kDefault : group;
    desc.userData = packInstance(id);
    return desc;
}

void PhysicsSync::syncCollisionGroups()
{
    const CollisionGroups& groups = m_scene.collisionGroups();
    if (groups.revision() == m_groupRevision)
        return;
    m_groupRevision = groups.revision();

    // Pushed wholesale rather than diffed. The table changes when a script
    // registers a group or flips a pair -- twice in a game's life -- and a
    // hundred-entry rewrite then is cheaper to be right about than a diff that
    // runs every tick.
    for (u32 index = 0; index < groups.count(); ++index) {
        const std::string_view name = m_scene.atoms().text(groups.nameAt(static_cast<u16>(index)));
        [[maybe_unused]] const physics::CollisionGroup registered = m_backend.registerCollisionGroup(m_world, name);
    }
    for (u32 a = 0; a < groups.count(); ++a) {
        for (u32 b = a; b < groups.count(); ++b) {
            m_backend.setGroupsCollidable(m_world, static_cast<physics::CollisionGroup>(a),
                                          static_cast<physics::CollisionGroup>(b),
                                          groups.collidable(static_cast<u16>(a), static_cast<u16>(b)));
        }
    }
}

void PhysicsSync::applyBody(core::InstanceId id, PartComponent& part, RigidBodyComponent& body)
{
    if (id.index >= m_bodies.size())
        m_bodies.resize(static_cast<usize>(id.index) + 1);

    BodyRecord& record = m_bodies[id.index];

    // A script's write, told apart from the mirror's own the same way the
    // transform sync below does it: the component differs from what this mirror
    // last put there. An anchored part that is being written is a platform in
    // motion, and it holds that state for `kAnchoredMovingTicks` afterwards
    // (D031).
    const u64 tick = m_scene.engineState().tick;
    const bool written = record.generation == id.generation && !(part.cframe == record.written);
    if (written)
        record.movingUntilTick = tick + kAnchoredMovingTicks;

    const physics::BodyDesc desc = descOf(id, part, body, record.movingUntilTick > tick);

    // Everything the record keeps about a description, minus the span it may not
    // keep. One place, because a field written in one branch and forgotten in
    // the other is how the two paths below used to disagree.
    const auto remember = [&record, &desc, &part] {
        record.shape = withoutPoints(desc.shape);
        record.motion = desc.motion;
        record.density = desc.density;
        record.friction = desc.friction;
        record.restitution = desc.restitution;
        record.collidable = desc.collidable;
        record.queryable = desc.queryable;
        record.group = desc.group;
        record.written = part.cframe;
    };

    if (record.generation != id.generation) {
        // The slot is empty, or it holds a body that belonged to a different
        // instance in the same slot. Either way this instance has no body yet.
        if (record.generation != 0 && record.live) {
            m_backend.destroyBody(m_world, record.handle);
            --m_bodyCount;
        }

        record = BodyRecord{};
        record.generation = id.generation;
        record.seen = true;
        remember();

        // **Remembered whether or not it worked**, which is the fix: a refusal
        // used to zero the record, so the next tick saw an empty slot and asked
        // again -- for ever, once per tick, burning a body generation each time
        // and saying nothing. Now the attempt is recorded, and only a
        // description that actually DIFFERS is worth another one.
        const physics::BodyHandle handle = m_backend.createBody(m_world, desc);
        if (!handle.valid())
            return;

        record.handle = handle;
        record.live = true;
        record.backendMotion = desc.motion;
        ++m_bodyCount;
        return;
    }

    record.seen = true;

    // A shape, motion type or density change is a rebuild on the backend's
    // side, so it is one call rather than four setters -- and the handle
    // survives it, because everything above holds one.
    const bool describedDifferently =
        !sameShape(record.shape, desc.shape) || record.motion != desc.motion || record.density != desc.density;

    if (!record.live) {
        // There is no body to set anything on. Asking again only when the
        // description has changed is what keeps a refusal costing one attempt:
        // a hull whose points have since arrived, or a size that moved, is a
        // different question and gets asked.
        if (!describedDifferently)
            return;
        remember();
        const physics::BodyHandle handle = m_backend.createBody(m_world, desc);
        if (!handle.valid())
            return;
        record.handle = handle;
        record.live = true;
        record.backendMotion = desc.motion;
        ++m_bodyCount;
        return;
    }

    if (describedDifferently) {
        const bool applied = m_backend.updateBody(m_world, record.handle, desc);
        if (applied) {
            remember();
            record.backendMotion = desc.motion;
        }
        else {
            // **A refusal leaves the body as it was, so only the retry gate is
            // recorded** (D135). Committing the whole description would be the
            // mirror claiming the backend took a motion type and a friction it
            // refused -- and because `describedDifferently` then goes false for
            // ever, an `Anchored` part would keep falling with its friction
            // change permanently lost.
            //
            // These three ARE recorded, and that is what keeps "a refused
            // rebuild costs one attempt" true: they are the comparison the
            // retry is gated on. Everything else stays as the backend has it,
            // so the incremental branch below pushes it through
            // `setBodyMaterial`, `setBodyFlags` and `setBodyGroup` next tick --
            // and `record.written` stays as it was, so a `CFrame` written in the
            // same tick as a refused rebuild is delivered next tick rather than
            // recorded as delivered and swallowed.
            record.shape = withoutPoints(desc.shape);
            record.motion = desc.motion;
            record.density = desc.density;
        }
    }
    else {
        if (record.friction != desc.friction || record.restitution != desc.restitution) {
            m_backend.setBodyMaterial(m_world, record.handle, desc.friction, desc.restitution);
            record.friction = desc.friction;
            record.restitution = desc.restitution;
        }
        if (record.collidable != desc.collidable || record.queryable != desc.queryable) {
            m_backend.setBodyFlags(m_world, record.handle, desc.collidable, desc.queryable);
            record.collidable = desc.collidable;
            record.queryable = desc.queryable;
        }
        if (record.group != desc.group) {
            m_backend.setBodyGroup(m_world, record.handle, desc.group);
            record.group = desc.group;
        }

        // The one place a script's write is told apart from the mirror's own:
        // the component differs from what this mirror last wrote into it, so
        // something else did. No dirty flag on either side.
        if (!(part.cframe == record.written)) {
            m_backend.setBodyTransform(m_world, record.handle, part.cframe);
            record.written = part.cframe;
        }
        else if (record.backendMotion == physics::MotionType::Kinematic) {
            // **A kinematic body that was not written this tick is told to stay
            // where it is**, and that is not a no-op: `MoveKinematic` sets a
            // velocity, and a body nobody re-targets keeps the last one and
            // COASTS. A platform whose tween finished sailed on past its
            // destination, and a welded part whose anchor stopped moving flew
            // away -- both found by two conformance cases the moment D027's fix
            // landed.
            //
            // Handing over the same target computes a velocity of zero, which is
            // the honest way to say "it is not moving" to a system whose whole
            // vocabulary is velocity.
            m_backend.setBodyTransform(m_world, record.handle, part.cframe);
        }
    }

    if (!(body.pendingImpulse == core::Vec3{0.0f, 0.0f, 0.0f})) {
        m_backend.applyImpulse(m_world, record.handle, body.pendingImpulse);
        body.pendingImpulse = core::Vec3{0.0f, 0.0f, 0.0f};
    }
}

void PhysicsSync::applyCharacter(core::InstanceId id, PartComponent& part, RigidBodyComponent& body,
                                 CharacterBodyComponent& character, f32 fixedDt)
{
    const u64 key = packInstance(id);
    const u16 sceneGroup = m_scene.collisionGroups().find(body.collisionGroup);
    const physics::CollisionGroup group = sceneGroup == CollisionGroups::kInvalid
                                              ? CollisionGroups::kDefault
                                              : static_cast<physics::CollisionGroup>(sceneGroup);

    auto found = m_characters.find(key);
    if (found == m_characters.end() || found->second.height != part.size.y ||
        found->second.diameter != std::max(part.size.x, part.size.z) ||
        found->second.maxSlopeAngle != character.maxSlopeAngle ||
        found->second.stepHeight != character.autoStepHeight || found->second.group != group) {
        // Rebuilt rather than adjusted: a controller's capsule, slope limit and
        // step height are settings it is constructed with, and a character that
        // changes size mid-stride is a rare enough event to pay for.
        if (found != m_characters.end()) {
            m_backend.destroyCharacter(m_world, found->second.handle);
            m_characters.erase(found);
        }

        physics::CharacterDesc desc;
        desc.transform = part.cframe;
        desc.height = part.size.y;
        desc.diameter = std::max(part.size.x, part.size.z);
        desc.maxSlopeAngle = character.maxSlopeAngle;
        desc.stepHeight = character.autoStepHeight;
        desc.group = group;
        desc.userData = key;

        CharacterRecord record;
        record.handle = m_backend.createCharacter(m_world, desc);
        record.height = desc.height;
        record.diameter = desc.diameter;
        record.maxSlopeAngle = desc.maxSlopeAngle;
        record.stepHeight = desc.stepHeight;
        record.group = group;
        record.written = part.cframe;
        record.seen = true;
        if (!record.handle.valid())
            return;
        found = m_characters.emplace(key, record).first;
    }

    CharacterRecord& record = found->second;
    record.seen = true;

    if (!(part.cframe == record.written)) {
        m_backend.setCharacterTransform(m_world, record.handle, part.cframe);
        record.written = part.cframe;
    }

    // The movement model is the caller's and the sweeping is the backend's.
    // Gravity integrates here rather than in the solver because a character
    // controller is not a body the solver knows about.
    const core::Vec3 gravity = m_scene.workspaces().find(m_workspace) != nullptr
                                   ? m_scene.workspaces().find(m_workspace)->gravity
                                   : core::Vec3{0.0f, -9.81f, 0.0f};

    const physics::CharacterState state = m_backend.characterState(m_world, record.handle);
    const bool grounded = state.ground == physics::CharacterGround::Grounded;

    if (grounded && character.verticalVelocity <= 0.0f)
        character.verticalVelocity = 0.0f;
    character.verticalVelocity += gravity.y * fixedDt;

    if (character.jumpRequested) {
        // Applied WHEREVER the character is, grounded or not (human decision,
        // 2026-08-21). The recorded reasoning had argued *ignored* against
        // *queued* -- "a jump that fires the moment you land is a jump you did
        // not ask for" -- and that argument is about queuing, which nothing
        // here proposes. Letting the caller decide was never among the
        // alternatives it ruled out.
        //
        // `Grounded` is already exposed, so `if character.Grounded then
        // character:Jump() end` reproduces the old behaviour in one line, in
        // the game, where a jump policy belongs. What it unlocks costs nothing
        // more: double jump, wall jump, coyote time and jump buffering all
        // become counters in Luau.
        //
        // What stays engine-side is the TICK. The velocity is set here, at the
        // next simulation step, and never inside the call -- or a replay
        // diverges (R10).
        character.verticalVelocity = character.jumpSpeed;
        character.jumpRequested = false;
    }

    // Horizontal only -- vertical movement is gravity's and Jump's -- and
    // scaled rather than normalised, so a shorter direction walks slower.
    const core::Vec3 horizontal{character.moveDirection.x * character.walkSpeed, 0.0f,
                                character.moveDirection.z * character.walkSpeed};
    const core::Vec3 velocity{horizontal.x, character.verticalVelocity, horizontal.z};
    m_backend.moveCharacter(m_world, record.handle, velocity, fixedDt);

    // Cleared once consumed: a character told nothing stops, which is what
    // `Move`'s Doc promises.
    character.moveDirection = core::Vec3{0.0f, 0.0f, 0.0f};
}

// --- Terrain colliders (ADR 0066, ADR 0067) ----------------------------------
//
// **One static `HeightField` body per height tile**, and **one static
// `TriangleMesh` body per bricked column.**
//
// A height tile is 32 by 32 columns, and its collider has 33 samples a side:
// its own 32 and the first row and column of its +x and +z neighbours, so the
// last quad reaches the neighbour's first column exactly as the drawn tile does.
// It used to be 32 samples stretched over the same 32 columns -- thirty-one
// intervals where the ground has thirty-two -- which put every collider up to a
// voxel sideways of the ground drawn over it, and on a slope that is a
// character standing in the air or sunk into the hill. Jolt pads 33 up to its
// block size with no-collision samples, so the odd count costs nothing.
//
// **A bricked column is a hole in the height field and a mesh in its place.**
// Bricks are where the field stops being a height function -- a cave, an
// overhang, a tunnel -- and a height field cannot say "ground, then air, then
// ground". So every sample inside a bricked column is no-collision, and the
// column's whole surface, height layer and bricks alike, is meshed from the
// field by the same mesher the renderer uses and handed over as triangles. The
// mesh reaches one lattice column past the footprint on every side, because a
// hole sample takes the quads on both sides of it: without the overlap there
// would be a one-voxel gap round every cave mouth.
//
// A column's mesh is a few thousand triangles at most, which A3's measurement
// (12 ms for 32,000) puts near a millisecond, and the shared budget caps how
// many are rebuilt in one tick.
namespace {

constexpr f32 HeightNoCollision = std::numeric_limits<f32>::max();

// How far above and below the surface a column's mesh looks, in lattice steps.
// The render loader's margin, for the same reason: a crossing needs a cell on
// each side of it.
constexpr i32 ColumnMargin = 2;

// How many lattice cells a cave collider reaches past its column on every side.
// The height field is open over the column and the quads either side of it, and
// a surface net's vertices sit at cell centres -- so the mesh has to reach two
// cells out to cover the opening, overlapping the height field beyond it.
constexpr i32 CaveRim = 2;

[[nodiscard]] i32 floorDivide(i32 value, i32 divisor) noexcept
{
    const i32 quotient = value / divisor;
    return (value % divisor != 0 && (value < 0) != (divisor < 0)) ? quotient - 1 : quotient;
}

// Order-sensitive, which is what a key built from an ordered walk wants.
[[nodiscard]] u64 combine(u64 seed, u64 value) noexcept
{
    u64 z = seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

[[nodiscard]] u64 packKey(i32 a, i32 b) noexcept
{
    return (static_cast<u64>(static_cast<u32>(a)) << 32) | static_cast<u32>(b);
}

// The height of one lattice column, or nothing where no tile holds it.
[[nodiscard]] std::optional<f32> columnHeight(const asset::TerrainField& field, i32 x, i32 z) noexcept
{
    const auto edge = static_cast<i32>(asset::TileEdge);
    const i32 tileX = floorDivide(x, edge);
    const i32 tileZ = floorDivide(z, edge);
    const asset::HeightTile* tile = field.findTile(asset::TileKey{tileX, tileZ});
    if (tile == nullptr)
        return std::nullopt;
    const auto localX = static_cast<usize>(x - tileX * edge);
    const auto localZ = static_cast<usize>(z - tileZ * edge);
    return tile->height[localZ * asset::TileEdge + localX];
}

// The digest of a tile, or a fixed marker where there is none -- a neighbour
// appearing changes this tile's collider though this tile did not change.
[[nodiscard]] u64 tileDigestOr(const asset::TerrainField& field, asset::TileKey key) noexcept
{
    const asset::HeightTile* tile = field.findTile(key);
    return tile == nullptr ? 0x6E6F74696C65ull : asset::digestOf(*tile);
}

} // namespace

void PhysicsSync::applyTerrain()
{
    for (auto& collider : m_terrainColliders)
        collider.seen = false;

    // **A count, never a clock.** How many colliders get rebuilt in a tick is
    // part of the operation sequence, so it cannot depend on how fast the
    // machine was: streaming's wall-clock exemption says "nothing measured
    // reaches the world hash", and a collider is the world hash.
    u32 rebuilt = 0;
    u32 caves = 0;

    // Sorted by (terrain, kind, key): the order the walk below meets them in,
    // which is the order bodies are created in, which decides the ids the
    // backend hands out (R10). It used to compare the key alone, which put two
    // terrains' tiles in one interleaved run and could find the other
    // terrain's entry.
    const auto locate = [this](core::InstanceId id, bool bricked, asset::TileKey key) {
        const auto at = std::lower_bound(m_terrainColliders.begin(), m_terrainColliders.end(), key,
                                         [&](const TerrainCollider& entry, const asset::TileKey& probe) {
                                             if (entry.terrain.index != id.index)
                                                 return entry.terrain.index < id.index;
                                             if (entry.bricked != bricked)
                                                 return !entry.bricked;
                                             return entry.key < probe;
                                         });
        const bool exists =
            at != m_terrainColliders.end() && at->terrain == id && at->bricked == bricked && at->key == key;
        return std::pair{at, exists};
    };

    std::vector<f32> heights;
    std::vector<asset::TileKey> brickedColumns;

    // **Where cave collision is wanted: near things that move.** A cave's
    // collider is a triangle mesh built from the field, a millisecond or two
    // apiece, and a column nothing can reach does not need one -- the same
    // bargain the voxel engines strike by generating terrain collision lazily
    // around bodies, and Terrain3D's "dynamic collision" strikes for its height
    // fields. Gathered once per tick, and only when some terrain has bricks.
    std::vector<core::DVec3> movers;
    bool moversGathered = false;
    const auto gatherMovers = [&] {
        if (moversGathered)
            return;
        moversGathered = true;
        m_scene.rigidBodies().forEach([&](core::InstanceId id, const RigidBodyComponent& body) {
            if (body.anchored && m_scene.characterBodies().find(id) == nullptr)
                return;
            const PartComponent* part = m_scene.parts().find(id);
            if (part != nullptr && inWorld(id))
                movers.push_back(part->cframe.position);
        });
    };

    m_scene.terrains().forEach([&](core::InstanceId id, TerrainComponent& terrain) {
        if (!inWorld(id))
            return;

        const asset::TerrainField& field = terrain.field;
        const f32 voxel = field.settings().voxelSize;
        const auto edge = static_cast<i32>(asset::TileEdge);
        const auto brickEdge = static_cast<i32>(asset::BrickEdge);
        const u64 placement =
            combine(combine(combine(std::bit_cast<u64>(terrain.origin.x), std::bit_cast<u64>(terrain.origin.y)),
                            std::bit_cast<u64>(terrain.origin.z)),
                    packKey(std::bit_cast<i32>(terrain.minHeight), std::bit_cast<i32>(terrain.maxHeight)));
        const std::vector<asset::BrickKey> bricks = field.brickKeys();
        // The same bricks ordered by column, then height, so one column's are a
        // contiguous run found by binary search.
        std::vector<asset::BrickKey> byColumn = bricks;
        std::sort(byColumn.begin(), byColumn.end(), [](const asset::BrickKey& a, const asset::BrickKey& b) {
            if (a.x != b.x)
                return a.x < b.x;
            if (a.z != b.z)
                return a.z < b.z;
            return a.y < b.y;
        });
        const auto bricksOf = [&byColumn](i32 x, i32 z) {
            const auto before = [](const asset::BrickKey& brick, std::pair<i32, i32> key) {
                return brick.x != key.first ? brick.x < key.first : brick.z < key.second;
            };
            const auto after = [](std::pair<i32, i32> key, const asset::BrickKey& brick) {
                return key.first != brick.x ? key.first < brick.x : key.second < brick.z;
            };
            const auto first = std::lower_bound(byColumn.begin(), byColumn.end(), std::pair{x, z}, before);
            const auto last = std::upper_bound(first, byColumn.end(), std::pair{x, z}, after);
            return std::span<const asset::BrickKey>(first, last);
        };

        // The bricked columns, sorted and unique.
        brickedColumns.clear();
        for (const asset::BrickKey brick : bricks)
            brickedColumns.push_back(asset::TileKey{brick.x, brick.z});
        std::sort(brickedColumns.begin(), brickedColumns.end());
        brickedColumns.erase(std::unique(brickedColumns.begin(), brickedColumns.end()), brickedColumns.end());
        const auto columnBricked = [&](i32 x, i32 z) {
            return std::binary_search(brickedColumns.begin(), brickedColumns.end(),
                                      asset::TileKey{floorDivide(x, brickEdge), floorDivide(z, brickEdge)});
        };

        // Whether a collider still describes the field -- cheaply first, then
        // by what it was built from. Answers true when nothing needs sending.
        const auto current = [&](TerrainCollider& collider, u64 content) {
            collider.seen = true;
            if (collider.placement != placement)
                return false;
            if (collider.revision == terrain.fieldRevision)
                return true;
            if (collider.content == content) {
                collider.revision = terrain.fieldRevision;
                return true;
            }
            return false;
        };

        // --- Height tiles ---------------------------------------------------
        //
        // Sorted, because `tileKeys()` answers sorted.
        for (const asset::TileKey key : field.tileKeys()) {
            const asset::HeightTile* tile = field.findTile(key);
            if (tile == nullptr)
                continue;
            auto [at, exists] = locate(id, false, key);
            // Nothing written and nothing moved: the tick's common case, and
            // answered before any key is built.
            if (exists && at->revision == terrain.fieldRevision && at->placement == placement) {
                at->seen = true;
                continue;
            }

            const i32 firstColumn = key.x * edge;
            const i32 firstRow = key.z * edge;
            u64 content = asset::digestOf(*tile);
            content = combine(content, tileDigestOr(field, asset::TileKey{key.x + 1, key.z}));
            content = combine(content, tileDigestOr(field, asset::TileKey{key.x, key.z + 1}));
            content = combine(content, tileDigestOr(field, asset::TileKey{key.x + 1, key.z + 1}));
            // The bricked columns this tile's samples reach: a window of three
            // by three brick columns at most.
            for (i32 bz = floorDivide(firstRow, brickEdge); bz <= floorDivide(firstRow + edge, brickEdge); ++bz) {
                for (i32 bx = floorDivide(firstColumn, brickEdge); bx <= floorDivide(firstColumn + edge, brickEdge);
                     ++bx) {
                    if (std::binary_search(brickedColumns.begin(), brickedColumns.end(), asset::TileKey{bx, bz}))
                        content = combine(content, packKey(bx, bz));
                }
            }

            if (exists && current(*at, content))
                continue;
            if (rebuilt >= TerrainRebuildsPerTick) {
                // Over budget: leave it as it was and come back next tick. Marked
                // seen so it is not retired for being stale, which would destroy
                // a collider somebody is standing on in order to rebuild it.
                if (exists)
                    at->seen = true;
                continue;
            }

            // Row-major, `Samples` on a side. The last row and column come from
            // the neighbours, and are holes where there is no neighbour -- the
            // ground stops where the drawn ground stops.
            constexpr u32 Samples = asset::TileEdge + 1;
            heights.assign(static_cast<usize>(Samples) * Samples, HeightNoCollision);
            for (u32 z = 0; z < Samples; ++z) {
                for (u32 x = 0; x < Samples; ++x) {
                    const i32 column = firstColumn + static_cast<i32>(x);
                    const i32 row = firstRow + static_cast<i32>(z);
                    if (columnBricked(column, row))
                        continue;
                    if (x < asset::TileEdge && z < asset::TileEdge) {
                        heights[z * Samples + x] = tile->height[z * asset::TileEdge + x];
                        continue;
                    }
                    if (const std::optional<f32> height = columnHeight(field, column, row))
                        heights[z * Samples + x] = *height;
                }
            }

            physics::BodyDesc desc;
            desc.shape.type = physics::ShapeType::HeightField;
            desc.shape.size = core::Vec3{static_cast<f32>(edge) * voxel, 1.0f, static_cast<f32>(edge) * voxel};
            desc.shape.heights = heights;
            desc.shape.heightSampleCount = Samples;
            // **The reservation, and it is why a terrain has Min/MaxHeight at
            // all**: the collider's precision is spread across this range when
            // the shape is built and cannot be widened afterwards, so digging
            // past it does not deepen the world.
            desc.shape.heightMin = terrain.minHeight;
            desc.shape.heightMax = terrain.maxHeight;
            desc.shape.geometryRevision = content;
            desc.motion = physics::MotionType::Static;

            // The shape centres itself on its footprint, which runs from the
            // tile's first column to its neighbour's first -- so the body sits
            // at the tile's middle. **Offset by the terrain's own origin**, which
            // is what makes a terrain a thing you can move: the field is
            // untouched, and every consumer applies the offset instead.
            const f32 originX = (static_cast<f32>(firstColumn) + static_cast<f32>(edge) * 0.5f) * voxel;
            const f32 originZ = (static_cast<f32>(firstRow) + static_cast<f32>(edge) * 0.5f) * voxel;
            desc.transform.position = core::DVec3{static_cast<f64>(originX) + terrain.origin.x, terrain.origin.y,
                                                  static_cast<f64>(originZ) + terrain.origin.z};

            if (exists) {
                // **`updateHeightField` while the reservation holds**, which is
                // the whole reason a height field is its own kind: A3 measured
                // it 58 times cheaper than a rebuild at this size, and it keeps
                // the body, its id and its contacts. A changed reservation
                // cannot be edited into a built shape, so it is a rebuild.
                const bool edited = at->placement == placement &&
                                    m_backend.updateHeightField(m_world, at->body, 0, 0, Samples, Samples, heights);
                if (!edited) {
                    m_backend.destroyBody(m_world, at->body);
                    at->body = m_backend.createBody(m_world, desc);
                }
                at->revision = terrain.fieldRevision;
                at->content = content;
                at->placement = placement;
                at->seen = true;
                rebuilt += 1;
                continue;
            }

            const physics::BodyHandle handle = m_backend.createBody(m_world, desc);
            if (!handle.valid())
                continue;
            m_terrainColliders.insert(
                at, TerrainCollider{id, false, key, handle, terrain.fieldRevision, content, placement, true});
            rebuilt += 1;
        }

        // --- Bricked columns ------------------------------------------------
        if (!brickedColumns.empty())
            gatherMovers();
        const double columnMetres = static_cast<double>(brickEdge) * static_cast<double>(voxel);
        for (const asset::TileKey column : brickedColumns) {
            // Not near anything that moves: no collider, and one that was
            // built while something was near is retired by not being seen.
            const double columnX = terrain.origin.x + static_cast<double>(column.x) * columnMetres;
            const double columnZ = terrain.origin.z + static_cast<double>(column.z) * columnMetres;
            bool near = false;
            for (const core::DVec3& mover : movers) {
                const double dx = std::max({columnX - mover.x, 0.0, mover.x - (columnX + columnMetres)});
                const double dz = std::max({columnZ - mover.z, 0.0, mover.z - (columnZ + columnMetres)});
                if (dx * dx + dz * dz <= CaveCollisionReach * CaveCollisionReach) {
                    near = true;
                    break;
                }
            }
            if (!near)
                continue;

            auto [at, exists] = locate(id, true, column);
            if (exists && at->revision == terrain.fieldRevision && at->placement == placement) {
                at->seen = true;
                continue;
            }

            // The region: the column's footprint plus one lattice column on
            // every side, from below its lowest surface to above its highest.
            const i32 minX = column.x * brickEdge - CaveRim;
            const i32 minZ = column.z * brickEdge - CaveRim;
            const i32 span = brickEdge + 2 * CaveRim;

            u64 content = 0;
            for (i32 tz = floorDivide(minZ, edge); tz <= floorDivide(minZ + span, edge); ++tz) {
                for (i32 tx = floorDivide(minX, edge); tx <= floorDivide(minX + span, edge); ++tx)
                    content = combine(content, tileDigestOr(field, asset::TileKey{tx, tz}));
            }
            // Every brick the region reaches -- its own column's and its
            // neighbours' rims -- read through the per-column index rather than
            // by walking every brick in the terrain for every column, which was
            // quadratic and was most of a sculpting tick.
            const i32 reachMinX = floorDivide(minX, brickEdge);
            const i32 reachMaxX = floorDivide(minX + span, brickEdge);
            const i32 reachMinZ = floorDivide(minZ, brickEdge);
            const i32 reachMaxZ = floorDivide(minZ + span, brickEdge);
            for (i32 bx = reachMinX; bx <= reachMaxX; ++bx) {
                for (i32 bz = reachMinZ; bz <= reachMaxZ; ++bz) {
                    for (const asset::BrickKey brick : bricksOf(bx, bz)) {
                        if (const asset::Brick* found = field.findBrick(brick)) {
                            content = combine(content, packKey(brick.x, brick.z));
                            content = combine(content, static_cast<u64>(static_cast<u32>(brick.y)));
                            content = combine(content, asset::digestOf(*found));
                        }
                    }
                }
            }

            if (exists && current(*at, content))
                continue;
            if (caves >= CaveRebuildsPerTick) {
                if (exists)
                    at->seen = true;
                continue;
            }

            // Only now, for a column that is actually being rebuilt: the slab
            // its surfaces live in.
            f32 lowest = std::numeric_limits<f32>::max();
            f32 highest = std::numeric_limits<f32>::lowest();
            for (i32 z = 0; z <= span; ++z) {
                for (i32 x = 0; x <= span; ++x) {
                    if (const std::optional<f32> height = columnHeight(field, minX + x, minZ + z)) {
                        lowest = std::min(lowest, *height);
                        highest = std::max(highest, *height);
                    }
                }
            }
            const bool anyHeight = lowest <= highest;
            i32 bottom = anyHeight ? static_cast<i32>(std::floor(lowest / voxel)) - ColumnMargin
                                   : std::numeric_limits<i32>::max();
            i32 top = anyHeight ? static_cast<i32>(std::ceil(highest / voxel)) + ColumnMargin
                                : std::numeric_limits<i32>::lowest();
            for (const asset::BrickKey brick : bricksOf(column.x, column.z)) {
                bottom = std::min(bottom, brick.y * brickEdge - ColumnMargin);
                top = std::max(top, (brick.y + 1) * brickEdge + ColumnMargin);
            }

            physics::BodyHandle handle{};
            if (bottom < top) {
                asset::MeshRegion region;
                region.minX = minX;
                region.minZ = minZ;
                region.minY = bottom;
                region.cellsX = static_cast<u32>(span);
                region.cellsZ = static_cast<u32>(span);
                region.cellsY = static_cast<u32>(top - bottom);
                const asset::TerrainMesh meshed = asset::meshField(field, region);

                if (meshed.colliderIndices.size() >= 3) {
                    physics::BodyDesc desc;
                    desc.shape.type = physics::ShapeType::TriangleMesh;
                    desc.shape.points = meshed.colliderPoints;
                    desc.shape.indices = meshed.colliderIndices;
                    desc.shape.pointsRevision = content;
                    desc.shape.geometryRevision = content;
                    desc.motion = physics::MotionType::Static;
                    desc.transform.position = terrain.origin;
                    handle = m_backend.createBody(m_world, desc);
                }
            }

            // Kept even with no body -- a column dug out to nothing is a column
            // whose collider is current -- so it is not remeshed every tick.
            if (exists) {
                if (at->body.valid())
                    m_backend.destroyBody(m_world, at->body);
                at->body = handle;
                at->revision = terrain.fieldRevision;
                at->content = content;
                at->placement = placement;
                at->seen = true;
            }
            else {
                m_terrainColliders.insert(
                    at, TerrainCollider{id, true, column, handle, terrain.fieldRevision, content, placement, true});
            }
            caves += 1;
        }
    });

    retireUnseenTerrain();
}

void PhysicsSync::applyVoxels()
{
    for (VoxelCollider& collider : m_voxelColliders)
        collider.seen = false;

    const VoxelComponent* voxels = nullptr;
    m_scene.voxels().forEach([&voxels](core::InstanceId, const VoxelComponent& found) {
        if (voxels == nullptr)
            voxels = &found;
    });

    if (voxels != nullptr && voxels->grid.chunkCount() > 0) {
        // Where things that move are: bodies not anchored, and characters.
        std::vector<core::DVec3> movers;
        m_scene.rigidBodies().forEach([&](core::InstanceId id, const RigidBodyComponent& body) {
            if (body.anchored && m_scene.characterBodies().find(id) == nullptr)
                return;
            const PartComponent* part = m_scene.parts().find(id);
            if (part != nullptr && inWorld(id))
                movers.push_back(part->cframe.position);
        });

        const f64 chunkMetres = static_cast<f64>(asset::VoxelChunkEdge) * static_cast<f64>(voxels->blockSize);
        u32 rebuilt = 0;
        for (const asset::VoxelChunkKey key : voxels->grid.chunkKeys()) {
            bool near = false;
            for (const core::DVec3& mover : movers) {
                const auto axis = [&](i32 index, f64 at) {
                    const f64 low = static_cast<f64>(index) * chunkMetres;
                    return std::max({low - at, 0.0, at - (low + chunkMetres)});
                };
                const f64 dx = axis(key.x, mover.x);
                const f64 dy = axis(key.y, mover.y);
                const f64 dz = axis(key.z, mover.z);
                if (dx * dx + dy * dy + dz * dz <= VoxelCollisionReach * VoxelCollisionReach) {
                    near = true;
                    break;
                }
            }
            if (!near)
                continue;

            u64 content = static_cast<u64>(std::bit_cast<u32>(voxels->blockSize));
            for (const asset::VoxelChunkKey neighbour :
                 {key, asset::VoxelChunkKey{key.x - 1, key.y, key.z}, asset::VoxelChunkKey{key.x + 1, key.y, key.z},
                  asset::VoxelChunkKey{key.x, key.y - 1, key.z}, asset::VoxelChunkKey{key.x, key.y + 1, key.z},
                  asset::VoxelChunkKey{key.x, key.y, key.z - 1}, asset::VoxelChunkKey{key.x, key.y, key.z + 1}}) {
                const asset::VoxelChunk* chunk = voxels->grid.findChunk(neighbour);
                content = combine(content, chunk == nullptr ? 0x6E6F6E65ull : asset::digestOf(*chunk));
            }

            auto at = std::lower_bound(
                m_voxelColliders.begin(), m_voxelColliders.end(), key,
                [](const VoxelCollider& entry, asset::VoxelChunkKey probe) { return entry.key < probe; });
            const bool exists = at != m_voxelColliders.end() && at->key == key;
            if (exists) {
                at->seen = true;
                if (at->content == content)
                    continue;
            }
            if (rebuilt >= VoxelRebuildsPerTick)
                continue; // the old collider, if any, stands until its turn

            const asset::VoxelMesh meshed = asset::meshVoxelChunk(voxels->grid, key, {}, voxels->blockSize);
            physics::BodyHandle handle{};
            if (meshed.colliderIndices.size() >= 3) {
                physics::BodyDesc desc;
                desc.shape.type = physics::ShapeType::TriangleMesh;
                desc.shape.points = meshed.colliderPoints;
                desc.shape.indices = meshed.colliderIndices;
                desc.shape.pointsRevision = content;
                desc.shape.geometryRevision = content;
                desc.motion = physics::MotionType::Static;
                handle = m_backend.createBody(m_world, desc);
            }
            if (exists) {
                if (at->body.valid())
                    m_backend.destroyBody(m_world, at->body);
                at->body = handle;
                at->content = content;
            }
            else {
                m_voxelColliders.insert(at, VoxelCollider{key, handle, content, true});
            }
            rebuilt += 1;
        }
    }

    // Chunks gone, or out of reach, give their bodies back.
    for (usize at = m_voxelColliders.size(); at > 0; --at) {
        VoxelCollider& collider = m_voxelColliders[at - 1];
        if (collider.seen)
            continue;
        if (collider.body.valid())
            m_backend.destroyBody(m_world, collider.body);
        m_voxelColliders.erase(m_voxelColliders.begin() + static_cast<std::ptrdiff_t>(at - 1));
    }
}

void PhysicsSync::retireUnseenTerrain()
{
    // A tile the field no longer holds -- cleared, or its terrain destroyed --
    // takes its collider with it. Walked back to front so an erase cannot move
    // an entry this loop has not reached.
    for (usize at = m_terrainColliders.size(); at > 0; --at) {
        TerrainCollider& collider = m_terrainColliders[at - 1];
        if (collider.seen)
            continue;
        if (collider.body.valid())
            m_backend.destroyBody(m_world, collider.body);
        m_terrainColliders.erase(m_terrainColliders.begin() + static_cast<std::ptrdiff_t>(at - 1));
    }
}

void PhysicsSync::applyScene()
{
    syncCollisionGroups();

    // **Terrain first, and the order is load-bearing.** Body slots are assigned
    // in creation order, so an interleave that depended on which pool happened
    // to be walked first would make the ids a fact about the walk rather than
    // about the world (R10).
    applyTerrain();
    applyVoxels();

    // Cleared every tick: a part reparented between two ticks must not be
    // answered from the previous one's memo.
    m_lastParent = core::InstanceId{};
    m_lastParentInWorld = false;

    for (BodyRecord& record : m_bodies)
        record.seen = false;
    for (auto& entry : m_characters)
        entry.second.seen = false;

    const f32 fixedDt = static_cast<f32>(m_scene.engineState().fixedTimestep);

    // The pool walk is dense and in slot order, which is a pure function of the
    // operation sequence -- so the order bodies are created in, and therefore
    // the order the backend assigns its own ids in, is deterministic (R10).
    m_scene.rigidBodies().forEach([&](core::InstanceId id, RigidBodyComponent& body) {
        PartComponent* part = m_scene.parts().find(id);
        if (part == nullptr || !inWorld(id))
            return;

        if (CharacterBodyComponent* character = m_scene.characterBodies().find(id); character != nullptr) {
            applyCharacter(id, *part, body, *character, fixedDt);
            return;
        }
        applyBody(id, *part, body);
    });

    // **A SECOND walk, after every body exists**, and in the constraint pool's
    // own order -- which is creation order, and which the backend solves in.
    // Folding this into the body walk would mean creating a joint before the
    // body at its other end had been made; creating that body from here instead
    // would put body creation order in the CONSTRAINT pool's order, and two
    // scenes that differ only in the order somebody added joints would then
    // simulate differently (R10).
    for (ConstraintRecord& record : m_constraints)
        record.seen = false;

    m_scene.constraints().forEach(
        [&](core::InstanceId id, ConstraintComponent& constraint) { applyConstraint(id, constraint); });

    retireUnseen();
}

bool PhysicsSync::isDriven(core::InstanceId id) const
{
    return std::find(m_drivenParts.begin(), m_drivenParts.end(), id) != m_drivenParts.end();
}

// Welds resolve AFTER the step and the writeback, which is the defined point in
// the tick the roadmap asks for. Before it, a driven part would follow where its
// anchor was last tick and lag by a frame; after it, it follows where the anchor
// ended up this one.
//
// The order within the pass is dependency order, not pool order: `resolveWeld`
// resolves whatever its anchor hangs from before it resolves itself, so a chain
// lands correctly however the pool holds it. Cycles cannot occur -- the property
// setters refuse the write that would create one -- and the recursion is bounded
// by the weld count regardless.
void PhysicsSync::resolveWelds()
{
    m_resolvedWelds.clear();
    m_resolvedAttachments.clear();
    m_drivenParts.clear();

    // Slot order, which is a pure function of the operation sequence. It decides
    // nothing about the result, because dependency order does -- what it decides
    // is that two runs walk the same list.
    m_scene.welds().forEach([&](core::InstanceId weldId, WeldComponent& weld) { resolveWeld(weldId, weld); });
}

// The part an id is or sits on: itself for a part, its parent for an attachment,
// invalid for anything else.
core::InstanceId PhysicsSync::rigAbove(core::InstanceId id) const
{
    // The nearest `MeshPart` with a skeleton, walking upwards. A handful of
    // steps: a bone sits on a character or on one of its limbs.
    for (core::InstanceId cursor = m_scene.parentOf(id); cursor.valid(); cursor = m_scene.parentOf(cursor)) {
        if (m_scene.meshParts().find(cursor) == nullptr)
            continue;
        if (m_skeleton != nullptr && m_skeleton->jointCount(cursor) > 0)
            return cursor;
    }
    return {};
}

core::InstanceId PhysicsSync::ownerOf(core::InstanceId id) const
{
    if (m_scene.parts().find(id) != nullptr)
        return id;
    if (m_scene.attachments().find(id) != nullptr)
        return m_scene.parentOf(id);
    return {};
}

bool PhysicsSync::anchorFrame(core::InstanceId id, core::CFrameD& out)
{
    if (const PartComponent* part = m_scene.parts().find(id); part != nullptr) {
        out = part->cframe;
        return true;
    }
    if (const AttachmentComponent* attachment = m_scene.attachments().find(id); attachment != nullptr) {
        // Resolved first, so an attachment used as an anchor is where it ends up
        // this tick rather than where it was last one -- the same rule the weld
        // chain follows.
        resolveAttachment(id);
        out = attachment->worldCFrame;
        return true;
    }
    return false;
}

void PhysicsSync::resolveAttachment(core::InstanceId id)
{
    if (std::find(m_resolvedAttachments.begin(), m_resolvedAttachments.end(), id) != m_resolvedAttachments.end())
        return;
    m_resolvedAttachments.push_back(id);

    AttachmentComponent* attachment = m_scene.attachments().find(id);
    if (attachment == nullptr)
        return;

    const core::InstanceId owner = m_scene.parentOf(id);
    const PartComponent* part = m_scene.parts().find(owner);
    if (part == nullptr) {
        // Parented to something that is not a part -- while a script is still
        // assigning, or by mistake. Its own frame is the honest answer: it is
        // where it says it is, relative to nothing.
        attachment->worldCFrame = attachment->cframe;
        return;
    }

    // No weld settle here, and that is checked rather than assumed. `step` runs
    // `resolveWelds()` before `resolveAttachments()`, so every weld is already
    // done by the time this walk starts -- and the one path that reaches an
    // attachment EARLY, a weld anchored to one, settles the owner itself
    // through `ownerOf` before it asks. A settle in this function changed no
    // observable behaviour, so it is not here.
    core::CFrameD base = part->cframe;
    // **The rig a bone belongs to is the nearest skinned `MeshPart` above it**,
    // which is the owner itself for a bone on a character and the CHARACTER for
    // a bone on a ragdoll's limb. A limb is a plain `Part` with no rig of its
    // own; asking it about a joint would make the answer depend on how defensive
    // the host happens to be, and relying on that is not a contract worth having.
    if (attachment->jointName.id != 0 && m_skeleton != nullptr) {
        const core::InstanceId rig = rigAbove(id);
        if (rig.valid()) {
            if (attachment->jointIndex < 0)
                attachment->jointIndex = m_skeleton->findJoint(rig, m_scene.atoms().text(attachment->jointName));

            // **Resolved against the rig, but FOLLOWED only when the rig is the
            // owner.** A bone on a character IS the joint and moves with the
            // animation; a bone on a ragdoll's limb is a LABEL saying which
            // joint that limb stands for, and it has to stay where the
            // simulation put the limb -- following the joint would make it chase
            // the pose it is itself about to write.
            //
            // The `>= 0` guard is for the CAST rather than for the host:
            // `jointModel` refuses an index it does not have, so no test can
            // tell this apart from asking with `static_cast<u32>(-1)`.
            if (rig == owner && attachment->jointIndex >= 0) {
                core::CFrameD joint;
                if (m_skeleton->jointModel(owner, static_cast<u32>(attachment->jointIndex), joint))
                    base = part->cframe * joint * attachment->transform;
            }
        }
    }

    attachment->worldCFrame = base * attachment->cframe;
}

void PhysicsSync::driveRagdolls()
{
    if (m_skeleton == nullptr)
        return;

    // Pool order, which is creation order. It decides nothing about the result
    // -- a ragdoll's joints are independent of each other's overrides -- but it
    // decides that two runs walk the same list (R10).
    m_scene.ragdolls().forEach([&](core::InstanceId id, RagdollComponent& ragdoll) {
        if (!ragdoll.enabled)
            return;

        // The mesh whose pose this drives is the thing the ragdoll is parented
        // to, exactly as an `AnimationPlayer`'s is.
        const core::InstanceId meshPart = m_scene.parentOf(id);
        const PartComponent* mesh = m_scene.parts().find(meshPart);
        if (mesh == nullptr || m_skeleton->jointCount(meshPart) == 0)
            return;

        // Every `Bone` under the ragdoll that resolved to a joint. The part it
        // sits on is where the simulation put that limb; the bone says which
        // joint it is. Nothing else is declared -- which is also why a PARTIAL
        // ragdoll works: the joints nobody drives keep their own place relative
        // to their parent when `commitOverrides` re-runs the forward pass.
        std::vector<core::InstanceId> descendants;
        m_scene.collectDescendants(id, descendants);
        for (const core::InstanceId child : descendants) {
            const AttachmentComponent* bone = m_scene.attachments().find(child);
            if (bone == nullptr || bone->jointIndex < 0)
                continue;

            // Into the MESH's own space, because that is the space a pose is in.
            // The bone's world frame is where the limb ended up; dividing out
            // the mesh part's own transform is what carries it there.
            const core::CFrameD simulated = core::inverse(mesh->cframe) * bone->worldCFrame;

            // **`Blend` interpolates the POSE, not the solver.** At 0.5 the limb
            // has fallen exactly as far as it would at 1 and the drawn joint is
            // carried halfway there, which is what a stumble is: the clip keeps
            // running and the character sags. Ramping it is what makes going
            // down something other than a one-frame snap.
            //
            // The animated end comes from `jointModel`, which answers for a mesh
            // in bind pose as well as for one mid-clip -- so a character with
            // nothing playing blends towards where it is standing rather than
            // towards nothing.
            core::CFrameD model = simulated;
            if (ragdoll.blend < 1.0f) {
                core::CFrameD animated;
                if (!m_skeleton->jointModel(meshPart, static_cast<u32>(bone->jointIndex), animated))
                    continue;
                model = core::lerp(animated, simulated, static_cast<f64>(ragdoll.blend));
            }
            m_skeleton->setJointOverride(meshPart, static_cast<u32>(bone->jointIndex), model);
        }
    });
}

void PhysicsSync::resolveAttachments()
{
    // Pool order, which is creation order. It decides nothing about the RESULT
    // -- the recursion above settles dependencies whatever order they are
    // reached in -- but it decides that two runs walk the same list (R10).
    m_scene.attachments().forEach([&](core::InstanceId id, AttachmentComponent&) { resolveAttachment(id); });
}

void PhysicsSync::resolveWeld(core::InstanceId weldId, WeldComponent& weld)
{
    if (std::find(m_resolvedWelds.begin(), m_resolvedWelds.end(), weldId) != m_resolvedWelds.end())
        return;
    m_resolvedWelds.push_back(weldId);

    if (!weld.enabled || !m_scene.alive(weld.part0) || !m_scene.alive(weld.part1))
        return;

    // **The DRIVEN end must be a part.** A weld moves something, and an
    // attachment has nothing of its own to move -- it is a place on a part.
    PartComponent* driven = m_scene.parts().find(weld.part1);
    if (driven == nullptr)
        return;

    // The anchor may itself be driven by another weld. Resolve that one first,
    // so a chain settles in one pass instead of lagging one link per tick.
    //
    // An anchor that is an ATTACHMENT resolves through the part it is on, which
    // is what makes "weld a sword to the hand of a character that is itself
    // welded to a platform" settle in the same single pass.
    const core::InstanceId anchorPart = ownerOf(weld.part0);
    m_scene.welds().forEach([&](core::InstanceId otherId, WeldComponent& other) {
        if (otherId != weldId && other.enabled && other.part1 == anchorPart)
            resolveWeld(otherId, other);
    });

    core::CFrameD anchorFrameValue;
    if (!anchorFrame(weld.part0, anchorFrameValue))
        return;

    // A constraint reads the relationship off the world the first time it holds;
    // a weld was told it. `C1` is what carries it either way, so the resolver
    // has one formula.
    if (weld.captures && !weld.captured) {
        weld.c1 = core::inverse(driven->cframe) * (anchorFrameValue * weld.c0);
        weld.captured = true;
    }

    driven->cframe = (anchorFrameValue * weld.c0) * core::inverse(weld.c1);
    m_drivenParts.push_back(weld.part1);
}

void PhysicsSync::retireUnseen()
{
    // **Constraints first, and this is a contract rather than a tidiness.** A
    // joint holding a body that is gone is a dangling pointer inside the solver
    // and it is silent -- so the backend drops a body's joints itself, and a
    // sweep that retired bodies first would then destroy those joints a second
    // time from here.
    for (ConstraintRecord& record : m_constraints) {
        if (record.generation == 0 || record.seen)
            continue;
        m_backend.destroyConstraint(m_world, record.handle);
        record = ConstraintRecord{};
    }

    for (BodyRecord& record : m_bodies) {
        if (record.generation == 0 || record.seen)
            continue;
        // **`live` decides whether there is anything to destroy.** A record that
        // remembers a REFUSAL carries this instance's generation and no body, so
        // destroying it would hand the backend a handle that names nothing and
        // take the count down for a body that was never made.
        if (record.live) {
            m_backend.destroyBody(m_world, record.handle);
            --m_bodyCount;
        }
        record = BodyRecord{};
    }
    for (auto it = m_characters.begin(); it != m_characters.end();) {
        if (it->second.seen) {
            ++it;
            continue;
        }
        m_backend.destroyCharacter(m_world, it->second.handle);
        it = m_characters.erase(it);
    }
}

// The body an instance has, or an invalid handle. A part outside the world, or
// one whose body has not been made yet, has none.
physics::BodyHandle PhysicsSync::bodyHandleOf(core::InstanceId id) const
{
    if (!id.valid() || id.index >= m_bodies.size())
        return {};
    const BodyRecord& record = m_bodies[id.index];
    // **`seen` and not just `generation`.** The body walk runs before the
    // constraint walk, so `seen` is already this tick's answer -- and a body
    // that left the world still HAS a record until `retireUnseen` runs, so
    // asking only about the generation would hand a joint a body that is about
    // to be destroyed under it. The joint would then be marked seen, survive the
    // sweep, and hold a dangling pointer inside the solver.
    //
    // **And `live`**, for the reason `retireUnseen` reads it: a record that
    // remembers a refusal has this instance's generation and no body behind it.
    return record.generation == id.generation && record.seen && record.live ? record.handle : physics::BodyHandle{};
}

void PhysicsSync::applyConstraint(core::InstanceId id, ConstraintComponent& constraint)
{
    // The two BODIES, through the parts the attachments sit on. A constraint
    // joins bodies; the attachments are where on them.
    const core::InstanceId body0 = m_scene.parentOf(constraint.attachment0);
    const core::InstanceId body1 = m_scene.parentOf(constraint.attachment1);

    const AttachmentComponent* end0 = m_scene.attachments().find(constraint.attachment0);
    const AttachmentComponent* end1 = m_scene.attachments().find(constraint.attachment1);
    const bool usable = end0 != nullptr && end1 != nullptr && body0.valid() && body1.valid() && body0 != body1 &&
                        inWorld(id) && bodyHandleOf(body0).valid() && bodyHandleOf(body1).valid() &&
                        // **A `CharacterBody` is swept, not solved** (the M5
                        // finding): `CharacterVirtual` is not a solver body, so
                        // there is nothing for a joint to hold.
                        m_scene.characterBodies().find(body0) == nullptr &&
                        m_scene.characterBodies().find(body1) == nullptr;

    if (id.index >= m_constraints.size())
        m_constraints.resize(id.index + 1);
    ConstraintRecord& record = m_constraints[id.index];

    if (!usable) {
        // Left unseen, so `retireUnseen` destroys it. A joint whose ends a
        // script is still assigning is not an error -- it is what every script
        // that sets two properties on two lines briefly produces.
        return;
    }

    // A rebuild is needed when what the joint IS changed. Everything else --
    // a limit, the collide flag -- is an update the backend can take without
    // moving the constraint's place in the solve order.
    const bool rebuild = record.generation != id.generation || record.body0 != body0 || record.body1 != body1 ||
                         record.kind != constraint.kind;

    physics::ConstraintDesc desc;
    desc.type = static_cast<physics::ConstraintType>(constraint.kind);
    desc.first = bodyHandleOf(body0);
    desc.second = bodyHandleOf(body1);
    // In each body's OWN space, which is what the seam asks for and what makes a
    // floating-origin rebase cost nothing.
    desc.firstFrame = end0->cframe;
    desc.secondFrame = end1->cframe;
    desc.collideConnected = constraint.collideConnected;
    if (constraint.limitsEnabled) {
        desc.limitLow = constraint.limitLow;
        desc.limitHigh = constraint.limitHigh;
        desc.swingLimit = constraint.swingLimit;
        desc.twistLimit = constraint.twistLimit;
    }
    desc.userData = packInstance(id);

    if (rebuild) {
        if (record.generation != 0)
            m_backend.destroyConstraint(m_world, record.handle);
        record = ConstraintRecord{};
        record.handle = m_backend.createConstraint(m_world, desc);
        if (!record.handle.valid())
            return;
        record.generation = id.generation;
    }
    else if (record.collideConnected != constraint.collideConnected || record.limitLow != constraint.limitLow ||
             record.limitHigh != constraint.limitHigh || record.swingLimit != constraint.swingLimit ||
             record.twistLimit != constraint.twistLimit || record.limitsEnabled != constraint.limitsEnabled) {
        m_backend.updateConstraint(m_world, record.handle, desc);
    }

    if (record.enabled != constraint.enabled) {
        m_backend.setConstraintEnabled(m_world, record.handle, constraint.enabled);
        record.enabled = constraint.enabled;
    }

    record.seen = true;
    record.body0 = body0;
    record.body1 = body1;
    record.kind = constraint.kind;
    record.collideConnected = constraint.collideConnected;
    record.limitLow = constraint.limitLow;
    record.limitHigh = constraint.limitHigh;
    record.swingLimit = constraint.swingLimit;
    record.twistLimit = constraint.twistLimit;
    record.limitsEnabled = constraint.limitsEnabled;
}

void PhysicsSync::writeBack()
{
    m_active.clear();
    m_backend.collectActiveBodies(m_world, m_active);

    for (const physics::ActiveBody& active : m_active) {
        const core::InstanceId id = unpackInstance(active.userData);
        if (!m_scene.alive(id))
            continue;

        if (id.index >= m_bodies.size() || m_bodies[id.index].generation != id.generation)
            continue;
        BodyRecord& record = m_bodies[id.index];

        // **A kinematic body is not written back** (D031). Its transform is the
        // script's -- a tween wrote it and the mirror moved the body to match --
        // so copying the solver's answer back into the component says nothing
        // and costs a pool lookup and a write per body per tick.
        //
        // Jolt reports every kinematic body as active, always, so before this
        // line `churn10k`'s writeback went from 0.03 ms to 9.9 ms the moment
        // moving anchored parts became kinematic. The measurement is what found
        // it, which is the whole argument for `perf-baselines.md`.
        if (record.backendMotion == physics::MotionType::Kinematic)
            continue;

        // The QUIET write: straight into the component, with the changed set
        // reaching a listener only if one exists (architecture.md §4). Ten
        // thousand moving parts enqueue nothing while nobody is watching.
        if (PartComponent* part = m_scene.parts().find(id); part != nullptr) {
            part->cframe = active.state.transform;
            record.written = active.state.transform;
        }
        if (RigidBodyComponent* body = m_scene.rigidBodies().find(id); body != nullptr) {
            body->linearVelocity = active.state.linearVelocity;
            body->angularVelocity = active.state.angularVelocity;
            body->active = active.state.active;
        }
    }

    // A body that just went to sleep is no longer in the active list, and its
    // component still says it is moving. One pass over the records fixes that
    // and costs nothing for a world where nothing is asleep.
    // Over what the SCENE says is still moving rather than over every record: a
    // body that just went to sleep has left the active list while its component
    // still says it is moving, and a world where nothing is asleep pays for
    // nothing.
    m_scene.rigidBodies().forEach([&](core::InstanceId id, RigidBodyComponent& body) {
        if (!body.active)
            return;
        const u64 packed = packInstance(id);
        const bool stillActive = std::any_of(m_active.begin(), m_active.end(), [&](const physics::ActiveBody& active) {
            return active.userData == packed;
        });
        if (!stillActive) {
            body.active = false;
            body.linearVelocity = core::Vec3{0.0f, 0.0f, 0.0f};
            body.angularVelocity = core::Vec3{0.0f, 0.0f, 0.0f};
        }
    });

    writeCharacters();
}

void PhysicsSync::writeCharacters()
{
    for (auto& entry : m_characters) {
        const core::InstanceId id = unpackInstance(entry.first);
        CharacterBodyComponent* character = m_scene.characterBodies().find(id);
        PartComponent* part = m_scene.parts().find(id);
        if (character == nullptr || part == nullptr)
            continue;

        const physics::CharacterState state = m_backend.characterState(m_world, entry.second.handle);
        part->cframe = state.transform;
        entry.second.written = state.transform;

        const bool grounded = state.ground == physics::CharacterGround::Grounded;
        const core::InstanceId ground =
            state.groundUserData == 0 ? core::InstanceId{} : instanceOf(state.groundUserData);

        // Landing is a transition, not a state: airborne last tick and grounded
        // now. Reading the flag alone would fire it every tick a character
        // stands still.
        if (grounded && !character->grounded) {
            Change change;
            change.kind = ChangeKind::InstanceEvent;
            change.subject = id;
            change.other = ground;
            change.name = m_scene.atoms().intern("Landed");
            m_scene.changes().push(change);
        }

        // **`Touched` is the backend's, all of it** (D028).
        //
        // M6 diffed the surface under the character's feet here, because the
        // rigid-body contact listener could not see a `CharacterVirtual` at all
        // and a checkpoint pad you walk onto is what an obby is made of. That
        // covered the ground and nothing else -- a wall walked into fired
        // nothing -- and it was a second diff for a signal that already had one.
        //
        // Both halves come through `publishContacts` now, from the character's
        // own active contacts. `groundPart` stays because `Landed` above is
        // about ground STATE rather than about contact, and because riding a
        // platform needs to know which one.

        character->grounded = grounded;
        character->state = grounded ? 0 : 1;
        character->groundPart = ground;
        if (RigidBodyComponent* body = m_scene.rigidBodies().find(id); body != nullptr) {
            body->linearVelocity = state.linearVelocity;
            body->active = true;
        }
    }
}

void PhysicsSync::publishContacts()
{
    const core::NameAtom touched = m_scene.atoms().intern("Touched");
    const core::NameAtom touchEnded = m_scene.atoms().intern("TouchEnded");

    // Both directions, because `Touched` is a fact about each part and a script
    // connects to one of them without knowing which side of the pair it is.
    for (const physics::ContactEvent& event : m_backend.drainContacts(m_world)) {
        const core::InstanceId first = unpackInstance(event.firstUserData);
        const core::InstanceId second = unpackInstance(event.secondUserData);
        if (!m_scene.alive(first) || !m_scene.alive(second))
            continue;

        // **`CanTouch` gates the PAIR**, which is why it is asked here rather
        // than per side below: a signal naming a part that said not to report
        // touches would be that part reporting one on somebody else's handler.
        // The contact was still solved -- what stops is the queueing, which is
        // the cost a world full of scenery is paying for listeners it does not
        // have.
        const RigidBodyComponent* firstBody = m_scene.rigidBodies().find(first);
        const RigidBodyComponent* secondBody = m_scene.rigidBodies().find(second);
        if ((firstBody != nullptr && !firstBody->canTouch) || (secondBody != nullptr && !secondBody->canTouch))
            continue;

        const core::NameAtom name = event.phase == physics::ContactPhase::Began ? touched : touchEnded;

        Change change;
        change.kind = ChangeKind::InstanceEvent;
        change.name = name;

        change.subject = first;
        change.other = second;
        m_scene.changes().push(change);

        change.subject = second;
        change.other = first;
        m_scene.changes().push(change);
    }
}

bool PhysicsSync::shouldRebase(core::DVec3 focus, f64 threshold) const noexcept
{
    const core::DVec3 offset = focus - m_origin;
    // Squared, so the common answer -- "no" -- costs no square root, and
    // compared per axis so the tolerance is a BOX rather than a sphere: a focus
    // travelling along one axis should rebase at the same distance whichever
    // axis it is.
    return std::abs(offset.x) > threshold || std::abs(offset.y) > threshold || std::abs(offset.z) > threshold;
}

void PhysicsSync::setOrigin(core::DVec3 origin)
{
    if (origin == m_origin) {
        return;
    }
    m_origin = origin;
    m_rebaseCount += 1;
    m_backend.setWorldOrigin(m_world, origin);

    // The mirror's own record of what it last pushed is in ABSOLUTE
    // coordinates, so it survives a rebase untouched -- which is the reason the
    // record was stored that way rather than as whatever the solver held. If it
    // had been local, every part in the world would look like a script write on
    // the next tick and the mirror would push six thousand transforms back down
    // for nothing.
}

void PhysicsSync::mirror()
{
    if (!m_world.valid() || !m_workspace.valid())
        return;
    applyScene();
}

void PhysicsSync::step(f64 fixedDt)
{
    if (!m_world.valid())
        return;

    // `Workspace` is handed in by the host rather than looked up here: `scene`
    // has no notion of the DataModel root, and the host already resolves it for
    // the renderer. An invalid id means no world root, which means nothing has
    // a body -- and that is exactly the M4.5 defect shape, so the host has a
    // test asserting it resolved (`world_host_tests.cpp`).
    if (!m_workspace.valid())
        return;

    const auto begin = std::chrono::steady_clock::now();
    applyScene();
    const auto applied = std::chrono::steady_clock::now();

    if (const WorkspaceComponent* workspace = m_scene.workspaces().find(m_workspace); workspace != nullptr)
        m_backend.setGravity(m_world, workspace->gravity);

    m_backend.step(m_world, static_cast<f32>(fixedDt));
    const auto stepped = std::chrono::steady_clock::now();

    writeBack();
    // After the writeback, so a driven part follows where its anchor ENDED UP
    // this tick rather than where it was at the start of it.
    resolveWelds();
    resolveAttachments();
    driveRagdolls();
    // And the pose is committed after everything that could have moved a joint,
    // because `commitOverrides` re-runs the forward pass and anything written
    // afterwards would be a frame behind. Nothing sets an override yet; this is
    // where a ragdoll's writes land when one exists.
    if (m_skeleton != nullptr)
        m_skeleton->commitOverrides();
    publishContacts();
    const auto end = std::chrono::steady_clock::now();

    m_timings.apply = std::chrono::duration<f64>(applied - begin).count();
    m_timings.step = std::chrono::duration<f64>(stepped - applied).count();
    m_timings.writeback = std::chrono::duration<f64>(end - stepped).count();
}

void PhysicsSync::setCollisionPoints(core::NameAtom content, std::vector<core::Vec3> points)
{
    const auto at = std::lower_bound(m_collisionPoints.begin(), m_collisionPoints.end(), content,
                                     [](const auto& entry, core::NameAtom key) { return entry.first.id < key.id; });
    // **Counted up on every replacement**, and never reset. It is what tells a
    // body whose hull came from this cloud that the geometry underneath it is
    // not the geometry it was built with -- a question two `ShapeDesc`s could
    // not answer from type and size alone, and could only answer by comparing
    // spans nobody is allowed to keep.
    ++m_collisionRevision;
    if (at != m_collisionPoints.end() && at->first == content) {
        at->second.points = std::move(points);
        at->second.revision = m_collisionRevision;
        return;
    }
    m_collisionPoints.insert(at, {content, CollisionMesh{std::move(points), m_collisionRevision}});
}

} // namespace luaug::scene
