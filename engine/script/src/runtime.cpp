#include "luaug/script/runtime.h"

#include "luaug/core/error.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/script/datatypes.h"
#include "luaug/script/input_events.h"
#include "luaug/script/instance_binding.h"
#include "luaug/script/modules.h"
#include "luaug/script/sandbox.h"
#include "luaug/script/services.h"
#include "luaug/script/signals.h"
#include "luaug/script/tasks.h"
#include "luaug/script/tweens.h"

#include <lua.h>
#include <luacode.h>
#include <lualib.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace luaug::script {
namespace {

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
    for (int index = 1; index <= count; ++index) {
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
    const std::string line = concatArguments(L);
    core::logText(core::LogLevel::Warn, line);
    // Exactly ONE deferred fire per call, carrying the text verbatim
    // (api-design.md §2.1). Not per argument, and not per log line.
    publishMessage(L, core::LogLevel::Warn, line);
    return 0;
}

int scriptPrint(lua_State* L)
{
    const std::string line = concatArguments(L);
    core::logText(core::LogLevel::Info, line);
    publishMessage(L, core::LogLevel::Info, line);
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
    // Owned here rather than by the context, because this is the object whose
    // lifetime brackets the `lua_State` -- the context is a view the bindings
    // reach through `lua_callbacks`.
    SignalSystem signals;
    TaskScheduler tasks;
    ServiceState services;
    ModuleRegistry modules;

    // The bag a host has not replaced. It dies with the VM, which is exactly
    // right for a world nobody is going to reload -- and it means `SaveState`
    // is never a documented no-op, which is the failure `DebugService` taught
    // us to design out (M2 Finding 15).
    ReloadState ownReload;
};

ScriptRuntime::ScriptRuntime(scene::World& world) : m_world(world), m_impl(std::make_unique<Impl>())
{
    m_impl->context.world = &world;
    m_impl->context.signals = &m_impl->signals;
    m_impl->context.tasks = &m_impl->tasks;
    m_impl->context.services = &m_impl->services;
    m_impl->context.modules = &m_impl->modules;
    m_impl->services.reload = &m_impl->ownReload;
}

ScriptRuntime::~ScriptRuntime()
{
    if (m_impl->state != nullptr) {
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
    registerInstanceBinding(L);
    registerDatatypes(L);
    registerSignals(L);
    registerTasks(L);
    registerEnums(L);
    // Last of the registrations, because it creates the DataModel and its two
    // boot services -- which needs `pushInstance`, and therefore the Instance
    // metatable, to already exist.
    registerServices(L);
    registerRequire(L);

    sealGlobals(L);
    return std::nullopt;
}

void ScriptRuntime::setGizmoSink(const GizmoSink& sink)
{
    m_impl->services.gizmos = sink;
}

void ScriptRuntime::setPhysics(scene::PhysicsSync* physics)
{
    m_impl->services.physics = physics;
}

void ScriptRuntime::setInput(input::InputSystem* input)
{
    m_impl->services.input = input;
}

void ScriptRuntime::fireInputEvents(std::span<const input::RawInputEvent> events)
{
    luaug::script::fireInputEvents(m_impl->state, events);
}

void ScriptRuntime::setAnimation(scene::AnimationHost* animation)
{
    m_impl->services.animation = animation;
}

void ScriptRuntime::fireAnimationEnded(std::span<const scene::TrackId> ended)
{
    luaug::script::fireAnimationEnded(m_impl->state, ended);
}

void ScriptRuntime::setReloadState(ReloadState* state)
{
    if (state != nullptr)
        m_impl->services.reload = state;
}

void ScriptRuntime::fireHotReload(bool before)
{
    if (m_impl->state == nullptr)
        return;
    fireHotReloadEvent(m_impl->state, before);
}

void ScriptRuntime::setModuleLoader(const ModuleLoader& loader)
{
    m_impl->modules.loader = loader;
}

MethodCoverage ScriptRuntime::methodCoverage() const noexcept
{
    return m_impl->state == nullptr ? MethodCoverage{} : script::methodCoverage(m_impl->state);
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
    if (bytecode == nullptr) {
        const core::I18nArg args[] = {{"source", chunkName},
                                      {"message", std::string_view{"compilation produced no bytecode"}}};
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

    if (loadStatus != LUA_OK) {
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
    if (resumeStatus != LUA_OK && resumeStatus != LUA_YIELD) {
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

void ScriptRuntime::stepTweens(f64 fixedDt)
{
    if (m_impl->state != nullptr)
        script::stepTweens(m_impl->state, fixedDt);
}

void ScriptRuntime::drain(core::Phase)
{
    if (m_impl->state == nullptr)
        return;

    // Anything the ENGINE raised outside a binding -- a scheduler write, a
    // future physics step. A script's own mutations were converted the moment
    // they happened, because a fire captures its connection list when it is
    // raised and not when it is drained (api-design.md §3.1).
    flushSceneChanges(m_impl->state);
    (void)drainDeferred(m_impl->state);

    // After the drain, which is what gives a `Destroying` handler a live handle
    // to work with and what makes every handle to the corpse stop resolving
    // afterwards (divergence #25).
    m_world.retireDestroyed();
}

void ScriptRuntime::resumeTimers()
{
    if (m_impl->state == nullptr)
        return;
    // Between `PostSimulation` and `Heartbeat` (architecture.md §3). Anything
    // these resumptions defer drains at `Heartbeat`, which is the drain that
    // follows this call.
    // Waiters BEFORE timers. A `WaitForChild` is satisfied by a tree state that
    // was already true when the tick began, while a timer is due only now -- so
    // a waiter resumed after the timer that observes it would make
    // `task.wait()` see a stale world one tick out of every one.
    resumeChildWaiters(m_impl->state);
    resumeDueTimers(m_impl->state, m_world.engineState().tick);
}

void ScriptRuntime::firePhase(core::Phase phase, f64 delta)
{
    if (m_impl->state == nullptr)
        return;

    // Only five of the phases have a signal. The rest are engine-internal
    // resumption points and always will be: `FrameStart` is where a hot reload
    // lands and the parallel windows are the checker's, not a script's.
    const char* name = nullptr;
    switch (phase) {
    case core::Phase::PreRender:
        name = "PreRender";
        break;
    case core::Phase::PreAnimation:
        name = "PreAnimation";
        break;
    case core::Phase::PreSimulation:
        name = "PreSimulation";
        break;
    case core::Phase::PostSimulation:
        name = "PostSimulation";
        break;
    case core::Phase::Heartbeat:
        name = "Heartbeat";
        break;
    default:
        return;
    }

    fireRunServiceEvent(m_impl->state, m_world.atoms().intern(name), delta);
}

void ScriptRuntime::fireLoaded()
{
    if (m_impl->state != nullptr)
        fireDataModelLoaded(m_impl->state);
}

void ScriptRuntime::fireEvent(core::InstanceId instance, core::NameAtom event, f64 argument)
{
    if (m_impl->state == nullptr)
        return;

    const scene::EventDesc* descriptor = m_world.classes().findEvent(m_world.classOf(instance), event);
    if (descriptor == nullptr)
        return;

    lua_pushnumber(m_impl->state, argument);
    fireInstanceEvent(m_impl->state, instance, descriptor->slot, lua_gettop(m_impl->state), 1);
    lua_pop(m_impl->state, 1);
}

u32 ScriptRuntime::deferredDepth() const noexcept
{
    return m_impl->state == nullptr ? 0u : currentDepth(m_impl->state);
}

core::InstanceId ScriptRuntime::dataModel() const noexcept
{
    return m_impl->services.dataModel;
}

lua_State* ScriptRuntime::state() const noexcept
{
    return m_impl->state;
}

} // namespace luaug::script
