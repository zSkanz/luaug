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
    core::Color3 ambient{0.15f, 0.16f, 0.2f};
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
    // An `asset://` URI to a TrueType file; empty means the built-in face.
    std::string font;
    core::Color3 textColor{0.0f, 0.0f, 0.0f};
    f32 textSize = 14.0f;
    // `Enum.HorizontalAlignment` / `Enum.VerticalAlignment`.
    i32 horizontalAlignment = 1;
    i32 verticalAlignment = 1;
    bool textWrapped = false;
    bool textScaled = false;
};

struct TextInputComponent
{
    std::string placeholderText;
    // **No caret.** There was a `caret` field here and nothing ever moved it:
    // v1's editor appends and backspaces at the end, which is what the class's
    // own doc promises. A field that named a feature nobody had written is the
    // same lie `Inert` exists to make impossible, and `inertcheck` is what found
    // it. When a real caret arrives it arrives with the code that moves it.
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
    // An `asset://` URI. Stored and not yet decoded (M7).
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
