// The value types scripts hold (api-design.md §2.3).
//
// Each is tagged userdata with its own metatable, except `Vector3`, which is
// the native `vector` primitive and never userdata (ADR 0013) -- the `Vector3`
// global is a table of functions over it.
//
// Casing follows ADR 0034 and the schema's validator enforces the same split on
// the IDL: a namespace function is camelCase (`CFrame.new`, `Color3.fromRGB`),
// and a member reached through a value is PascalCase (`cf:Inverse()`,
// `color.R`).
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/value.h"
#include "luaug/script/binding.h"

#include <optional>
#include <span>
#include <string_view>

struct lua_State;

namespace luaug::script {

// Installs the metatables for every datatype tag and the namespace tables that
// construct them. Runs during boot, after the tags exist and before the
// sandbox: a metatable installed later is one the already-loaded chunks reach
// only through the slow path (binding.h, rule 2).
void registerDatatypes(lua_State* L);

// The `Enum` global. Values come from the IDL, so adding an enum is a
// definition-file edit rather than a C++ one.
void registerEnums(lua_State* L);

// Push/check helpers, so that no call site repeats a tag check. Each `check`
// raises a Luau error naming the type it wanted rather than returning a null --
// a binding that returns null on a type mismatch is a binding whose callers all
// forget the check once.
void pushCFrame(lua_State* L, const core::CFrameD& value);
[[nodiscard]] const core::CFrameD& checkCFrame(lua_State* L, int index);

void pushColor3(lua_State* L, core::Color3 value);
[[nodiscard]] core::Color3 checkColor3(lua_State* L, int index);

void pushVector3(lua_State* L, core::Vec3 value);
[[nodiscard]] core::Vec3 checkVector3(lua_State* L, int index);

void pushVector2(lua_State* L, core::Vec2 value);
[[nodiscard]] core::Vec2 checkVector2(lua_State* L, int index);

// An enum item by id and value. Exported because an engine-raised fire carries
// one -- `DebugService.MessageOut` hands its handler an `Enum.LogLevel` -- and
// the registry lookup that turns it back into a name is this module's.
void pushEnumItem(lua_State* L, scene::EnumValue value);

// The reverse, raising if the slot is not an enum item at all. Which ENUM it
// belongs to is the caller's to check: every item shares one userdata tag
// (binding.h, rule 1), so the tag can only say "an enum item", and the caller is
// the one that knows which enum it wanted and can name it in the error.
[[nodiscard]] scene::EnumValue checkEnumItem(lua_State* L, int index);

// A `RaycastParams` as the query bindings need to read it, without exposing the
// userdata payload: the filter is a view into the object's own storage and is
// valid only while it is on the stack, which is the whole life of a query.
struct RaycastQuery
{
    std::span<const core::InstanceId> filter;
    // `Enum.RaycastFilterType`'s value: 0 Exclude, 1 Include.
    core::i32 mode = 0;
    // Empty means every group.
    std::string_view collisionGroup;
};

[[nodiscard]] RaycastQuery checkRaycastParams(lua_State* L, int index);

// Built here rather than in the caller so that `RaycastResult`'s payload stays
// private to this file, the way every other datatype's does.
void pushRaycastResult(lua_State* L, core::InstanceId instance, core::DVec3 position, core::Vec3 normal,
                       core::f32 distance);

// The bridge between a `scene::Value` and a Luau value. It lives here rather
// than beside the Instance bindings because this file owns every userdata type
// a `Value` can hold, and a conversion that lived elsewhere would need all of
// them exposed.
void pushValue(lua_State* L, const scene::Value& value);

// Reads the stack slot as `expected`. Returns nullopt on a type mismatch rather
// than raising, because the caller knows the property name and the class and can
// therefore say something specific -- `scene.err.expected_number` names both,
// and a raise from in here could name neither.
[[nodiscard]] std::optional<scene::Value> toValue(lua_State* L, int index, scene::ValueType expected);

} // namespace luaug::script
