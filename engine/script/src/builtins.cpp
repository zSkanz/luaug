// Installs the builtins written in Luau (ADR 0094). See builtins.h.
#include "luaug/script/builtins.h"

#include "luaug/core/error.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"

#include <lua.h>
#include <lualib.h>

#include <string>
#include <string_view>

namespace luaug::script {

void installBuiltins(lua_State* L)
{
    for (const BuiltinChunk& chunk : builtinChunks()) {
        const std::string name = std::string("=") + chunk.global;
        const int loaded = luau_load(L, name.c_str(), reinterpret_cast<const char*>(chunk.bytecode), chunk.size, 0);
        if (loaded != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* message = lua_tostring(L, -1);
            const core::I18nArg args[] = {
                {"name", std::string_view{chunk.global}},
                {"message", std::string_view{message != nullptr ? message : ""}},
            };
            core::logText(core::LogLevel::Error, core::formatKeyPrefixed(LUAUG_TR("script.err.builtin_failed"), args));
            lua_pop(L, 1);
            continue;
        }
        lua_setglobal(L, chunk.global);
    }
}

} // namespace luaug::script
