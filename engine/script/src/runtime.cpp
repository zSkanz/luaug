#include "luaug/script/runtime.h"

#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "luaug/core/error.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/script/datatypes.h"
#include "luaug/script/instance_binding.h"
#include "luaug/script/sandbox.h"

namespace luaug::script
{
namespace
{

// Fires for the life of the VM rather than only during boot -- every string the
// VM interns passes through it -- so the sink has to be reachable from a free
// function. It is reached through `lua_callbacks(L)->userdata` like every other
// binding: the callback is handed the `lua_State`, which is what makes a
// process-global unnecessary and the VM count irrelevant here.
int16_t internAtom(lua_State* L, const char* text, size_t length)
{
    void* stored = lua_callbacks(L)->userdata;
    if (stored == nullptr)
        return -1;

    VmContext& ctx = *static_cast<VmContext*>(stored);
    const auto atom = static_cast<int16_t>(ctx.atomToName.size());
    // Luau's atom is an int16, so there are 32767 of them. A world that interned
    // more distinct property and method names than that has other problems, but
    // latching at -1 rather than wrapping is the difference between a slow
    // dispatch and a wrong one.
    if (static_cast<usize>(atom) >= 32767u)
        return -1;

    ctx.atomToName.push_back(ctx.world->atoms().intern(std::string_view{text, length}).id);
    return atom;
}

// Concatenates the call's arguments the way `print` does -- tab-separated,
// `tostring` applied to each -- so that `warn` differs from `print` in severity
// and in nothing else.
std::string concatArguments(lua_State* L)
{
    std::string line;
    const int count = lua_gettop(L);
    for (int index = 1; index <= count; ++index)
    {
        size_t length = 0;
        const char* text = luaL_tolstring(L, index, &length);
        if (index > 1)
            line += '\t';
        line.append(text, length);
        lua_pop(L, 1);
    }
    return line;
}

// `warn` is a global api-design.md §1.1 lists and Luau does not define -- its
// base library has neither `warn` nor `collectgarbage`. Script output is the
// script author's own text and is NOT an engine message, so it passes through
// verbatim rather than through the catalog: R3 governs what the engine says,
// not what a game says.
int scriptWarn(lua_State* L)
{
    core::logText(core::LogLevel::Warn, concatArguments(L));
    return 0;
}

int scriptPrint(lua_State* L)
{
    core::logText(core::LogLevel::Info, concatArguments(L));
    return 0;
}

void installConsole(lua_State* L)
{
    lua_pushcfunction(L, scriptPrint, "print");
    lua_setglobal(L, "print");
    lua_pushcfunction(L, scriptWarn, "warn");
    lua_setglobal(L, "warn");
}

} // namespace

struct ScriptRuntime::Impl
{
    lua_State* state = nullptr;
    // Reached from every binding through `lua_callbacks(L)->userdata`. Owned
    // here, and by a stable address: the callbacks hold a pointer to it for the
    // life of the state.
    VmContext context;
    MethodCoverage coverage;
    u32 drainDepth = 0;
};

ScriptRuntime::ScriptRuntime(scene::World& world) : m_world(world), m_impl(std::make_unique<Impl>())
{
    m_impl->context.world = &world;
}

ScriptRuntime::~ScriptRuntime()
{
    if (m_impl->state != nullptr)
    {
        lua_close(m_impl->state);
        m_impl->state = nullptr;
    }
}

std::optional<core::EngineError> ScriptRuntime::boot()
{
    if (m_impl->state != nullptr)
        return std::nullopt;

    lua_State* L = luaL_newstate();
    if (L == nullptr)
        return core::makeError(LUAUG_TR("script.err.vm_create_failed"));

    m_impl->state = L;

    // BEFORE `luaL_openlibs`, and this is not a preference. The callback is
    // lazy and one-shot per string: a name interned while it is absent latches
    // at -1 for the life of the VM, and the symptom is a property lookup that
    // silently stops using its atom -- slower, still correct, and invisible.
    lua_callbacks(L)->userdata = &m_impl->context;
    lua_callbacks(L)->useratom = internAtom;

    // Interned through the atom table rather than through the VM, because these
    // are compared against an engine `NameAtom` and not against a Luau one.
    m_impl->context.wellKnown.parent = m_world.atoms().intern("Parent");
    m_impl->context.wellKnown.cframe = m_world.atoms().intern("CFrame");

    luaL_openlibs(L);

    // `luaL_sandbox` removes nothing (see sandbox.h). Everything api-design.md
    // §1.1 calls removed goes here, before the freeze.
    removeUnsafeGlobals(L);

    // Globals belong between here and the seal. There is no second chance: the
    // sandbox marks the global table read-only and a later `lua_setglobal`
    // fails inside the VM rather than returning something a caller can check.
    // M0 got this wrong from the outside once, which is why the ordering lives
    // in one function instead of in a caller's head.
    installConsole(L);

    // Every tag metatable, then every global that constructs one. All of it
    // before the first `luau_load`: the fast dispatch opcodes are chosen once
    // per `Proto` at load time and a deopt is permanent (binding.h, rule 2).
    m_impl->coverage = registerInstanceBinding(L);
    registerDatatypes(L);
    registerEnums(L);

    sealGlobals(L);
    return std::nullopt;
}

MethodCoverage ScriptRuntime::methodCoverage() const noexcept
{
    return m_impl->coverage;
}

std::optional<core::EngineError> ScriptRuntime::runSource(std::string_view source, std::string_view chunkName)
{
    lua_State* L = m_impl->state;
    if (L == nullptr)
        return core::makeError(LUAUG_TR("script.err.vm_not_booted"));

    size_t bytecodeSize = 0;
    lua_CompileOptions options{};
    options.optimizationLevel = 2;
    options.debugLevel = 2;
    // ADR 0013: these three are what make `Vector3.new(1, 2, 3)` a constant
    // rather than a call, and what makes a dynamic one a fastcall. The type
    // name is a checker hint only -- the folding comes from the library and
    // constructor names alone.
    options.vectorLib = "Vector3";
    options.vectorCtor = "new";
    options.vectorType = "Vector3";

    const std::string chunk = "@" + std::string(chunkName);
    char* bytecode = luau_compile(source.data(), source.size(), &options, &bytecodeSize);
    if (bytecode == nullptr)
    {
        const core::I18nArg args[] = {{"source", chunkName}, {"message", std::string_view{"compilation produced no bytecode"}}};
        return core::makeError(LUAUG_TR("script.err.syntax"), args);
    }

    // Each script runs on its own thread with its own globals table, which is
    // what "per-script sandboxing" means (api-design.md §3): a global one script
    // sets is not visible to another.
    //
    // The chunk is loaded ONTO the thread rather than onto the main state and
    // moved across. `lua_newthread` pushes the thread on top of whatever was
    // already there, so a load-then-move sequence moves the thread and leaves
    // the function behind -- which fails as a call on a non-function, some
    // distance from the mistake.
    lua_State* thread = lua_newthread(L);
    luaL_sandboxthread(thread);

    const int loadStatus = luau_load(thread, chunk.c_str(), bytecode, bytecodeSize, 0);
    std::free(bytecode);

    if (loadStatus != LUA_OK)
    {
        const char* message = lua_tostring(thread, -1);
        const std::string text = message == nullptr ? std::string{} : std::string(message);
        const core::I18nArg args[] = {{"source", chunkName}, {"message", text}};
        core::EngineError error = core::makeError(LUAUG_TR("script.err.syntax"), args);
        lua_pop(L, 1); // the thread
        return error;
    }

    const int resumeStatus = lua_resume(thread, nullptr, 0);
    // LUA_YIELD is the normal outcome for anything that calls `task.wait`: the
    // script has not finished, it is parked, and the scheduler will resume it.
    if (resumeStatus != LUA_OK && resumeStatus != LUA_YIELD)
    {
        const char* message = lua_tostring(thread, -1);
        const std::string text = message == nullptr ? std::string{} : std::string(message);
        const core::I18nArg args[] = {{"source", chunkName}, {"message", text}};
        core::EngineError error = core::makeError(LUAUG_TR("script.err.runtime"), args);
        lua_pop(L, 1); // the thread
        return error;
    }

    lua_pop(L, 1); // the thread
    return std::nullopt;
}

void ScriptRuntime::drain(core::Phase)
{
    // The scene's POD facts become signal fires here, and the queue then drains
    // to fixpoint (api-design.md §3.1). Nothing is connected yet -- the Instance
    // bindings that create connections are the next step -- so this currently
    // consumes the facts and runs no handlers, which is what an unwatched world
    // should cost.
    (void)m_world.changes().take();
    m_world.retireDestroyed();
}

void ScriptRuntime::resumeTimers(f64)
{
}

void ScriptRuntime::fireEvent(core::InstanceId, core::NameAtom, f64)
{
}

u32 ScriptRuntime::deferredDepth() const noexcept
{
    return m_impl->drainDepth;
}

lua_State* ScriptRuntime::state() const noexcept
{
    return m_impl->state;
}

} // namespace luaug::script
