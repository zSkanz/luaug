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

#include "luaug/asset/material.h"
#include "luaug/asset/terrain.h"
#include "luaug/asset/voxel.h"
#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/types.h"
#include "luaug/scene/types.h"

#include <array>
#include <map>
#include <string>

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

    // `BasePart.Material` (ADR 0090): the material asset this part wears, as its
    // URN interned, or invalid for the engine default. **A part has no colour of
    // its own**; its look is this material's, changed only where
    // `materialParameters` overrides what the material declares.
    //
    // A URN rather than an instance, which is what it was for one release: an
    // instance reference means something only inside one world, and a surface
    // is not a thing in any world. Interned so the component stays trivially
    // copyable and `World::snapshot` a per-pool copy.
    core::NameAtom material;
    // Nonzero when the part wears a runtime CLONE of that asset: the clone's id
    // in `World::materialClones`.
    u32 materialClone = 0;
    // `BasePart.MaterialParameters`: what this part overrides. An override the
    // current material does not declare is kept here and ignored when drawn.
    asset::MaterialOverrides materialParameters;
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

    // What the mesh measures at `Size == meshSize`, so that `Size` means the
    // same thing on a `MeshPart` as it does on a `Part`: the renderer and the
    // physics hull both scale by `size / meshSize`.
    //
    // **Authored, not derived.** The mesh's real bounds are known only where
    // something loaded it, and the world hash is required to be a pure function
    // of the operation sequence -- a derived divisor would make a headless run
    // and a rendered run disagree about the same scene. The import writes this
    // from the compiled bounds; until then it is one, and a part whose `Size` is
    // also one draws exactly as it did before this field existed.
    core::Vec3 meshSize{1.0f, 1.0f, 1.0f};
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
    // **Gates the PAIR rather than one side.** A part with this off is silent
    // and so is whatever touches it, because a signal naming a part that said
    // not to report touches would be that part reporting one on somebody else's
    // handler. Contacts are still solved; what stops is the queueing.
    bool canTouch = true;
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

// `Weld` and `WeldConstraint` (M5, added to the milestone by human decision).
//
// One component for both, because the two differ in where the offset comes from
// and in nothing else: a `Weld` is told it and a `WeldConstraint` reads it off
// the world when it becomes active. Sharing the storage is what keeps the
// resolver from having to walk two pools in a defined order relative to each
// other, which is a source of non-determinism that would exist for no reason.
//
// **A transform weld, and the roadmap says why it has to be one:** a
// `CharacterVirtual` is not a body in the physics system, a solver constraint
// joins bodies, so a constraint could never reach a character. The driven part
// stops being independently simulated and follows its anchor; the solver is not
// involved.
// `Attachment` and `Bone`, in one pool.
//
// **One component for two classes**, exactly as one `WeldComponent` serves
// `Weld` and `WeldConstraint` -- and for the same reason: two pools would need a
// defined resolution order relative to each other, which is a determinism
// problem invented for nothing.
//
// An attachment is a named place ON something: on a part, for a socket a weld or
// a constraint can hold; on a mesh part's joint, for a sword in a hand. It has
// no body, is not simulated, and costs one entry in a `forEach` that a world
// with none never runs.
struct AttachmentComponent
{
    // Where it sits relative to whatever it is parented to. Authored.
    core::CFrameD cframe;

    // Where that lands in the world. **Derived, recomputed every tick**, and
    // never written by a script: a world transform that could be assigned would
    // be a second source of truth about the same place.
    core::CFrameD worldCFrame;

    // `Bone` only: the joint in the parent `MeshPart`'s skeleton this follows.
    // Empty on a plain `Attachment`, which follows its parent part instead.
    core::NameAtom jointName;

    // Where that name resolved to, or -1. Derived, and re-resolved when the rig
    // reloads -- an index survives a file change where a pointer would not.
    //
    // **-1 falls back to the parent part's own transform**, not to the origin. A
    // bone whose joint an artist renamed puts a sword at the character's feet if
    // it resolves to zero and on the character if it resolves to nothing.
    i32 jointIndex = -1;

    // `Bone` only: a local-space offset applied on TOP of the animated pose.
    // This is the writable half of procedural animation -- inverse kinematics, a
    // look-at, a recoil -- and it is the identity for a bone nobody drives.
    core::CFrameD transform;
};

// `BallSocketConstraint`, `HingeConstraint` and `FixedConstraint`, in one pool.
//
// **A constraint takes two `Attachment`s, not two parts**, and that is what
// makes the joint frame authorable: where a door hinges is a place on the door
// and a place on the frame, and both are things you can see and move in the
// editor. Two parts and a pair of offset `CFrame`s would be the same numbers
// with nowhere to put them.
//
// A `Weld` is not one of these. A weld DRIVES its second part from its first
// every tick and the solver is never asked; a constraint hands both bodies to
// the solver and lets it work out where they end up. Welding a MeshPart to a
// character keeps them together whatever the solver thinks; a hinge lets a door
// swing under its own weight.
struct ConstraintComponent
{
    core::InstanceId attachment0;
    core::InstanceId attachment1;

    // `physics::ConstraintType`'s value, stored raw for the reason
    // `PartComponent` stores a shape that way: the generated accessor then needs
    // no per-enum C++ type. Set by the class's own hook, not by a property --
    // a `HingeConstraint` that could become a slider is two classes wearing one
    // name.
    i32 kind = 0;

    bool enabled = true;

    // Hinge: the range about the joint's own X axis, in radians. `low > high` is
    // unlimited, which is the default.
    f32 limitLow = 1.0f;
    f32 limitHigh = -1.0f;

    // Ball socket: the half-angle of the swing cone and the twist range. Past
    // these it stops being a free ball joint and becomes the swing-twist a
    // shoulder is.
    f32 swingLimit = 3.14159265f;
    f32 twistLimit = 3.14159265f;
    bool limitsEnabled = false;

    // Whether the two bodies still collide with each other. **False is what a
    // ragdoll needs**: an upper arm and a lower arm overlap at the elbow by
    // construction, and left colliding they shove each other apart every step.
    bool collideConnected = true;
};

// `Ragdoll`: the switch that makes a character's pose come from the simulation
// instead of from a clip.
//
// **It owns nothing.** A ragdoll is parts, `Bone`s and constraints -- all of
// which are real instances somebody can see, select and move -- and this is the
// one flag that says "drive the pose from them". That is deliberate: a class
// that owned its own hidden bodies would be a second owner of things the mirror
// creates by mark-and-sweep from the tree, and two owners of one body is the
// mirror rule broken.
//
// How the pose is found: every `Bone` under this ragdoll that names a joint, on
// a part. The part is where the simulation put that limb; the bone says which
// joint it is. Nothing else has to be declared.
struct RagdollComponent
{
    // Off is a character animated by clips with a set of parts sitting there
    // doing nothing. On is a character whose every named joint comes from where
    // its part ended up.
    bool enabled = false;

    // How much of each driven joint comes from the simulation, 0 to 1. A POSE
    // blend: the limbs fall exactly as hard at 0.5 as at 1, and what changes is
    // only how far the drawn joint is carried towards them. Ramping it is what
    // makes going down and getting up something other than a one-frame snap.
    f32 blend = 1.0f;
};

struct WeldComponent
{
    // The anchor and the driven part. Either may be invalid while a script is
    // still assigning them, which is not an error -- it is what every script
    // that sets two properties on two lines briefly produces.
    core::InstanceId part0;
    core::InstanceId part1;

    // `Part0.CFrame * c0 == Part1.CFrame * c1`, which is the one sentence that
    // says where both offsets go.
    core::CFrameD c0;
    core::CFrameD c1;

    bool enabled = true;

    // A `WeldConstraint` captures `c1` when it becomes active and a `Weld` never
    // does. One flag rather than two components.
    bool captures = false;
    // Whether the capture has happened, so that enabling a constraint twice
    // does not re-capture a transform the weld itself produced.
    bool captured = false;
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
    // `Enum.CameraProjection`: 0 Perspective, 1 Orthographic (the 2D layer).
    i32 projection = 0;
    // Half the view's height in metres, for an orthographic camera.
    f32 orthographicSize = 10.0f;
};

struct PointLightComponent
{
    core::Color3 color{1.0f, 1.0f, 1.0f};
    f32 brightness = 1.0f;
    f32 range = 16.0f;
    // Skipped before the renderer counts it against the light budget, which is
    // what makes this different from a brightness of zero.
    bool enabled = true;
    // Stored and reported faithfully; this release casts shadows from the sun
    // alone (M4 brief, Decision 10). A property that round-trips is honest; one
    // that silently reads back false would not be.
    bool shadows = false;
};

// `ParticleEmitter` (F2). Everything a script can set, and one counter.
//
// **The particles themselves are not here.** They are a picture, not the
// world: what a spark does after it leaves the emitter decides nothing a
// script can observe, so they live in `render::ParticleSystem` and are
// simulated on the render clock. What IS world state is how the emitter is
// configured and how many bursts a script asked for -- the second as a running
// total, so the renderer spawns the difference since it last looked and never
// has to write back into the world to say it did.
struct ParticleEmitterComponent
{
    bool enabled = true;
    // Particles a second, continuously, while enabled.
    f32 rate = 20.0f;
    // Seconds each particle lives.
    f32 lifetime = 2.0f;
    // Metres a second at birth, along the emitter's up direction within
    // `spreadAngle` degrees of it.
    f32 speed = 4.0f;
    f32 spreadAngle = 15.0f;
    // Metres a second squared, in world space: `(0, -9.81, 0)` is gravity.
    core::Vec3 acceleration{0.0f, 0.0f, 0.0f};
    // The fraction of velocity lost per second, so smoke slows and sparks do not.
    f32 drag = 0.0f;
    // Start and end of life, interpolated linearly over it.
    core::Color3 color{1.0f, 1.0f, 1.0f};
    core::Color3 colorEnd{1.0f, 1.0f, 1.0f};
    f32 size = 0.5f;
    f32 sizeEnd = 0.5f;
    f32 transparency = 0.0f;
    f32 transparencyEnd = 1.0f;
    // 0 is an ordinary blended particle, 1 adds its light to what is behind it
    // -- fire and sparks -- and anything between is both.
    f32 lightEmission = 0.0f;
    // A multiplier on the colour, unclamped: above one is what makes a spark
    // bloom.
    f32 brightness = 1.0f;
    // `Enum.ParticleShape`: 0 Soft, 1 Disc, 2 Square.
    i32 shape = 0;
    // Every particle `Emit` has asked for since the emitter existed.
    u64 emitted = 0;
};

// `Decal` (F2): an image projected onto whatever is inside a box.
struct DecalComponent
{
    // Where the box is. Relative to the parent `BasePart` when it has one, so a
    // decal on a moving crate moves with it; in the world otherwise. The image
    // lies in the box's X and Y and is projected along its Z.
    core::CFrameD cframe;
    // The box, in metres: width, height, and how deep it reaches.
    core::Vec3 size{2.0f, 2.0f, 1.0f};
    // The image, by content URN; none paints a solid colour.
    core::NameAtom texture;
    // Multiplies the image.
    core::Color3 color{1.0f, 1.0f, 1.0f};
    f32 transparency = 0.0f;
};

struct SpotLightComponent
{
    core::Color3 color{1.0f, 1.0f, 1.0f};
    f32 brightness = 1.0f;
    f32 range = 16.0f;
    // Full cone width in degrees.
    f32 angle = 45.0f;
    // As on `PointLightComponent`: skipped before the budget is counted.
    bool enabled = true;
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
    // Enclosed spaces, and open ones (ADR 0084). Equal by default, so a world
    // that sets neither is lit the same inside and out.
    core::Color3 ambient{0.15f, 0.16f, 0.2f};
    core::Color3 outdoorAmbient{0.15f, 0.16f, 0.2f};
    f32 brightness = 2.0f;
    core::Color3 fogColor{0.6f, 0.7f, 0.85f};
    f32 fogStart = 200.0f;
    // Equal to or below `fogStart` means no fog at all, which is how fog is
    // turned off without a second flag to keep in sync.
    f32 fogEnd = 0.0f;
    // EV stops on top of the automatic exposure (M7.5). Zero means "whatever
    // the frame measured"; the unit is the photographer's, so +1 is twice the
    // light. Unbounded on purpose -- an exposure a scene deliberately blows out
    // is a look, not an error.
    f32 exposureCompensation = 0.0f;
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

// `Terrain`'s own state: the field, and the reservation that bounds it.
//
// **The field lives in `asset` and is held here by value** rather than being
// re-declared. `TerrainField` is two sorted vectors of `shared_ptr<const T>`, so
// a copy of this component copies pointers and bumps refcounts -- which is what
// makes `World::snapshot`, and therefore the editor's sixty-four-deep undo
// stack, affordable over a sculpted world.
//
// **Neither `field` nor `fieldRevision` has a property**, deliberately. The
// field IS the terrain -- megabytes of samples that no inspector row could show
// and no `scene::Value` could carry -- and it is read by the mesher and the
// collider builder through this pool rather than through an accessor. Both are
// in `inertcheck`'s `StorageOnly` list with that argument written down.
struct TerrainComponent
{
    asset::TerrainField field;

    // **Where the field's own origin sits in the world.**
    //
    // A terrain is an instance and an instance can be moved, which is what the
    // owner asked for and is the right answer: two terrains in one world, an
    // island placed beside another, ground shifted after the fact.
    //
    // **A position and not a `CFrame`.** A grid of voxels could be turned, but
    // every consumer -- the mesher's draw transform, the colliders, the
    // brush's raycast -- would then carry a rotation for something nobody has
    // asked for, so a terrain translates and does not turn.
    //
    // f64 because it is a world coordinate (R9), and every consumer -- the
    // mesher's draw transform, the chunk colliders, the brush's raycast, the
    // script's `HeightAt` -- offsets by it rather than baking it into the field.
    // Baking would make moving a terrain a rewrite of every voxel.
    core::DVec3 origin;

    // **Bumped on every write to `field`**, and read by `PhysicsSync` to decide
    // whether a tile's collider is still current -- the same trick
    // `ShapeDesc::pointsRevision` uses, and for the same reason: comparing the
    // contents would mean keeping a copy of them.
    core::u64 fieldRevision = 0;

    // How wide one streamed cell is, in metres. Not settable from a script: it
    // is the streaming grid's own spacing, and a terrain that disagreed with it
    // would load and unload on different boundaries from everything else.
    f32 cellSize = 64.0f;

    // **The world's floor and ceiling**: no voxel is written outside them, and
    // the verbs that lay ground rather than add to it fill from the floor up.
    // Mirrored into the field's settings, where the brushes read them.
    f32 minHeight = -256.0f;
    f32 maxHeight = 256.0f;

    // **Where the ground lives when it is not in the scene** (ADR 0087): the
    // cell index of a terrain saved as a folder of cells, relative to the
    // project's `content/`, or empty for one whose field the scene carries.
    //
    // With one, `field` holds what is resident -- the cells around whoever is
    // looking, and every cell edited since it was last saved -- and the rest is
    // on disk, streamed in the editor exactly as a game streams it. Not a
    // property: it is where the scene's ground is stored, which a script has
    // no business moving, and not simulation state, so the world hash leaves
    // it out as it leaves out a mesh's file name.
    std::string cellIndex;
};

// --- The 2D layer (post-v1 phase 3, docs/briefs/p3-2d-kickoff.md) ------------

// `Part2D`: a sprite and a body in one, on the XY plane.
struct Part2DComponent
{
    core::Vec2 position{0.0f, 0.0f};
    // Degrees, counter-clockwise, as authored.
    f32 rotation = 0.0f;
    core::Vec2 size{1.0f, 1.0f};
    // `Enum.Shape2D`: 0 Box, 1 Circle, 2 Capsule.
    i32 shape = 0;
    core::Color3 color{1.0f, 1.0f, 1.0f};
    f32 transparency = 0.0f;
    i32 zIndex = 0;
    bool flipX = false;
    bool flipY = false;
    core::NameAtom image;
    // Pixels; a zero size draws the whole image.
    core::Vec2 imageRectOffset{0.0f, 0.0f};
    core::Vec2 imageRectSize{0.0f, 0.0f};
    // `Enum.TextureFilter`: 0 Linear, 1 Nearest.
    i32 filter = 0;
    bool anchored = false;
    bool canCollide = true;
    bool sensor = false;
    f32 density = 1.0f;
    f32 friction = 0.3f;
    f32 elasticity = 0.0f;
    bool fixedRotation = false;
    f32 gravityScale = 1.0f;
    core::Vec2 velocity{0.0f, 0.0f};
    // Degrees per second, as authored.
    f32 angularVelocity = 0.0f;
    core::NameAtom collisionGroup;
    // What `ApplyImpulse` asked for since the last tick, applied at the start
    // of the next -- as a `BasePart`'s is (M5).
    core::Vec2 pendingImpulse{0.0f, 0.0f};
};

// One 16 by 16 block of a tilemap's cells.
inline constexpr i32 TileChunkEdge = 16;
struct TileChunkKey
{
    i32 x = 0;
    i32 y = 0;
    [[nodiscard]] constexpr auto operator<=>(const TileChunkKey&) const noexcept = default;
};
using TileChunk = std::array<core::u16, static_cast<core::usize>(TileChunkEdge* TileChunkEdge)>;

// `Tilemap2D`: a grid of tiles from one tileset.
//
// **Sparse by blocks**, in a sorted map (R10: its order reaches the physics
// bodies and the draw order). A block that goes empty is dropped, so a map
// is as large as what is painted, not as the rectangle it spans.
struct Tilemap2DComponent
{
    core::Vec2 position{0.0f, 0.0f};
    f32 cellSize = 1.0f;
    core::NameAtom tileset;
    core::Vec2 tileSize{16.0f, 16.0f};
    i32 zIndex = 0;
    core::Color3 color{1.0f, 1.0f, 1.0f};
    // `Enum.TextureFilter`, Nearest by default: tiles are pixel art.
    i32 filter = 1;
    bool collides = true;
    f32 friction = 0.3f;
    core::NameAtom collisionGroup;
    std::map<TileChunkKey, TileChunk> chunks;
    // Bumped on every change of a cell, and read by whatever rebuilds from
    // them -- the colliders and the drawing -- the way `fieldRevision` is.
    core::u64 revision = 0;

    [[nodiscard]] static constexpr i32 floorDivide(i32 value, i32 by) noexcept
    {
        return value >= 0 ? value / by : -((-value + by - 1) / by);
    }

    [[nodiscard]] core::u16 cell(i32 x, i32 y) const noexcept
    {
        const TileChunkKey key{floorDivide(x, TileChunkEdge), floorDivide(y, TileChunkEdge)};
        const auto found = chunks.find(key);
        if (found == chunks.end())
            return 0;
        const i32 lx = x - key.x * TileChunkEdge;
        const i32 ly = y - key.y * TileChunkEdge;
        return found->second[static_cast<core::usize>(ly * TileChunkEdge + lx)];
    }

    // Answers whether the cell changed.
    bool setCell(i32 x, i32 y, core::u16 tile)
    {
        const TileChunkKey key{floorDivide(x, TileChunkEdge), floorDivide(y, TileChunkEdge)};
        auto found = chunks.find(key);
        if (found == chunks.end()) {
            if (tile == 0)
                return false;
            found = chunks.emplace(key, TileChunk{}).first;
        }
        const i32 lx = x - key.x * TileChunkEdge;
        const i32 ly = y - key.y * TileChunkEdge;
        core::u16& at = found->second[static_cast<core::usize>(ly * TileChunkEdge + lx)];
        if (at == tile)
            return false;
        at = tile;
        if (tile == 0) {
            bool empty = true;
            for (const core::u16 other : found->second)
                empty = empty && other == 0;
            if (empty)
                chunks.erase(found);
        }
        revision += 1;
        return true;
    }
};

// `NavigationService`'s agent (ADR 0089): the one body size its walkable mesh
// is built for. The mesh itself is not state -- it is built from the world
// where queries ask, and a restored world rebuilds it -- so this is all of the
// service a world holds.
struct NavigationComponent
{
    f32 agentRadius = 0.5f;
    f32 agentHeight = 2.0f;
    f32 agentMaxClimb = 0.5f;
    // Degrees.
    f32 agentMaxSlope = 45.0f;
};

// A registered block type (V1, `VoxelService`). Its id is its position in the
// registry plus one, which is registration order -- a pure function of the
// script that registered it.
struct VoxelBlockType
{
    core::NameAtom name;
    // The top face, and every face of a block registered with one colour.
    core::Color3 color{1.0f, 1.0f, 1.0f};
    core::Color3 side{1.0f, 1.0f, 1.0f};
    core::Color3 bottom{1.0f, 1.0f, 1.0f};
    // Images, by content URN, for the same three faces; empty for none. The
    // colours tint them, so a white block shows its image as drawn.
    core::NameAtom texture;
    core::NameAtom sideTexture;
    core::NameAtom bottomTexture;
    // `Enum.BlockOpacity`: 0 Opaque, 1 Cutout, 2 Translucent.
    i32 opacity = 0;
    // How much a translucent block lets through, 0 to 1, where it has no image
    // alpha of its own to say.
    f32 transparency = 0.5f;
    // **A fluid, when above zero**: how many blocks it spreads sideways from a
    // source, 1 to `asset::MaxFluidReach` (`luaug/scene/voxel_fluid.h`).
    core::u8 fluidReach = 0;
    // And how often it moves, in simulation ticks per step -- five is a
    // quarter-second of water at sixty hertz, and a slower fluid is a larger
    // number.
    u32 fluidTicks = 5;
};

// One thing a player did this tick: an input action's name and its value (N1).
//
// **In the currency `InputActionComponent` uses**, so `Player:GetIntent`
// answers exactly what `InputAction:GetState` answered on the machine where the
// key was pressed -- and a game reads its own player and a remote one through
// the same call.
struct PlayerIntent
{
    core::NameAtom action;
    // `Enum.InputActionType`: 0 Bool, 1 Direction1D, 2 Direction2D,
    // 3 Direction3D, 4 ViewportPosition.
    i32 type = 0;
    core::Vec3 axis;
    bool pressed = false;
};

// `Player` (N1): somebody taking part in this world, local or not.
struct PlayerComponent
{
    // The authority's number for them: 1 for whoever sits at a solo or hosting
    // machine, 2 and up for replicas in the order they joined, and 0 on a
    // replica until the authority's welcome names it.
    u32 userId = 0;
    // Whether this player is the one at this machine, whose intents are this
    // machine's own input actions, copied each tick.
    bool local = false;
    // This tick's intents, in the order the sending machine's actions are.
    std::vector<PlayerIntent> intents;
    // `Player.Character`: the part that is them, on this machine.
    core::InstanceId character;
};

// The block world `VoxelService` owns. **Not the terrain** -- see
// `luaug/asset/voxel.h`. One per `VoxelService`, which is one per DataModel.
struct VoxelComponent
{
    asset::VoxelGrid grid;
    f32 blockSize = 1.0f;
    std::vector<VoxelBlockType> types;
    // **Bumped on every write to `grid`**, the same trick the terrain uses: the
    // renderer and the physics mirror compare it before doing any work.
    core::u64 revision = 0;
    // The blocks the fluid step is due to look at, by position, with the tick
    // each is due. Ordered, so the step visits them the same way on every
    // machine (R10), and copied with the component, so a world restored from
    // a snapshot resumes its water where it was.
    std::map<std::array<i32, 3>, core::u64> fluidWakes;
    // **What a fluid becomes where it touches another** (`SetFluidReaction`):
    // `from` touching `touching` turns into `result` -- lava meeting water is
    // stone, and the water stays water. Sorted by (`from`, `touching`), one
    // entry per pair.
    struct FluidReaction
    {
        asset::BlockId from = asset::AirBlock;
        asset::BlockId touching = asset::AirBlock;
        asset::BlockId result = asset::AirBlock;
    };
    std::vector<FluidReaction> fluidReactions;
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

    // `Enum.StreamingMode`'s value, stored raw for the reason `PartComponent`
    // stores a shape that way: the generated accessor then needs no per-enum
    // C++ type. Read by the partitioner and by nothing on a frame path -- it
    // decides what a cell holds, once, when the world is written down.
    i32 streamingMode = 0;

    // `Model.Scale`. **Absolute and already applied**: the number here is what
    // the property reads back, and every part under the model is ALREADY at
    // this size. Writing the property fans the ratio out over the subtree once;
    // nothing re-reads this field per frame, and nothing multiplies by it.
    //
    // That is what makes a scaled model cost nothing to draw, and it is also
    // why a descendant added afterwards is not scaled -- the fan-out already
    // happened. Stated on the property's own Doc, because it is the one thing
    // about `Scale` that surprises people.
    f32 scale = 1.0f;
};

struct ScriptComponent
{
    bool enabled = true;

    // **The Luau this instance carries.** Data on the instance rather than a
    // path to a file, which is what makes a script an ordinary thing: created
    // like any other instance, copied, put inside a prefab, and saved inside
    // whatever holds it.
    //
    // A `std::string` in a component, like `TextLabel`'s text and `Sound`'s
    // asset before it: a snapshot copies pools by value, so the deep copy is
    // the correct one and there is no memcpy path to break.
    std::string source;
};

// --- Input (M6) --------------------------------------------------------------
//
// The three Input Action System classes' storage. It lives here for the same
// reason the render module's does: `scene` (L3) owns the pools, `input` (L4)
// owns the meaning, and scene never interprets a byte of it. See the note above
// the render components for why this is not a type-erased registry.

// `InputContext`. A group of actions that are live together, and the unit
// resolution walks.
struct InputContextComponent
{
    // Resolution order, highest first. It orders FALLTHROUGH only; the clock is
    // `rate` (ADR 0039), and conflating the two would change an action's
    // determinism class whenever somebody re-tuned a number.
    f32 priority = 0.0f;
    // `Enum.InputRate`: 0 Simulation, 1 Render.
    i32 rate = 0;
    bool enabled = true;
    bool sink = false;
};

// `InputAction`. Its resolved value lives here rather than in `input`'s own
// storage so that a snapshot of the world carries it: an action's state is
// simulation state, and a replay that restored the tree without it would resume
// with every key released.
struct InputActionComponent
{
    // The value as of the last dispatch. One field per shape rather than a
    // variant: the whole struct is 20 bytes either way, and a POD component is
    // what makes the pool memcpy-able.
    core::Vec3 axis;
    // `Enum.InputActionType`: 0 Bool, 1 Direction1D, 2 Direction2D,
    // 3 Direction3D, 4 ViewportPosition.
    i32 type = 0;
    bool enabled = true;
    bool pressed = false;
};

// `InputBinding`. Every field is `Enum.KeyCode`'s value except the three at the
// end; `0` is `Unknown`, which is why the enum carries that item at all -- an
// unbound binding is expressible without a nullable property (ADR 0039).
struct InputBindingComponent
{
    i32 keyCode = 0;
    i32 up = 0;
    i32 down = 0;
    i32 left = 0;
    i32 right = 0;
    // What this binding's value is multiplied by before it reaches the action.
    f32 scale = 1.0f;
    // A localization key is the intended value (§6), which is why this is a
    // string rather than a `TextKey`: the catalog is resolved when a prompt
    // draws, not when a binding is built.
    std::string displayName;
    // An `asset://` URI, empty for none.
    std::string image;
};

// --- UI (M6) -----------------------------------------------------------------
//
// The same arrangement again: `scene` (L3) owns the pools, `ui` (L5) owns the
// meaning. This is the third module to store here and the first at L5, which is
// worth noticing -- the pattern is now the rule rather than the exception, and
// the type-erased registry architecture.md §4 said would be "a problem to solve
// when M4 brings the first one" has still not paid for itself: thirteen classes
// cost nine structs and nine pool members, and a registry would cost an
// indirection on every property read in the engine.

// `ScreenGui`. One screen-space tree, and the unit of layout: the dirty flag
// that decides whether the solver runs at all lives here.
struct ScreenGuiComponent
{
    f32 displayOrder = 0.0f;
    bool enabled = true;
    bool screenInsets = true;
    // Set by any layout-affecting write anywhere beneath this tree, cleared by
    // the layout that answers it. NOT a property: no script can read it, and a
    // script that could would be reading the engine's opinion of its own work.
    bool layoutDirty = true;
};

// `BillboardGui` (F3): a tree hung in the world, facing the camera. Laid out
// every frame it is drawn rather than on a dirty flag -- its canvas can change
// size with distance, and a name tag is a handful of elements.
struct BillboardGuiComponent
{
    bool enabled = true;
    core::InstanceId adornee;
    // Scale in metres, offset in pixels (the property's doc says why).
    core::UDim2 size{core::UDim{0.0f, 200.0f}, core::UDim{0.0f, 50.0f}};
    core::Vec3 worldOffset{};
    bool alwaysOnTop = false;
    f32 maxDistance = 0.0f;
    f32 brightness = 1.0f;
};

// `SurfaceGui` (F3): a tree drawn onto one face of a part.
struct SurfaceGuiComponent
{
    bool enabled = true;
    core::InstanceId adornee;
    // `Enum.Face`: Front, Back, Top, Bottom, Right, Left.
    i32 face = 0;
    f32 pixelsPerMetre = 50.0f;
    bool alwaysOnTop = false;
    f32 brightness = 1.0f;
};

// `UIObject`, and therefore attached to every element on screen.
//
// The two `absolute*` fields are OUTPUTS of the solver rather than state a
// script owns, which is why their properties are read-only. They live here
// rather than in a parallel array because the layout writes them and the draw
// list reads them, and a second structure indexed the same way would be a
// second thing to keep in step.
struct UIObjectComponent
{
    core::UDim2 position;
    core::UDim2 size;
    core::Vec2 anchorPoint;
    core::Color3 backgroundColor{1.0f, 1.0f, 1.0f};
    core::Vec2 absolutePosition;
    core::Vec2 absoluteSize;
    f32 rotation = 0.0f;
    f32 backgroundTransparency = 0.0f;
    f32 zIndex = 0.0f;
    f32 layoutOrder = 0.0f;
    // `Enum.AutomaticSize`: 0 None, 1 X, 2 Y, 3 XY.
    i32 automaticSize = 0;
    bool visible = true;
    bool clipsDescendants = false;
};

struct TextLabelComponent
{
    std::string text;
    // An `asset://` URI to a TrueType file. Empty is the engine's own default
    // face -- Inter, staged beside the binary -- and so is the literal name
    // `Inter`. The built-in vector face is what is left when even that file is
    // absent; it is not what an empty `Font` asks for.
    std::string font;
    core::Color3 textColor{0.0f, 0.0f, 0.0f};
    f32 textSize = 14.0f;
    // `Enum.HorizontalAlignment` / `Enum.VerticalAlignment`.
    i32 horizontalAlignment = 1;
    i32 verticalAlignment = 1;
    bool textWrapped = false;
    bool textScaled = false;
    // `TextLabel.RichText` (F3): read `text` as markup.
    bool richText = false;
};

struct TextInputComponent
{
    std::string placeholderText;

    // **Where the next character goes**, as a BYTE offset into `Text` (S6.7).
    //
    // This field was here once with nothing moving it, and it was taken out for
    // that reason -- a field naming a feature nobody had written is the lie
    // `Inert` exists to make impossible, and `inertcheck` found it. The comment
    // that replaced it said a real caret would arrive with the code that moves
    // it. This is that code: `ui::interaction` moves it on every arrow, home,
    // end, insertion and deletion, and `ui::draw` puts a bar there.
    //
    // **Bytes and not code points**, which is the same choice `ScriptDocument`
    // makes and for the same reason: the text is UTF-8, every offset that
    // reaches a renderer or a substring is a byte offset, and converting at each
    // boundary is where the off-by-ones live. Movement steps whole code points
    // so a caret never lands inside one.
    //
    // Clamped into the text on every use rather than trusted: `Text` is a
    // property a script may assign at any moment, and a caret left past the end
    // of a string somebody just shortened is a crash rather than a wrong
    // picture.
    u32 caret = 0;

    bool focused = false;
};

struct ImageLabelComponent
{
    std::string image;
    core::Rect sliceCenter;
    core::Color3 imageColor{1.0f, 1.0f, 1.0f};
    // `Enum.ScaleType`: 0 Stretch, 1 Slice, 2 Tile.
    i32 scaleType = 0;
};

struct ScrollFrameComponent
{
    core::UDim2 canvasSize;
    core::Vec2 canvasPosition;
    f32 scrollBarThickness = 12.0f;
};

struct UIListLayoutComponent
{
    core::UDim padding;
    // `Enum.FillDirection`: 0 Horizontal, 1 Vertical.
    i32 fillDirection = 1;
    i32 horizontalAlignment = 0;
    i32 verticalAlignment = 0;
    // `Enum.SortOrder`: 0 Name, 1 LayoutOrder.
    i32 sortOrder = 1;
    bool wraps = false;
};

struct UIPaddingComponent
{
    core::UDim paddingTop;
    core::UDim paddingBottom;
    core::UDim paddingLeft;
    core::UDim paddingRight;
};

struct UICornerComponent
{
    core::UDim cornerRadius{0.0f, 8.0f};
};

// --- Audio (M6) --------------------------------------------------------------

struct AudioGroupComponent
{
    f32 volume = 1.0f;
};

// `Sound`. The timeline is the SIMULATION's (M6 brief, Decision 9): every field
// here is advanced by the tick and read by the mixer, never the other way round.
// A field the mixer wrote would be the wall clock entering the world.
struct SoundComponent
{
    // An `asset://` URI, kept as the script wrote it. `audio` resolves it
    // against the content mounts and decodes the file into the mixer's own
    // format, cached under this string; a URI that resolves to nothing is
    // audibly a placeholder tone rather than silence, and its timeline is then
    // measured against the placeholder's length rather than a real one.
    //
    // **The first ask decodes the whole file on the calling thread**, and that
    // is still open (D129): the render frame reaches it, and so does the sim
    // tick, through the clip length `timePosition` is compared against. The
    // tick's half cannot simply become asynchronous -- it advances
    // `TimePosition` and clears `Playing` on the tick that fires `Ended`, and
    // the world hash covers both, so a length that depended on when a decode
    // finished would be a world hash that depended on disk speed (R10).
    std::string content;
    core::InstanceId group;
    // Seconds. f64 because it is compared against `simTime`-shaped quantities
    // and a 32-bit second drifts visibly over a long track.
    f64 timePosition = 0.0;
    f32 volume = 0.5f;
    f32 playbackSpeed = 1.0f;
    f32 rollOffMinDistance = 8.0f;
    f32 rollOffMaxDistance = 80.0f;
    bool playing = false;
    bool looped = false;
    // Whether `Loaded` has been raised. One shot: the event is a past-tense fact
    // and a sound is loaded once.
    bool loadedFired = false;
};

} // namespace luaug::scene
