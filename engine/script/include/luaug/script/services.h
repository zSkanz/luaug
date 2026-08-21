// The DataModel, the services, and the globals that reach them (api-design.md
// §2.1, §1.2).
//
// The tree the world boots with is `game`, with `Workspace` and `ScriptService`
// under it; every other service is created by its first `GetService` and is an
// ordinary child of `game` once it exists. That "created on demand, singleton
// thereafter" rule is the whole of the service concept, and it lives here rather
// than in `scene` because `scene` has no reason to know that some of its classes
// are singletons.
//
// The phase signals are fired from here too. `RunService.Heartbeat` and its four
// siblings are engine-raised events with no POD fact behind them: nothing in
// `scene` changed, the frame merely advanced, so there is no `Change` for the
// drain to convert and the scheduler enqueues them directly.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/log.h"
#include "luaug/core/math.h"
#include "luaug/core/name_atom.h"
#include "luaug/input/input.h"
#include "luaug/net/async_client.h"
#include "luaug/platform/event.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/physics_sync.h"
#include "luaug/script/animation.h"
#include "luaug/script/binding.h"
#include "luaug/script/reload_state.h"
#include "luaug/script/tweens.h"

#include <array>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

struct lua_State;

namespace luaug::script {

// Where a gizmo call goes. Null in a headless run, which is why
// `DebugService:DrawLine` is a silent no-op there rather than an error --
// debug drawing left in shared code must not fail a headless test.
//
// A sink rather than a direct call into `render`, because `script` does not
// depend on it: the renderer is L4 and this is L5, and the dependency would run
// the right way but would put a graphics API behind a scripting header for the
// sake of three functions.
struct GizmoSink
{
    void* user = nullptr;
    void (*line)(void* user, core::Vec3 a, core::Vec3 b, core::Color3 color) = nullptr;
    void (*box)(void* user, const core::CFrameD& frame, core::Vec3 size, core::Color3 color) = nullptr;
    void (*sphere)(void* user, core::Vec3 position, f32 radius, core::Color3 color) = nullptr;
};

// Named instrumentation counters. `GetStat` raises for a name nothing has
// published rather than answering zero: a misspelt stat is a bug in the caller,
// and a debug surface that answers zero hides that bug in the one place people
// are already confused.
struct StatTable
{
    // Insertion-ordered rather than hashed. Nothing iterates it today, but a
    // stats panel will, and R10 forbids a container from deciding what a panel
    // shows first.
    std::vector<std::pair<core::NameAtom, f64>> entries;
};

// The engine's own per-frame instrumentation, published by the host once a
// frame (architecture.md §9). Separate from `StatTable` because these are
// engine facts with fixed names rather than whatever a game chose to publish,
// and because a game must not be able to overwrite one -- `GetStat("FPS")`
// reads the engine's number or nothing at all.
//
// **Wall-clock derived, and therefore never readable by simulation code.** R10
// forbids the sim from seeing a clock; these exist for a human looking at an
// overlay, and a script that fed one back into the world would make its replay
// diverge. `luaug check` flags that in M3.
struct FrameStats
{
    f64 fps = 0.0;
    f64 frameTimeMs = 0.0;
    f64 drawCalls = 0.0;
    f64 physicsBodies = 0.0;
    f64 luaMemoryKb = 0.0;
    // The mixer's own two, and the first of them is the M6 gate's number: the
    // roadmap asks for "buffer underrun counter zero in a 60 s soak", and a
    // counter a script cannot read is a gate a human has to take on trust.
    f64 audioUnderruns = 0.0;
    f64 audioVoices = 0.0;
    f64 audioClipsLoaded = 0.0;
    f64 audioClipsMissing = 0.0;

    // How many of this frame's draws used a level of detail COARSER than zero
    // (roadmap M7: "basic LOD switching").
    //
    // Here because a selector nobody can see is a selector nobody can tell is
    // working. It costs nothing to compute -- the frame already walks its draws
    // to count them -- and it is the difference between "LOD switching shipped"
    // and "LOD switching shipped and is choosing something".
    f64 meshLodDraws = 0.0;
};

// A coroutine parked on `WaitForChild`. Kept apart from the timer list because
// the contract is about a *state* -- "a child of that name exists" -- and not
// about a deadline: a sibling renamed into the awaited name satisfies a waiter
// exactly as a newly parented child does, so nothing about it can be scheduled.
struct ChildWaiter
{
    core::InstanceId parent;
    core::NameAtom name;
    int threadRef = -1;

    bool hasTimeout = false;
    u64 deadlineTick = 0;

    u64 scheduledTick = 0;
    // The unbounded form warns once after five sim-seconds and keeps waiting.
    // The timeout form never warns, however long its timeout: you said how long
    // you were prepared to wait.
    bool warned = false;
};

// Per-VM. Held by `VmContext` as a pointer and owned by `ScriptRuntime`.
class ServiceState
{
public:
    // Invalid until `registerServices` runs. Every service is a child of it,
    // and `game` is the only way a script reaches it.
    core::InstanceId dataModel;

    GizmoSink gizmos;
    StatTable stats;
    FrameStats frameStats;

    // The overlay panels a script has opened, in the order it opened them. The
    // host reads this when it draws; a name outside the documented set never
    // reaches here, because `ShowPanel` raises on one.
    std::vector<core::NameAtom> openPanels;

    // `BindToClose` callbacks, by registry ref, in registration order.
    std::vector<int> closeHandlers;

    // The close handlers that yielded and are still parked, as thread refs.
    // Held only between `runCloseHandlers` and the end of the grace period.
    std::vector<int> closePending;
    bool shutdown = false;

    std::vector<ChildWaiter> childWaiters;

    // `StreamingService.LoadAreaAsync` (M7), the same shape as a child waiter
    // and for the same reason: the caller is parked on a condition the world
    // will satisfy later, and the host is what notices. What a teleport calls
    // before it moves the character, so the destination exists on arrival.
    struct AreaWaiter
    {
        core::DVec3 position;
        core::f64 radius = 0.0;
        int threadRef = -1;
        u64 scheduledTick = 0;
    };
    std::vector<AreaWaiter> areaWaiters;

    // `@std/net.request` (api-design.md 7), the same parking shape as the two
    // waiters above and for the same reason -- except that what satisfies it is
    // a socket rather than the world, so the condition is a TICKET rather than a
    // predicate over world state.
    struct NetWaiter
    {
        net::NetTicket ticket;
        int threadRef = -1;
    };
    std::vector<NetWaiter> netWaiters;

    // Created on the first `@std/net.request` and never before: it owns worker
    // THREADS, and a project that never touches the network should not be paying
    // for two of them. Owned here so it dies with the VM -- the coroutines
    // waiting on its tickets die with the VM too.
    std::unique_ptr<net::AsyncClient> netClient;

    // Resolved once at boot. `fireRunServiceEvent` runs four times a tick and
    // `publishMessage` runs per `print`; hashing a string literal on either path
    // is a cost with no reason to exist.
    core::NameAtom messageOut;
    core::NameAtom loaded;
    core::NameAtom preReload;
    core::NameAtom postReload;
    core::NameAtom instanceStreamedOut;
    core::NameAtom areaLoaded;
    scene::ClassId runServiceClass = 0;
    scene::ClassId tagServiceClass = 0;
    scene::ClassId debugServiceClass = 0;
    scene::ClassId hotReloadServiceClass = 0;
    scene::ClassId streamingServiceClass = 0;

    // The hot-reload bag (ADR 0024). Never null: `ScriptRuntime` owns one so
    // that `SaveState` is never a silent no-op, and the host substitutes its
    // own -- which outlives `WorldHost` -- when it intends the values to
    // survive a reload.
    ReloadState* reload = nullptr;

    // The physics mirror, or null in a build with no physics backend. Set by
    // the host, which owns it: `scene::PhysicsSync` is L3 and reachable from
    // here, but the instance belongs to the world's lifetime.
    //
    // Null is a real state and not an error -- `Workspace:Raycast` answers nil,
    // which is the same answer an empty world gives -- so every reader checks
    // rather than assuming.
    scene::PhysicsSync* physics = nullptr;

    // The input system `InputService` reads and, in exactly one place, writes:
    // `SetVirtualState` drives the four virtual channels, which is the seam that
    // lets a HUD button feed an action without becoming a second input model.
    // Null before the host hands it over. Same arrangement as `physics` and
    // `animation`: the system belongs to the host's lifetime and this is a view
    // onto it.
    input::InputSystem* input = nullptr;

    // The animation host, or null in a build with no render module. Same
    // arrangement and same rule as `physics` above: null is a real state, and a
    // track that has no host plays nothing rather than raising.
    scene::AnimationHost* animation = nullptr;

    // Every `AnimationTrack` handle this VM has handed out. Append-only: a
    // record is four bytes and a signal id, and `LoadAnimation` is a load-once
    // call rather than a per-frame one -- which is what its own doc says.
    std::vector<TrackRecord> animationTracks;

    // Every live tween in this VM (api-design.md §2.1). Per-VM rather than
    // process-global for the same reason everything else here is: two worlds in
    // one process must not share one.
    TweenSystem tweens;
};

// Creates `game` and the two services that exist from boot, installs the
// `game` and `workspace` globals, and binds every service method. Runs during
// boot, before the sandbox.
void registerServices(lua_State* L);

// What the host publishes each frame. Called between frames rather than inside
// one, so a stat never changes halfway through a tick that might read it.
void publishFrameStats(lua_State* L, const FrameStats& stats);

// `DebugService.MessageOut`, which carries every console message with its level
// (api-design.md §2.1). Called beside the log rather than from a log sink: the
// sink is a process-wide slot the host owns, and a signal system that competed
// for it would silently lose to whoever installed one last.
void publishMessage(lua_State* L, core::LogLevel level, std::string_view text);

// `StreamingService.InstanceStreamedOut`, one fire per instance that became a
// husk. A husk is REPARENTED TO NIL rather than destroyed, so the handle a
// script holds still resolves when the handler reads it (§4).
void fireStreamedOut(lua_State* L, core::InstanceId instance);

// Wakes every `LoadAreaAsync` whose area is now resident and fires
// `AreaLoaded` for it. `resident` is the host's answer, because whether a
// region is loaded is a question only the streaming host can answer and this
// module must not learn what a chunk is.
void resumeAreaWaiters(lua_State* L, const std::function<bool(core::DVec3, core::f64)>& resident);

// Enqueues one of `RunService`'s phase signals with its delta in seconds. The
// scheduler calls this at each resumption point; the fire drains like any other.
void fireRunServiceEvent(lua_State* L, core::NameAtom event, f64 delta);

// Enqueues `game.Loaded`. Fires once, after every entry script has had its first
// resumption (api-design.md §3).
void fireDataModelLoaded(lua_State* L);

// Enqueues `HotReloadService.PreReload` on the outgoing world, or `PostReload`
// on the world a reload built. A no-op when no script ever asked for the
// service, because then there is no instance and nothing can have connected.
void fireHotReloadEvent(lua_State* L, bool before);

// Points the bindings at the bag the host owns. Called before any script runs;
// the runtime's own bag is what they use until it is.
void setReloadState(lua_State* L, ReloadState* state);

// Wakes every `WaitForChild` whose awaited child now exists, and expires the
// ones whose timeout has passed. Called from the task-resume phase, because a
// waiter is a pending resumption like any other -- but keyed on a tree state
// rather than on a deadline, so it cannot live in the timer list.
void resumeChildWaiters(lua_State* L);

// Whether a `BindToClose` callback asked the run to end, or a script called
// `Shutdown`. The host polls it; nothing here can end a process.
[[nodiscard]] bool shutdownRequested(lua_State* L);

// Runs the registered `BindToClose` callbacks. They are given their timeout by
// the host and the shutdown proceeds when it expires, finished or not: a close
// handler is a chance to finish, never a veto.
void runCloseHandlers(lua_State* L);

// Whether any close handler is still parked. The host polls this while it
// spends the grace period `architecture.md` §app promises -- before M5 a
// handler that yielded was simply cut off at the next drain (D016).
[[nodiscard]] bool closeHandlersPending(lua_State* L);

// Whether a script has asked for an overlay panel by name
// (`DebugService:ShowPanel`). The host reads it to decide what to draw; M5's
// first reader is the physics wireframe, which is expensive enough that
// drawing it unasked would be a frame cost nobody chose.
[[nodiscard]] bool panelOpen(lua_State* L, std::string_view name);

// Lets go of whatever is still parked when the grace period runs out. The
// process is going away; what this releases is the reference, so the VM can be
// torn down without a live thread rooted in the registry.
void abandonCloseHandlers(lua_State* L);

} // namespace luaug::script
