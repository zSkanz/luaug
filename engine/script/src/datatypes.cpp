#include "luaug/script/datatypes.h"

#include <lua.h>
#include <lualib.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <string_view>

#include "class_descriptors.gen.h"
#include "luaug/scene/world.h"
#include "luaug/script/instance_binding.h"

namespace luaug::script
{
namespace
{

using core::CFrameD;
using core::Color3;
using core::Vec3;

constexpr f64 IdentityUp[3] = {0.0, 1.0, 0.0};

// --- Userdata plumbing -------------------------------------------------------

// `withmetatable` rather than `newuserdatatagged` + `lua_setmetatable`: it skips
// an API round trip, a stack push/pop and a GC barrier, and it is only legal
// once the tag's metatable is registered -- which is why every constructor here
// is unreachable before `registerDatatypes` has run.
template <class T>
void pushTagged(lua_State* L, UserdataTag tag, const T& value)
{
    void* memory = lua_newuserdatataggedwithmetatable(L, sizeof(T), static_cast<int>(tag));
    // Placement-new rather than a memcpy: `Pcg32` has no public default
    // constructor, and a type that gains an invariant later should not silently
    // skip running it.
    new (memory) T(value);
}

template <class T>
[[nodiscard]] T* toTagged(lua_State* L, int index, UserdataTag tag) noexcept
{
    return static_cast<T*>(lua_touserdatatagged(L, index, static_cast<int>(tag)));
}

// Raises naming the type it wanted, which `luaL_checkudatatagged` reads out of
// the tag metatable's `__type` -- the same string `typeof` reports, so an
// argument error and a type test never disagree.
template <class T>
[[nodiscard]] T& checkTagged(lua_State* L, int index, UserdataTag tag)
{
    return *static_cast<T*>(luaL_checkudatatagged(L, index, static_cast<int>(tag)));
}

[[nodiscard]] Vec3 checkVec3(lua_State* L, int index)
{
    const float* v = luaL_checkvector(L, index);
    return Vec3{v[0], v[1], v[2]};
}

void pushVec3(lua_State* L, Vec3 v)
{
    lua_pushvector(L, v.x, v.y, v.z);
}

// `lua_pushnumber` takes a double and every scalar in this file is an f32, so
// the widening is written out rather than left implicit: `-Wdouble-promotion`
// is an error on `engine/`, and it is the diagnostic that catches a narrow
// value silently entering a wide computation. Clang enforces it; MSVC does not.
void pushNumber(lua_State* L, f32 value)
{
    lua_pushnumber(L, static_cast<f64>(value));
}

// --- CFrame ------------------------------------------------------------------

int cframeGetPosition(lua_State* L)
{
    pushVec3(L, core::toVec3(checkTagged<CFrameD>(L, 1, UserdataTag::CFrame).position));
    return 1;
}

int cframeGetRotation(lua_State* L)
{
    CFrameD result;
    result.rotation = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame).rotation;
    pushTagged(L, UserdataTag::CFrame, result);
    return 1;
}

int cframeGetRightVector(lua_State* L)
{
    const core::Mat3& m = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame).rotation;
    pushVec3(L, Vec3{m.m[0][0], m.m[0][1], m.m[0][2]});
    return 1;
}

int cframeGetUpVector(lua_State* L)
{
    const core::Mat3& m = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame).rotation;
    pushVec3(L, Vec3{m.m[1][0], m.m[1][1], m.m[1][2]});
    return 1;
}

int cframeGetLookVector(lua_State* L)
{
    // The **negated** +Z column: forward is -Z in a right-handed Y-up world, so
    // `RightVector:Cross(UpVector) == -LookVector` (api-design.md §2.3).
    const core::Mat3& m = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame).rotation;
    pushVec3(L, Vec3{-m.m[2][0], -m.m[2][1], -m.m[2][2]});
    return 1;
}

int cframeInverse(lua_State* L)
{
    pushTagged(L, UserdataTag::CFrame, core::inverse(checkTagged<CFrameD>(L, 1, UserdataTag::CFrame)));
    return 1;
}

int cframeLerp(lua_State* L)
{
    const CFrameD& self = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    const CFrameD& goal = checkTagged<CFrameD>(L, 2, UserdataTag::CFrame);
    pushTagged(L, UserdataTag::CFrame, core::lerp(self, goal, luaL_checknumber(L, 3)));
    return 1;
}

int cframeOrthonormalize(lua_State* L)
{
    CFrameD result = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    result.rotation = core::orthonormalize(result.rotation);
    pushTagged(L, UserdataTag::CFrame, result);
    return 1;
}

int cframeToWorldSpace(lua_State* L)
{
    const CFrameD& self = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    pushTagged(L, UserdataTag::CFrame, self * checkTagged<CFrameD>(L, 2, UserdataTag::CFrame));
    return 1;
}

int cframeToObjectSpace(lua_State* L)
{
    const CFrameD& self = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    pushTagged(L, UserdataTag::CFrame, core::inverse(self) * checkTagged<CFrameD>(L, 2, UserdataTag::CFrame));
    return 1;
}

int cframePointToWorldSpace(lua_State* L)
{
    const CFrameD& self = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    pushVec3(L, core::toVec3(core::transformPoint(self, core::toDVec3(checkVec3(L, 2)))));
    return 1;
}

int cframePointToObjectSpace(lua_State* L)
{
    const CFrameD& self = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    pushVec3(L, core::toVec3(core::transformPoint(core::inverse(self), core::toDVec3(checkVec3(L, 2)))));
    return 1;
}

int cframeVectorToWorldSpace(lua_State* L)
{
    const CFrameD& self = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    // Rotation only. A surface normal run through `PointToWorldSpace` instead
    // comes out wrong by exactly the frame's position, which is a bug that looks
    // right until the object moves.
    pushVec3(L, core::transformDirection(self, checkVec3(L, 2)));
    return 1;
}

int cframeVectorToObjectSpace(lua_State* L)
{
    const CFrameD& self = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    pushVec3(L, core::transformDirection(core::inverse(self), checkVec3(L, 2)));
    return 1;
}

// `Enum.RotationOrder` values are the `core::RotationOrder` enumerators in
// declaration order (`enums.api.luau`), so the conversion is a range check
// rather than a table. YXZ wherever the argument is omitted.
[[nodiscard]] core::RotationOrder checkRotationOrder(lua_State* L, int index, const char* functionName)
{
    if (lua_isnoneornil(L, index))
        return core::RotationOrder::YXZ;

    // The tag check raises on its own for anything that is not an enum item at
    // all; what is left is an item of the WRONG enum, which the tag cannot tell
    // apart because every enum item shares one.
    const scene::EnumValue& item = checkTagged<scene::EnumValue>(L, index, UserdataTag::EnumItem);
    if (item.enumId != scene::generated::RotationOrderEnumId || item.value < 0
        || item.value > static_cast<i32>(core::RotationOrder::ZYX))
    {
        const core::I18nArg args[] = {
            {"function", std::string_view{functionName}},
            {"enumName", std::string_view{"RotationOrder"}},
            {"parameter", std::string_view{"order"}},
        };
        raise(L, LUAUG_TR("script.err.expected_enum_item"), args);
    }
    return static_cast<core::RotationOrder>(item.value);
}

int cframeToEuler(lua_State* L)
{
    const CFrameD& self = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    const Vec3 angles = core::toEuler(self.rotation, checkRotationOrder(L, 2, "CFrame:ToEuler"));
    // Always (rx, ry, rz), whatever the order: the order says which axis turns
    // first, never which number means what (api-design.md §2.3).
    pushNumber(L, angles.x);
    pushNumber(L, angles.y);
    pushNumber(L, angles.z);
    return 3;
}

int cframeToAxisAngle(lua_State* L)
{
    const CFrameD& self = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    Vec3 axis;
    f32 angle = 0.0f;
    core::toAxisAngle(self.rotation, axis, angle);
    pushVec3(L, axis);
    pushNumber(L, angle);
    return 2;
}

int cframeToQuaternion(lua_State* L)
{
    const CFrameD& self = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 1.0f;
    core::toQuaternion(self.rotation, x, y, z, w);
    // w last, matching `fromQuaternion`'s tail -- the half of the convention
    // people get wrong.
    pushNumber(L, x);
    pushNumber(L, y);
    pushNumber(L, z);
    pushNumber(L, w);
    return 4;
}

int cframeMul(lua_State* L)
{
    const CFrameD& self = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    if (lua_isvector(L, 2))
    {
        // A **point**: the translation applies. `VectorToWorldSpace` is the
        // direction form, and the two differ by exactly the frame's position.
        pushVec3(L, core::toVec3(core::transformPoint(self, core::toDVec3(checkVec3(L, 2)))));
        return 1;
    }
    pushTagged(L, UserdataTag::CFrame, self * checkTagged<CFrameD>(L, 2, UserdataTag::CFrame));
    return 1;
}

int cframeEq(lua_State* L)
{
    const CFrameD* a = toTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    const CFrameD* b = toTagged<CFrameD>(L, 2, UserdataTag::CFrame);
    // Exact and component-wise: an identity test, never a tolerance
    // (api-design.md §2.3). `__eq` only fires for two userdata of the same type,
    // so a null here means one of them is some other tag.
    lua_pushboolean(L, a != nullptr && b != nullptr && *a == *b);
    return 1;
}

int cframeTostring(lua_State* L)
{
    const CFrameD& self = checkTagged<CFrameD>(L, 1, UserdataTag::CFrame);
    const Vec3 angles = core::toEulerYxz(self.rotation);
    char text[192];
    // Position first and then the YXZ euler angles in degrees, because that is
    // what `Orientation` shows and a frame printed in nine matrix entries is not
    // something anybody reads.
    std::snprintf(
        text,
        sizeof(text),
        "%.6g, %.6g, %.6g, %.6g, %.6g, %.6g",
        self.position.x,
        self.position.y,
        self.position.z,
        static_cast<f64>(angles.x) * 57.29577951308232,
        static_cast<f64>(angles.y) * 57.29577951308232,
        static_cast<f64>(angles.z) * 57.29577951308232);
    lua_pushstring(L, text);
    return 1;
}

int cframeNew(lua_State* L)
{
    CFrameD result;
    if (lua_isvector(L, 1))
    {
        // The vector overload enters through f32, which is why the numeric one
        // exists: `CFrame.new(x, y, z)` keeps every bit the caller wrote.
        result.position = core::toDVec3(checkVec3(L, 1));
    }
    else
    {
        result.position = core::DVec3{luaL_optnumber(L, 1, 0.0), luaL_optnumber(L, 2, 0.0), luaL_optnumber(L, 3, 0.0)};
    }
    pushTagged(L, UserdataTag::CFrame, result);
    return 1;
}

int cframeLookAt(lua_State* L)
{
    const Vec3 position = checkVec3(L, 1);
    const Vec3 target = checkVec3(L, 2);
    const float defaultUp[3] = {
        static_cast<float>(IdentityUp[0]),
        static_cast<float>(IdentityUp[1]),
        static_cast<float>(IdentityUp[2]),
    };
    const float* up = luaL_optvector(L, 3, defaultUp);
    pushTagged(
        L,
        UserdataTag::CFrame,
        core::lookAtCFrame(core::toDVec3(position), core::toDVec3(target), Vec3{up[0], up[1], up[2]}));
    return 1;
}

int cframeFromEuler(lua_State* L)
{
    const Vec3 angles{
        static_cast<f32>(luaL_checknumber(L, 1)),
        static_cast<f32>(luaL_checknumber(L, 2)),
        static_cast<f32>(luaL_checknumber(L, 3)),
    };
    CFrameD result;
    result.rotation = core::fromEuler(angles, checkRotationOrder(L, 4, "CFrame.fromEuler"));
    pushTagged(L, UserdataTag::CFrame, result);
    return 1;
}

int cframeFromAxisAngle(lua_State* L)
{
    CFrameD result;
    result.rotation = core::fromAxisAngle(checkVec3(L, 1), static_cast<f32>(luaL_checknumber(L, 2)));
    pushTagged(L, UserdataTag::CFrame, result);
    return 1;
}

int cframeFromQuaternion(lua_State* L)
{
    CFrameD result;
    result.position = core::toDVec3(checkVec3(L, 1));
    result.rotation = core::fromQuaternion(
        static_cast<f32>(luaL_checknumber(L, 2)),
        static_cast<f32>(luaL_checknumber(L, 3)),
        static_cast<f32>(luaL_checknumber(L, 4)),
        static_cast<f32>(luaL_checknumber(L, 5)));
    pushTagged(L, UserdataTag::CFrame, result);
    return 1;
}

int cframeFromMatrix(lua_State* L)
{
    CFrameD result;
    result.position = core::toDVec3(checkVec3(L, 1));
    const Vec3 right = checkVec3(L, 2);
    const Vec3 up = checkVec3(L, 3);
    // The third axis is **back**, not forward, so `LookVector == -back`. Stored
    // as given: a constructor that silently repaired its input would hide the
    // bug that produced it, and `Orthonormalize` is the explicit repair.
    const Vec3 back = checkVec3(L, 4);
    result.rotation.m[0][0] = right.x;
    result.rotation.m[0][1] = right.y;
    result.rotation.m[0][2] = right.z;
    result.rotation.m[1][0] = up.x;
    result.rotation.m[1][1] = up.y;
    result.rotation.m[1][2] = up.z;
    result.rotation.m[2][0] = back.x;
    result.rotation.m[2][1] = back.y;
    result.rotation.m[2][2] = back.z;
    pushTagged(L, UserdataTag::CFrame, result);
    return 1;
}

// --- Color3 ------------------------------------------------------------------

int color3GetR(lua_State* L)
{
    pushNumber(L, checkTagged<Color3>(L, 1, UserdataTag::Color3).r);
    return 1;
}

int color3GetG(lua_State* L)
{
    pushNumber(L, checkTagged<Color3>(L, 1, UserdataTag::Color3).g);
    return 1;
}

int color3GetB(lua_State* L)
{
    pushNumber(L, checkTagged<Color3>(L, 1, UserdataTag::Color3).b);
    return 1;
}

int color3Lerp(lua_State* L)
{
    const Color3& self = checkTagged<Color3>(L, 1, UserdataTag::Color3);
    const Color3& goal = checkTagged<Color3>(L, 2, UserdataTag::Color3);
    pushTagged(L, UserdataTag::Color3, core::lerp(self, goal, static_cast<f32>(luaL_checknumber(L, 3))));
    return 1;
}

int color3ToHsv(lua_State* L)
{
    const Color3& self = checkTagged<Color3>(L, 1, UserdataTag::Color3);
    f32 hue = 0.0f;
    f32 saturation = 0.0f;
    f32 value = 0.0f;
    core::toHsv(self, hue, saturation, value);
    pushNumber(L, hue);
    pushNumber(L, saturation);
    pushNumber(L, value);
    return 3;
}

[[nodiscard]] u32 toByte(f32 channel) noexcept
{
    // Clamped, because eight bits cannot represent anything else. This is the
    // one place a Color3 is narrowed, and it is why a round trip through
    // `fromHex` does not preserve an HDR value.
    const f32 scaled = channel * 255.0f;
    if (!(scaled > 0.0f))
        return 0;
    if (scaled >= 255.0f)
        return 255;
    return static_cast<u32>(scaled + 0.5f);
}

int color3ToHex(lua_State* L)
{
    const Color3& self = checkTagged<Color3>(L, 1, UserdataTag::Color3);
    char text[8];
    std::snprintf(text, sizeof(text), "%02x%02x%02x", toByte(self.r), toByte(self.g), toByte(self.b));
    lua_pushstring(L, text);
    return 1;
}

int color3Eq(lua_State* L)
{
    const Color3* a = toTagged<Color3>(L, 1, UserdataTag::Color3);
    const Color3* b = toTagged<Color3>(L, 2, UserdataTag::Color3);
    lua_pushboolean(L, a != nullptr && b != nullptr && *a == *b);
    return 1;
}

int color3Tostring(lua_State* L)
{
    const Color3& self = checkTagged<Color3>(L, 1, UserdataTag::Color3);
    char text[96];
    std::snprintf(
        text,
        sizeof(text),
        "%.6g, %.6g, %.6g",
        static_cast<f64>(self.r),
        static_cast<f64>(self.g),
        static_cast<f64>(self.b));
    lua_pushstring(L, text);
    return 1;
}

int color3New(lua_State* L)
{
    pushTagged(
        L,
        UserdataTag::Color3,
        Color3{
            static_cast<f32>(luaL_optnumber(L, 1, 0.0)),
            static_cast<f32>(luaL_optnumber(L, 2, 0.0)),
            static_cast<f32>(luaL_optnumber(L, 3, 0.0)),
        });
    return 1;
}

int color3FromRgb(lua_State* L)
{
    // Divided and stored, not rounded to a byte: 0-255 is the range colour
    // pickers speak in, not a narrower storage format.
    pushTagged(
        L,
        UserdataTag::Color3,
        Color3{
            static_cast<f32>(luaL_checknumber(L, 1) / 255.0),
            static_cast<f32>(luaL_checknumber(L, 2) / 255.0),
            static_cast<f32>(luaL_checknumber(L, 3) / 255.0),
        });
    return 1;
}

int color3FromHsv(lua_State* L)
{
    // All three 0-1, hue included, so `fromHSV(1/3, 1, 1)` is green and not a
    // hue of one third of a degree.
    pushTagged(
        L,
        UserdataTag::Color3,
        core::fromHsv(
            static_cast<f32>(luaL_checknumber(L, 1)),
            static_cast<f32>(luaL_checknumber(L, 2)),
            static_cast<f32>(luaL_checknumber(L, 3))));
    return 1;
}

[[nodiscard]] bool hexDigit(char c, u32& out) noexcept
{
    if (c >= '0' && c <= '9')
    {
        out = static_cast<u32>(c - '0');
        return true;
    }
    const char lower = static_cast<char>(c | ' ');
    if (lower >= 'a' && lower <= 'f')
    {
        out = static_cast<u32>(lower - 'a') + 10u;
        return true;
    }
    return false;
}

int color3FromHex(lua_State* L)
{
    size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);
    std::string_view hex{text, length};
    if (!hex.empty() && hex.front() == '#')
        hex.remove_prefix(1);

    u32 channels[6] = {};
    bool ok = hex.size() == 6 || hex.size() == 3;
    for (usize index = 0; ok && index < hex.size(); ++index)
        ok = hexDigit(hex[index], channels[index]);

    if (!ok)
    {
        const core::I18nArg args[] = {{"value", std::string_view{text, length}}};
        raise(L, LUAUG_TR("script.err.color_hex_invalid"), args);
    }

    // `#rgb` is the shorthand where each digit is doubled, so `#f80` and
    // `#ff8800` are the same colour and the round trip with `ToHex` is defined
    // in both directions.
    const u32 r = hex.size() == 3 ? channels[0] * 17u : channels[0] * 16u + channels[1];
    const u32 g = hex.size() == 3 ? channels[1] * 17u : channels[2] * 16u + channels[3];
    const u32 b = hex.size() == 3 ? channels[2] * 17u : channels[4] * 16u + channels[5];

    pushTagged(
        L,
        UserdataTag::Color3,
        Color3{static_cast<f32>(r) / 255.0f, static_cast<f32>(g) / 255.0f, static_cast<f32>(b) / 255.0f});
    return 1;
}

// --- Random ------------------------------------------------------------------

int randomNextNumber(lua_State* L)
{
    core::Pcg32& self = checkTagged<core::Pcg32>(L, 1, UserdataTag::Random);
    if (lua_isnoneornil(L, 2) && lua_isnoneornil(L, 3))
    {
        lua_pushnumber(L, self.nextDouble());
        return 1;
    }

    const f64 min = luaL_optnumber(L, 2, 0.0);
    const f64 max = luaL_optnumber(L, 3, 1.0);
    if (min > max)
    {
        const core::I18nArg args[] = {{"min", min}, {"max", max}};
        raise(L, LUAUG_TR("script.err.random_range"), args);
    }
    lua_pushnumber(L, self.nextDouble(min, max));
    return 1;
}

int randomNextInteger(lua_State* L)
{
    core::Pcg32& self = checkTagged<core::Pcg32>(L, 1, UserdataTag::Random);
    const f64 min = luaL_checknumber(L, 2);
    const f64 max = luaL_checknumber(L, 3);

    // Whole numbers, and in range: `NextInteger` is inclusive at both ends, so a
    // fractional bound has no honest interpretation and a bound outside i32
    // cannot be drawn.
    const bool whole = std::floor(min) == min && std::floor(max) == max;
    const bool inRange = min >= -2147483648.0 && max <= 2147483647.0;
    if (!whole || !inRange || min > max)
    {
        const core::I18nArg args[] = {{"min", min}, {"max", max}};
        raise(L, LUAUG_TR("script.err.random_range"), args);
    }

    lua_pushinteger(L, self.nextInt(static_cast<i32>(min), static_cast<i32>(max)));
    return 1;
}

int randomNextUnitVector(lua_State* L)
{
    pushVec3(L, checkTagged<core::Pcg32>(L, 1, UserdataTag::Random).nextUnitVector());
    return 1;
}

int randomClone(lua_State* L)
{
    // Positioned at the same point in the stream and continuing it
    // independently, which is how a subsystem takes a deterministic branch
    // without consuming draws the rest of the frame is counting on.
    pushTagged(L, UserdataTag::Random, checkTagged<core::Pcg32>(L, 1, UserdataTag::Random));
    return 1;
}

int randomNew(lua_State* L)
{
    if (lua_isnoneornil(L, 1))
    {
        // Unseeded. R10 forbids this in simulation code and `luaug check` flags
        // it (M3); the world's own stream is what supplies the entropy here, so
        // even the "unseeded" generator is reproducible from the recorded world
        // seed rather than from a clock. A wall-clock seed would make a replay
        // diverge on the second run and there would be nothing to compare.
        core::Pcg32& world = context(L).world->rng();
        const u64 seed = (static_cast<u64>(world.nextU32()) << 32) | world.nextU32();
        pushTagged(L, UserdataTag::Random, core::Pcg32{seed});
        return 1;
    }

    // Truncated toward zero, so `Random.new(2.9)` and `Random.new(2)` are the
    // same stream (api-design.md §2.3).
    const f64 seed = luaL_checknumber(L, 1);
    pushTagged(L, UserdataTag::Random, core::Pcg32{static_cast<u64>(static_cast<i64>(seed))});
    return 1;
}

// --- Enum, EnumItem and the Enum global --------------------------------------

[[nodiscard]] const scene::EnumRegistry& enums(lua_State* L) noexcept
{
    return context(L).world->enums();
}

[[nodiscard]] std::string_view atomText(lua_State* L, core::NameAtom atom)
{
    return context(L).world->atoms().text(atom);
}

void pushEnumObject(lua_State* L, scene::EnumId id)
{
    pushTagged(L, UserdataTag::Enum, id);
}

void pushEnumItem(lua_State* L, scene::EnumValue value)
{
    pushTagged(L, UserdataTag::EnumItem, value);
}

int enumItemGetName(lua_State* L)
{
    const scene::EnumValue& self = checkTagged<scene::EnumValue>(L, 1, UserdataTag::EnumItem);
    const scene::EnumItemDesc* item = enums(L).findValue(self.enumId, self.value);
    if (item == nullptr)
    {
        lua_pushstring(L, "");
        return 1;
    }
    const std::string_view text = atomText(L, item->name);
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

int enumItemGetValue(lua_State* L)
{
    lua_pushinteger(L, checkTagged<scene::EnumValue>(L, 1, UserdataTag::EnumItem).value);
    return 1;
}

int enumItemGetEnumType(lua_State* L)
{
    // The enum **object**, not its name as a string, so that
    // `Enum.PartShape.Ball.EnumType == Enum.PartShape` holds.
    pushEnumObject(L, checkTagged<scene::EnumValue>(L, 1, UserdataTag::EnumItem).enumId);
    return 1;
}

int enumItemEq(lua_State* L)
{
    const scene::EnumValue* a = toTagged<scene::EnumValue>(L, 1, UserdataTag::EnumItem);
    const scene::EnumValue* b = toTagged<scene::EnumValue>(L, 2, UserdataTag::EnumItem);
    lua_pushboolean(L, a != nullptr && b != nullptr && *a == *b);
    return 1;
}

// `Enum.<EnumName>.<ItemName>`, which is the spelling every comparison in a
// script is written in and therefore what a `tostring` should read back as.
[[nodiscard]] std::string qualifiedName(lua_State* L, scene::EnumId id, core::NameAtom item)
{
    const scene::EnumDescriptor* descriptor = enums(L).find(id);
    std::string text = "Enum.";
    if (descriptor != nullptr)
        text.append(atomText(L, descriptor->name));
    if (item.valid())
    {
        text.push_back('.');
        text.append(atomText(L, item));
    }
    return text;
}

int enumItemTostring(lua_State* L)
{
    const scene::EnumValue& self = checkTagged<scene::EnumValue>(L, 1, UserdataTag::EnumItem);
    const scene::EnumItemDesc* item = enums(L).findValue(self.enumId, self.value);
    const std::string text = qualifiedName(L, self.enumId, item == nullptr ? core::NameAtom{} : item->name);
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

int enumObjectIndex(lua_State* L)
{
    const scene::EnumId id = checkTagged<scene::EnumId>(L, 1, UserdataTag::Enum);

    int atom = -1;
    const char* key = lua_tostringatom(L, 2, &atom);
    if (key != nullptr)
    {
        if (const scene::EnumItemDesc* item = enums(L).findItem(id, context(L).resolve(atom)))
        {
            pushEnumItem(L, scene::EnumValue{id, item->value});
            return 1;
        }
    }

    raiseUnknownMember(L, UserdataTag::Enum, key);
}

int enumObjectGetEnumItems(lua_State* L)
{
    const scene::EnumId id = checkTagged<scene::EnumId>(L, 1, UserdataTag::Enum);
    const scene::EnumDescriptor* descriptor = enums(L).find(id);
    const usize count = descriptor == nullptr ? 0 : descriptor->items.size();

    // A **fresh** array on every call: fresh so a caller may sort it, and in
    // declaration order because R10 forbids a container's own order from
    // reaching observable order.
    lua_createtable(L, static_cast<int>(count), 0);
    for (usize index = 0; index < count; ++index)
    {
        pushEnumItem(L, scene::EnumValue{id, descriptor->items[index].value});
        lua_rawseti(L, -2, static_cast<int>(index) + 1);
    }
    return 1;
}

int enumObjectNamecall(lua_State* L)
{
    int atom = -1;
    const char* method = lua_namecallatom(L, &atom);
    if (method != nullptr)
    {
        const VmContext& ctx = context(L);
        if (const MemberEntry* entry = findMember(ctx.methods[static_cast<usize>(UserdataTag::Enum)], ctx.resolve(atom)))
            return entry->fn(L);
    }
    raiseUnknownMember(L, UserdataTag::Enum, method);
}

int enumObjectEq(lua_State* L)
{
    const scene::EnumId* a = toTagged<scene::EnumId>(L, 1, UserdataTag::Enum);
    const scene::EnumId* b = toTagged<scene::EnumId>(L, 2, UserdataTag::Enum);
    lua_pushboolean(L, a != nullptr && b != nullptr && *a == *b);
    return 1;
}

int enumObjectTostring(lua_State* L)
{
    const std::string text = qualifiedName(L, checkTagged<scene::EnumId>(L, 1, UserdataTag::Enum), {});
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

int enumsIndex(lua_State* L)
{
    int atom = -1;
    const char* key = lua_tostringatom(L, 2, &atom);
    if (key != nullptr)
    {
        const scene::EnumId id = enums(L).findId(context(L).resolve(atom));
        if (id != scene::InvalidEnum)
        {
            pushEnumObject(L, id);
            return 1;
        }
    }
    raiseUnknownMember(L, UserdataTag::Enums, key);
}

// --- Vector3 -----------------------------------------------------------------

int vectorGetX(lua_State* L)
{
    pushNumber(L, luaL_checkvector(L, 1)[0]);
    return 1;
}

int vectorGetY(lua_State* L)
{
    pushNumber(L, luaL_checkvector(L, 1)[1]);
    return 1;
}

int vectorGetZ(lua_State* L)
{
    pushNumber(L, luaL_checkvector(L, 1)[2]);
    return 1;
}

int vectorGetMagnitude(lua_State* L)
{
    pushNumber(L, core::length(checkVec3(L, 1)));
    return 1;
}

int vectorGetUnit(lua_State* L)
{
    pushVec3(L, core::normalize(checkVec3(L, 1)));
    return 1;
}

// The vector metatable is not a tagged one -- `vector` is a VM primitive, not
// userdata (ADR 0013) -- so its dispatch cannot go through `VmContext`'s tag
// tables. The member set is five names and four methods, and comparing the key
// text directly is what a primitive with no atom table of its own can do.
[[nodiscard]] bool keyIs(const char* key, size_t length, std::string_view expected) noexcept
{
    return length == expected.size() && std::memcmp(key, expected.data(), length) == 0;
}

// `vector` is a primitive and has no `UserdataTag`, so the shared raise cannot
// name it -- and naming it "userdata" would be the one thing a message about
// `v.X` must not say.
[[noreturn]] void raiseUnknownVectorMember(lua_State* L, const char* member)
{
    const core::I18nArg args[] = {
        {"typeName", std::string_view{"vector"}},
        {"member", std::string_view{member == nullptr ? "" : member}},
    };
    raise(L, LUAUG_TR("script.err.unknown_member"), args);
}

int vectorIndex(lua_State* L)
{
    size_t length = 0;
    int atom = -1;
    const char* key = lua_tolstringatom(L, 2, &length, &atom);
    if (key != nullptr)
    {
        // Lowercase only. `v.X` raises rather than answering, which is the whole
        // point of divergence #9: the components are the primitive's own fields
        // and there is exactly one spelling (api-design.md §2.3). Luau's own
        // `vector_index` accepts either case, which is why this metatable
        // replaces it rather than extending it.
        if (keyIs(key, length, "x"))
            return vectorGetX(L);
        if (keyIs(key, length, "y"))
            return vectorGetY(L);
        if (keyIs(key, length, "z"))
            return vectorGetZ(L);
        if (keyIs(key, length, "Magnitude"))
            return vectorGetMagnitude(L);
        if (keyIs(key, length, "Unit"))
            return vectorGetUnit(L);
    }

    raiseUnknownVectorMember(L, key);
}

int vectorDot(lua_State* L)
{
    pushNumber(L, core::dot(checkVec3(L, 1), checkVec3(L, 2)));
    return 1;
}

int vectorCross(lua_State* L)
{
    pushVec3(L, core::cross(checkVec3(L, 1), checkVec3(L, 2)));
    return 1;
}

int vectorLerp(lua_State* L)
{
    const Vec3 a = checkVec3(L, 1);
    const Vec3 b = checkVec3(L, 2);
    // Not clamped, so values outside [0, 1] extrapolate.
    const f32 alpha = static_cast<f32>(luaL_checknumber(L, 3));
    pushVec3(L, Vec3{a.x + (b.x - a.x) * alpha, a.y + (b.y - a.y) * alpha, a.z + (b.z - a.z) * alpha});
    return 1;
}

int vectorAngle(lua_State* L)
{
    const Vec3 a = checkVec3(L, 1);
    const Vec3 b = checkVec3(L, 2);
    const Vec3 crossed = core::cross(a, b);
    const f32 angle = std::atan2(core::length(crossed), core::dot(a, b));

    if (lua_isnoneornil(L, 3))
    {
        pushNumber(L, angle);
        return 1;
    }

    // Signed about the axis by the right-hand rule, which is the only way to
    // tell a rotation from its mirror image -- so the result spans (-pi, pi].
    pushNumber(L, core::dot(crossed, checkVec3(L, 3)) < 0.0f ? -angle : angle);
    return 1;
}

int vectorNamecall(lua_State* L)
{
    size_t length = 0;
    int atom = -1;
    const char* method = lua_namecallatom(L, &atom);
    if (method != nullptr)
    {
        length = std::strlen(method);
        if (keyIs(method, length, "Dot"))
            return vectorDot(L);
        if (keyIs(method, length, "Cross"))
            return vectorCross(L);
        if (keyIs(method, length, "Lerp"))
            return vectorLerp(L);
        if (keyIs(method, length, "Angle"))
            return vectorAngle(L);
    }

    raiseUnknownVectorMember(L, method);
}

int vectorNew(lua_State* L)
{
    // A missing component is 0, unlike `vector.create`, which requires x and y.
    // Bound as the compiler's `vectorCtor` (ADR 0013), so a literal call is
    // constant-folded and a dynamic one is a fastcall -- neither reaches here.
    lua_pushvector(
        L,
        static_cast<float>(luaL_optnumber(L, 1, 0.0)),
        static_cast<float>(luaL_optnumber(L, 2, 0.0)),
        static_cast<float>(luaL_optnumber(L, 3, 0.0)));
    return 1;
}

// --- Registration ------------------------------------------------------------

void addMember(MemberTable& table, core::AtomTable& atoms, const char* name, lua_CFunction fn)
{
    table.push_back(MemberEntry{atoms.intern(name), fn});
}

void registerCFrame(lua_State* L, VmContext& ctx, core::AtomTable& atoms)
{
    MemberTable& getters = ctx.getters[static_cast<usize>(UserdataTag::CFrame)];
    addMember(getters, atoms, "Position", cframeGetPosition);
    addMember(getters, atoms, "Rotation", cframeGetRotation);
    addMember(getters, atoms, "RightVector", cframeGetRightVector);
    addMember(getters, atoms, "UpVector", cframeGetUpVector);
    addMember(getters, atoms, "LookVector", cframeGetLookVector);

    MemberTable& methods = ctx.methods[static_cast<usize>(UserdataTag::CFrame)];
    addMember(methods, atoms, "Inverse", cframeInverse);
    addMember(methods, atoms, "Lerp", cframeLerp);
    addMember(methods, atoms, "Orthonormalize", cframeOrthonormalize);
    addMember(methods, atoms, "ToWorldSpace", cframeToWorldSpace);
    addMember(methods, atoms, "ToObjectSpace", cframeToObjectSpace);
    addMember(methods, atoms, "PointToWorldSpace", cframePointToWorldSpace);
    addMember(methods, atoms, "PointToObjectSpace", cframePointToObjectSpace);
    addMember(methods, atoms, "VectorToWorldSpace", cframeVectorToWorldSpace);
    addMember(methods, atoms, "VectorToObjectSpace", cframeVectorToObjectSpace);
    addMember(methods, atoms, "ToEuler", cframeToEuler);
    addMember(methods, atoms, "ToAxisAngle", cframeToAxisAngle);
    addMember(methods, atoms, "ToQuaternion", cframeToQuaternion);

    beginTagMetatable(L, UserdataTag::CFrame);
    lua_pushcfunction(L, cframeMul, "__mul");
    lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, cframeEq, "__eq");
    lua_setfield(L, -2, "__eq");
    lua_pushcfunction(L, cframeTostring, "__tostring");
    lua_setfield(L, -2, "__tostring");
    endTagMetatable(L);

    const luaL_Reg constructors[] = {
        {"new", cframeNew},
        {"lookAt", cframeLookAt},
        {"fromEuler", cframeFromEuler},
        {"fromAxisAngle", cframeFromAxisAngle},
        {"fromQuaternion", cframeFromQuaternion},
        {"fromMatrix", cframeFromMatrix},
        {nullptr, nullptr},
    };
    luaL_register(L, "CFrame", constructors);
    pushTagged(L, UserdataTag::CFrame, CFrameD{});
    lua_setfield(L, -2, "identity");
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);
}

void registerColor3(lua_State* L, VmContext& ctx, core::AtomTable& atoms)
{
    MemberTable& getters = ctx.getters[static_cast<usize>(UserdataTag::Color3)];
    addMember(getters, atoms, "R", color3GetR);
    addMember(getters, atoms, "G", color3GetG);
    addMember(getters, atoms, "B", color3GetB);

    MemberTable& methods = ctx.methods[static_cast<usize>(UserdataTag::Color3)];
    addMember(methods, atoms, "Lerp", color3Lerp);
    addMember(methods, atoms, "ToHSV", color3ToHsv);
    addMember(methods, atoms, "ToHex", color3ToHex);

    installTagMetatable(L, UserdataTag::Color3, color3Eq, color3Tostring);

    const luaL_Reg constructors[] = {
        {"new", color3New},
        {"fromRGB", color3FromRgb},
        {"fromHSV", color3FromHsv},
        {"fromHex", color3FromHex},
        {nullptr, nullptr},
    };
    luaL_register(L, "Color3", constructors);
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);
}

void registerRandom(lua_State* L, VmContext& ctx, core::AtomTable& atoms)
{
    MemberTable& methods = ctx.methods[static_cast<usize>(UserdataTag::Random)];
    addMember(methods, atoms, "NextNumber", randomNextNumber);
    addMember(methods, atoms, "NextInteger", randomNextInteger);
    addMember(methods, atoms, "NextUnitVector", randomNextUnitVector);
    addMember(methods, atoms, "Clone", randomClone);

    // No getters and no `__eq`: a generator has no readable fields and no value
    // equality -- two generators at the same point in the same stream are still
    // two generators, and a script comparing them wants identity.
    installTagMetatable(L, UserdataTag::Random, nullptr, nullptr);

    const luaL_Reg constructors[] = {{"new", randomNew}, {nullptr, nullptr}};
    luaL_register(L, "Random", constructors);
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);
}

void registerEnumTypes(lua_State* L, VmContext& ctx, core::AtomTable& atoms)
{
    MemberTable& itemGetters = ctx.getters[static_cast<usize>(UserdataTag::EnumItem)];
    addMember(itemGetters, atoms, "Name", enumItemGetName);
    addMember(itemGetters, atoms, "Value", enumItemGetValue);
    addMember(itemGetters, atoms, "EnumType", enumItemGetEnumType);

    addMember(ctx.methods[static_cast<usize>(UserdataTag::Enum)], atoms, "GetEnumItems", enumObjectGetEnumItems);

    installTagMetatable(L, UserdataTag::EnumItem, enumItemEq, enumItemTostring);

    // An enum object overrides the shared `__index`: its members are its items,
    // which come from the registry rather than from a member table. `__namecall`
    // stays shared in spirit but is written out here for the same reason -- the
    // shared one would look the method up in the tag's table, which is where
    // `GetEnumItems` in fact lives, so this override exists only to keep the
    // unknown-member message naming `Enum`.
    beginTagMetatable(L, UserdataTag::Enum);
    lua_pushcfunction(L, enumObjectIndex, "__index");
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, enumObjectNamecall, "__namecall");
    lua_setfield(L, -2, "__namecall");
    lua_pushcfunction(L, enumObjectEq, "__eq");
    lua_setfield(L, -2, "__eq");
    lua_pushcfunction(L, enumObjectTostring, "__tostring");
    lua_setfield(L, -2, "__tostring");
    endTagMetatable(L);

    beginTagMetatable(L, UserdataTag::Enums);
    lua_pushcfunction(L, enumsIndex, "__index");
    lua_setfield(L, -2, "__index");
    endTagMetatable(L);
}

void registerVector(lua_State* L)
{
    // Replaces the stock vector metatable rather than extending it: Luau's own
    // `vector_index` lowercases the key, so `v.X` would answer where
    // api-design.md §2.3 requires it to raise. `lua_setmetatable` on a
    // non-table, non-userdata value writes `global->mt[LUA_TVECTOR]`
    // (`lapi.cpp:1058`), which is the whole type's metatable.
    lua_createtable(L, 0, 4);
    lua_pushstring(L, "vector");
    lua_setfield(L, -2, "__type");
    lua_pushcfunction(L, vectorIndex, "__index");
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, vectorNamecall, "__namecall");
    lua_setfield(L, -2, "__namecall");
    lua_setreadonly(L, -1, true);

    lua_pushvector(L, 0.0f, 0.0f, 0.0f);
    lua_pushvalue(L, -2);
    lua_setmetatable(L, -2);
    lua_pop(L, 2); // the dummy vector and the metatable

    // `Vector3` is the `vector` library plus `new`: a superset table, so every
    // function is reachable under both names and `Vector3.zero == vector.zero`.
    // No table identity is promised, and this is why -- they are two tables.
    lua_createtable(L, 0, 17);
    lua_getglobal(L, "vector");
    if (lua_istable(L, -1))
    {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0)
        {
            // key at -2, value at -1; `lua_rawset` pops both, and the key copy
            // is what keeps `lua_next` able to continue from it.
            lua_pushvalue(L, -2);
            lua_insert(L, -2);
            lua_rawset(L, -5);
        }
    }
    lua_pop(L, 1); // the vector library

    lua_pushcfunction(L, vectorNew, "new");
    lua_setfield(L, -2, "new");
    lua_setreadonly(L, -1, true);
    lua_setglobal(L, "Vector3");
}

} // namespace

void registerDatatypes(lua_State* L)
{
    VmContext& ctx = context(L);
    core::AtomTable& atoms = ctx.world->atoms();

    registerCFrame(L, ctx, atoms);
    registerColor3(L, ctx, atoms);
    registerRandom(L, ctx, atoms);
    registerEnumTypes(L, ctx, atoms);
    registerVector(L);
}

void registerEnums(lua_State* L)
{
    // One userdata for the whole global, holding nothing: every lookup goes
    // through the registry, so there is no per-enum state to carry and the
    // payload exists only because a zero-sized userdata is not a thing.
    pushTagged(L, UserdataTag::Enums, u8{0});
    lua_setglobal(L, "Enum");
}

void pushCFrame(lua_State* L, const core::CFrameD& value)
{
    pushTagged(L, UserdataTag::CFrame, value);
}

const core::CFrameD& checkCFrame(lua_State* L, int index)
{
    return checkTagged<CFrameD>(L, index, UserdataTag::CFrame);
}

void pushColor3(lua_State* L, core::Color3 value)
{
    pushTagged(L, UserdataTag::Color3, value);
}

core::Color3 checkColor3(lua_State* L, int index)
{
    return checkTagged<Color3>(L, index, UserdataTag::Color3);
}

void pushVector3(lua_State* L, core::Vec3 value)
{
    pushVec3(L, value);
}

core::Vec3 checkVector3(lua_State* L, int index)
{
    return checkVec3(L, index);
}

void pushValue(lua_State* L, const scene::Value& value)
{
    switch (scene::valueType(value))
    {
    case scene::ValueType::Nil:
        lua_pushnil(L);
        return;
    case scene::ValueType::Bool:
        lua_pushboolean(L, std::get<bool>(value));
        return;
    case scene::ValueType::Number:
        lua_pushnumber(L, std::get<f64>(value));
        return;
    case scene::ValueType::String:
    {
        const std::string& text = std::get<std::string>(value);
        lua_pushlstring(L, text.data(), text.size());
        return;
    }
    case scene::ValueType::Vector3:
        pushVec3(L, std::get<core::Vec3>(value));
        return;
    case scene::ValueType::CFrame:
        pushTagged(L, UserdataTag::CFrame, std::get<core::CFrameD>(value));
        return;
    case scene::ValueType::Color3:
        pushTagged(L, UserdataTag::Color3, std::get<core::Color3>(value));
        return;
    case scene::ValueType::Instance:
        pushInstance(L, std::get<core::InstanceId>(value));
        return;
    case scene::ValueType::EnumItem:
        pushEnumItem(L, std::get<scene::EnumValue>(value));
        return;
    }
    lua_pushnil(L);
}

std::optional<scene::Value> toValue(lua_State* L, int index, scene::ValueType expected)
{
    switch (expected)
    {
    case scene::ValueType::Nil:
        return lua_isnoneornil(L, index) ? std::optional<scene::Value>{scene::Value{}} : std::nullopt;
    case scene::ValueType::Bool:
        // Strict rather than truthy: `part.Anchored = "yes"` is a mistake, and
        // Lua's truthiness would accept it silently.
        if (!lua_isboolean(L, index))
            return std::nullopt;
        return scene::Value{lua_toboolean(L, index) != 0};
    case scene::ValueType::Number:
        if (lua_type(L, index) != LUA_TNUMBER)
            return std::nullopt;
        return scene::Value{lua_tonumber(L, index)};
    case scene::ValueType::String:
    {
        // Also strict: `lua_tostring` would coerce a number, and a property that
        // accepted `part.Name = 3` would be storing the coercion's answer rather
        // than the caller's intent.
        if (lua_type(L, index) != LUA_TSTRING)
            return std::nullopt;
        size_t length = 0;
        const char* text = lua_tolstring(L, index, &length);
        return scene::Value{std::string(text, length)};
    }
    case scene::ValueType::Vector3:
    {
        const float* v = lua_tovector(L, index);
        if (v == nullptr)
            return std::nullopt;
        return scene::Value{core::Vec3{v[0], v[1], v[2]}};
    }
    case scene::ValueType::CFrame:
    {
        const CFrameD* cframe = toTagged<CFrameD>(L, index, UserdataTag::CFrame);
        if (cframe == nullptr)
            return std::nullopt;
        return scene::Value{*cframe};
    }
    case scene::ValueType::Color3:
    {
        const Color3* color = toTagged<Color3>(L, index, UserdataTag::Color3);
        if (color == nullptr)
            return std::nullopt;
        return scene::Value{*color};
    }
    case scene::ValueType::Instance:
    {
        // nil is a legal Instance value -- it is what an unparented `Parent`
        // reads as and what clearing a reference writes.
        if (lua_isnoneornil(L, index))
            return scene::Value{};
        const core::InstanceId* id = toInstance(L, index);
        if (id == nullptr)
            return std::nullopt;
        return scene::Value{*id};
    }
    case scene::ValueType::EnumItem:
    {
        const scene::EnumValue* item = toTagged<scene::EnumValue>(L, index, UserdataTag::EnumItem);
        if (item == nullptr)
            return std::nullopt;
        return scene::Value{*item};
    }
    }
    return std::nullopt;
}

} // namespace luaug::script
