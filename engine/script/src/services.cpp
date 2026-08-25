#include "luaug/script/services.h"

#include "luaug/input/input.h"
#include "luaug/platform/event.h"
#include "luaug/scene/world.h"
#include "luaug/script/datatypes.h"
#include "luaug/script/instance_binding.h"
#include "luaug/script/signals.h"
#include "luaug/script/tweens.h"

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "class_descriptors.gen.h"

namespace luaug::script {
namespace {

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
    if (descriptor == nullptr || !hasFlag(descriptor->flags, scene::ClassFlags::Service)) {
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
    for (usize index = 0; index < tagged.size(); ++index) {
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
    for (usize index = 0; index < tags.size(); ++index) {
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
    if (length == 0) {
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
    if (name == "InstanceCount") {
        lua_pushnumber(L, static_cast<f64>(world(L).instanceCount()));
        return 1;
    }

    // The engine's own counters, checked BEFORE the custom table so a game
    // cannot shadow one: `GetStat("FPS")` reads the engine's number or nothing.
    const FrameStats& frame = services(L).frameStats;
    if (name == "FPS") {
        lua_pushnumber(L, frame.fps);
        return 1;
    }
    if (name == "FrameTimeMs") {
        lua_pushnumber(L, frame.frameTimeMs);
        return 1;
    }
    if (name == "DrawCalls") {
        lua_pushnumber(L, frame.drawCalls);
        return 1;
    }
    if (name == "PhysicsBodies") {
        // Zero until M5, and zero is the truthful answer: there is no physics
        // world, so nothing is in it. A raise would say the stat does not
        // exist, which is a different and false claim.
        lua_pushnumber(L, frame.physicsBodies);
        return 1;
    }
    if (name == "LuaMemoryKB") {
        lua_pushnumber(L, frame.luaMemoryKb);
        return 1;
    }
    if (name == "AudioUnderruns") {
        // Zero on a headless run because there is no device to starve, and that
        // is the truthful answer rather than a missing one -- the same reasoning
        // `PhysicsBodies` carries above.
        lua_pushnumber(L, frame.audioUnderruns);
        return 1;
    }
    if (name == "AudioVoices") {
        lua_pushnumber(L, frame.audioVoices);
        return 1;
    }
    if (name == "AudioClipsLoaded") {
        lua_pushnumber(L, frame.audioClipsLoaded);
        return 1;
    }
    if (name == "AudioClipsMissing") {
        // The number that answers "is this the real sound or the placeholder
        // tone", which a person listening on laptop speakers often cannot.
        lua_pushnumber(L, frame.audioClipsMissing);
        return 1;
    }
    if (name == "VisibleObjects") {
        // Not the same number as `DrawCalls` since M7.5, and the difference is
        // the point: a run of objects sharing a mesh and a material is one call.
        lua_pushnumber(L, frame.visibleObjects);
        return 1;
    }
    if (name == "InstancedDraws") {
        lua_pushnumber(L, frame.instancedDraws);
        return 1;
    }
    if (name == "MeshLodDraws") {
        // Zero on a scene whose meshes have one level, which is the truthful
        // answer and not a missing one -- the same reasoning `PhysicsBodies`
        // carries above.
        lua_pushnumber(L, frame.meshLodDraws);
        return 1;
    }

    const core::NameAtom atom = world(L).atoms().lookup(name);
    for (const auto& [key, value] : services(L).stats.entries) {
        if (key == atom) {
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
    for (auto& entry : stats.entries) {
        if (entry.first == name) {
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

    for (const std::string_view panel : Panels) {
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
    if (const core::InstanceId found = w.findFirstChild(self, atom); found.valid()) {
        pushInstance(L, found);
        return 1;
    }

    ChildWaiter waiter;
    waiter.parent = self;
    waiter.name = atom;
    waiter.scheduledTick = w.engineState().tick;
    if (!lua_isnoneornil(L, 3)) {
        waiter.hasTimeout = true;
        waiter.deadlineTick =
            waiter.scheduledTick + timeoutTicks(luaL_checknumber(L, 3), w.engineState().fixedTimestep);
    }

    lua_pushthread(L);
    waiter.threadRef = lua_ref(L, -1);
    lua_pop(L, 1);

    services(L).childWaiters.push_back(waiter);
    return lua_yield(L, 0);
}

// --- StreamingService (M7) ---------------------------------------------------
//
// A focus is an INSTANCE rather than a position, so the world streams around
// something that moves without a script pushing coordinates every frame. The
// host reads the set once per frame and asks each entry where it is.

int streamingAddFocus(lua_State* L)
{
    (void)checkInstance(L, 1);
    const core::InstanceId focus = checkInstance(L, 2);

    World& w = world(L);
    // An instance with no position cannot anchor a world, and accepting one
    // would silently stream around the origin -- which looks like "streaming is
    // broken" rather than like "that was the wrong instance".
    if (w.parts().find(focus) == nullptr && w.cameras().find(focus) == nullptr)
        raise(L, LUAUG_TR("scene.err.streaming_focus_unlocatable"));

    std::vector<core::InstanceId>& foci = w.streamingFoci();
    const auto at = std::lower_bound(foci.begin(), foci.end(), focus,
                                     [](core::InstanceId a, core::InstanceId b) { return a.index < b.index; });
    // Sorted and deduplicated: adding the same focus twice would double its
    // weight in nothing at all, but it would also make removal ambiguous.
    if (at == foci.end() || at->index != focus.index)
        foci.insert(at, focus);
    return 0;
}

int streamingRemoveFocus(lua_State* L)
{
    (void)checkInstance(L, 1);
    const core::InstanceId focus = checkInstance(L, 2);

    std::vector<core::InstanceId>& foci = world(L).streamingFoci();
    const auto at = std::lower_bound(foci.begin(), foci.end(), focus,
                                     [](core::InstanceId a, core::InstanceId b) { return a.index < b.index; });
    if (at != foci.end() && at->index == focus.index)
        foci.erase(at);
    return 0;
}

int streamingLoadAreaAsync(lua_State* L)
{
    (void)checkInstance(L, 1);
    const core::Vec3 position = checkVector3(L, 2);
    const f64 radius = luaL_checknumber(L, 3);
    if (!(radius > 0.0))
        raise(L, LUAUG_TR("scene.err.number_positive"));

    ServiceState& state = services(L);
    ServiceState::AreaWaiter waiter;
    waiter.position = core::toDVec3(position);
    waiter.radius = radius;
    waiter.scheduledTick = world(L).engineState().tick;

    lua_pushthread(L);
    waiter.threadRef = lua_ref(L, -1);
    lua_pop(L, 1);

    // Always parks, even when the area is already resident: whether it is is a
    // question only the host can answer, and answering it here would need this
    // module to know what a chunk is. The host resumes it on the very next
    // pump, so an already-loaded area costs one frame rather than a wait.
    state.areaWaiters.push_back(waiter);
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
    if (!value.has_value()) {
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
    if (stored == nullptr) {
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

// --- PhysicsService (M5) -----------------------------------------------------
//
// The group table is world state (`scene::CollisionGroups`) rather than the
// backend's, so these are ordinary scene writes: the mirror pushes the table
// down at the next tick, and a script reading it back gets what it wrote
// whether or not a backend exists.

int physicsRegisterCollisionGroup(lua_State* L)
{
    (void)checkInstance(L, 1);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 2, &length);

    World& w = world(L);
    const core::NameAtom name = w.atoms().intern(std::string_view{text, length});
    if (w.collisionGroups().add(name) == scene::CollisionGroups::kInvalid)
        raise(L, LUAUG_TR("scene.err.collision_groups_full"));
    w.collisionGroups().bumpRevision();
    return 0;
}

int physicsCollisionGroupSetCollidable(lua_State* L)
{
    (void)checkInstance(L, 1);
    size_t firstLength = 0;
    const char* first = luaL_checklstring(L, 2, &firstLength);
    size_t secondLength = 0;
    const char* second = luaL_checklstring(L, 3, &secondLength);
    luaL_checktype(L, 4, LUA_TBOOLEAN);
    const bool collidable = lua_toboolean(L, 4) != 0;

    World& w = world(L);
    const u16 a = w.collisionGroups().find(w.atoms().lookup(std::string_view{first, firstLength}));
    const u16 b = w.collisionGroups().find(w.atoms().lookup(std::string_view{second, secondLength}));
    if (a == scene::CollisionGroups::kInvalid || b == scene::CollisionGroups::kInvalid)
        raise(L, LUAUG_TR("scene.err.unknown_collision_group"));

    w.collisionGroups().setCollidable(a, b, collidable);
    w.collisionGroups().bumpRevision();
    return 0;
}

int physicsGetRegisteredCollisionGroups(lua_State* L)
{
    (void)checkInstance(L, 1);
    World& w = world(L);
    const scene::CollisionGroups& groups = w.collisionGroups();

    // A fresh array every call, in registration order. Fresh so a caller may
    // sort it; ordered because R10 forbids a container's own order reaching a
    // script.
    lua_createtable(L, static_cast<int>(groups.count()), 0);
    for (u32 index = 0; index < groups.count(); ++index) {
        const std::string_view name = w.atoms().text(groups.nameAt(static_cast<u16>(index)));
        lua_pushlstring(L, name.data(), name.size());
        lua_rawseti(L, -2, static_cast<int>(index) + 1);
    }
    return 1;
}

// --- Workspace queries (M5) --------------------------------------------------
//
// Every one of them reads the same physics world the tick steps. A build with
// no physics backend answers "nothing", which is the same answer an empty world
// gives -- a query is a question about the world, and a world with no bodies in
// it has an honest empty answer rather than an error.
//
// The filter crosses the seam as a list of opaque user-data values, because that
// is the only identity `physics` has. Descendants are expanded here: filtering a
// Model means filtering its parts, and the module below has no tree to walk.

[[nodiscard]] physics::QueryFilter buildFilter(lua_State* L, int index, std::vector<u64>& storage)
{
    physics::QueryFilter filter;
    if (lua_isnoneornil(L, index))
        return filter;

    const RaycastQuery params = checkRaycastParams(L, index);
    filter.mode = params.mode == 1 ? physics::QueryFilter::Mode::Include : physics::QueryFilter::Mode::Exclude;

    World& w = world(L);
    const scene::PhysicsSync& sync = *services(L).physics;

    std::vector<core::InstanceId> descendants;
    for (const core::InstanceId id : params.filter) {
        if (!w.alive(id))
            continue;
        storage.push_back(sync.userDataOf(id));
        // "Descendants of a named instance are covered too, so filtering a Model
        // filters its parts" -- the property's own Doc.
        descendants.clear();
        w.collectDescendants(id, descendants);
        for (const core::InstanceId descendant : descendants)
            storage.push_back(sync.userDataOf(descendant));
    }
    filter.userData = storage;

    if (!params.collisionGroup.empty()) {
        const u16 group = w.collisionGroups().find(w.atoms().lookup(params.collisionGroup));
        if (group == scene::CollisionGroups::kInvalid)
            raise(L, LUAUG_TR("scene.err.unknown_collision_group"));
        filter.filterGroup = true;
        filter.group = static_cast<physics::CollisionGroup>(group);
    }
    return filter;
}

int workspaceRaycast(lua_State* L)
{
    (void)checkInstance(L, 1);
    const core::Vec3 origin = checkVector3(L, 2);
    const core::Vec3 direction = checkVector3(L, 3);

    scene::PhysicsSync* sync = services(L).physics;
    if (sync == nullptr) {
        lua_pushnil(L);
        return 1;
    }

    std::vector<u64> storage;
    const physics::QueryFilter filter = buildFilter(L, 4, storage);

    physics::RayD ray;
    ray.origin = core::toDVec3(origin);
    ray.direction = direction;

    physics::RayHit hit;
    if (!sync->backend().raycast(sync->worldHandle(), ray, filter, hit)) {
        lua_pushnil(L);
        return 1;
    }
    pushRaycastResult(L, sync->instanceOf(hit.userData), hit.position, hit.normal, hit.distance);
    return 1;
}

int workspaceSpherecast(lua_State* L)
{
    (void)checkInstance(L, 1);
    const core::Vec3 origin = checkVector3(L, 2);
    const auto radius = static_cast<f32>(luaL_checknumber(L, 3));
    const core::Vec3 direction = checkVector3(L, 4);

    scene::PhysicsSync* sync = services(L).physics;
    if (sync == nullptr) {
        lua_pushnil(L);
        return 1;
    }

    std::vector<u64> storage;
    const physics::QueryFilter filter = buildFilter(L, 5, storage);

    physics::RayD ray;
    ray.origin = core::toDVec3(origin);
    ray.direction = direction;

    physics::RayHit hit;
    if (!sync->backend().spherecast(sync->worldHandle(), ray, radius, filter, hit)) {
        lua_pushnil(L);
        return 1;
    }
    pushRaycastResult(L, sync->instanceOf(hit.userData), hit.position, hit.normal, hit.distance);
    return 1;
}

int workspaceGetBodiesInBox(lua_State* L)
{
    (void)checkInstance(L, 1);
    const core::CFrameD& frame = checkCFrame(L, 2);
    const core::Vec3 size = checkVector3(L, 3);

    scene::PhysicsSync* sync = services(L).physics;
    if (sync == nullptr) {
        lua_createtable(L, 0, 0);
        return 1;
    }

    std::vector<u64> storage;
    const physics::QueryFilter filter = buildFilter(L, 4, storage);

    std::vector<u64> hits;
    sync->backend().overlapBox(sync->worldHandle(), frame, size, filter, hits);

    // Built after the query rather than during it, because a hit that names an
    // instance the world has since retired is a hit with nothing to hand back.
    lua_createtable(L, static_cast<int>(hits.size()), 0);
    int written = 0;
    for (const u64 userData : hits) {
        const core::InstanceId id = sync->instanceOf(userData);
        if (!id.valid())
            continue;
        pushInstance(L, id);
        lua_rawseti(L, -2, ++written);
    }
    return 1;
}

// --- InputService and InputAction (M6) ---------------------------------------

int inputActionGetState(lua_State* L)
{
    const core::InstanceId id = checkInstance(L, 1);
    const World& w = world(L);
    const scene::InputActionComponent* action = w.inputActions().find(id);
    if (action == nullptr) {
        lua_pushboolean(L, 0);
        return 1;
    }

    // The value as of the last dispatch, in the currency the action's own type
    // names. A snapshot: two calls inside one tick agree, which is what makes a
    // recorded input stream able to answer with no hardware attached.
    switch (static_cast<input::ActionType>(action->type)) {
    case input::ActionType::Bool:
        lua_pushboolean(L, action->pressed ? 1 : 0);
        return 1;
    case input::ActionType::Direction1D:
        lua_pushnumber(L, static_cast<f64>(action->axis.x));
        return 1;
    case input::ActionType::Direction2D:
    case input::ActionType::ViewportPosition:
        pushVector2(L, core::Vec2{action->axis.x, action->axis.y});
        return 1;
    case input::ActionType::Direction3D:
        // Declared and not driveable in v1: no binding in api-design.md §2.4's
        // list names three axes. The zero vector rather than an error, because
        // the item exists so that code written today keeps meaning the same
        // thing when it does become driveable.
        pushVector3(L, action->axis);
        return 1;
    }

    lua_pushboolean(L, 0);
    return 1;
}

int inputActionGetPreferredBinding(lua_State* L)
{
    const core::InstanceId id = checkInstance(L, 1);
    World& w = world(L);

    // Without an argument, the family the player last used -- so a prompt
    // follows the pad the moment somebody picks one up.
    auto wanted = static_cast<input::DeviceType>(w.engineState().lastInputDeviceType);
    if (!lua_isnoneornil(L, 2)) {
        const scene::EnumValue item = checkEnumItem(L, 2);
        const scene::EnumDescriptor* descriptor = w.enums().find(item.enumId);
        if (descriptor == nullptr || w.atoms().text(descriptor->name) != "InputDeviceType")
            luaL_argerror(L, 2, "Enum.InputDeviceType");
        wanted = static_cast<input::DeviceType>(item.value);
    }

    // Child order, which is the order the bindings were created and therefore
    // the same on every run (R10). "First match" is a promise a prompt relies
    // on: the glyph must not change between two frames nothing touched.
    for (core::InstanceId bindingId = w.firstChild(id); bindingId.valid(); bindingId = w.nextSibling(bindingId)) {
        const scene::InputBindingComponent* binding = w.inputBindings().find(bindingId);
        if (binding == nullptr)
            continue;
        if (input::deviceOf(binding->keyCode) == wanted) {
            pushInstance(L, bindingId);
            return 1;
        }
    }

    lua_pushnil(L);
    return 1;
}

int inputServiceGetPointerPosition(lua_State* L)
{
    pushVector2(L, world(L).engineState().pointerPosition);
    return 1;
}

int inputServiceSetVirtualState(lua_State* L)
{
    const scene::EnumValue item = checkEnumItem(L, 2);
    if (item.enumId != scene::generated::KeyCodeEnumId)
        luaL_argerror(L, 2, "Enum.KeyCode");
    if (!input::isVirtual(item.value)) {
        // Writing to `Space` would be a script pretending to be a keyboard, and
        // nothing downstream could then tell the two apart. The four channels
        // the engine set aside are the seam, and this is the line that keeps it
        // one-way.
        luaL_argerror(L, 2, "Enum.KeyCode.Virtual1..Virtual4");
    }

    const auto value = static_cast<f32>(luaL_checknumber(L, 3));
    if (input::InputSystem* devices = services(L).input; devices != nullptr)
        devices->setVirtualState(item.value, value);
    return 0;
}

int inputServiceIsKeyDown(lua_State* L)
{
    const scene::EnumValue item = checkEnumItem(L, 2);
    if (item.enumId != scene::generated::KeyCodeEnumId)
        luaL_argerror(L, 2, "Enum.KeyCode");

    // Null in a world the host has not handed a device to -- a bare
    // `ScriptRuntime` in a test. Nothing is down in a world with no devices,
    // which is the same answer an unfocused window gives.
    const input::InputSystem* devices = services(L).input;
    lua_pushboolean(L, devices != nullptr && devices->isKeyDown(item.value));
    return 1;
}

// --- Sound and AudioService (M6) ---------------------------------------------

int soundPlay(lua_State* L)
{
    const core::InstanceId id = checkInstance(L, 1);
    if (scene::SoundComponent* sound = world(L).sounds().find(id); sound != nullptr) {
        // From wherever `TimePosition` is, which is 0 for a sound that has never
        // played and wherever `Pause` left it otherwise. Playing one that is
        // already playing is a no-op rather than a restart: restarting is
        // `TimePosition = 0` and then this, and a `Play` that silently rewound
        // would make a repeated call cut its own sound off.
        sound->playing = true;
    }
    return 0;
}

int soundPause(lua_State* L)
{
    const core::InstanceId id = checkInstance(L, 1);
    if (scene::SoundComponent* sound = world(L).sounds().find(id); sound != nullptr)
        sound->playing = false;
    return 0;
}

int soundStop(lua_State* L)
{
    const core::InstanceId id = checkInstance(L, 1);
    if (scene::SoundComponent* sound = world(L).sounds().find(id); sound != nullptr) {
        sound->playing = false;
        // Rewound, which is the whole difference from `Pause`. `Ended` is NOT
        // raised: it is a past-tense fact about reaching the end, and code that
        // awards something when a jingle finishes must not be fooled by one that
        // was cut off.
        sound->timePosition = 0.0;
    }
    return 0;
}

int audioServicePlayLocal(lua_State* L)
{
    World& w = world(L);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 2, &length);

    const scene::ClassId soundClass = w.classes().findId(w.atoms().intern("Sound"));
    if (soundClass == scene::InvalidClass) {
        lua_pushnil(L);
        return 1;
    }

    const core::InstanceId id = w.create(soundClass);
    if (scene::SoundComponent* sound = w.sounds().find(id); sound != nullptr) {
        sound->content = std::string(text, length);
        sound->playing = true;
    }

    // Parented to the service, which is what makes it 2D: positional is "parented
    // to a BasePart" and nothing else. It is a real instance in the tree rather
    // than a hidden voice, so a script can still turn it down on the tick it
    // starts -- and `AudioService`'s doc says keeping it past `Ended` is holding
    // a destroyed instance.
    (void)w.setParent(id, checkInstance(L, 1));
    pushInstance(L, id);
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

    {"StreamingService", "AddFocus", streamingAddFocus},
    {"StreamingService", "RemoveFocus", streamingRemoveFocus},
    {"StreamingService", "LoadAreaAsync", streamingLoadAreaAsync},

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

    {"InputAction", "GetState", inputActionGetState},
    {"InputAction", "GetPreferredBinding", inputActionGetPreferredBinding},
    {"InputService", "GetPointerPosition", inputServiceGetPointerPosition},
    {"InputService", "IsKeyDown", inputServiceIsKeyDown},
    {"InputService", "SetVirtualState", inputServiceSetVirtualState},

    {"AnimationPlayer", "LoadAnimation", animationPlayerLoadAnimation},

    {"TweenService", "Create", tweenServiceCreate},
    {"TweenService", "GetValue", tweenServiceGetValue},

    {"Sound", "Play", soundPlay},
    {"Sound", "Pause", soundPause},
    {"Sound", "Stop", soundStop},
    {"AudioService", "PlayLocal", audioServicePlayLocal},

    {"Workspace", "Raycast", workspaceRaycast},
    {"Workspace", "Spherecast", workspaceSpherecast},
    {"Workspace", "GetBodiesInBox", workspaceGetBodiesInBox},

    {"PhysicsService", "RegisterCollisionGroup", physicsRegisterCollisionGroup},
    {"PhysicsService", "CollisionGroupSetCollidable", physicsCollisionGroupSetCollidable},
    {"PhysicsService", "GetRegisteredCollisionGroups", physicsGetRegisteredCollisionGroups},
};

} // namespace

void registerServices(lua_State* L, core::InstanceId adopt)
{
    VmContext& ctx = context(L);
    ServiceState& state = *ctx.services;
    World& w = *ctx.world;
    core::AtomTable& atoms = w.atoms();

    state.messageOut = atoms.intern("MessageOut");
    state.instanceStreamedOut = atoms.intern("InstanceStreamedOut");
    state.areaLoaded = atoms.intern("AreaLoaded");
    state.loaded = atoms.intern("Loaded");
    state.runServiceClass = w.classes().findId(atoms.intern("RunService"));
    state.tagServiceClass = w.classes().findId(atoms.intern("TagService"));
    state.debugServiceClass = w.classes().findId(atoms.intern("DebugService"));
    state.streamingServiceClass = w.classes().findId(atoms.intern("StreamingService"));
    state.hotReloadServiceClass = w.classes().findId(atoms.intern("HotReloadService"));
    state.preReload = atoms.intern("PreReload");
    state.postReload = atoms.intern("PostReload");

    bindInstanceMethods(L, ServiceMethods);

    // **Adopted when there is one, created when there is not.** A `DataModel` is
    // the one instance a VM makes that outlives the VM: everything else under it
    // is found by `getServiceOfClass`, which has always been find-or-create, so
    // this line was the only thing standing between a world and a second runtime
    // bound to it.
    state.dataModel = w.alive(adopt) ? adopt : w.create(w.classes().findId(atoms.intern("DataModel")));

    // **Every service exists from boot**, and the paragraphs this replaced are
    // the argument for it. There were five of them: `Workspace` and
    // `ScriptService` because a script reaches one through a global and the
    // other is the mount point; `Lighting` because the renderer reads the
    // environment every frame whether or not anybody asked; `UIService` because
    // the frame lays out every `ScreenGui` under it; `AudioService` because the
    // mixer reads `MasterVolume`. That is not five decisions. It is one decision
    // found five times, each time after something had already read a service
    // nobody had created -- and M4 spent four milestones lighting every scene
    // with struct defaults because `Lighting` had not had its turn yet.
    //
    // **The editor is what turns it from a convenience into a rule.** A tree
    // whose shape depends on whether a script has called `GetService` is a tree
    // that changes when you press play, which is what was reported: `RunService`
    // and `HotReloadService` appearing in the Explorer on play and vanishing on
    // stop. Scripts do not run until play now (ADR 0058), so a project nobody
    // has played would show none of them at all -- and "the world you are
    // editing is the world" cannot survive half its services being invisible.
    //
    // **`Workspace`, then `Lighting`, then every other service by name.**
    //
    // The order is what the Explorer shows, so it is a decision about reading
    // rather than about construction -- which is why it is not registration
    // order. Registration order is a fact about which module was compiled in and
    // in what sequence its file was generated, and a person scanning a panel has
    // no way to know any of that.
    //
    // The two at the front are the two a scene IS: the world, and how it is lit.
    // They are also the two an editor opens into, and the only ones somebody
    // reaches for by position rather than by name. Past those, twelve names is a
    // list you scan alphabetically, so it is sorted alphabetically -- and a
    // service added next year lands where its name puts it without anybody
    // deciding where.
    //
    // Sorted rather than listed, because a list written by hand is a list that
    // goes stale, and this order is also the CREATION order and therefore part
    // of the world (R10): a comparison on names is the same on every machine,
    // where registration order is only the same by luck.
    //
    // `DataModel` is not among them: it carries `NotCreatable` rather than
    // `Service`, because it is the services' parent rather than one of them.
    const ClassId workspaceClass = w.classes().findId(atoms.intern("Workspace"));
    const ClassId lightingClass = w.classes().findId(atoms.intern("Lighting"));

    std::vector<ClassId> serviceClasses;
    for (ClassId id = 1; id < static_cast<ClassId>(w.classes().classCount()); ++id) {
        const scene::ClassDescriptor* descriptor = w.classes().find(id);
        if (descriptor != nullptr && hasFlag(descriptor->flags, scene::ClassFlags::Service))
            serviceClasses.push_back(id);
    }

    const auto rank = [&](ClassId id) {
        if (id == workspaceClass)
            return 0;
        if (id == lightingClass)
            return 1;
        return 2;
    };
    std::sort(serviceClasses.begin(), serviceClasses.end(), [&](ClassId a, ClassId b) {
        if (rank(a) != rank(b))
            return rank(a) < rank(b);
        const scene::ClassDescriptor* left = w.classes().find(a);
        const scene::ClassDescriptor* right = w.classes().find(b);
        if (left == nullptr || right == nullptr)
            return left != nullptr;
        return atoms.text(left->name) < atoms.text(right->name);
    });

    core::InstanceId workspace;
    for (const ClassId id : serviceClasses) {
        const core::InstanceId created = getServiceOfClass(L, id);
        if (id == workspaceClass)
            workspace = created;
    }

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

    const scene::EventDesc* descriptor = world(L).classes().findEvent(world(L).classOf(debug), state.messageOut);
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

void fireStreamedOut(lua_State* L, core::InstanceId instance)
{
    const core::InstanceId service = findServiceOfClass(L, services(L).streamingServiceClass);
    if (!service.valid())
        return;

    const scene::EventDesc* descriptor =
        world(L).classes().findEvent(world(L).classOf(service), services(L).instanceStreamedOut);
    if (descriptor == nullptr)
        return;

    pushInstance(L, instance);
    fireInstanceEvent(L, service, descriptor->slot, lua_gettop(L), 1);
    lua_pop(L, 1);
}

void fireAreaLoaded(lua_State* L, core::Vec3 position, f64 radius)
{
    const core::InstanceId service = findServiceOfClass(L, services(L).streamingServiceClass);
    if (!service.valid())
        return;

    const scene::EventDesc* descriptor =
        world(L).classes().findEvent(world(L).classOf(service), services(L).areaLoaded);
    if (descriptor == nullptr)
        return;

    pushVector3(L, position);
    lua_pushnumber(L, radius);
    fireInstanceEvent(L, service, descriptor->slot, lua_gettop(L) - 1, 2);
    lua_pop(L, 2);
}

void resumeAreaWaiters(lua_State* L, const std::function<bool(core::DVec3, f64)>& resident)
{
    ServiceState& state = services(L);
    if (state.areaWaiters.empty())
        return;

    // Collected first, for the reason `resumeChildWaiters` gives: a resumed
    // coroutine may park another waiter, and the vector it would push onto is
    // the one being walked.
    std::vector<ServiceState::AreaWaiter> ready;
    for (usize index = 0; index < state.areaWaiters.size();) {
        const ServiceState::AreaWaiter& waiter = state.areaWaiters[index];
        if (!resident(waiter.position, waiter.radius)) {
            ++index;
            continue;
        }
        ready.push_back(waiter);
        state.areaWaiters.erase(state.areaWaiters.begin() + static_cast<std::ptrdiff_t>(index));
    }

    for (const ServiceState::AreaWaiter& waiter : ready) {
        // The signal fires whether or not the coroutine survives the resume:
        // `AreaLoaded` is a fact about the world rather than a reply to the
        // caller, and a script that connected to it did not necessarily call
        // `LoadAreaAsync`.
        fireAreaLoaded(L, core::toVec3(waiter.position), waiter.radius);

        lua_getref(L, waiter.threadRef);
        lua_State* co = lua_tothread(L, -1);
        if (co == nullptr) {
            lua_pop(L, 1);
            (void)lua_unref(L, waiter.threadRef);
            continue;
        }
        const bool finished = resumeScheduled(L, co, 0);
        lua_pop(L, 1);
        if (finished)
            (void)lua_unref(L, waiter.threadRef);
    }
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

    const scene::EventDesc* descriptor = world(L).classes().findEvent(world(L).classOf(state.dataModel), state.loaded);
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
    for (usize index = 0; index < state.childWaiters.size();) {
        ChildWaiter& waiter = state.childWaiters[index];
        const bool satisfied = w.findFirstChild(waiter.parent, waiter.name).valid();
        const bool expired = waiter.hasTimeout && tick >= waiter.deadlineTick;

        if (!satisfied && !expired) {
            // Five sim-seconds, once, and then it keeps waiting. The timeout
            // form never warns however long its timeout: you said how long you
            // were prepared to wait.
            if (!waiter.hasTimeout && !waiter.warned &&
                static_cast<f64>(tick - waiter.scheduledTick) * timestep >= 5.0) {
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

    for (const ChildWaiter& waiter : ready) {
        lua_getref(L, waiter.threadRef);
        lua_State* co = lua_tothread(L, -1);
        if (co == nullptr) {
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

    for (const int ref : handlers) {
        // Its own coroutine, like a signal handler: a close handler must be
        // allowed to yield, and one that errors must not stop the others.
        lua_State* co = lua_newthread(L);
        lua_getref(L, ref);
        lua_xmove(L, co, 1);
        const bool finished = resumeScheduled(L, co, 0);
        if (!finished) {
            // Parked, which is the case D016 was about: before this, a handler
            // that yielded was cut off at the next drain rather than waited
            // for, and `architecture.md` §app promises a capped grace period.
            // Kept referenced so the host can ask whether it is still running.
            lua_pushvalue(L, -1);
            state.closePending.push_back(lua_ref(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        (void)lua_unref(L, ref);
    }
}

bool panelOpen(lua_State* L, std::string_view name)
{
    const core::NameAtom atom = world(L).atoms().lookup(name);
    if (!atom.valid())
        return false;
    const std::vector<core::NameAtom>& open = services(L).openPanels;
    return std::find(open.begin(), open.end(), atom) != open.end();
}

bool closeHandlersPending(lua_State* L)
{
    ServiceState& state = services(L);

    // Compacted as it goes, so a long grace period does not re-check threads
    // that finished on its first pass.
    std::vector<int> stillRunning;
    for (const int ref : state.closePending) {
        lua_getref(L, ref);
        lua_State* co = lua_tothread(L, -1);
        const int status = co == nullptr ? LUA_COFIN : lua_costatus(L, co);
        lua_pop(L, 1);

        if (status == LUA_COSUS || status == LUA_CONOR) {
            stillRunning.push_back(ref);
        }
        else {
            (void)lua_unref(L, ref);
        }
    }
    state.closePending.swap(stillRunning);
    return !state.closePending.empty();
}

void abandonCloseHandlers(lua_State* L)
{
    ServiceState& state = services(L);
    for (const int ref : state.closePending)
        (void)lua_unref(L, ref);
    state.closePending.clear();
}

} // namespace luaug::script
