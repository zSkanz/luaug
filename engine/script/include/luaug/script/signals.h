// Deferred-only signals and the one queue they share (api-design.md §3.1,
// ADR 0015).
//
// §3.1 is the contract and it was written before this file existed, on purpose:
// the conformance specs are authored against the document, so the document is
// what this implements rather than the other way round. Four of its rules are
// the ones that shape the data structures here, and each is a decision that
// would be invisible if it were wrong:
//
//   1. **One queue, one order.** Engine fires, script fires and `task.defer`
//      callbacks all enter the same FIFO in raise order. That is what makes the
//      relative order of a script's own `:Fire()` and the `ChildAdded` its
//      `part.Parent = x` caused well defined -- so the queue cannot be per
//      signal, and there is no priority.
//
//   2. **A fire captures the connection list.** Enqueuing records which
//      connections were live at that instant. A connection made afterwards does
//      not run for it, and one disconnected before it is invoked does not run
//      either -- so the snapshot is a list of handles re-validated at invocation
//      rather than a list of functions.
//
//   3. **A drain runs to fixpoint and does not block.** Handlers append to the
//      same queue and the drain continues; a handler that yields is left parked
//      and the drain moves on. So each handler needs its own coroutine, and the
//      loop cannot snapshot the queue's end.
//
//   4. **Re-entrancy is a generation depth, capped at 10.** Every entry carries
//      the depth of whatever raised it. Depths 0 through 10 run, so a
//      self-refiring handler is invoked exactly 11 times. The cap counts
//      everything on the queue, `task.defer` included -- a fires-only cap makes
//      a self-deferring callback a hang rather than a wrong number.
#pragma once

#include <deque>
#include <span>
#include <unordered_map>
#include <vector>

#include "luaug/core/id.h"
#include "luaug/core/name_atom.h"
#include "luaug/core/slotmap.h"
#include "luaug/scene/change_queue.h"
#include "luaug/script/binding.h"

struct lua_State;

namespace luaug::script
{

// A generation-checked handle, distinct from `core::InstanceId` so that a signal
// slot cannot be handed to a binding that wants an instance. Same layout, no
// conversion.
struct SignalId
{
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }
    [[nodiscard]] constexpr bool operator==(const SignalId&) const noexcept = default;
};

struct ConnectionId
{
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }
    [[nodiscard]] constexpr bool operator==(const ConnectionId&) const noexcept = default;
};

// api-design.md §3.1. Depths 0..10 run; an entry that would be depth 11 is
// dropped and logged, so a handler that re-fires its own signal is invoked
// exactly eleven times.
inline constexpr u32 MaxDeferredDepth = 10;

// What a signal belongs to. A script-created signal is `Owned` by nothing; the
// other three are reached through an instance and are closed with it.
enum class SignalKind : u8
{
    Script,
    Event,
    PropertyChanged,
    AttributeChanged,
    // `TagService:GetInstanceAddedSignal(tag)` and its removed twin. Owned by
    // the TagService instance and keyed by the tag's atom, so the pair behaves
    // like any other instance signal -- one object per tag, closed with its
    // owner.
    TagAdded,
    TagRemoved,
};

struct ConnectionRecord
{
    SignalId signal;
    // The handler function, or -- for a `:Wait()` -- the parked coroutine.
    // Held by registry ref, because that is the only thing keeping either alive
    // between the connect and the drain.
    int ref = -1;
    bool once = false;
    // A one-shot registration made by `:Wait()`. It resumes a parked coroutine
    // instead of calling a handler, and it takes its place among the connections
    // by registration order (api-design.md §3.1).
    bool waiter = false;
    bool connected = true;
};

struct SignalRecord
{
    // Registration order, which is invocation order within one fire.
    std::vector<ConnectionId> connections;

    SignalKind kind = SignalKind::Script;
    // Invalid for a script-created signal.
    core::InstanceId owner;
    // The event slot for `Event`, the property or attribute atom otherwise.
    u32 member = 0;

    bool closed = false;
};

enum class EntryKind : u8
{
    Fire,
    // A `task.defer` callback. It shares the queue and the depth cap: a cap
    // that counted only fires would make a self-deferring callback an unbounded
    // drain, which is a hang rather than a wrong number.
    TaskCallback,
    // Closes an instance's remaining signals, enqueued straight after its
    // `Destroying` fire so that later queued fires for it find nothing live.
    CloseOwner,
};

struct DeferredEntry
{
    EntryKind kind = EntryKind::Fire;
    // The generation depth of whatever raised this, not a call-stack depth --
    // so a wide fan-out does not trip the cap.
    u32 depth = 0;

    SignalId signal;
    core::InstanceId owner;

    // A window into the connection snapshot arena: which connections were live
    // when this was raised.
    u32 snapshotBase = 0;
    u32 snapshotCount = 0;

    // A window into the Luau-side argument arena, 1-based like a Luau array.
    u32 argBase = 0;
    u32 argCount = 0;

    // `TaskCallback`: the thread to resume, held by registry ref.
    int threadRef = -1;
};

// Per-VM. Held by `VmContext` as a pointer and owned by `ScriptRuntime`, which
// is the only thing whose lifetime brackets the `lua_State`.
class SignalSystem
{
public:
    core::SlotMap<SignalRecord, SignalId> signals;
    core::SlotMap<ConnectionRecord, ConnectionId> connections;

    std::deque<DeferredEntry> queue;
    // Flat arena, windowed per entry. A vector of vectors would allocate per
    // fire, and a property storm is the load case this design is measured
    // against.
    std::vector<ConnectionId> snapshots;

    // A Luau table holding every pending fire's arguments, so they are GC-rooted
    // for exactly as long as the queue needs them. One table rather than one per
    // fire: `lua_ref` slots are a shared free list and churning them per fire is
    // registry pressure for nothing.
    int argArenaRef = -1;
    u32 argArenaTop = 0;

    // Signals reached through an instance, so `Destroy` can close them and so
    // `part.ChildAdded` is the same object twice.
    struct OwnedSignal
    {
        SignalKind kind = SignalKind::Event;
        u32 member = 0;
        SignalId id;
    };
    // Keyed by the owner's full id, so a recycled slot cannot inherit the old
    // occupant's signals. Never iterated in observable order (R10): the value is
    // a short vector scanned linearly, and the map is only ever probed by key.
    std::unordered_map<u64, std::vector<OwnedSignal>> byOwner;

    // Interned once at boot; `enqueueSceneChanges` compares against these rather
    // than looking a name up per change.
    core::NameAtom childAdded;
    core::NameAtom childRemoved;
    core::NameAtom descendantAdded;
    core::NameAtom descendantRemoving;
    core::NameAtom ancestryChanged;
    core::NameAtom attributeChanged;
    core::NameAtom destroying;

    u32 depth = 0;
    // Set while a drain is running, so an enqueue can tell "raised by a handler"
    // from "raised by a script" without inspecting the stack.
    bool draining = false;
};

// Installs the `Signal` and `Connection` metatables and the `Signal` global.
// Runs during boot with everything else, before the sandbox.
void registerSignals(lua_State* L);

// The signal object for one of an instance's declared events, created on first
// use and cached per (instance, slot) so that `part.ChildAdded` is the same
// object every time -- a script that connects in one place and disconnects in
// another depends on it.
void pushInstanceEvent(lua_State* L, core::InstanceId owner, u16 slot);

// `GetPropertyChangedSignal` / `GetAttributeChangedSignal`: the same object on
// every call for the same name, carrying no arguments so its type stays
// independent of the property's (api-design.md §2.2).
void pushPropertyChangedSignal(lua_State* L, core::InstanceId owner, core::NameAtom property);
void pushAttributeChangedSignal(lua_State* L, core::InstanceId owner, core::NameAtom attribute);

// `TagService`'s per-tag signals. `owner` is the TagService instance, which is
// what closes them if it is ever destroyed.
void pushTagSignal(lua_State* L, core::InstanceId owner, SignalKind kind, core::NameAtom tag);

// Closes every signal `owner` has except `Destroying`, disconnecting everything
// connected to them. Called **synchronously from `Instance:Destroy`**, not from
// the drain, because api-design.md §3.1 says `Destroy` enqueues `Destroying` and
// *then* closes the others -- so a `ChildAdded` queued earlier in the same frame
// finds no live connections when the drain reaches it. `Destroying` is the one
// exception: its own fire has already been queued and its handlers must run.
void closeInstanceSignalsExceptDestroying(lua_State* L, core::InstanceId owner);

// Enqueues a fire on an instance's event signal with `count` arguments starting
// at stack index `first`. Does nothing when nothing has ever connected: an
// engine-raised event costs a hash probe on an unwatched instance, which is what
// lets `Heartbeat` fire sixty times a second into an empty world for free.
void fireInstanceEvent(lua_State* L, core::InstanceId owner, u16 slot, int first, int count);

// Turns `scene`'s POD change facts into fires, in queue order. This is the whole
// of the scene->script direction: `scene` holds no Luau value and never will
// (M2 brief, Decision 3).
void enqueueSceneChanges(lua_State* L, std::span<const scene::Change> changes);

// Converts everything `scene` has queued since the last call.
//
// Called after **every** mutating binding rather than once at the drain, and
// that is a correctness requirement rather than a latency one: a fire captures
// the connection list at the moment it is raised, so converting a batch of facts
// at drain time would let a connection made after the mutation run for it. §3.1
// leans on the opposite in two places, and the conformance specs test both.
void flushSceneChanges(lua_State* L);

// Runs the queue to fixpoint. Returns how many entries were invoked, which is
// what a test asserts on to pin the re-entrancy cap.
usize drainDeferred(lua_State* L);

// How deep the drain currently is. Zero outside a drain.
[[nodiscard]] u32 currentDepth(lua_State* L);

// --- Shared with `task` ------------------------------------------------------
//
// `task.defer` entries ride the same queue and `task.delay` arguments live in
// the same arena, because §3.1's depth cap counts everything on the queue and
// two arenas would be two lifetimes to get wrong rather than one.

// Moves `count` values starting at stack index `first` into the argument arena.
// Returns the 1-based base of the window, or 0 for an empty one.
[[nodiscard]] u32 captureDeferredArguments(lua_State* L, int first, int count);

// Pushes a window onto `target` and frees its arena slots.
void releaseDeferredArguments(lua_State* L, lua_State* target, u32 base, u32 count);

// Frees a window without pushing it, for an entry that will never run.
void dropDeferredArguments(lua_State* L, u32 base, u32 count);

// Enqueues a `task.defer` callback at the depth of whatever raised it. Returns
// false when the re-entrancy cap dropped it, in which case the arena window has
// already been released.
bool enqueueTaskCallback(lua_State* L, int threadRef, u32 argBase, u32 argCount);

// Resumes a scheduler-owned coroutine, reporting an error the way a handler's
// is reported. Returns true when the thread is finished with.
bool resumeScheduled(lua_State* L, lua_State* co, int argCount);

} // namespace luaug::script
