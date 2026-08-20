#include "luaug/scene/value.h"

namespace luaug::scene
{

const char* valueTypeName(ValueType type) noexcept
{
    // No `default` label, deliberately: with one, MSVC's /w44062 and Clang's
    // -Wswitch go quiet and an enumerator added to `ValueType` reaches a
    // release build spelled as whatever the default said. The trailing return
    // is what satisfies the "not all paths return" analysis instead, and it is
    // reachable only through a cast of an out-of-range integer.
    //
    // These are the names api-design.md §2.3 puts in front of developers --
    // `typeof` on a Vector3 answers "vector", not "Vector3" -- so an error that
    // formats one of them names the thing the reader already knows.
    switch (type)
    {
    case ValueType::Nil: return "nil";
    case ValueType::Bool: return "boolean";
    case ValueType::Number: return "number";
    case ValueType::String: return "string";
    case ValueType::Vector3: return "vector";
    case ValueType::CFrame: return "CFrame";
    case ValueType::Color3: return "Color3";
    case ValueType::Instance: return "Instance";
    case ValueType::EnumItem: return "EnumItem";
    }
    return "nil";
}

} // namespace luaug::scene
