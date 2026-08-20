#include "luaug/script/services.h"

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

#include "class_descriptors.gen.h"
#include "luaug/scene/world.h"
#include "luaug/script/datatypes.h"
#include "luaug/script/instance_binding.h"
#include "luaug/script/signals.h"

namespace luaug::script
{
namespace
{

using scene::ClassId;
using scene::World;

[[nodiscard]] ServiceState& services(lua_State* L) noexcept
{
    return *context(L).services;
}

[[nodiscard]] World& world(lua_State* L) noexcept
{
    return *context(L).world;
}

// --- Service lookup ----------------------------------------------------------

[[nodiscard]] core::InstanceId findServiceOfClass(lua_State* L, ClassId serviceClass)
{
    const core::InstanceId root = services(L).dataModel;
    if (!root.valid() || serviceClass == scene::InvalidClass)
        return {};
    return world(L).findFirstChildOfClass(root, serviceClass);
}

// Singletons: every later call returns the same instance, and it is an ordinary
// child of `game` once created (api-design.md §2.1).
[[nodiscard]] core::InstanceId getServiceOfClass(lua_State* L, ClassId serviceClass)
{
    if (const core::InstanceId existing = findServiceOfClass(L, serviceClass); existing.valid())
        return existing;

    World& w = world(L);
    const core::InstanceId created = w.create(serviceClass);
    if (!created.valid())
        return {};
    (void)w.setParent(created, services(L).dataModel);
    flushSceneChanges(L);
    return created;
}

[[nodiscard]] core::InstanceId serviceByName(lua_State* L, std::string_view name)
{
    const World& w = world(L);
    const ClassId classId = w.classes().findId(w.atoms().lookup(name));
    const scene::ClassDescriptor* descriptor = w.classes().find(classId);
    if (descriptor == nullptr || !hasFlag(descriptor->flags, scene::ClassFlags::Service))
        return {};
    return findServiceOfClass(L, classId);
}

[[nodiscard]] ClassId checkServiceClass(lua_State* L, int index)
{
    size_t length = 0;
    const char* text = luaL_checklstring(L, index, &length);
    const std::string_view name{text, length};

    const World& w = world(L);
    const ClassId classId = w.classes().findId(w.atoms().lookup(name));
    const scene::ClassDescriptor* descriptor = w.classes().find(classId);
    if (descriptor == nullptr || !hasFlag(descriptor->flags, scene::ClassFlags::Service))
    {
        const core::I18nArg args[] = {{"className", name}};
        raise(L, LUAUG_TR("scene.err.unknown_service"), args);
    }
    return classId;
}

// --- DataModel ---------------------------------------------------------------

int dataModelGetService(lua_State* L)
{
    (void)checkInstance(L, 1);
    pushInstance(L, getServiceOfClass(L, checkServiceClass(L, 2)));
    return 1;
}

int dataModelFindService(lua_State* L)
{
    (void)checkInstance(L, 1);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 2, &length);
    // Creates nothing, so it answers whether a service is in use rather than
    // forcing it into existence -- which is the whole difference from
    // `GetService`. An unknown *name* is nil here rather than a raise: asking
    // whether something exists is not the same as asking for it.
    pushInstance(L, serviceByName(L, std::string_view{text, length}));
    return 1;
}

int dataModelBindToClose(lua_State* L)
{
    (void)checkInstance(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    services(L).closeHandlers.push_back(lua_ref(L, -1));
    lua_pop(L, 1);
    return 0;
}

int dataModelShutdown(lua_State* L)
{
    (void)checkInstance(L, 1);
    // A flag rather than an exit: nothing in the VM can end a process, and a
    // script that could would take the close handlers down with it.
    services(L).shutdown = true;
    return 0;
}

// --- RunService --------------------------------------------------------------

int runServicePause(lua_State* L)
{
    (void)checkInstance(L, 1);
    // Idempotent: pausing a paused world is a no-op, not an error.
    world(L).engineState().paused = true;
    return 0;
}

int runServiceResume(lua_State* L)
{
    (void)checkInstance(L, 1);
    world(L).engineState().paused = false;
    return 0;
}

int runServiceIsPaused(lua_State* L)
{
    (void)checkInstance(L, 1);
    // Since `Pause` and `Resume` are both idempotent, this is how code tells the
    // two states apart rather than by watching a call fail.
    lua_pushboolean(L, world(L).engineState().paused);
    return 1;
}

// --- TagService --------------------------------------------------------------

int tagServiceGetTagged(lua_State* L)
{
    (void)checkInstance(L, 1);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 2, &length);

    std::vector<core::InstanceId> tagged;
    // `lookup` rather than `intern`: a query for a tag nothing carries is a
    // normal answer, and interning would let a loop over random strings grow
    // the atom table without bound.
    world(L).collectTagged(world(L).atoms().lookup(std::string_view{text, length}), tagged);

    lua_createtable(L, static_cast<int>(tagged.size()), 0);
    for (usize index = 0; index < tagged.size(); ++index)
    {
        pushInstance(L, tagged[index]);
        lua_rawseti(L, -2, static_cast<int>(index) + 1);
    }
    return 1;
}

int tagServiceGetAllTags(lua_State* L)
{
    (void)checkInstance(L, 1);

    // Tags currently carried by at least one instance, not every name ever seen.
    scene::TagSet tags;
    world(L).collectAllTags(tags);

    lua_createtable(L, static_cast<int>(tags.size()), 0);
    for (usize index = 0; index < tags.size(); ++index)
    {
        const std::string_view text = world(L).atoms().text(tags[index]);
        lua_pushlstring(L, text.data(), text.size());
        lua_rawseti(L, -2, static_cast<int>(index) + 1);
    }
    return 1;
}

[[nodiscard]] core::NameAtom checkTagName(lua_State* L, int index)
{
    size_t length = 0;
    const char* text = luaL_checklstring(L, index, &length);
    if (length == 0)
    {
        const core::I18nArg args[] = {{"name", std::string_view{""}}};
        raise(L, LUAUG_TR("scene.err.invalid_name"), args);
    }
    // Interned, unlike the query above: a signal for a tag nothing carries yet
    // is a reasonable thing to hold, so the name has to have an atom.
    return world(L).atoms().intern(std::string_view{text, length});
}

int tagServiceGetInstanceAddedSignal(lua_State* L)
{
    const core::InstanceId self = checkInstance(L, 1);
    pushTagSignal(L, self, SignalKind::TagAdded, checkTagName(L, 2));
    return 1;
}

int tagServiceGetInstanceRemovedSignal(lua_State* L)
{
    const core::InstanceId self = checkInstance(L, 1);
    pushTagSignal(L, self, SignalKind::TagRemoved, checkTagName(L, 2));
    return 1;
}

// --- DebugService ------------------------------------------------------------

[[nodiscard]] core::Color3 optionalColor(lua_State* L, int index)
{
    if (lua_isnoneornil(L, index))
        return core::Color3{1.0f, 1.0f, 1.0f};
    return checkColor3(L, index);
}

int debugServiceDrawLine(lua_State* L)
{
    (void)checkInstance(L, 1);
    const core::Vec3 a = checkVector3(L, 2);
    const core::Vec3 b = checkVector3(L, 3);
    const core::Color3 color = optionalColor(L, 4);

    // Arguments are checked BEFORE the sink is consulted, so a headless run
    // still rejects a bad call. A no-op that also skipped validation would make
    // headless the one place a typo survives.
    const GizmoSink& sink = services(L).gizmos;
    if (sink.line != nullptr)
        sink.line(sink.user, a, b, color);
    return 0;
}

int debugServiceDrawBox(lua_State* L)
{
    (void)checkInstance(L, 1);
    const core::CFrameD frame = checkCFrame(L, 2);
    const core::Vec3 size = checkVector3(L, 3);
    const core::Color3 color = optionalColor(L, 4);

    const GizmoSink& sink = services(L).gizmos;
    if (sink.box != nullptr)
        sink.box(sink.user, frame, size, color);
    return 0;
}

int debugServiceDrawSphere(lua_State* L)
{
    (void)checkInstance(L, 1);
    const core::Vec3 position = checkVector3(L, 2);
    const auto radius = static_cast<f32>(luaL_checknumber(L, 3));
    const core::Color3 color = optionalColor(L, 4);

    const GizmoSink& sink = services(L).gizmos;
    if (sink.sphere != nullptr)
        sink.sphere(sink.user, position, radius, color);
    return 0;
}

int debugServiceGetStat(lua_State* L)
{
    (void)checkInstance(L, 1);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 2, &length);
    const std::string_view name{text, length};

    // Answered from the world rather than published per frame, because it is
    // exact at any moment and a per-frame copy could only be stale.
    if (name == "InstanceCount")
    {
        lua_pushnumber(L, static_cast<f64>(world(L).instanceCount()));
        return 1;
    }

    // The engine's own counters, checked BEFORE the custom table so a game
    // cannot shadow one: `GetStat("FPS")` reads the engine's number or nothing.
    const FrameStats& frame = services(L).frameStats;
    if (name == "FPS")
    {
        lua_pushnumber(L, frame.fps);
        return 1;
    }
    if (name == "FrameTimeMs")
    {
        lua_pushnumber(L, frame.frameTimeMs);
        return 1;
    }
    if (name == "DrawCalls")
    {
        lua_pushnumber(L, frame.drawCalls);
        return 1;
    }
    if (name == "PhysicsBodies")
    {
        // Zero until M5, and zero is the truthful answer: there is no physics
        // world, so nothing is in it. A raise would say the stat does not
        // exist, which is a different and false claim.
        lua_pushnumber(L, frame.physicsBodies);
        return 1;
    }
    if (name == "LuaMemoryKB")
    {
        lua_pushnumber(L, frame.luaMemoryKb);
        return 1;
    }

    const core::NameAtom atom = world(L).atoms().lookup(name);
    for (const auto& [key, value] : services(L).stats.entries)
    {
        if (key == atom)
        {
            lua_pushnumber(L, value);
            return 1;
        }
    }

    // A name nothing has published raises rather than answering zero: a
    // misspelt stat is a bug in the caller, and a debug surface that answers
    // zero hides that bug in the one place people are already confused.
    const core::I18nArg args[] = {{"name", name}};
    raise(L, LUAUG_TR("scene.err.unknown_stat"), args);
}

int debugServiceSetCustomStat(lua_State* L)
{
    (void)checkInstance(L, 1);
    const core::NameAtom name = checkTagName(L, 2);
    const f64 value = luaL_checknumber(L, 3);

    StatTable& stats = services(L).stats;
    for (auto& entry : stats.entries)
    {
        if (entry.first == name)
        {
            entry.second = value;
            return 0;
        }
    }
    stats.entries.emplace_back(name, value);
    return 0;
}

// The built-in panels, api-design.md §2.1. A closed list rather than a free
// namespace, because an unknown panel raises and there has to be something to
// compare against.
constexpr std::string_view Panels[] = {"Stats", "Scene", "Log", "Streaming", "Physics"};

[[nodiscard]] core::NameAtom checkPanelName(lua_State* L, int index)
{
    size_t length = 0;
    const char* text = luaL_checklstring(L, index, &length);
    const std::string_view name{text, length};

    for (const std::string_view panel : Panels)
    {
        if (panel == name)
            return world(L).atoms().intern(name);
    }

    const core::I18nArg args[] = {{"name", name}};
    raise(L, LUAUG_TR("scene.err.unknown_stat"), args);
}

int debugServiceShowPanel(lua_State* L)
{
    (void)checkInstance(L, 1);
    const core::NameAtom name = checkPanelName(L, 2);

    std::vector<core::NameAtom>& open = services(L).openPanels;
    if (std::find(open.begin(), open.end(), name) == open.end())
        open.push_back(name);
    return 0;
}

int debugServiceHidePanel(lua_State* L)
{
    (void)checkInstance(L, 1);
    const core::NameAtom name = checkPanelName(L, 2);

    std::vector<core::NameAtom>& open = services(L).openPanels;
    const auto found = std::find(open.begin(), open.end(), name);
    if (found != open.end())
        open.erase(found);
    return 0;
}

// --- WaitForChild ------------------------------------------------------------

// Deadlines are whole ticks, exactly as `task.wait`'s are: the timeout is
// SimClock seconds like every other timer, and a float compared against a float
// is what makes a replay diverge.
[[nodiscard]] u64 timeoutTicks(f64 seconds, f64 timestep) noexcept
{
    if (!(timestep > 0.0) || !(seconds > 0.0))
        return 1;
    constexpr f64 tolerance = 1e-9;
    const f64 raw = std::ceil(seconds / timestep - tolerance);
    return raw > 1.0 ? static_cast<u64>(raw) : 1u;
}

int instanceWaitForChild(lua_State* L)
{
    const core::InstanceId self = checkInstance(L, 1);
    World& w = world(L);

    size_t length = 0;
    const char* text = luaL_checklstring(L, 2, &length);
    const std::string_view name{text, length};

    // Interned rather than looked up: the awaited name may not exist anywhere
    // yet, and it has to be comparable when it does.
    const core::NameAtom atom = w.atoms().intern(name);

    // A matching child already present returns immediately without yielding.
    // The contract is about the state, not about the event that produced it.
    if (const core::InstanceId found = w.findFirstChild(self, atom); found.valid())
    {
        pushInstance(L, found);
        return 1;
    }

    ChildWaiter waiter;
    waiter.parent = self;
    waiter.name = atom;
    waiter.scheduledTick = w.engineState().tick;
    if (!lua_isnoneornil(L, 3))
    {
        waiter.hasTimeout = true;
        waiter.deadlineTick = waiter.scheduledTick + timeoutTicks(luaL_checknumber(L, 3), w.engineState().fixedTimestep);
    }

    lua_pushthread(L);
    waiter.threadRef = lua_ref(L, -1);
    lua_pop(L, 1);

    services(L).childWaiters.push_back(waiter);
    return lua_yield(L, 0);
}

// --- Registration ------------------------------------------------------------

// --- HotReloadService --------------------------------------------------------

int hotReloadSaveState(lua_State* L)
{
    (void)checkInstance(L, 1);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 2, &length);
    // Present but nil is a legal value to save -- it is how a script clears a
    // key -- so this checks that the argument exists rather than what it is.
    luaL_checkany(L, 3);

    std::string reason;
    std::optional<BagValue> value = toBagValue(L, 3, reason);
    if (!value.has_value())
    {
        // Raising rather than dropping. A reload that quietly loses state is
        // worse than one that says which value it could not keep, because the
        // first is discovered a save later and blamed on the reload.
        const core::I18nArg args[] = {{"key", std::string_view{text, length}}, {"reason", std::string_view{reason}}};
        raise(L, LUAUG_TR("script.err.unsavable_state"), args);
    }

    services(L).reload->save(std::string_view{text, length}, std::move(*value));
    return 0;
}

int hotReloadLoadState(lua_State* L)
{
    (void)checkInstance(L, 1);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 2, &length);

    const BagValue* stored = services(L).reload->load(std::string_view{text, length});
    if (stored == nullptr)
    {
        lua_pushnil(L);
        return 1;
    }

    pushBagValue(L, *stored);
    return 1;
}

int hotReloadIsReload(lua_State* L)
{
    (void)checkInstance(L, 1);
    lua_pushboolean(L, services(L).reload->isReload() ? 1 : 0);
    return 1;
}

// `WaitForChild` is here rather than in `instance_binding.cpp` because it parks
// on a tree state and only the resumption phase this file owns can wake it.
constexpr InstanceMethodBinding ServiceMethods[] = {
    {"Instance", "WaitForChild", instanceWaitForChild},

    {"DataModel", "GetService", dataModelGetService},
    {"DataModel", "FindService", dataModelFindService},
    {"DataModel", "BindToClose", dataModelBindToClose},
    {"DataModel", "Shutdown", dataModelShutdown},

    {"RunService", "Pause", runServicePause},
    {"RunService", "Resume", runServiceResume},
    {"RunService", "IsPaused", runServiceIsPaused},

    {"TagService", "GetTagged", tagServiceGetTagged},
    {"TagService", "GetAllTags", tagServiceGetAllTags},
    {"TagService", "GetInstanceAddedSignal", tagServiceGetInstanceAddedSignal},
    {"TagService", "GetInstanceRemovedSignal", tagServiceGetInstanceRemovedSignal},

    {"DebugService", "DrawLine", debugServiceDrawLine},
    {"DebugService", "DrawBox", debugServiceDrawBox},
    {"DebugService", "DrawSphere", debugServiceDrawSphere},
    {"DebugService", "GetStat", debugServiceGetStat},
    {"DebugService", "SetCustomStat", debugServiceSetCustomStat},
    {"DebugService", "ShowPanel", debugServiceShowPanel},
    {"DebugService", "HidePanel", debugServiceHidePanel},
    {"HotReloadService", "SaveState", hotReloadSaveState},
    {"HotReloadService", "LoadState", hotReloadLoadState},
    {"HotReloadService", "IsReload", hotReloadIsReload},
};

} // namespace

void registerServices(lua_State* L)
{
    VmContext& ctx = context(L);
    ServiceState& state = *ctx.services;
    World& w = *ctx.world;
    core::AtomTable& atoms = w.atoms();

    state.messageOut = atoms.intern("MessageOut");
    state.loaded = atoms.intern("Loaded");
    state.runServiceClass = w.classes().findId(atoms.intern("RunService"));
    state.tagServiceClass = w.classes().findId(atoms.intern("TagService"));
    state.debugServiceClass = w.classes().findId(atoms.intern("DebugService"));
    state.hotReloadServiceClass = w.classes().findId(atoms.intern("HotReloadService"));
    state.preReload = atoms.intern("PreReload");
    state.postReload = atoms.intern("PostReload");

    bindInstanceMethods(L, ServiceMethods);

    state.dataModel = w.create(w.classes().findId(atoms.intern("DataModel")));

    // `Workspace`, `ScriptService` and `Lighting` exist from boot; every other
    // service is created by its first `GetService` (api-design.md §1.2). The
    // first two are here because a script reaches `workspace` through a global
    // rather than through a call, and because the mount point has to exist
    // before anything mounts.
    const core::InstanceId workspace = getServiceOfClass(L, w.classes().findId(atoms.intern("Workspace")));
    (void)getServiceOfClass(L, w.classes().findId(atoms.intern("ScriptService")));

    // `Lighting` is here for a different reason, and it is the one M4.5 exists
    // for: the renderer reads the environment every frame whether or not a
    // script ever asks for the service, so "created on first `GetService`"
    // cannot be true of it. It was, and the host cached the id of a service
    // that did not exist yet -- so every frame M4 ever drew used the struct
    // defaults and no scene's `Lighting` reached a pixel.
    //
    // Found by `getServiceOfClass` rather than named as an id, and guarded on
    // the class existing: `Lighting` is registered by `render`, and an engine
    // built without that module has no such class. This file is in `script` and
    // must not acquire an opinion about which modules are compiled in.
    if (const ClassId lightingClass = w.classes().findId(atoms.intern("Lighting")); lightingClass != scene::InvalidClass)
        (void)getServiceOfClass(L, lightingClass);

    // Whatever the boot tree raised is consumed rather than queued: nothing can
    // have connected yet, and a fire nobody could have subscribed to is a fire
    // with no observer.
    (void)w.changes().take();

    pushInstance(L, state.dataModel);
    lua_setglobal(L, "game");
    pushInstance(L, workspace);
    lua_setglobal(L, "workspace");
}

void publishFrameStats(lua_State* L, const FrameStats& stats)
{
    services(L).frameStats = stats;
}

void publishMessage(lua_State* L, core::LogLevel level, std::string_view text)
{
    ServiceState& state = services(L);
    const core::InstanceId debug = findServiceOfClass(L, state.debugServiceClass);
    if (!debug.valid())
        return;

    const scene::EventDesc* descriptor =
        world(L).classes().findEvent(world(L).classOf(debug), state.messageOut);
    if (descriptor == nullptr)
        return;

    // `print` and `warn` each produce exactly one deferred fire carrying the
    // text verbatim, at `Info` and `Warning` (api-design.md §2.1).
    const i32 logLevel = static_cast<i32>(level);
    lua_pushlstring(L, text.data(), text.size());
    pushEnumItem(L, scene::EnumValue{scene::generated::LogLevelEnumId, logLevel});
    fireEngineMessage(L, debug, descriptor->slot, lua_gettop(L) - 1, 2);
    lua_pop(L, 2);
}

void fireRunServiceEvent(lua_State* L, core::NameAtom event, f64 delta)
{
    const core::InstanceId runService = findServiceOfClass(L, services(L).runServiceClass);
    if (!runService.valid())
        return;

    const scene::EventDesc* descriptor = world(L).classes().findEvent(world(L).classOf(runService), event);
    if (descriptor == nullptr)
        return;

    lua_pushnumber(L, delta);
    fireInstanceEvent(L, runService, descriptor->slot, lua_gettop(L), 1);
    lua_pop(L, 1);
}

void fireHotReloadEvent(lua_State* L, bool before)
{
    ServiceState& state = services(L);
    const core::InstanceId service = findServiceOfClass(L, state.hotReloadServiceClass);
    // No instance means no script ever asked for the service, so nothing can
    // have connected to it. Not an error, and not worth a log line every save.
    if (!service.valid())
        return;

    const core::NameAtom event = before ? state.preReload : state.postReload;
    const scene::EventDesc* descriptor = world(L).classes().findEvent(world(L).classOf(service), event);
    if (descriptor == nullptr)
        return;

    fireInstanceEvent(L, service, descriptor->slot, 0, 0);
}

void setReloadState(lua_State* L, ReloadState* state)
{
    if (state != nullptr)
        services(L).reload = state;
}

void fireDataModelLoaded(lua_State* L)
{
    ServiceState& state = services(L);
    if (!state.dataModel.valid())
        return;

    const scene::EventDesc* descriptor =
        world(L).classes().findEvent(world(L).classOf(state.dataModel), state.loaded);
    if (descriptor == nullptr)
        return;
    fireInstanceEvent(L, state.dataModel, descriptor->slot, 0, 0);
}

void resumeChildWaiters(lua_State* L)
{
    ServiceState& state = services(L);
    World& w = world(L);
    const u64 tick = w.engineState().tick;
    const f64 timestep = w.engineState().fixedTimestep;

    // Collected first, because a resumed coroutine may park another waiter and
    // the vector it would push onto is the one being walked.
    std::vector<ChildWaiter> ready;
    for (usize index = 0; index < state.childWaiters.size();)
    {
        ChildWaiter& waiter = state.childWaiters[index];
        const bool satisfied = w.findFirstChild(waiter.parent, waiter.name).valid();
        const bool expired = waiter.hasTimeout && tick >= waiter.deadlineTick;

        if (!satisfied && !expired)
        {
            // Five sim-seconds, once, and then it keeps waiting. The timeout
            // form never warns however long its timeout: you said how long you
            // were prepared to wait.
            if (!waiter.hasTimeout && !waiter.warned
                && static_cast<f64>(tick - waiter.scheduledTick) * timestep >= 5.0)
            {
                waiter.warned = true;
                const core::I18nArg args[] = {
                    {"name", w.atoms().text(waiter.name)},
                    {"parent", w.atoms().text(w.name(waiter.parent))},
                };
                const std::string message = core::formatKeyPrefixed(LUAUG_TR("scene.warn.wait_for_child"), args);
                core::logText(core::LogLevel::Warn, message);
                publishMessage(L, core::LogLevel::Warn, message);
            }
            ++index;
            continue;
        }

        ready.push_back(waiter);
        state.childWaiters.erase(state.childWaiters.begin() + static_cast<std::ptrdiff_t>(index));
    }

    for (const ChildWaiter& waiter : ready)
    {
        lua_getref(L, waiter.threadRef);
        lua_State* co = lua_tothread(L, -1);
        if (co == nullptr)
        {
            lua_pop(L, 1);
            (void)lua_unref(L, waiter.threadRef);
            continue;
        }

        // Expiry returns nil -- no error, no warning.
        pushInstance(L, w.findFirstChild(waiter.parent, waiter.name));
        lua_xmove(L, co, 1);
        const bool finished = resumeScheduled(L, co, 1);
        lua_pop(L, 1);
        if (finished)
            (void)lua_unref(L, waiter.threadRef);
    }
}

bool shutdownRequested(lua_State* L)
{
    return services(L).shutdown;
}

void runCloseHandlers(lua_State* L)
{
    ServiceState& state = services(L);
    // Copied, because a handler may register another and the vector it would
    // push onto is the one being walked. A callback registered during shutdown
    // does not run for this shutdown, which is the same rule a fire follows.
    const std::vector<int> handlers = state.closeHandlers;
    state.closeHandlers.clear();

    for (const int ref : handlers)
    {
        // Its own coroutine, like a signal handler: a close handler must be
        // allowed to yield, and one that errors must not stop the others.
        lua_State* co = lua_newthread(L);
        lua_getref(L, ref);
        lua_xmove(L, co, 1);
        (void)resumeScheduled(L, co, 0);
        lua_pop(L, 1);
        (void)lua_unref(L, ref);
    }
}

} // namespace luaug::script
