#include "luaug/script/sandbox.h"

#include "luaug/core/dmath.h"

#include <lua.h>
#include <lualib.h>

#include <cmath>

namespace luaug::script {

// api-design.md §1.1's removal list, verbatim and in its order. The list lives
// here and nowhere else so that the document and the code cannot drift.
//
// Most of these are legacy scheduling globals that stock Luau never defines, so
// removing them is a no-op today. They stay on the list anyway: a removal that
// does nothing now is a guard against a future Luau -- or a future library
// linked into the same VM -- defining one, and the cost is a nil assignment at
// boot.
//
// Three are real. `getfenv`, `setfenv` and `newproxy` are in Luau's base
// library (lbaselib.cpp), and the first two are not merely a sandbox hole:
// mentioning either disables the `safeenv` optimisation for the whole module,
// which is simultaneously R4's guarantee and the import fastpath, and native
// codegen gives up on any function that touches them. Leaving them reachable
// would quietly invalidate every performance number measured after this point.
const char* const RemovedGlobals[] = {
    "wait",    "spawn",   "delay",    "tick",   "time", "elapsedTime", "loadstring",
    "getfenv", "setfenv", "newproxy", "shared", "io",   nullptr,
};

void removeUnsafeGlobals(lua_State* L)
{
    for (const char* const* name = RemovedGlobals; *name != nullptr; ++name) {
        lua_pushnil(L);
        lua_setglobal(L, *name);
    }

    // `os` is trimmed rather than removed: api-design.md §1.1 keeps `clock`,
    // `time` and `date` and nothing else. Luau's own `os` is exactly those
    // four names (loslib.cpp), so `difftime` is the whole difference -- and it
    // goes because the surface is defined by what the document lists, not by
    // what happens to be cheap to leave.
    lua_getglobal(L, "os");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, "difftime");
    }
    lua_pop(L, 1);

    // Luau's base library points `_G` at the real globals table, which would
    // make it a back channel between scripts and a way around per-script
    // sandboxing. It stays *defined* -- referencing it should not be a surprise
    // -- as an empty table that the seal below freezes.
    lua_newtable(L);
    lua_setglobal(L, "_G");
}

namespace {

namespace dm = core::dmath;

// One argument in, one number out: the shape of every function below but
// `log` and `atan2`.
template <double (*Function)(double)>
int unary(lua_State* L)
{
    lua_pushnumber(L, Function(luaL_checknumber(L, 1)));
    return 1;
}

int mathAtan2(lua_State* L)
{
    lua_pushnumber(L, dm::atan2(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

int mathPow(lua_State* L)
{
    lua_pushnumber(L, dm::pow(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

// Luau's own semantics for the optional base: two and ten are answered by
// their own functions, so `math.log(8, 2)` is 3 and not a quotient's rounding.
int mathLog(lua_State* L)
{
    const double x = luaL_checknumber(L, 1);
    if (lua_isnoneornil(L, 2)) {
        lua_pushnumber(L, dm::log(x));
        return 1;
    }
    const double base = luaL_checknumber(L, 2);
    if (base == 2.0)
        lua_pushnumber(L, dm::log2(x));
    else if (base == 10.0)
        lua_pushnumber(L, dm::log10(x));
    else
        lua_pushnumber(L, dm::log(x) / dm::log(base));
    return 1;
}

// `vector.angle`, as Luau writes it -- the cross product's length against the
// dot product, signed by an optional axis -- with the arctangent this engine's.
int vectorAngle(lua_State* L)
{
    const float* a = luaL_checkvector(L, 1);
    const float* b = luaL_checkvector(L, 2);
    const float* axis = luaL_optvector(L, 3, nullptr);
    const float cross[] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    const double sine = std::sqrt(static_cast<double>(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]));
    const auto cosine = static_cast<double>(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
    double angle = dm::atan2(sine, cosine);
    if (axis != nullptr && cross[0] * axis[0] + cross[1] * axis[1] + cross[2] * axis[2] < 0.0f)
        angle = -angle;
    lua_pushnumber(L, angle);
    return 1;
}

} // namespace

void installDeterministicMath(lua_State* L)
{
    static const luaL_Reg Math[] = {
        {"sin", unary<dm::sin>},     {"cos", unary<dm::cos>},   {"tan", unary<dm::tan>}, {"asin", unary<dm::asin>},
        {"acos", unary<dm::acos>},   {"atan", unary<dm::atan>}, {"atan2", mathAtan2},    {"sinh", unary<dm::sinh>},
        {"cosh", unary<dm::cosh>},   {"tanh", unary<dm::tanh>}, {"exp", unary<dm::exp>}, {"log", mathLog},
        {"log10", unary<dm::log10>}, {"pow", mathPow},          {nullptr, nullptr},
    };
    lua_getglobal(L, "math");
    if (lua_istable(L, -1)) {
        for (const luaL_Reg* entry = Math; entry->name != nullptr; ++entry) {
            lua_pushcfunction(L, entry->func, entry->name);
            lua_setfield(L, -2, entry->name);
        }
    }
    lua_pop(L, 1);

    lua_getglobal(L, "vector");
    if (lua_istable(L, -1)) {
        lua_pushcfunction(L, vectorAngle, "angle");
        lua_setfield(L, -2, "angle");
    }
    lua_pop(L, 1);
}

void sealGlobals(lua_State* L)
{
    // `luaL_sandbox` freezes the global table and the tables one level below
    // it, which is what makes `_G` immutable and what makes a write to it
    // raise. It removes nothing, whatever its own comment suggests -- that is
    // why `removeUnsafeGlobals` has to have run first.
    luaL_sandbox(L);
}

} // namespace luaug::script
