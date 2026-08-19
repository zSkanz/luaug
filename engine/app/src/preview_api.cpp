#include "luaug/app/preview_api.h"

#include <array>
#include <cmath>

#include <lua.h>
#include <lualib.h>

#include "luaug/app/script_host.h"
#include "luaug/core/text_key.h"

namespace luaug::app
{
namespace
{

using core::f32;

// The registered frame callback, held in the Luau registry rather than as a
// global. `ScriptHost::run` executes on its own sandboxed thread whose globals
// table is discarded afterwards, so a function stored as a global would simply
// be gone by the next frame. The registry outlives every thread.
//
// One VM, one hook: this is a stopgap for a single preview script, not a
// dispatch system. M2's signals are the real answer.
int g_frameHookRef = LUA_NOREF;

[[nodiscard]] PreviewState& stateFromUpvalue(lua_State* L)
{
    return *static_cast<PreviewState*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
}

[[nodiscard]] f32 argFloat(lua_State* L, int index)
{
    return static_cast<f32>(luaL_checknumber(L, index));
}

int previewOnFrame(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);

    if (g_frameHookRef != LUA_NOREF)
        lua_unref(L, g_frameHookRef);

    lua_pushvalue(L, 1);
    g_frameHookRef = lua_ref(L, -1);
    lua_pop(L, 1);

    stateFromUpvalue(L).hasFrameHook = true;
    return 0;
}

int previewSetClearColor(lua_State* L)
{
    PreviewState& state = stateFromUpvalue(L);
    state.clearColor = {
        .r = argFloat(L, 1),
        .g = argFloat(L, 2),
        .b = argFloat(L, 3),
        .a = 1.0f,
    };
    return 0;
}

int previewWireBox(lua_State* L)
{
    PreviewState& state = stateFromUpvalue(L);
    if (state.draw == nullptr)
        return 0;

    const core::Vec3 center{argFloat(L, 1), argFloat(L, 2), argFloat(L, 3)};
    const core::Vec3 halfExtents{argFloat(L, 4), argFloat(L, 5), argFloat(L, 6)};
    const render::DebugColor color
        = render::DebugColor::fromLinear(argFloat(L, 7), argFloat(L, 8), argFloat(L, 9));

    state.draw->wireBox(center, halfExtents, color);
    return 0;
}

int previewAxes(lua_State* L)
{
    PreviewState& state = stateFromUpvalue(L);
    if (state.draw == nullptr)
        return 0;

    state.draw->axes(core::Mat4{}, argFloat(L, 1));
    return 0;
}

void registerFunction(lua_State* L, PreviewState& state, const char* name, lua_CFunction fn)
{
    // The state travels as a light-userdata upvalue rather than a file-scope
    // pointer, so a second VM would get its own -- which M2's actor VMs will
    // need, and which costs nothing to get right now.
    lua_pushlightuserdata(L, &state);
    lua_pushcclosure(L, fn, name, 1);
    lua_setfield(L, -2, name);
}

} // namespace

void installPreviewApi(lua_State* L, PreviewState& state)
{
    if (L == nullptr)
        return;

    lua_newtable(L);
    registerFunction(L, state, "OnFrame", previewOnFrame);
    registerFunction(L, state, "SetClearColor", previewSetClearColor);
    registerFunction(L, state, "WireBox", previewWireBox);
    registerFunction(L, state, "Axes", previewAxes);

    // Frozen, and this is the same reasoning the sandbox uses: a mutable global
    // table defeats `safeenv`, which is simultaneously the sandbox guarantee
    // (R4) and the import fastpath. A script that wants its own helpers makes
    // its own table.
    lua_setreadonly(L, -1, true);
    lua_setglobal(L, "luaug");
}

std::optional<core::EngineError> callFrameHook(
    ScriptHost& host, core::u64 tick, core::f64 elapsedSeconds)
{
    lua_State* L = host.state();
    if (L == nullptr || g_frameHookRef == LUA_NOREF)
        return std::nullopt;

    lua_getref(L, g_frameHookRef);
    lua_pushnumber(L, static_cast<double>(tick));
    lua_pushnumber(L, elapsedSeconds);

    if (lua_pcall(L, 2, 0, 0) != LUA_OK)
    {
        const char* message = lua_tostring(L, -1);
        const std::array<core::I18nArg, 2> args{
            core::I18nArg{"source", "OnFrame"},
            core::I18nArg{"message", message != nullptr ? message : "unknown error"}};
        auto error = core::makeError(LUAUG_TR("script.err.runtime"), args);
        lua_pop(L, 1);
        return error;
    }

    return std::nullopt;
}

} // namespace luaug::app
