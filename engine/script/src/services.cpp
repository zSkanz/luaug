#include "luaug/script/services.h"

#include "luaug/asset/terrain.h"
#include "luaug/asset/voxel_mesher.h"
#include "luaug/input/input.h"
#include "luaug/platform/event.h"
#include "luaug/scene/players.h"
#include "luaug/scene/voxel_fluid.h"
#include "luaug/scene/world.h"
#include "luaug/script/datatypes.h"
#include "luaug/script/instance_binding.h"
#include "luaug/script/remote.h"
#include "luaug/script/signals.h"
#include "luaug/script/tweens.h"

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
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
    if (name == "AudioClipsStreamed") {
        // Of `AudioClipsLoaded`, the ones long enough that the file stays
        // encoded and plays through a decoder rather than being held whole.
        lua_pushnumber(L, frame.audioClipsStreamed);
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
constexpr std::string_view Panels[] = {"Stats", "Scene", "Log", "Streaming", "Physics", "Terrain"};

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

// **The nearest terrain surface along a ray, asked of the voxels themselves**
// (ADR 0082). A terrain only has colliders near things that move, so a ray far
// from every body would pass through ground that has none; the field answers
// everywhere. Honours the instance filter: an excluded terrain is not hit, and
// an include list that does not name it leaves it out.
struct TerrainRayHit
{
    core::InstanceId terrain;
    core::DVec3 position;
    core::Vec3 normal;
    f64 distance = std::numeric_limits<f64>::max();
};

[[nodiscard]] TerrainRayHit raycastTerrains(lua_State* L, core::InstanceId workspace, core::Vec3 origin,
                                            core::Vec3 direction, const physics::QueryFilter& filter)
{
    TerrainRayHit best;
    const f64 reach = static_cast<f64>(core::length(direction));
    if (!(reach > 0.0))
        return best;
    World& w = world(L);
    const scene::PhysicsSync* sync = services(L).physics;
    w.terrains().forEach([&](core::InstanceId id, const scene::TerrainComponent& terrain) {
        if (terrain.field.empty() || !w.isAncestorOf(workspace, id))
            return;
        if (sync != nullptr) {
            const u64 mine = sync->userDataOf(id);
            const bool named = std::find(filter.userData.begin(), filter.userData.end(), mine) != filter.userData.end();
            if (filter.mode == physics::QueryFilter::Mode::Include ? !named : named)
                return;
        }
        const core::DVec3 local{static_cast<f64>(origin.x) - terrain.origin.x,
                                static_cast<f64>(origin.y) - terrain.origin.y,
                                static_cast<f64>(origin.z) - terrain.origin.z};
        const std::optional<asset::TerrainHit> hit = asset::raycastField(terrain.field, local, direction, reach);
        if (!hit.has_value() || hit->distance >= best.distance)
            return;
        best.terrain = id;
        best.position = core::DVec3{hit->position.x + terrain.origin.x, hit->position.y + terrain.origin.y,
                                    hit->position.z + terrain.origin.z};
        best.normal = hit->normal;
        best.distance = hit->distance;
    });
    return best;
}

int workspaceRaycast(lua_State* L)
{
    const core::InstanceId workspace = checkInstance(L, 1);
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
    const bool bodyHit = sync->backend().raycast(sync->worldHandle(), ray, filter, hit);
    const TerrainRayHit ground = raycastTerrains(L, workspace, origin, direction, filter);
    // Whichever is nearer. A terrain's own collider, where it has one, and
    // the field agree to within the mesh; the body wins a tie so a part lying
    // on the ground is what a ray at it meets.
    if (ground.terrain.valid() && (!bodyHit || ground.distance < static_cast<f64>(hit.distance))) {
        pushRaycastResult(L, ground.terrain, ground.position, ground.normal, static_cast<f32>(ground.distance));
        return 1;
    }
    if (!bodyHit) {
        lua_pushnil(L);
        return 1;
    }
    pushRaycastResult(L, sync->instanceOf(hit.userData), hit.position, hit.normal, hit.distance);
    return 1;
}

// **The plane's ray** (the 2D layer). The same `RaycastParams` as the 3D one,
// read the same way: the filter names instances and their descendants, and the
// group is the one the ray collides as.
int workspaceRaycast2D(lua_State* L)
{
    (void)checkInstance(L, 1);
    const core::Vec2 origin = checkVector2(L, 2);
    const core::Vec2 direction = checkVector2(L, 3);

    const scene::PhysicsSync2D* sync = services(L).physics2d;
    if (sync == nullptr) {
        lua_pushnil(L);
        return 1;
    }

    physics::Raycast2DFilter filter;
    std::vector<u64> storage;
    if (!lua_isnoneornil(L, 4)) {
        const RaycastQuery params = checkRaycastParams(L, 4);
        filter.include = params.mode == 1;
        World& w = world(L);
        std::vector<core::InstanceId> descendants;
        for (const core::InstanceId id : params.filter) {
            if (!w.alive(id))
                continue;
            storage.push_back(scene::PhysicsSync2D::userDataOf(id));
            descendants.clear();
            w.collectDescendants(id, descendants);
            for (const core::InstanceId descendant : descendants)
                storage.push_back(scene::PhysicsSync2D::userDataOf(descendant));
        }
        filter.userData = storage;
        if (!params.collisionGroup.empty()) {
            const u16 group = w.collisionGroups().find(w.atoms().lookup(params.collisionGroup));
            if (group == scene::CollisionGroups::kInvalid)
                raise(L, LUAUG_TR("scene.err.unknown_collision_group"));
            if (group < physics::kMaxCollisionGroups2D)
                filter.group = static_cast<physics::CollisionGroup2D>(group);
        }
    }

    const std::optional<scene::PhysicsSync2D::Hit> hit = sync->raycast(origin, direction, filter);
    if (!hit.has_value()) {
        lua_pushnil(L);
        return 1;
    }
    pushRaycastResult2D(L, hit->instance, hit->position, hit->normal, hit->distance);
    return 1;
}

// --- NavigationService (ADR 0089) ---------------------------------------------
//
// Every query hands the service's agent to the navigation first: the agent is
// scene state a script sets, and the mesh is built for it -- a different one
// throws the old mesh away, which is the property's documented cost.

[[nodiscard]] nav::INavigation* navigationFor(lua_State* L, core::InstanceId service)
{
    nav::INavigation* navigation = services(L).navigation;
    if (navigation == nullptr)
        return nullptr;
    if (const scene::NavigationComponent* agent = world(L).navigation().find(service); agent != nullptr) {
        navigation->setAgent(
            nav::NavAgent{agent->agentRadius, agent->agentHeight, agent->agentMaxClimb, agent->agentMaxSlope});
    }
    return navigation;
}

int navigationFindPath(lua_State* L)
{
    const core::InstanceId service = checkInstance(L, 1);
    const core::Vec3 from = checkVector3(L, 2);
    const core::Vec3 to = checkVector3(L, 3);
    nav::INavigation* navigation = navigationFor(L, service);
    const std::optional<nav::NavPath> path =
        navigation != nullptr ? navigation->findPath(core::toDVec3(from), core::toDVec3(to)) : std::nullopt;
    if (!path.has_value()) {
        lua_pushnil(L);
        lua_pushboolean(L, 0);
        return 2;
    }
    // A fresh array every call, so a caller may keep it and change it.
    lua_createtable(L, static_cast<int>(path->points.size()), 0);
    for (usize at = 0; at < path->points.size(); ++at) {
        pushVector3(L, core::toVec3(path->points[at]));
        lua_rawseti(L, -2, static_cast<int>(at) + 1);
    }
    lua_pushboolean(L, path->complete ? 1 : 0);
    return 2;
}

int navigationNearestPoint(lua_State* L)
{
    const core::InstanceId service = checkInstance(L, 1);
    const core::Vec3 point = checkVector3(L, 2);
    const auto reach = static_cast<f32>(luaL_optnumber(L, 3, 4.0));
    nav::INavigation* navigation = navigationFor(L, service);
    const std::optional<core::DVec3> found =
        navigation != nullptr ? navigation->nearestPoint(core::toDVec3(point), reach) : std::nullopt;
    if (!found.has_value())
        lua_pushnil(L);
    else
        pushVector3(L, core::toVec3(*found));
    return 1;
}

int navigationRaycast(lua_State* L)
{
    const core::InstanceId service = checkInstance(L, 1);
    const core::Vec3 from = checkVector3(L, 2);
    const core::Vec3 to = checkVector3(L, 3);
    nav::INavigation* navigation = navigationFor(L, service);
    const std::optional<core::DVec3> stop =
        navigation != nullptr ? navigation->raycast(core::toDVec3(from), core::toDVec3(to)) : std::nullopt;
    if (!stop.has_value())
        lua_pushnil(L);
    else
        pushVector3(L, core::toVec3(*stop));
    return 1;
}

int navigationBuildRegion(lua_State* L)
{
    const core::InstanceId service = checkInstance(L, 1);
    const core::Vec3 minimum = checkVector3(L, 2);
    const core::Vec3 maximum = checkVector3(L, 3);
    nav::INavigation* navigation = navigationFor(L, service);
    const usize built =
        navigation != nullptr ? navigation->buildRegion(core::toDVec3(minimum), core::toDVec3(maximum)) : 0;
    lua_pushnumber(L, static_cast<double>(built));
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

// --- ParticleEmitter (F2) ------------------------------------------------------

// Adds to the emitter's running total rather than spawning anything: the
// particles are the renderer's, and it spawns the difference the next frame it
// looks. That keeps a burst a fact about the world -- hashed, saved, replicated
// -- and the particles a picture of it.
int particleEmitterEmit(lua_State* L)
{
    const core::InstanceId self = checkInstance(L, 1);
    const double requested = luaL_checknumber(L, 2);
    scene::ParticleEmitterComponent* emitter = world(L).particleEmitters().find(self);
    if (emitter == nullptr || !std::isfinite(requested))
        return 0;
    // A thousand a call is more than any one effect needs and few enough that a
    // loop calling it every tick cannot bury the frame.
    const auto count = static_cast<core::u64>(std::clamp(std::floor(requested), 0.0, 1000.0));
    emitter->emitted += count;
    return 0;
}

// --- NetworkService and Player (N1) -------------------------------------------

int networkServiceGetPlayers(lua_State* L)
{
    const core::InstanceId self = checkInstance(L, 1);
    const World& w = world(L);
    std::vector<core::InstanceId> players;
    // Child order is join order: the engine parents each player as it arrives.
    for (core::InstanceId child = w.firstChild(self); child.valid(); child = w.nextSibling(child)) {
        if (w.players().find(child) != nullptr && !w.destroyed(child))
            players.push_back(child);
    }
    lua_createtable(L, static_cast<int>(players.size()), 0);
    for (usize index = 0; index < players.size(); ++index) {
        pushInstance(L, players[index]);
        lua_rawseti(L, -2, static_cast<int>(index) + 1);
    }
    return 1;
}

int playerGetIntent(lua_State* L)
{
    const core::InstanceId self = checkInstance(L, 1);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 2, &length);
    const World& w = world(L);
    const scene::PlayerComponent* player = w.players().find(self);
    const core::NameAtom name = w.atoms().lookup(std::string_view{text, length});
    if (player != nullptr && name.id != 0) {
        for (const scene::PlayerIntent& intent : player->intents) {
            if (!(intent.action == name))
                continue;
            // The same switch `InputAction:GetState` answers through, so the
            // two can never disagree about what a value looks like.
            switch (static_cast<input::ActionType>(intent.type)) {
            case input::ActionType::Bool:
                lua_pushboolean(L, intent.pressed ? 1 : 0);
                return 1;
            case input::ActionType::Direction1D:
                lua_pushnumber(L, static_cast<f64>(intent.axis.x));
                return 1;
            case input::ActionType::Direction2D:
            case input::ActionType::ViewportPosition:
                pushVector2(L, core::Vec2{intent.axis.x, intent.axis.y});
                return 1;
            case input::ActionType::Direction3D:
                pushVector3(L, intent.axis);
                return 1;
            }
        }
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

// --- VoxelService (V1) ---------------------------------------------------------
//
// A block world: integer block coordinates in, block ids out. Every write bumps
// `revision`, which is the one signal the renderer and the physics mirror read.

scene::VoxelComponent& voxelsOf(lua_State* L)
{
    const core::InstanceId self = checkInstance(L, 1);
    scene::VoxelComponent* voxels = world(L).voxels().find(self);
    if (voxels == nullptr) {
        const core::I18nArg args[] = {{"className", std::string_view{"VoxelService"}}};
        raise(L, LUAUG_TR("scene.err.unknown_service"), args);
    }
    return *voxels;
}

// A block coordinate from a `vector`: each component rounded DOWN, so `(0.9,
// 0, 0)` is block 0 and `(-0.1, 0, 0)` is block -1 -- the same rule
// `WorldToBlock` uses, which is what makes the two agree.
struct BlockCoord
{
    core::i32 x = 0;
    core::i32 y = 0;
    core::i32 z = 0;
};

BlockCoord checkBlockCoord(lua_State* L, int index)
{
    const core::Vec3 v = checkVector3(L, index);
    return BlockCoord{static_cast<core::i32>(std::floor(v.x)), static_cast<core::i32>(std::floor(v.y)),
                      static_cast<core::i32>(std::floor(v.z))};
}

asset::BlockId checkBlockId(lua_State* L, int index, const scene::VoxelComponent& voxels)
{
    const lua_Integer id = luaL_checkinteger(L, index);
    if (id < 0 || static_cast<core::u64>(id) > voxels.types.size()) {
        const core::I18nArg args[] = {{"id", static_cast<core::i64>(id)},
                                      {"count", static_cast<core::i64>(voxels.types.size())}};
        raise(L, LUAUG_TR("scene.err.voxel_unknown_block"), args);
    }
    return static_cast<asset::BlockId>(id);
}

int voxelSetBlockTextures(lua_State* L)
{
    scene::VoxelComponent& voxels = voxelsOf(L);
    const asset::BlockId id = checkBlockId(L, 2, voxels);
    if (id == asset::AirBlock) {
        const core::I18nArg args[] = {{"id", static_cast<core::i64>(id)},
                                      {"count", static_cast<core::i64>(voxels.types.size())}};
        raise(L, LUAUG_TR("scene.err.voxel_unknown_block"), args);
    }
    World& w = world(L);
    const auto image = [&](int index, core::NameAtom fallback) {
        if (lua_isnoneornil(L, index))
            return fallback;
        usize length = 0;
        const char* text = luaL_checklstring(L, index, &length);
        return length == 0 ? core::NameAtom{} : w.atoms().intern(std::string_view{text, length});
    };
    const core::NameAtom top = image(3, core::NameAtom{});
    const core::NameAtom side = image(4, top);
    const core::NameAtom bottom = image(5, side);
    scene::VoxelBlockType& type = voxels.types[id - 1u];
    if (!(type.texture == top) || !(type.sideTexture == side) || !(type.bottomTexture == bottom)) {
        type.texture = top;
        type.sideTexture = side;
        type.bottomTexture = bottom;
        voxels.revision += 1;
    }
    return 0;
}

int voxelSetBlockOpacity(lua_State* L)
{
    scene::VoxelComponent& voxels = voxelsOf(L);
    const asset::BlockId id = checkBlockId(L, 2, voxels);
    if (id == asset::AirBlock) {
        const core::I18nArg args[] = {{"id", static_cast<core::i64>(id)},
                                      {"count", static_cast<core::i64>(voxels.types.size())}};
        raise(L, LUAUG_TR("scene.err.voxel_unknown_block"), args);
    }
    const scene::EnumValue opacity = checkEnumItem(L, 3);
    if (opacity.enumId != scene::generated::BlockOpacityEnumId)
        luaL_argerror(L, 3, "Enum.BlockOpacity");
    const double transparency = luaL_optnumber(L, 4, 0.5);
    scene::VoxelBlockType& type = voxels.types[id - 1u];
    type.opacity = opacity.value;
    type.transparency = static_cast<f32>(std::clamp(std::isfinite(transparency) ? transparency : 0.5, 0.0, 1.0));
    voxels.revision += 1;
    return 0;
}

int voxelRegisterBlock(lua_State* L)
{
    scene::VoxelComponent& voxels = voxelsOf(L);
    usize length = 0;
    const char* text = luaL_checklstring(L, 2, &length);
    const core::Color3 color = checkColor3(L, 3);
    const core::Color3 side = lua_isnoneornil(L, 4) ? color : checkColor3(L, 4);
    const core::Color3 bottom = lua_isnoneornil(L, 5) ? side : checkColor3(L, 5);
    const core::NameAtom name = world(L).atoms().intern(std::string_view{text, length});
    for (usize at = 0; at < voxels.types.size(); ++at) {
        if (voxels.types[at].name == name) {
            scene::VoxelBlockType& type = voxels.types[at];
            if (!(type.color == color) || !(type.side == side) || !(type.bottom == bottom)) {
                type.color = color;
                type.side = side;
                type.bottom = bottom;
                voxels.revision += 1;
            }
            lua_pushinteger(L, static_cast<int>(at + 1));
            return 1;
        }
    }
    // Four thousand and ninety-five types is the id's range -- the rest of a
    // stored id is the block's state (`asset::BlockTypeMask`) -- and refusing
    // past it is kinder than wrapping an id onto somebody else's block.
    if (voxels.types.size() >= asset::MaxBlockType) {
        const core::I18nArg args[] = {{"id", static_cast<core::i64>(voxels.types.size() + 1)},
                                      {"count", static_cast<core::i64>(voxels.types.size())}};
        raise(L, LUAUG_TR("scene.err.voxel_unknown_block"), args);
    }
    voxels.types.push_back(scene::VoxelBlockType{name, color, side, bottom, {}, {}, {}, 0, 0.5f});
    voxels.revision += 1;
    lua_pushinteger(L, static_cast<int>(voxels.types.size()));
    return 1;
}

int voxelGetBlockId(lua_State* L)
{
    const scene::VoxelComponent& voxels = voxelsOf(L);
    usize length = 0;
    const char* text = luaL_checklstring(L, 2, &length);
    const core::NameAtom name = world(L).atoms().lookup(std::string_view{text, length});
    for (usize at = 0; at < voxels.types.size(); ++at) {
        if (name.valid() && voxels.types[at].name == name) {
            lua_pushinteger(L, static_cast<int>(at + 1));
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

int voxelSetBlockFluid(lua_State* L)
{
    scene::VoxelComponent& voxels = voxelsOf(L);
    const asset::BlockId id = checkBlockId(L, 2, voxels);
    if (id == asset::AirBlock) {
        const core::I18nArg args[] = {{"id", static_cast<core::i64>(id)},
                                      {"count", static_cast<core::i64>(voxels.types.size())}};
        raise(L, LUAUG_TR("scene.err.voxel_unknown_block"), args);
    }
    const lua_Integer reach = luaL_checkinteger(L, 3);
    const lua_Integer ticks = luaL_optinteger(L, 4, 5);
    if (reach < 0 || reach > static_cast<lua_Integer>(asset::MaxFluidReach)) {
        const core::I18nArg args[] = {{"reach", static_cast<core::i64>(reach)},
                                      {"max", static_cast<core::i64>(asset::MaxFluidReach)}};
        raise(L, LUAUG_TR("scene.err.voxel_fluid_reach"), args);
    }
    if (ticks < 1 || ticks > 65535) {
        const core::I18nArg args[] = {{"ticks", static_cast<core::i64>(ticks)}};
        raise(L, LUAUG_TR("scene.err.voxel_fluid_ticks"), args);
    }
    scene::VoxelBlockType& type = voxels.types[id - 1u];
    type.fluidReach = static_cast<core::u8>(reach);
    type.fluidTicks = static_cast<core::u32>(ticks);
    voxels.revision += 1;
    // A type that just became a fluid has blocks that should start moving.
    scene::wakeAllFluids(voxels);
    return 0;
}

int voxelSetFluidReaction(lua_State* L)
{
    scene::VoxelComponent& voxels = voxelsOf(L);
    const asset::BlockId from = checkBlockId(L, 2, voxels);
    const asset::BlockId touching = checkBlockId(L, 3, voxels);
    const asset::BlockId result = checkBlockId(L, 4, voxels);
    // Both sides have to be fluids: a reaction is what moving fluid does, and
    // one on a solid would never be looked at.
    if (!scene::isFluidType(voxels, from) || !scene::isFluidType(voxels, touching)) {
        const core::I18nArg args[] = {{"from", static_cast<core::i64>(from)},
                                      {"touching", static_cast<core::i64>(touching)}};
        raise(L, LUAUG_TR("scene.err.voxel_reaction_not_fluid"), args);
    }
    scene::setFluidReaction(voxels, from, touching, result);
    // Fluids already touching are due to react.
    scene::wakeAllFluids(voxels);
    return 0;
}

int voxelSetBlock(lua_State* L)
{
    scene::VoxelComponent& voxels = voxelsOf(L);
    const BlockCoord at = checkBlockCoord(L, 2);
    const asset::BlockId id = checkBlockId(L, 3, voxels);
    const bool changed = voxels.grid.set(at.x, at.y, at.z, id);
    if (changed) {
        voxels.revision += 1;
        scene::wakeFluids(voxels, at.x, at.y, at.z);
    }
    lua_pushboolean(L, changed ? 1 : 0);
    return 1;
}

int voxelGetBlock(lua_State* L)
{
    const scene::VoxelComponent& voxels = voxelsOf(L);
    const BlockCoord at = checkBlockCoord(L, 2);
    // The TYPE: a fluid's level is a state of its block, not a different block.
    lua_pushinteger(L, static_cast<int>(asset::blockTypeOf(voxels.grid.get(at.x, at.y, at.z))));
    return 1;
}

int voxelGetFluidDepth(lua_State* L)
{
    const scene::VoxelComponent& voxels = voxelsOf(L);
    const BlockCoord at = checkBlockCoord(L, 2);
    const asset::BlockId id = voxels.grid.get(at.x, at.y, at.z);
    const asset::BlockId type = asset::blockTypeOf(id);
    if (id == asset::AirBlock || !scene::isFluidType(voxels, type)) {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    const asset::BlockId above = voxels.grid.get(at.x, at.y + 1, at.z);
    lua_pushnumber(L, static_cast<double>(asset::fluidSurface(id, above, voxels.types[type - 1u].fluidReach)));
    return 1;
}

int voxelFillBlocks(lua_State* L)
{
    scene::VoxelComponent& voxels = voxelsOf(L);
    const BlockCoord from = checkBlockCoord(L, 2);
    const BlockCoord to = checkBlockCoord(L, 3);
    const asset::BlockId id = checkBlockId(L, 4, voxels);
    const core::u32 changed = voxels.grid.fill(from.x, from.y, from.z, to.x, to.y, to.z, id);
    if (changed > 0) {
        voxels.revision += 1;
        scene::wakeFluidsInBox(voxels, from.x, from.y, from.z, to.x, to.y, to.z);
    }
    lua_pushinteger(L, static_cast<int>(changed));
    return 1;
}

int voxelClear(lua_State* L)
{
    scene::VoxelComponent& voxels = voxelsOf(L);
    if (voxels.grid.chunkCount() > 0) {
        voxels.grid.clear();
        voxels.revision += 1;
    }
    voxels.fluidWakes.clear();
    return 0;
}

int voxelWorldToBlock(lua_State* L)
{
    const scene::VoxelComponent& voxels = voxelsOf(L);
    const core::Vec3 position = checkVector3(L, 2);
    const f32 size = voxels.blockSize;
    pushVector3(
        L, core::Vec3{std::floor(position.x / size), std::floor(position.y / size), std::floor(position.z / size)});
    return 1;
}

int voxelBlockToWorld(lua_State* L)
{
    const scene::VoxelComponent& voxels = voxelsOf(L);
    const BlockCoord at = checkBlockCoord(L, 2);
    const f32 size = voxels.blockSize;
    pushVector3(L, core::Vec3{(static_cast<f32>(at.x) + 0.5f) * size, (static_cast<f32>(at.y) + 0.5f) * size,
                              (static_cast<f32>(at.z) + 0.5f) * size});
    return 1;
}

// Amanatides and Woo's traversal: step from block to block along the ray,
// always into whichever neighbour the ray reaches first, so no block it passes
// through is skipped and none it misses is visited.
int voxelRaycast(lua_State* L)
{
    const scene::VoxelComponent& voxels = voxelsOf(L);
    const core::Vec3 origin = checkVector3(L, 2);
    const core::Vec3 direction = checkVector3(L, 3);
    // A fluid is passed through: a pickaxe swung at a lake bed hits the bed.
    // (An array rather than a `std::vector<bool>`, which packs its bits and
    // cannot be viewed as a span.)
    const core::usize count = voxels.types.size() + 1;
    const std::unique_ptr<bool[]> passable = std::make_unique<bool[]>(count);
    for (core::usize type = 0; type < voxels.types.size(); ++type)
        passable[type + 1] = voxels.types[type].fluidReach > 0;
    // The direction's length is the reach, as `Workspace:Raycast` has it.
    const std::optional<asset::VoxelHit> hit = asset::raycastVoxels(
        voxels.grid, voxels.blockSize,
        core::DVec3{static_cast<f64>(origin.x), static_cast<f64>(origin.y), static_cast<f64>(origin.z)}, direction,
        static_cast<f64>(core::length(direction)), std::span<const bool>{passable.get(), count});
    if (!hit.has_value()) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
    pushVector3(L, core::Vec3{static_cast<f32>(hit->block[0]), static_cast<f32>(hit->block[1]),
                              static_cast<f32>(hit->block[2])});
    pushVector3(
        L, core::Vec3{static_cast<f32>(hit->face[0]), static_cast<f32>(hit->face[1]), static_cast<f32>(hit->face[2])});
    return 2;
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
    {"NetworkService", "GetPlayers", networkServiceGetPlayers},
    {"Player", "GetIntent", playerGetIntent},
    {"ParticleEmitter", "Emit", particleEmitterEmit},
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
    {"Workspace", "Raycast2D", workspaceRaycast2D},
    {"NavigationService", "FindPath", navigationFindPath},
    {"NavigationService", "NearestPoint", navigationNearestPoint},
    {"NavigationService", "Raycast", navigationRaycast},
    {"NavigationService", "BuildRegion", navigationBuildRegion},
    {"Workspace", "Spherecast", workspaceSpherecast},
    {"Workspace", "GetBodiesInBox", workspaceGetBodiesInBox},

    {"PhysicsService", "RegisterCollisionGroup", physicsRegisterCollisionGroup},
    {"PhysicsService", "CollisionGroupSetCollidable", physicsCollisionGroupSetCollidable},
    {"PhysicsService", "GetRegisteredCollisionGroups", physicsGetRegisteredCollisionGroups},

    {"VoxelService", "RegisterBlock", voxelRegisterBlock},
    {"VoxelService", "GetBlockId", voxelGetBlockId},
    {"VoxelService", "SetBlockTextures", voxelSetBlockTextures},
    {"VoxelService", "SetBlockOpacity", voxelSetBlockOpacity},
    {"VoxelService", "SetBlockFluid", voxelSetBlockFluid},
    {"VoxelService", "SetFluidReaction", voxelSetFluidReaction},
    {"VoxelService", "GetFluidDepth", voxelGetFluidDepth},
    {"VoxelService", "SetBlock", voxelSetBlock},
    {"VoxelService", "GetBlock", voxelGetBlock},
    {"VoxelService", "FillBlocks", voxelFillBlocks},
    {"VoxelService", "Clear", voxelClear},
    {"VoxelService", "WorldToBlock", voxelWorldToBlock},
    {"VoxelService", "BlockToWorld", voxelBlockToWorld},
    {"VoxelService", "Raycast", voxelRaycast},
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
    bindInstanceMethods(L, remoteMethodBindings());

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
