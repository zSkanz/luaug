#include "luaug/app/script_host.h"

#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include <lua.h>
#include <lualib.h>

#include <Luau/Compiler.h>

#include "luaug/core/log.h"

// ADR 0013 is an ABI decision, not a preference: LUA_VECTOR_DOUBLE also moves
// LUA_TVECTOR's ordinal within `lua_Type`, so a mismatch between the VM and
// engine code is silent memory corruption rather than a compile error. These
// assertions read the vendored luaconf.h, which is the only authority.
static_assert(LUA_VECTOR_SIZE == 3, "ADR 0013: Vector3 IS the native 3-wide Luau vector");
static_assert(LUA_VECTOR_DOUBLE == 0, "ADR 0013: vectors are f32; f64 world coords are solved engine-side (ADR 0014)");

namespace luaug::app
{
namespace
{

// Script `print` is user-authored text, not an engine message: it is routed to
// the log verbatim and must never be treated as an i18n key (rule R3 governs
// engine-originated prose only).
int scriptPrint(lua_State* L)
{
    const int count = lua_gettop(L);

    std::string line;
    for (int i = 1; i <= count; ++i)
    {
        std::size_t length = 0;
        const char* text = luaL_tolstring(L, i, &length);
        if (i > 1)
            line.push_back('\t');
        line.append(text, length);
        lua_pop(L, 1); // luaL_tolstring pushes its result in every branch
    }

    core::logText(core::LogLevel::Info, line);
    return 0;
}

// api-design.md §1.1 removes these from the game VM outright -- no aliases,
// ever. Most are simply absent from Luau already; `getfenv`, `setfenv` and
// `newproxy` are not, and Luau's base library additionally points `_G` at the
// real globals table.
//
// This is not tidiness. `getfenv`/`setfenv` disable the `safeenv` optimization,
// which is simultaneously the sandbox guarantee (R4) and the import fastpath;
// native codegen also gives up on any function that touches them
// (docs/research/luau-2026.md §3). Leaving them reachable would quietly
// invalidate every performance number measured from here on.
//
// Must run before luaL_sandbox, which makes the globals table readonly.
void removeForbiddenGlobals(lua_State* L)
{
    static constexpr const char* kRemoved[] = {
        "wait",
        "spawn",
        "delay",
        "tick",
        "elapsedTime",
        "loadstring",
        "getfenv",
        "setfenv",
        "newproxy",
        "shared",
        "io",
    };

    for (const char* name : kRemoved)
    {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }

    // `_G` stays defined so that referencing it is not a surprise, but it is an
    // empty table rather than the environment itself. luaL_sandbox freezes it
    // along with every other global table.
    lua_newtable(L);
    lua_setglobal(L, "_G");
}

std::string popErrorMessage(lua_State* thread)
{
    std::size_t length = 0;
    const char* text = lua_tolstring(thread, -1, &length);
    std::string message = text != nullptr ? std::string(text, length) : std::string("unknown error");
    lua_pop(thread, 1);
    return message;
}

} // namespace

ScriptHost::ScriptHost(GlobalInstaller installGlobals)
{
    state_ = luaL_newstate();
    luaL_openlibs(state_);

    // Engine globals must be installed BEFORE luaL_sandbox: the sandbox marks
    // the globals table readonly, so anything registered afterwards would fail.
    lua_pushcfunction(state_, scriptPrint, "print");
    lua_setglobal(state_, "print");

    // The caller's globals go in the same window, for the same reason. This is
    // architecture.md §5's boot order -- open the stdlib, register engine
    // libraries, install globals, then sandbox -- with the third step finally
    // reachable from outside this file.
    if (installGlobals)
        installGlobals(state_);

    removeForbiddenGlobals(state_);

    // Mandatory, and never disabled to make something work (rule R4). This is
    // also a performance feature: it is what enables the safeenv import
    // fastpaths (docs/research/luau-2026.md §4).
    luaL_sandbox(state_);
}

ScriptHost::~ScriptHost()
{
    if (state_ != nullptr)
    {
        lua_close(state_);
        state_ = nullptr;
    }
}

std::optional<core::EngineError> ScriptHost::run(std::string_view source, std::string_view chunkName)
{
    Luau::CompileOptions options;
    options.optimizationLevel = 2; // inlining + loop unrolling
    options.debugLevel = 2;        // local/upvalue names, so tracebacks are useful
    options.typeInfoLevel = 1;     // required for native codegen to be effective
    // vectorLib/vectorCtor/vectorType are set in M2, when a Vector3 global
    // exists for the compiler to fold into (architecture.md §5).

    const std::string bytecode = Luau::compile(std::string(source), options);

    // Each script runs on its own thread with its own globals table, so one
    // script cannot reach into another's environment.
    lua_State* thread = lua_newthread(state_);
    luaL_sandboxthread(thread);

    const std::string chunk = "@" + std::string(chunkName);

    // A compile error is encoded into the blob and decoded here rather than
    // thrown, so syntax and runtime failures share one reporting path.
    if (luau_load(thread, chunk.c_str(), bytecode.data(), bytecode.size(), 0) != LUA_OK)
    {
        const std::string message = popErrorMessage(thread);
        lua_pop(state_, 1); // the thread
        const std::array<core::I18nArg, 2> args{
            core::I18nArg{"source", chunkName}, core::I18nArg{"message", message}};
        return core::makeError(LUAUG_TR("script.err.syntax"), args);
    }

    const int status = lua_resume(thread, nullptr, 0);

    if (status != LUA_OK && status != LUA_YIELD)
    {
        const std::string message = popErrorMessage(thread);
        const char* trace = lua_debugtrace(thread);
        std::string traceback = trace != nullptr ? trace : std::string{};
        lua_pop(state_, 1); // the thread

        const std::array<core::I18nArg, 2> args{
            core::I18nArg{"source", chunkName}, core::I18nArg{"message", message}};
        return core::makeError(LUAUG_TR("script.err.runtime"), args, std::move(traceback));
    }

    lua_pop(state_, 1); // the thread
    return std::nullopt;
}

std::optional<core::EngineError> ScriptHost::runFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        const std::array<core::I18nArg, 1> args{core::I18nArg{"path", path.string()}};
        return core::makeError(LUAUG_TR("engine.cli.err.script_missing"), args);
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string source = buffer.str();

    return run(source, path.filename().string());
}

} // namespace luaug::app
