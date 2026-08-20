#include "luaug/script/signals.h"

#include "luaug/core/log.h"
#include "luaug/scene/world.h"
#include "luaug/script/instance_binding.h"
#include "luaug/script/services.h"

#include <lua.h>
#include <lualib.h>

#include <cstddef>
#include <string>
#include <utility>

namespace luaug::script {
namespace {

[[nodiscard]] SignalSystem& system(lua_State* L) noexcept
{
    return *context(L).signals;
}

[[nodiscard]] scene::World& world(lua_State* L) noexcept
{
    return *context(L).world;
}

[[nodiscard]] u64 ownerKey(core::InstanceId id) noexcept
{
    return (static_cast<u64>(id.index) << 32) | static_cast<u64>(id.generation);
}

// --- Userdata ----------------------------------------------------------------

void pushSignal(lua_State* L, SignalId id)
{
    void* memory = lua_newuserdatataggedwithmetatable(L, sizeof(SignalId), static_cast<int>(UserdataTag::Signal));
    *static_cast<SignalId*>(memory) = id;
}

void pushConnection(lua_State* L, ConnectionId id)
{
    void* memory =
        lua_newuserdatataggedwithmetatable(L, sizeof(ConnectionId), static_cast<int>(UserdataTag::Connection));
    *static_cast<ConnectionId*>(memory) = id;
}

[[nodiscard]] SignalId* toSignalId(lua_State* L, int index) noexcept
{
    return static_cast<SignalId*>(lua_touserdatatagged(L, index, static_cast<int>(UserdataTag::Signal)));
}

[[nodiscard]] SignalRecord& checkSignal(lua_State* L, int index)
{
    SignalId* id = toSignalId(L, index);
    if (id == nullptr)
        luaL_checkudatatagged(L, index, static_cast<int>(UserdataTag::Signal));

    SignalRecord* record = system(L).signals.find(*id);
    if (record == nullptr) {
        // The slot was freed and its generation bumped. A signal handle only
        // goes stale when its owning instance was retired, so this is the same
        // condition `Instance` reports and it gets the same key.
        raise(L, LUAUG_TR("script.err.instance_dead"));
    }
    return *record;
}

// --- Subscription bookkeeping ------------------------------------------------

// A property write is quiet unless something is listening (architecture.md §4),
// and this is the switch. Called whenever a connection list becomes empty or
// stops being empty, which are the only two transitions that matter.
void syncPropertySubscription(lua_State* L, const SignalRecord& record)
{
    if (record.kind != SignalKind::PropertyChanged || !world(L).alive(record.owner))
        return;

    bool anyLive = false;
    for (const ConnectionId id : record.connections) {
        const ConnectionRecord* connection = system(L).connections.find(id);
        if (connection != nullptr && connection->connected) {
            anyLive = true;
            break;
        }
    }
    world(L).setPropertySubscribed(record.owner, core::NameAtom{record.member}, anyLive && !record.closed);
}

void disconnectRecord(lua_State* L, ConnectionId id)
{
    SignalSystem& sys = system(L);
    ConnectionRecord* connection = sys.connections.find(id);
    if (connection == nullptr || !connection->connected)
        return;

    connection->connected = false;
    if (connection->ref > 0)
        connection->ref = lua_unref(L, connection->ref);

    const SignalId signal = connection->signal;
    if (SignalRecord* record = sys.signals.find(signal)) {
        for (usize index = 0; index < record->connections.size(); ++index) {
            if (record->connections[index] == id) {
                record->connections.erase(record->connections.begin() + static_cast<std::ptrdiff_t>(index));
                break;
            }
        }
        // Read back through `find`: `syncPropertySubscription` needs the record
        // after the erase, and the erase cannot have moved it.
        syncPropertySubscription(L, *record);
    }
    // The connection record itself is kept until nothing can name it: a fire
    // already on the queue holds this id in its snapshot and re-validates it,
    // and a freed slot could be handed to a later connection whose generation
    // would then collide with the snapshot's. Erasing here is safe only because
    // the generation makes the stale id unresolvable.
    sys.connections.erase(id);
}

void closeSignal(lua_State* L, SignalId id)
{
    SignalSystem& sys = system(L);
    SignalRecord* record = sys.signals.find(id);
    if (record == nullptr || record->closed)
        return;

    record->closed = true;
    // Copied, because `disconnectRecord` erases from the very vector this walks.
    const std::vector<ConnectionId> live = record->connections;
    for (const ConnectionId connection : live)
        disconnectRecord(L, connection);

    if (record->kind == SignalKind::PropertyChanged && world(L).alive(record->owner))
        world(L).setPropertySubscribed(record->owner, core::NameAtom{record->member}, false);
}

// --- The argument arena ------------------------------------------------------
//
// One Luau table holds every pending fire's arguments, so they stay GC-rooted
// for exactly as long as the queue needs them and no longer. Slots are handed
// out monotonically within a drain cycle and the top resets once the queue
// empties, which is the only moment at which no window is live.

[[nodiscard]] int pushArena(lua_State* L)
{
    lua_getref(L, system(L).argArenaRef);
    return lua_gettop(L);
}

// Moves `count` values starting at `first` off `L`'s stack into the arena.
// Returns the 1-based base index of the window.
[[nodiscard]] u32 captureArgumentsImpl(lua_State* L, int first, int count)
{
    SignalSystem& sys = system(L);
    if (count <= 0)
        return 0;

    const int arena = pushArena(L);
    const u32 base = sys.argArenaTop + 1;
    for (int index = 0; index < count; ++index) {
        lua_pushvalue(L, first + index);
        lua_rawseti(L, arena, static_cast<int>(base) + index);
    }
    sys.argArenaTop += static_cast<u32>(count);
    lua_pop(L, 1);
    return base;
}

// Pushes the window onto `target` and releases the arena slots. `target` is a
// handler's own coroutine, so the values are moved rather than copied twice.
void releaseArgumentsImpl(lua_State* L, lua_State* target, u32 base, u32 count)
{
    if (count == 0)
        return;

    const int arena = pushArena(L);
    for (u32 index = 0; index < count; ++index)
        lua_rawgeti(L, arena, static_cast<int>(base + index));
    lua_xmove(L, target, static_cast<int>(count));

    for (u32 index = 0; index < count; ++index) {
        lua_pushnil(L);
        lua_rawseti(L, arena, static_cast<int>(base + index));
    }
    lua_pop(L, 1);
}

void dropArgumentsImpl(lua_State* L, u32 base, u32 count)
{
    if (count == 0)
        return;

    const int arena = pushArena(L);
    for (u32 index = 0; index < count; ++index) {
        lua_pushnil(L);
        lua_rawseti(L, arena, static_cast<int>(base + index));
    }
    lua_pop(L, 1);
}

// --- Enqueuing ---------------------------------------------------------------

// The depth an entry raised right now carries: a handler running at depth *d*
// raises entries at *d*+1, and anything raised outside a drain starts at 0.
[[nodiscard]] u32 raisedDepth(const SignalSystem& sys) noexcept
{
    return sys.draining ? sys.depth + 1 : 0;
}

// Publishes an engine message on `MessageOut` unless one is already being
// published. See `SignalSystem::reporting` for the cycle this breaks.
void report(lua_State* L, core::LogLevel level, const std::string& message)
{
    core::logText(level, message);

    SignalSystem& sys = system(L);
    if (sys.reporting)
        return;
    sys.reporting = true;
    publishMessage(L, level, message);
    sys.reporting = false;
}

void logDepthExceeded(lua_State* L)
{
    // §2.1: the re-entrancy cap's dropped-fire log comes through `MessageOut`
    // like every other console message. A log line a script cannot observe is
    // a log line that cannot be tested from a spec.
    // Key-PREFIXED, like every engine message: §2.1 promises that a
    // `MessageOut` handler matching on a key substring sees engine output while
    // one matching prose sees script output, and `Catalog::format` alone drops
    // the key that makes the first half of that true.
    report(L, core::LogLevel::Warn, core::formatKeyPrefixed(LUAUG_TR("script.err.reentrancy_limit")));
}

// `first` addresses the first of `count` argument values on `L`'s stack.
//
// `depthOverride` is for an engine message reporting on the handler it came
// from: the report is *about* that handler rather than raised by it, so it
// carries the handler's own depth. Without that, the re-entrancy cap's own log
// is raised at depth 11, dropped by the very cap it is describing, and the
// diagnostic disappears exactly when it matters -- which is what the first run
// of the conformance suite caught.
void enqueueFireAt(lua_State* L, SignalId id, int first, int count, const u32* depthOverride)
{
    SignalSystem& sys = system(L);
    SignalRecord* record = sys.signals.find(id);
    if (record == nullptr || record->closed)
        return;

    const u32 depth = depthOverride != nullptr ? *depthOverride : raisedDepth(sys);
    if (depth > MaxDeferredDepth) {
        // Dropped at enqueue rather than at invocation, so it never takes a
        // queue slot and the log lands while the raiser is still on the stack.
        logDepthExceeded(L);
        return;
    }

    // The connection list AS IT IS NOW. A connection made after this does not
    // run for this fire, which is the half of §3.1 people actually rely on.
    DeferredEntry entry;
    entry.kind = EntryKind::Fire;
    entry.depth = depth;
    entry.signal = id;
    entry.snapshotBase = static_cast<u32>(sys.snapshots.size());
    entry.snapshotCount = static_cast<u32>(record->connections.size());
    sys.snapshots.insert(sys.snapshots.end(), record->connections.begin(), record->connections.end());
    entry.argCount = static_cast<u32>(count < 0 ? 0 : count);
    entry.argBase = captureArgumentsImpl(L, first, count);
    sys.queue.push_back(entry);
}

void enqueueFire(lua_State* L, SignalId id, int first, int count)
{
    enqueueFireAt(L, id, first, count, nullptr);
}

// --- Signal methods ----------------------------------------------------------

[[nodiscard]] ConnectionId addConnection(lua_State* L, SignalRecord& record, SignalId id, int ref, bool once,
                                         bool waiter)
{
    SignalSystem& sys = system(L);
    ConnectionRecord connection;
    connection.signal = id;
    connection.ref = ref;
    connection.once = once;
    connection.waiter = waiter;

    const ConnectionId handle = sys.connections.insert(connection);
    record.connections.push_back(handle);
    syncPropertySubscription(L, record);
    return handle;
}

int signalConnect(lua_State* L)
{
    SignalRecord& record = checkSignal(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    const SignalId id = *toSignalId(L, 1);
    lua_pushvalue(L, 2);
    const int ref = lua_ref(L, -1);
    lua_pop(L, 1);

    // Deliberately NOT refused on a closed signal. api-design.md §3.1 is
    // explicit that a destroyed instance's corpse is still usable and that
    // `Connect` still succeeds -- the handler does not run because the fire
    // captured its connection list before this connection existed, not because
    // the call failed.
    pushConnection(L, addConnection(L, record, id, ref, false, false));
    return 1;
}

int signalOnce(lua_State* L)
{
    SignalRecord& record = checkSignal(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    const SignalId id = *toSignalId(L, 1);
    lua_pushvalue(L, 2);
    const int ref = lua_ref(L, -1);
    lua_pop(L, 1);

    pushConnection(L, addConnection(L, record, id, ref, true, false));
    return 1;
}

int signalWait(lua_State* L)
{
    SignalRecord& record = checkSignal(L, 1);
    const SignalId id = *toSignalId(L, 1);

    // A one-shot registration made at the moment of the call, which is why it
    // takes its place among the connections by registration order: a `:Wait()`
    // between handlers A and B resumes between them (api-design.md §3.1).
    lua_pushthread(L);
    const int ref = lua_ref(L, -1);
    lua_pop(L, 1);

    (void)addConnection(L, record, id, ref, true, true);

    // Tail-returned, which is the protocol: `lua_yield` returns -1 and
    // `luau_precall` reads any negative C return as a yield. On resume the
    // values handed to `lua_resume` become this call's results, because a C
    // frame with no continuation is finished by `luau_poscall`.
    return lua_yield(L, 0);
}

int signalFire(lua_State* L)
{
    (void)checkSignal(L, 1);
    const SignalId id = *toSignalId(L, 1);
    // Nothing runs before the next drain. Arguments are captured by reference
    // and not copied: mutating a table between the fire and the drain is
    // visible to the handlers.
    enqueueFire(L, id, 2, lua_gettop(L) - 1);
    return 0;
}

int signalDestroy(lua_State* L)
{
    SignalRecord& record = checkSignal(L, 1);
    const SignalId id = *toSignalId(L, 1);
    if (record.kind != SignalKind::Script) {
        // An instance's own signals are closed by `Instance:Destroy`, and
        // letting a script close one would leave the instance alive with a
        // member that silently stopped working.
        raise(L, LUAUG_TR("script.err.signal_not_destroyable"));
    }
    closeSignal(L, id);
    return 0;
}

int signalNew(lua_State* L)
{
    SignalRecord record;
    record.kind = SignalKind::Script;
    pushSignal(L, system(L).signals.insert(std::move(record)));
    return 1;
}

int signalEq(lua_State* L)
{
    const SignalId* a = toSignalId(L, 1);
    const SignalId* b = toSignalId(L, 2);
    lua_pushboolean(L, a != nullptr && b != nullptr && *a == *b);
    return 1;
}

// --- Connection --------------------------------------------------------------

int connectionGetConnected(lua_State* L)
{
    const auto* id = static_cast<ConnectionId*>(lua_touserdatatagged(L, 1, static_cast<int>(UserdataTag::Connection)));
    if (id == nullptr)
        luaL_checkudatatagged(L, 1, static_cast<int>(UserdataTag::Connection));

    const ConnectionRecord* record = system(L).connections.find(*id);
    lua_pushboolean(L, record != nullptr && record->connected);
    return 1;
}

int connectionDisconnect(lua_State* L)
{
    const auto* id = static_cast<ConnectionId*>(lua_touserdatatagged(L, 1, static_cast<int>(UserdataTag::Connection)));
    if (id == nullptr)
        luaL_checkudatatagged(L, 1, static_cast<int>(UserdataTag::Connection));

    // Idempotent: a second call is a no-op and `Connected` stays false, so
    // cleanup code never has to guard it.
    disconnectRecord(L, *id);
    return 0;
}

int connectionEq(lua_State* L)
{
    const auto* a = static_cast<ConnectionId*>(lua_touserdatatagged(L, 1, static_cast<int>(UserdataTag::Connection)));
    const auto* b = static_cast<ConnectionId*>(lua_touserdatatagged(L, 2, static_cast<int>(UserdataTag::Connection)));
    lua_pushboolean(L, a != nullptr && b != nullptr && *a == *b);
    return 1;
}

// --- Instance-owned signals --------------------------------------------------

[[nodiscard]] SignalId ownedSignal(lua_State* L, core::InstanceId owner, SignalKind kind, u32 member)
{
    SignalSystem& sys = system(L);
    std::vector<SignalSystem::OwnedSignal>& owned = sys.byOwner[ownerKey(owner)];
    for (const SignalSystem::OwnedSignal& entry : owned) {
        if (entry.kind == kind && entry.member == member)
            return entry.id;
    }

    SignalRecord record;
    record.kind = kind;
    record.owner = owner;
    record.member = member;
    const SignalId id = sys.signals.insert(std::move(record));
    owned.push_back(SignalSystem::OwnedSignal{kind, member, id});
    return id;
}

[[nodiscard]] SignalId findOwnedSignal(lua_State* L, core::InstanceId owner, SignalKind kind, u32 member)
{
    SignalSystem& sys = system(L);
    const auto found = sys.byOwner.find(ownerKey(owner));
    if (found == sys.byOwner.end())
        return {};
    for (const SignalSystem::OwnedSignal& entry : found->second) {
        if (entry.kind == kind && entry.member == member)
            return entry.id;
    }
    return {};
}

// --- The drain ---------------------------------------------------------------

void reportHandlerError(lua_State* L, lua_State* co, int status)
{
    // Mapped on the STATUS, not on the message: `luaD_seterrorobj` substitutes
    // fixed English literals for out-of-memory and error-in-error-handling and
    // discards the original object, so inspecting the value would be reading
    // untranslatable text (U-32).
    std::string text;
    if (status == LUA_ERRMEM) {
        text = "not enough memory";
    }
    else {
        const char* message = lua_tostring(co, -1);
        text = message == nullptr ? std::string{} : std::string(message);
        // Onto the scheduler's stack, walking the coroutine's frames -- which
        // survive a failed resume until the thread is reset (U-31). Never
        // `lua_debugtrace`: its buffer is a process-wide static.
        luaL_traceback(L, co, text.c_str(), 0);
        if (const char* traced = lua_tostring(L, -1))
            text = traced;
        lua_pop(L, 1);
    }

    const core::I18nArg args[] = {
        {"source", std::string_view{"a deferred handler"}},
        {"message", std::string_view{text}},
    };
    // §3.1: a contained handler error goes to the console AND to
    // `DebugService.MessageOut`. Containment is not silence.
    report(L, core::LogLevel::Error, core::formatKeyPrefixed(LUAUG_TR("script.err.runtime"), args));
}

// Resumes `co` and reports whatever went wrong. Returns true when the thread is
// finished with, so the caller can drop its root.
[[nodiscard]] bool resumeHandler(lua_State* L, lua_State* co, int argCount)
{
    // `from = nullptr` resets the coroutine's C-call accounting to zero, which
    // is both the cheapest option and the one that keeps a drain from inheriting
    // a deep resume chain's budget (research §2.3).
    const int status = lua_resume(co, nullptr, argCount);
    if (status == LUA_OK)
        return true;
    if (status == LUA_YIELD) {
        // Left parked, and the drain moves straight on. A drain that blocked
        // would let one `task.wait(5)` stall every other listener of every
        // other signal.
        return false;
    }
    if (status == LUA_BREAK) {
        // The third outcome: a debugger break, neither error nor yield. Treated
        // like a yield -- the thread is still resumable and something else will
        // resume it.
        return false;
    }

    reportHandlerError(L, co, status);
    return true;
}

void invokeFire(lua_State* L, const DeferredEntry& entry)
{
    SignalSystem& sys = system(L);

    // The snapshot is copied out because a handler may connect or disconnect,
    // and `snapshots` is a vector that reallocates when it does.
    std::vector<ConnectionId> targets(sys.snapshots.begin() + static_cast<std::ptrdiff_t>(entry.snapshotBase),
                                      sys.snapshots.begin() +
                                          static_cast<std::ptrdiff_t>(entry.snapshotBase + entry.snapshotCount));

    const u32 previousDepth = sys.depth;
    sys.depth = entry.depth;

    for (const ConnectionId id : targets) {
        ConnectionRecord* connection = sys.connections.find(id);
        // Re-validated at invocation, which is what makes `:Disconnect()`
        // reliable rather than advisory -- including a disconnect performed by
        // an earlier handler in this same fire.
        if (connection == nullptr || !connection->connected)
            continue;

        const int ref = connection->ref;
        const bool waiter = connection->waiter;
        const bool once = connection->once;

        // The referenced value is fetched onto the stack BEFORE the connection
        // is disconnected, and this ordering is load-bearing rather than tidy.
        // `disconnectRecord` calls `lua_unref`, which returns the registry slot
        // to a free list threaded through the registry's own integer slots
        // (`lapi.cpp:1814`) -- so a `lua_getref` afterwards reads a free-list
        // index and hands back a *number*. The symptom is "attempt to call a
        // number value" from a `:Once` handler, some distance from the cause.
        lua_State* co = nullptr;
        int rooted = -1;
        if (waiter) {
            lua_getref(L, ref);
            rooted = lua_gettop(L);
            co = lua_tothread(L, rooted);
            // Consumed before the resume, because a `:Wait()` is a one-shot and
            // the resumed coroutine may fire this very signal again. The stack
            // slot above is what keeps the thread alive across the unref.
            disconnectRecord(L, id);
            if (co == nullptr) {
                lua_remove(L, rooted);
                continue;
            }
        }
        else {
            // Its own coroutine, because a handler must be allowed to yield and
            // an error in one must not touch the others. `lua_pcall` cannot do
            // either: everything under it is non-yieldable (U-34).
            co = lua_newthread(L);
            rooted = lua_gettop(L);
            lua_getref(L, ref);
            lua_xmove(L, co, 1);

            // After the function is on the coroutine, for the reason above.
            if (once)
                disconnectRecord(L, id);
        }

        // The arguments are pushed after the function, and the arena slots are
        // released as they are moved -- one fire's window is read exactly once.
        lua_State* argSource = L;
        if (entry.argCount > 0) {
            const int arena = pushArena(argSource);
            for (u32 index = 0; index < entry.argCount; ++index)
                lua_rawgeti(argSource, arena, static_cast<int>(entry.argBase + index));
            lua_xmove(argSource, co, static_cast<int>(entry.argCount));
            lua_remove(argSource, arena);
        }

        // A parked handler is NOT rooted here. Whatever it yielded on owns it --
        // `task.wait` and `Signal:Wait` each `lua_ref` the calling thread before
        // yielding -- and a second root taken here would never be dropped,
        // because nothing on this path ever learns that the handler finished. A
        // handler that yields on plain `coroutine.yield` has nothing that will
        // resume it and is correctly collectable.
        (void)resumeHandler(L, co, static_cast<int>(entry.argCount));
        if (rooted > 0)
            lua_remove(L, rooted);
    }

    sys.depth = previousDepth;
    // Released once, after every connection has taken its copy.
    dropArgumentsImpl(L, entry.argBase, entry.argCount);
}

} // namespace

u32 captureDeferredArguments(lua_State* L, int first, int count)
{
    return captureArgumentsImpl(L, first, count);
}

void releaseDeferredArguments(lua_State* L, lua_State* target, u32 base, u32 count)
{
    releaseArgumentsImpl(L, target, base, count);
}

void dropDeferredArguments(lua_State* L, u32 base, u32 count)
{
    dropArgumentsImpl(L, base, count);
}

bool enqueueTaskCallback(lua_State* L, int threadRef, u32 argBase, u32 argCount)
{
    SignalSystem& sys = system(L);
    const u32 depth = raisedDepth(sys);
    if (depth > MaxDeferredDepth) {
        // The cap counts everything on the queue, `task.defer` included: a
        // fires-only cap makes a self-deferring callback an unbounded drain,
        // which is a hang rather than a wrong number (§3.1).
        logDepthExceeded(L);
        dropArgumentsImpl(L, argBase, argCount);
        return false;
    }

    DeferredEntry entry;
    entry.kind = EntryKind::TaskCallback;
    entry.depth = depth;
    entry.threadRef = threadRef;
    entry.argBase = argBase;
    entry.argCount = argCount;
    sys.queue.push_back(entry);
    return true;
}

bool resumeScheduled(lua_State* L, lua_State* co, int argCount)
{
    return resumeHandler(L, co, argCount);
}

void registerSignals(lua_State* L)
{
    VmContext& ctx = context(L);
    SignalSystem& sys = *ctx.signals;
    core::AtomTable& atoms = ctx.world->atoms();

    sys.childAdded = atoms.intern("ChildAdded");
    sys.childRemoved = atoms.intern("ChildRemoved");
    sys.descendantAdded = atoms.intern("DescendantAdded");
    sys.descendantRemoving = atoms.intern("DescendantRemoving");
    sys.ancestryChanged = atoms.intern("AncestryChanged");
    sys.attributeChanged = atoms.intern("AttributeChanged");
    sys.destroying = atoms.intern("Destroying");

    lua_createtable(L, 0, 0);
    sys.argArenaRef = lua_ref(L, -1);
    lua_pop(L, 1);

    MemberTable& signalMethods = ctx.methods[static_cast<usize>(UserdataTag::Signal)];
    signalMethods.push_back(MemberEntry{atoms.intern("Connect"), signalConnect});
    signalMethods.push_back(MemberEntry{atoms.intern("Once"), signalOnce});
    signalMethods.push_back(MemberEntry{atoms.intern("Wait"), signalWait});
    signalMethods.push_back(MemberEntry{atoms.intern("Fire"), signalFire});
    signalMethods.push_back(MemberEntry{atoms.intern("Destroy"), signalDestroy});

    ctx.getters[static_cast<usize>(UserdataTag::Connection)].push_back(
        MemberEntry{atoms.intern("Connected"), connectionGetConnected});
    ctx.methods[static_cast<usize>(UserdataTag::Connection)].push_back(
        MemberEntry{atoms.intern("Disconnect"), connectionDisconnect});

    installTagMetatable(L, UserdataTag::Signal, signalEq, nullptr);
    installTagMetatable(L, UserdataTag::Connection, connectionEq, nullptr);

    const luaL_Reg constructors[] = {{"new", signalNew}, {nullptr, nullptr}};
    luaL_register(L, "Signal", constructors);
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);
}

void pushInstanceEvent(lua_State* L, core::InstanceId owner, u16 slot)
{
    pushSignal(L, ownedSignal(L, owner, SignalKind::Event, slot));
}

void pushPropertyChangedSignal(lua_State* L, core::InstanceId owner, core::NameAtom property)
{
    pushSignal(L, ownedSignal(L, owner, SignalKind::PropertyChanged, property.id));
}

void pushAttributeChangedSignal(lua_State* L, core::InstanceId owner, core::NameAtom attribute)
{
    pushSignal(L, ownedSignal(L, owner, SignalKind::AttributeChanged, attribute.id));
}

void closeInstanceSignalsExceptDestroying(lua_State* L, core::InstanceId owner)
{
    SignalSystem& sys = system(L);
    const auto found = sys.byOwner.find(ownerKey(owner));
    if (found == sys.byOwner.end())
        return;

    const scene::EventDesc* destroyingEvent = world(L).classes().findEvent(world(L).classOf(owner), sys.destroying);

    // Copied: closing disconnects, which touches the vector this walks.
    const std::vector<SignalSystem::OwnedSignal> owned = found->second;
    for (const SignalSystem::OwnedSignal& signal : owned) {
        const bool isDestroying =
            signal.kind == SignalKind::Event && destroyingEvent != nullptr && signal.member == destroyingEvent->slot;
        if (!isDestroying)
            closeSignal(L, signal.id);
    }
}

void flushSceneChanges(lua_State* L)
{
    scene::World& w = world(L);
    if (w.changes().empty())
        return;
    enqueueSceneChanges(L, w.changes().take());
}

void fireInstanceEvent(lua_State* L, core::InstanceId owner, u16 slot, int first, int count)
{
    const SignalId id = findOwnedSignal(L, owner, SignalKind::Event, slot);
    // Nothing has ever connected, so there is no signal object and nothing to
    // fire into. That is the whole cost of `Heartbeat` in an empty world.
    if (id.valid())
        enqueueFire(L, id, first, count);
}

void fireEngineMessage(lua_State* L, core::InstanceId owner, u16 slot, int first, int count)
{
    const SignalId id = findOwnedSignal(L, owner, SignalKind::Event, slot);
    if (!id.valid())
        return;

    // At the reporting handler's own depth rather than one below it. See
    // `enqueueFireAt`.
    SignalSystem& sys = system(L);
    const u32 depth = sys.draining ? sys.depth : 0;
    enqueueFireAt(L, id, first, count, &depth);
}

void pushTagSignal(lua_State* L, core::InstanceId owner, SignalKind kind, core::NameAtom tag)
{
    pushSignal(L, ownedSignal(L, owner, kind, tag.id));
}

void enqueueSceneChanges(lua_State* L, std::span<const scene::Change> changes)
{
    SignalSystem& sys = system(L);
    scene::World& w = world(L);

    for (const scene::Change& change : changes) {
        // Resolved through the class, because an event's slot is per class and
        // the descriptor is what says whether this class has the event at all.
        const auto eventSignal = [&](core::NameAtom name) -> SignalId {
            const scene::EventDesc* descriptor = w.classes().findEvent(w.classOf(change.subject), name);
            if (descriptor == nullptr)
                return {};
            return findOwnedSignal(L, change.subject, SignalKind::Event, descriptor->slot);
        };

        switch (change.kind) {
        case scene::ChangeKind::ChildAdded:
        case scene::ChangeKind::ChildRemoved:
        case scene::ChangeKind::DescendantAdded:
        case scene::ChangeKind::DescendantRemoving: {
            const core::NameAtom name = change.kind == scene::ChangeKind::ChildAdded        ? sys.childAdded
                                        : change.kind == scene::ChangeKind::ChildRemoved    ? sys.childRemoved
                                        : change.kind == scene::ChangeKind::DescendantAdded ? sys.descendantAdded
                                                                                            : sys.descendantRemoving;
            const SignalId id = eventSignal(name);
            if (!id.valid())
                break;
            pushInstance(L, change.other);
            enqueueFire(L, id, lua_gettop(L), 1);
            lua_pop(L, 1);
            break;
        }

        case scene::ChangeKind::AncestryChanged: {
            const SignalId id = eventSignal(sys.ancestryChanged);
            if (!id.valid())
                break;
            // `(instance, newParent)`, the instance being the one whose ancestry
            // changed -- which for this fact is the subject itself, since scene
            // enqueues one per affected member.
            pushInstance(L, change.subject);
            pushInstance(L, change.other);
            enqueueFire(L, id, lua_gettop(L) - 1, 2);
            lua_pop(L, 2);
            break;
        }

        case scene::ChangeKind::PropertyChanged: {
            const SignalId id = findOwnedSignal(L, change.subject, SignalKind::PropertyChanged, change.name.id);
            if (id.valid())
                enqueueFire(L, id, 0, 0);
            break;
        }

        case scene::ChangeKind::AttributeChanged: {
            // The named signal first, then the catch-all (§3.1): the narrow
            // subscription asked about this attribute, and the catch-all is what
            // routes.
            const SignalId named = findOwnedSignal(L, change.subject, SignalKind::AttributeChanged, change.name.id);
            if (named.valid())
                enqueueFire(L, named, 0, 0);

            const SignalId general = eventSignal(sys.attributeChanged);
            if (general.valid()) {
                const std::string_view text = w.atoms().text(change.name);
                lua_pushlstring(L, text.data(), text.size());
                enqueueFire(L, general, lua_gettop(L), 1);
                lua_pop(L, 1);
            }
            break;
        }

        case scene::ChangeKind::Destroying: {
            const SignalId id = eventSignal(sys.destroying);
            if (id.valid())
                enqueueFire(L, id, 0, 0);

            // Straight after the fire, so a fire queued for this instance later
            // in the same drain finds no live connections and invokes nothing.
            DeferredEntry close;
            close.kind = EntryKind::CloseOwner;
            close.depth = raisedDepth(sys);
            close.owner = change.subject;
            sys.queue.push_back(close);
            break;
        }

        case scene::ChangeKind::TagAdded:
        case scene::ChangeKind::TagRemoved: {
            // The listener is `TagService:GetInstanceAddedSignal(tag)`, owned by
            // the TagService instance and keyed by the tag. Nothing fires until
            // the service exists, which is correct: a service nobody asked for
            // has no subscribers.
            const core::InstanceId tagService =
                w.findFirstChildOfClass(context(L).services->dataModel, context(L).services->tagServiceClass);
            if (!tagService.valid())
                break;

            const SignalKind kind =
                change.kind == scene::ChangeKind::TagAdded ? SignalKind::TagAdded : SignalKind::TagRemoved;
            const SignalId id = findOwnedSignal(L, tagService, kind, change.name.id);
            if (!id.valid())
                break;

            pushInstance(L, change.subject);
            enqueueFire(L, id, lua_gettop(L), 1);
            lua_pop(L, 1);
            break;
        }
        }
    }
}

usize drainDeferred(lua_State* L)
{
    SignalSystem& sys = system(L);
    if (sys.draining)
        return 0;

    sys.draining = true;
    usize processed = 0;

    // To fixpoint: handlers append to this same queue and the loop continues.
    // Deliberately not a snapshot of the original end.
    while (!sys.queue.empty()) {
        const DeferredEntry entry = sys.queue.front();
        sys.queue.pop_front();
        ++processed;

        switch (entry.kind) {
        case EntryKind::Fire:
            invokeFire(L, entry);
            break;

        case EntryKind::TaskCallback: {
            const u32 previousDepth = sys.depth;
            sys.depth = entry.depth;
            lua_getref(L, entry.threadRef);
            lua_State* co = lua_tothread(L, -1);
            if (co != nullptr) {
                if (entry.argCount > 0)
                    releaseArgumentsImpl(L, co, entry.argBase, entry.argCount);
                (void)resumeHandler(L, co, static_cast<int>(entry.argCount));
            }
            else {
                dropArgumentsImpl(L, entry.argBase, entry.argCount);
            }
            lua_pop(L, 1);
            sys.depth = previousDepth;
            break;
        }

        case EntryKind::CloseOwner: {
            // `Instance:Destroy` closed everything but `Destroying`, whose fire
            // has just run. This finishes the job and frees the records, which
            // is what makes a `Signal` handle held past this point go stale.
            const auto found = sys.byOwner.find(ownerKey(entry.owner));
            if (found != sys.byOwner.end()) {
                const std::vector<SignalSystem::OwnedSignal> owned = found->second;
                for (const SignalSystem::OwnedSignal& signal : owned)
                    closeSignal(L, signal.id);
                sys.byOwner.erase(ownerKey(entry.owner));
                for (const SignalSystem::OwnedSignal& signal : owned)
                    (void)sys.signals.erase(signal.id);
            }
            break;
        }
        }
    }

    // The only moment at which no window into either arena is live -- which is
    // true because both arenas are drain-scoped and nothing else may park a
    // window in them. `task.delay` did, once, and its arguments were being
    // overwritten by the next drain's; they live on the timer's own coroutine
    // now (`tasks.cpp`). Anything that needs to hold values ACROSS a drain has
    // to own them, not borrow a slot here.
    sys.snapshots.clear();
    sys.argArenaTop = 0;
    sys.draining = false;
    return processed;
}

u32 currentDepth(lua_State* L)
{
    const SignalSystem& sys = system(L);
    return sys.draining ? sys.depth : 0;
}

} // namespace luaug::script
