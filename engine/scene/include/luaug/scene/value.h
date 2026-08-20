// The typed value that crosses the reflection boundary (architecture.md §4).
//
// Property reads and writes, attribute storage, and the arguments of an
// engine-raised change all move through this type. It is deliberately a closed
// set: the API schema decides what a property may hold (api-design.md §2.2's
// attribute domain is the same list), so a value that is not in this union is a
// value the public surface has no way to produce.
//
// It carries no Luau types and never will. `scene` is L3 and the VM is L5
// (architecture.md §2 rule 2); the conversion between a `Value` and a Luau
// value happens in `script`, which is the only module allowed to know both.
#pragma once

#include <string>
#include <variant>

#include "luaug/core/id.h"
#include "luaug/core/math.h"
#include "luaug/core/types.h"
#include "luaug/scene/types.h"

namespace luaug::scene
{

// An enum member, stored as the enum's registry id plus the item's value rather
// than as a pointer: it has to survive a snapshot and a restore, and a pointer
// does not (ADR 0016).
struct EnumValue
{
    u16 enumId = 0;
    i32 value = 0;

    [[nodiscard]] constexpr bool operator==(const EnumValue&) const noexcept = default;
};

// Order matters: the index into the variant IS the wire tag, so it is written
// into snapshots and compared by the world hash. Appending is safe; reordering
// is a format break.
using Value = std::variant<
    std::monostate, // absent -- an unset attribute, or a nil Instance reference
    bool,
    f64,
    std::string,
    core::Vec3,
    core::CFrameD,
    core::Color3,
    core::InstanceId,
    EnumValue>;

enum class ValueType : u8
{
    Nil = 0,
    Bool = 1,
    Number = 2,
    String = 3,
    Vector3 = 4,
    CFrame = 5,
    Color3 = 6,
    Instance = 7,
    EnumItem = 8,
};

[[nodiscard]] constexpr ValueType valueType(const Value& value) noexcept
{
    return static_cast<ValueType>(value.index());
}

// For error messages -- these are engine-internal type names that get formatted
// into an i18n parameter, never a user-facing sentence of their own (R3).
[[nodiscard]] const char* valueTypeName(ValueType type) noexcept;

} // namespace luaug::scene
