// Class-specific state, stored as POD components (architecture.md §4).
//
// Every field here backs a property the IDL declares, and the generated
// accessors are the only things that read or write them. They are plain structs
// with no invariants of their own on purpose: `World::snapshot` is a per-pool
// copy, and a component with a constructor, a pointer or a heap allocation
// would make that a traversal instead (ADR 0016).
//
// Only the M2 surface is here. A class arrives with the milestone that gives it
// behaviour, and so does its component.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
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

struct ModelComponent
{
    core::InstanceId primaryPart;
};

struct ScriptComponent
{
    bool enabled = true;
};

} // namespace luaug::scene
