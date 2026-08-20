#include "luaug/script/tasks.h"

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cmath>

#include "luaug/scene/world.h"
#include "luaug/script/signals.h"

namespace luaug::script
{
namespace
{

[[nodiscard]] TaskScheduler& scheduler(lua_State* L) noexcept
{
    return *context(L).tasks;
}

[[nodiscard]] const scene::EngineState& engineState(lua_State* L) noexcept
{
    return context(L).world->engineState();
}

// A duration in seconds becomes a whole number of ticks, and the epsilon is not
// slop: at `dt = 1/60`, `1 / (1/60)` evaluates to 60.000000000000007, so a bare
// ceil yields 61 and contradicts api-design.md §3.2's guarantee that
// `task.wait(1)` is exactly 60 ticks. The tolerance is a thousand times smaller
// than any timestep the engine will run and a million times larger than the
// representation error it absorbs.
//
// The floor is one tick, never zero: `task.wait(0)` must yield and
// `task.delay(0, fn)` must not run before returning, so "at or after 0 seconds"
// cannot be satisfied by the phase you are standing in. A negative duration
// clamps to the same thing -- it is what subtracting two elapsed values across
// a tick boundary produces, and raising on it would be a trap.
[[nodiscard]] u64 ticksFor(f64 seconds, f64 timestep) noexcept
{
    if (!(timestep > 0.0) || !(seconds > 0.0))
        return 1;

    constexpr f64 tolerance = 1e-9;
    const f64 raw = std::ceil(seconds / timestep - tolerance);
    if (!(raw > 1.0))
        return 1;
    return static_cast<u64>(raw);
}

void insertTimer(TaskScheduler& tasks, TimerEntry entry)
{
    entry.sequence = tasks.nextSequence++;
    // Kept sorted by `(deadline, sequence)` at insert rather than sorted at
    // resume: §3.2 makes the order a rule, and a rule maintained in one place is
    // one that cannot be forgotten at the other.
    const auto position = std::upper_bound(
        tasks.timers.begin(),
        tasks.timers.end(),
        entry,
        [](const TimerEntry& a, const TimerEntry& b) {
            return a.deadlineTick != b.deadlineTick ? a.deadlineTick < b.deadlineTick : a.sequence < b.sequence;
        });
    tasks.timers.insert(position, entry);
}

// Creates the coroutine the scheduler hands back. All three schedulers create it
// themselves and return it: the thread the callback sees from
// `coroutine.running()` is the same one `task.cancel` takes, and that is only
// true because nothing here ever accepts an existing thread.
[[nodiscard]] lua_State* newCallbackThread(lua_State* L, int functionIndex, int& threadRef)
{
    lua_State* co = lua_newthread(L);
    lua_xpush(L, co, functionIndex);
    threadRef = lua_ref(L, -1);
    // Left on the stack: the caller returns it to the script, which is what
    // makes `local thread = task.defer(fn)` work.
    return co;
}

int taskSpawn(lua_State* L)
{
    // A function, never a thread. Resuming a coroutine you already hold is
    // `coroutine.resume`'s job, and keeping the two surfaces apart is what
    // guarantees every scheduled thread is one the scheduler can account for.
    luaL_checktype(L, 1, LUA_TFUNCTION);

    const int count = lua_gettop(L) - 1;
    int threadRef = -1;
    lua_State* co = newCallbackThread(L, 1, threadRef);
    for (int index = 0; index < count; ++index)
        lua_xpush(L, co, 2 + index);

    // The one non-deferred call, and deliberately so: it runs synchronously
    // while the caller's stack is still live. Its error still does not propagate
    // to that caller -- `task.spawn` returns normally either way (§3.1).
    const bool finished = resumeScheduled(L, co, count);
    if (finished)
        threadRef = lua_unref(L, threadRef);
    else
        (void)threadRef; // parked; whatever it yielded on holds its own root
    return 1;
}

int taskDefer(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);

    const int count = lua_gettop(L) - 1;
    const u32 argBase = captureDeferredArguments(L, 2, count);

    int threadRef = -1;
    (void)newCallbackThread(L, 1, threadRef);
    if (!enqueueTaskCallback(L, threadRef, argBase, static_cast<u32>(count < 0 ? 0 : count)))
        threadRef = lua_unref(L, threadRef);
    return 1;
}

int taskDelay(lua_State* L)
{
    const f64 duration = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    const int count = lua_gettop(L) - 2;

    int threadRef = -1;
    lua_State* co = newCallbackThread(L, 2, threadRef);

    // Onto the callback's OWN stack, exactly as `task.spawn` does above, and
    // NOT into the deferred arena. A timer outlives the drain it was created
    // in -- that is what a timer is -- and the arena's top is reset at the end
    // of every drain, so a `task.delay(0.7, fn, x)` came back with `x` as nil
    // after forty-two ticks of someone else's arguments being written over it.
    //
    // Held alive by `threadRef`, so the arguments' lifetime is the timer's with
    // nothing to keep in step.
    for (int index = 0; index < count; ++index)
        lua_xpush(L, co, 3 + index);

    const scene::EngineState& state = engineState(L);
    TimerEntry entry;
    entry.deadlineTick = state.tick + ticksFor(duration, state.fixedTimestep);
    entry.threadRef = threadRef;
    entry.argCount = static_cast<u32>(count < 0 ? 0 : count);
    entry.scheduledTick = state.tick;
    insertTimer(scheduler(L), entry);
    return 1;
}

int taskWait(lua_State* L)
{
    const f64 duration = luaL_optnumber(L, 1, 0.0);
    const scene::EngineState& state = engineState(L);

    lua_pushthread(L);
    const int threadRef = lua_ref(L, -1);
    lua_pop(L, 1);

    TimerEntry entry;
    entry.deadlineTick = state.tick + ticksFor(duration, state.fixedTimestep);
    entry.threadRef = threadRef;
    entry.wait = true;
    entry.scheduledTick = state.tick;
    insertTimer(scheduler(L), entry);

    // Tail-returned: `lua_yield` returns -1 and `luau_precall` reads any
    // negative C return as a yield. A C frame with no continuation is finished
    // by `luau_poscall` on the way back, so the values `lua_resume` is given
    // become this call's results.
    return lua_yield(L, 0);
}

int taskCancel(lua_State* L)
{
    lua_State* target = lua_tothread(L, 1);
    if (target == nullptr)
        luaL_checktype(L, 1, LUA_TTHREAD);

    TaskScheduler& tasks = scheduler(L);
    SignalSystem& signals = *context(L).signals;

    // Scanned rather than indexed by thread: `cancel` is rare, the lists are
    // short, and a pointer-keyed map would be a container whose order R10 would
    // then have to be argued about.
    for (usize index = 0; index < tasks.timers.size(); ++index)
    {
        lua_getref(L, tasks.timers[index].threadRef);
        const bool match = lua_tothread(L, -1) == target;
        lua_pop(L, 1);
        if (!match)
            continue;

        const TimerEntry entry = tasks.timers[index];
        tasks.timers.erase(tasks.timers.begin() + static_cast<std::ptrdiff_t>(index));
        // No arguments to release: a timer's arguments sit on its own coroutine,
        // and dropping the ref drops them with it.
        (void)lua_unref(L, entry.threadRef);
        return 0;
    }

    // A queued `task.defer` entry counts as a pending resumption, so cancelling
    // the thread `task.defer` returned stops its callback from running (§3.2).
    for (auto it = signals.queue.begin(); it != signals.queue.end(); ++it)
    {
        if (it->kind != EntryKind::TaskCallback)
            continue;
        lua_getref(L, it->threadRef);
        const bool match = lua_tothread(L, -1) == target;
        lua_pop(L, 1);
        if (!match)
            continue;

        const DeferredEntry entry = *it;
        signals.queue.erase(it);
        dropDeferredArguments(L, entry.argBase, entry.argCount);
        (void)lua_unref(L, entry.threadRef);
        return 0;
    }

    // A finished thread, an already-cancelled one, and the thread currently
    // running all land here: none of the three holds a pending resumption, and a
    // pending resumption is the only thing `cancel` can take away.
    const core::I18nArg args[] = {
        {"status", std::string_view{lua_costatus(L, target) == LUA_CORUN ? "running" : "not scheduled"}},
    };
    raise(L, LUAUG_TR("script.err.task_not_scheduled"), args);
}

} // namespace

void registerTasks(lua_State* L)
{
    const luaL_Reg entries[] = {
        {"spawn", taskSpawn},
        {"defer", taskDefer},
        {"delay", taskDelay},
        {"wait", taskWait},
        {"cancel", taskCancel},
        {nullptr, nullptr},
    };
    luaL_register(L, "task", entries);
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);
}

void resumeDueTimers(lua_State* L, u64 tick)
{
    TaskScheduler& tasks = scheduler(L);
    const f64 timestep = engineState(L).fixedTimestep;

    // Collected first, because a resumed coroutine may schedule more timers and
    // the list it would insert into is the one being walked. Everything due at
    // this tick was already ordered by `(deadline, sequence)` at insert, so the
    // prefix is exactly the FIFO §3.2 describes.
    std::vector<TimerEntry> due;
    usize count = 0;
    while (count < tasks.timers.size() && tasks.timers[count].deadlineTick <= tick)
        ++count;
    due.assign(tasks.timers.begin(), tasks.timers.begin() + static_cast<std::ptrdiff_t>(count));
    tasks.timers.erase(tasks.timers.begin(), tasks.timers.begin() + static_cast<std::ptrdiff_t>(count));

    for (const TimerEntry& entry : due)
    {
        lua_getref(L, entry.threadRef);
        lua_State* co = lua_tothread(L, -1);
        if (co == nullptr)
        {
            lua_pop(L, 1);
            (void)lua_unref(L, entry.threadRef);
            continue;
        }

        int resumeCount = 0;
        if (entry.wait)
        {
            // The elapsed sim time, as a product of whole ticks and the timestep
            // rather than a running sum of per-tick values -- so an exact
            // multiple of the timestep comes back exact and a long wait carries
            // no accumulated drift.
            lua_pushnumber(co, static_cast<f64>(tick - entry.scheduledTick) * timestep);
            resumeCount = 1;
        }
        else if (entry.argCount > 0)
        {
            // Already on `co`, pushed when the timer was created.
            resumeCount = static_cast<int>(entry.argCount);
        }

        const bool finished = resumeScheduled(L, co, resumeCount);
        lua_pop(L, 1);
        if (finished)
            (void)lua_unref(L, entry.threadRef);
    }
}

} // namespace luaug::script
