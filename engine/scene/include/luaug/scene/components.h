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
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/types.h"
#include "luaug/scene/types.h"

namespace luaug::scene
{

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
