#include "luaug/script/sandbox.h"

#include <lua.h>
#include <lualib.h>

namespace luaug::script
{

// api-design.md §1.1's removal list, verbatim and in its order. The list lives
// here and nowhere else so that the document and the code cannot drift.
//
// Most of these are Roblox globals that stock Luau never defines, so removing
// them is a no-op today. They stay on the list anyway: a removal that does
// nothing now is a guard against a future Luau -- or a future library linked
// into the same VM -- defining one, and the cost is a nil assignment at boot.
//
// Three are real. `getfenv`, `setfenv` and `newproxy` are in Luau's base
// library (lbaselib.cpp), and the first two are not merely a sandbox hole:
// mentioning either disables the `safeenv` optimisation for the whole module,
// which is simultaneously R4's guarantee and the import fastpath, and native
// codegen gives up on any function that touches them. Leaving them reachable
// would quietly invalidate every performance number measured after this point.
const char* const RemovedGlobals[] = {
    "wait",
    "spawn",
    "delay",
    "tick",
    "time",
    "elapsedTime",
    "loadstring",
    "getfenv",
    "setfenv",
    "newproxy",
    "shared",
    "io",
    nullptr,
};

void removeUnsafeGlobals(lua_State* L)
{
    for (const char* const* name = RemovedGlobals; *name != nullptr; ++name)
    {
        lua_pushnil(L);
        lua_setglobal(L, *name);
    }

    // `os` is trimmed rather than removed: api-design.md §1.1 keeps `clock`,
    // `time` and `date` and nothing else. Luau's own `os` is exactly those
    // four names (loslib.cpp), so `difftime` is the whole difference -- and it
    // goes because the surface is defined by what the document lists, not by
    // what happens to be cheap to leave.
    lua_getglobal(L, "os");
    if (lua_istable(L, -1))
    {
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

void sealGlobals(lua_State* L)
{
    // `luaL_sandbox` freezes the global table and the tables one level below
    // it, which is what makes `_G` immutable and what makes a write to it
    // raise. It removes nothing, whatever its own comment suggests -- that is
    // why `removeUnsafeGlobals` has to have run first.
    luaL_sandbox(L);
}

} // namespace luaug::script
