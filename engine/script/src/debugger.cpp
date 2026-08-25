#include "luaug/script/debugger.h"

#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/script/binding.h"
#include "luaug/script/signals.h"

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cstdio>

namespace luaug::script {
namespace {

using core::u32;

// The chunk name Luau reports, without the `@` the loader prefixes. That prefix
// is what tells Luau the name is a file rather than the source itself, and
// nothing outside the VM should ever see it.
[[nodiscard]] std::string chunkOf(lua_State* co, int level)
{
    lua_Debug info{};
    if (lua_getinfo(co, level, "s", &info) == 0 || info.short_src == nullptr)
        return {};
    std::string_view name(info.short_src);
    if (!name.empty() && name.front() == '@')
        name.remove_prefix(1);
    return std::string(name);
}

// A value at `index` on `co`, described without being kept. Deliberately short:
// a preview is what fits on a row, and expanding one is a question asked at the
// next safe point rather than answered here.
[[nodiscard]] DebugValue describe(lua_State* co, std::string_view name, int index)
{
    DebugValue value;
    value.name = std::string(name);
    value.type = lua_typename(co, lua_type(co, index));

    switch (lua_type(co, index)) {
    case LUA_TNIL:
        value.preview = "nil";
        break;
    case LUA_TBOOLEAN:
        value.preview = lua_toboolean(co, index) != 0 ? "true" : "false";
        break;
    case LUA_TNUMBER: {
        char buffer[32]{};
        (void)std::snprintf(buffer, sizeof(buffer), "%.14g", lua_tonumber(co, index));
        value.preview = buffer;
        break;
    }
    case LUA_TSTRING: {
        std::size_t length = 0;
        const char* text = lua_tolstring(co, index, &length);
        // Quoted and clipped: a preview is a row, and a script holding a
        // hundred kilobytes of text in a local must not put it in a panel.
        value.preview = "\"";
        value.preview.append(text, std::min<std::size_t>(length, 64));
        if (length > 64)
            value.preview.append("...");
        value.preview.push_back('"');
        break;
    }
    case LUA_TTABLE:
        value.preview = "{...}";
        break;
    case LUA_TFUNCTION:
        value.preview = "function";
        break;
    default: {
        // Userdata, which in this engine is almost always an Instance -- and
        // `tostring` is what the bindings already teach it to answer.
        if (const char* text = luaL_tolstring(co, index, nullptr); text != nullptr) {
            value.preview = text;
            lua_pop(co, 1);
        }
        break;
    }
    }
    return value;
}

// Reads one frame's locals and upvalues, copying everything out. Every
// non-null `lua_getlocal` PUSHED a value, and popping is not tidiness: a value
// left on a parked coroutine's stack changes what it resumes with.
void readFrame(lua_State* co, int level, DebugFrame& frame)
{
    for (int n = 1; n <= static_cast<int>(kMaxDebugValues); ++n) {
        const char* name = lua_getlocal(co, level, n);
        if (name == nullptr)
            break;
        frame.locals.push_back(describe(co, name, -1));
        lua_pop(co, 1);
    }

    // Upvalues need the FUNCTION on the stack rather than a level, so it is
    // pushed, read from, and popped.
    lua_Debug info{};
    if (lua_getinfo(co, level, "f", &info) == 0)
        return;
    for (int n = 1; n <= static_cast<int>(kMaxDebugValues); ++n) {
        const char* name = lua_getupvalue(co, -1, n);
        if (name == nullptr)
            break;
        frame.upvalues.push_back(describe(co, name, -1));
        lua_pop(co, 1);
    }
    lua_pop(co, 1);
}

void debugBreakHook(lua_State* co, lua_Debug*)
{
    if (Debugger* debugger = context(co).debugger; debugger != nullptr)
        debugger->onBreak(co, BreakReason::Breakpoint);
}

void debugStepHook(lua_State* co, lua_Debug*)
{
    if (Debugger* debugger = context(co).debugger; debugger != nullptr)
        debugger->onStep(co);
}

} // namespace

void Debugger::install(lua_State* L)
{
    lua_callbacks(L)->debugbreak = debugBreakHook;
    lua_callbacks(L)->debugstep = debugStepHook;
    context(L).debugger = this;
}

void Debugger::onBreak(lua_State* co, BreakReason reason)
{
    // Already stopped somewhere: a break cannot nest, and the second one would
    // overwrite the stack the panel is drawing.
    if (m_snapshot.parked)
        return;

    // The instruction Continue resumed into is the one it was already stopped
    // on. See `m_skipArmed`.
    if (m_skipArmed && reason == BreakReason::Breakpoint) {
        lua_Debug where{};
        const bool sameLine = lua_getinfo(co, 0, "l", &where) != 0 &&
                              static_cast<u32>(where.currentline) == m_skipLine && chunkOf(co, 0) == m_skipChunk;
        m_skipArmed = false;
        if (sameLine)
            return;
    }

    // **`lua_break` refuses across a C-call boundary** (`ldo.cpp:756-762`
    // raises "attempt to break across metamethod/C-call boundary"), so a
    // breakpoint reached inside a metamethod or a C-called comparator cannot
    // park at all. Running through is the honest outcome -- turning somebody's
    // breakpoint into a runtime error in their game would be worse than a
    // breakpoint that did not stop.
    if (lua_isyieldable(co) == 0) {
        core::log(core::LogLevel::Warn, LUAUG_TR("script.warn.breakpoint_not_yieldable"));
        return;
    }

    // The registry reference, taken BEFORE the break and the only root the
    // parked thread has: both resume sites drop their bookkeeping before
    // `lua_resume` returns, so without this the coroutine is collectable and
    // Continue would resume nothing.
    lua_pushthread(co);
    m_threadRef = lua_ref(co, -1);
    lua_pop(co, 1);

    m_snapshot = DebugSnapshot{};
    m_snapshot.parked = true;
    m_snapshot.reason = reason;

    const int depth = lua_stackdepth(co);
    for (int level = 0; level < depth && level < static_cast<int>(kMaxDebugFrames); ++level) {
        lua_Debug info{};
        if (lua_getinfo(co, level, "sln", &info) == 0)
            break;
        DebugFrame frame;
        frame.chunk = chunkOf(co, level);
        frame.line = info.currentline > 0 ? static_cast<u32>(info.currentline) : 0u;
        frame.function = info.name != nullptr ? info.name : "?";
        readFrame(co, level, frame);
        m_snapshot.frames.push_back(std::move(frame));
    }

    if (!m_snapshot.frames.empty()) {
        m_snapshot.chunk = m_snapshot.frames.front().chunk;
        m_snapshot.line = m_snapshot.frames.front().line;
    }

    m_step = StepMode::None;
    m_stepDepth = -1;
    lua_singlestep(co, 0);
    (void)lua_break(co);
}

void Debugger::onStep(lua_State* co)
{
    if (m_step == StepMode::None || m_snapshot.parked)
        return;

    const int depth = lua_stackdepth(co);

    // **Not where it started.** The single-step hook fires before anything has
    // moved, so without this every step stops on the line it was already on.
    lua_Debug where{};
    if (lua_getinfo(co, 0, "l", &where) != 0 && depth == m_stepDepth &&
        static_cast<u32>(where.currentline) == m_stepFromLine) {
        return;
    }

    switch (m_step) {
    case StepMode::Into:
        break;
    case StepMode::Over:
        // Deeper than where the step began is inside a call somebody stepped
        // OVER, so it keeps running.
        if (depth > m_stepDepth)
            return;
        break;
    case StepMode::Out:
        if (depth >= m_stepDepth)
            return;
        break;
    case StepMode::None:
        return;
    }

    onBreak(co, BreakReason::Step);
}

u32 Debugger::setBreakpoint(lua_State* L, std::string_view chunk, u32 line)
{
    const auto found = std::find_if(m_armed.begin(), m_armed.end(),
                                    [&](const Armed& a) { return a.chunk == chunk && a.line == line; });
    Armed& armed =
        found != m_armed.end() ? *found : m_armed.emplace_back(Armed{.chunk = std::string(chunk), .line = line});

    const auto chunkIt =
        std::find_if(m_chunks.begin(), m_chunks.end(), [&](const Chunk& c) { return c.name == chunk; });
    if (chunkIt == m_chunks.end() || chunkIt->closureRef < 0) {
        // Not loaded yet, which is the normal state for a breakpoint set before
        // the world runs. It is bound by `bindChunk` when the chunk arrives.
        armed.boundLine = 0;
        return 0;
    }

    lua_getref(L, chunkIt->closureRef);
    const int target = lua_breakpoint(L, -1, static_cast<int>(line), 1);
    lua_pop(L, 1);
    // -1 is "no executable code at or after this line", which the panel draws
    // hollow rather than pretending.
    armed.boundLine = target > 0 ? static_cast<u32>(target) : 0u;
    return armed.boundLine;
}

void Debugger::clearBreakpoint(lua_State* L, std::string_view chunk, u32 line)
{
    const auto found = std::find_if(m_armed.begin(), m_armed.end(),
                                    [&](const Armed& a) { return a.chunk == chunk && a.line == line; });
    if (found == m_armed.end())
        return;

    const auto chunkIt =
        std::find_if(m_chunks.begin(), m_chunks.end(), [&](const Chunk& c) { return c.name == chunk; });
    if (chunkIt != m_chunks.end() && chunkIt->closureRef >= 0 && found->boundLine != 0) {
        lua_getref(L, chunkIt->closureRef);
        (void)lua_breakpoint(L, -1, static_cast<int>(found->boundLine), 0);
        lua_pop(L, 1);
    }
    m_armed.erase(found);
}

void Debugger::bindChunk(lua_State* L, lua_State* co, std::string_view chunk, int closureIndex)
{
    lua_pushvalue(co, closureIndex);
    // Into the registry, which is global to the VM, so any state can `getref`
    // it afterwards.
    const int ref = lua_ref(co, -1);
    lua_pop(co, 1);

    const auto found = std::find_if(m_chunks.begin(), m_chunks.end(), [&](const Chunk& c) { return c.name == chunk; });
    if (found != m_chunks.end()) {
        if (found->closureRef >= 0)
            lua_unref(L, found->closureRef);
        found->closureRef = ref;
    }
    else {
        m_chunks.push_back(Chunk{.name = std::string(chunk), .closureRef = ref});
    }

    // Everything already asked for on this chunk, applied now that there is a
    // function to apply it to. This is what makes "set a breakpoint, then press
    // play" -- the order people actually use -- work.
    for (Armed& armed : m_armed) {
        if (armed.chunk != chunk)
            continue;
        lua_getref(L, ref);
        const int target = lua_breakpoint(L, -1, static_cast<int>(armed.line), 1);
        lua_pop(L, 1);
        armed.boundLine = target > 0 ? static_cast<u32>(target) : 0u;
    }
}

void Debugger::resume(lua_State* L)
{
    if (!m_snapshot.parked)
        return;

    const std::string skipChunk = m_snapshot.chunk;
    const u32 skipLine = m_snapshot.line;

    const int threadRef = m_threadRef;
    m_threadRef = -1;
    m_snapshot = DebugSnapshot{};
    if (threadRef < 0)
        return;

    // Armed before the resume, because the hook fires inside it.
    m_skipChunk = skipChunk;
    m_skipLine = skipLine;
    m_skipArmed = true;

    lua_getref(L, threadRef);
    lua_State* co = lua_tothread(L, -1);
    lua_pop(L, 1);

    // **Resumed here rather than pushed back through the deferred queue**, and
    // the first draft did the latter. A thread parked on `LUA_BREAK` is not a
    // thread waiting to START, which is what `enqueueTaskCallback` is for: the
    // queue resumed it as though it were fresh and the script never continued.
    // Two tests caught it, which is the whole reason they exist.
    //
    // Out of band is not a determinism problem: the only caller is the frame
    // loop's safe point, which is where a drain would have run anyway.
    if (co != nullptr) {
        const int status = lua_resume(co, nullptr, 0);
        // `LUA_BREAK` again is a step arriving, and the hook has already taken
        // its own reference by the time this returns.
        if (status != LUA_OK && status != LUA_YIELD && status != LUA_BREAK) {
            const char* message = lua_tostring(co, -1);
            core::logText(core::LogLevel::Error, std::string("A script resumed from a breakpoint failed: ") +
                                                     (message != nullptr ? message : "unknown error"));
        }
    }

    lua_unref(L, threadRef);
}

void Debugger::stepOver(lua_State* L)
{
    if (!m_snapshot.parked)
        return;
    lua_getref(L, m_threadRef);
    if (lua_State* co = lua_tothread(L, -1); co != nullptr) {
        m_step = StepMode::Over;
        m_stepDepth = lua_stackdepth(co);
        m_stepFromLine = m_snapshot.line;
        lua_singlestep(co, 1);
    }
    lua_pop(L, 1);
    resume(L);
}

void Debugger::stepInto(lua_State* L)
{
    if (!m_snapshot.parked)
        return;
    lua_getref(L, m_threadRef);
    if (lua_State* co = lua_tothread(L, -1); co != nullptr) {
        m_step = StepMode::Into;
        m_stepDepth = lua_stackdepth(co);
        m_stepFromLine = m_snapshot.line;
        lua_singlestep(co, 1);
    }
    lua_pop(L, 1);
    resume(L);
}

void Debugger::stepOut(lua_State* L)
{
    if (!m_snapshot.parked)
        return;
    lua_getref(L, m_threadRef);
    if (lua_State* co = lua_tothread(L, -1); co != nullptr) {
        m_step = StepMode::Out;
        m_stepDepth = lua_stackdepth(co);
        m_stepFromLine = m_snapshot.line;
        lua_singlestep(co, 1);
    }
    lua_pop(L, 1);
    resume(L);
}

void Debugger::detach(lua_State* L)
{
    if (m_threadRef >= 0) {
        lua_unref(L, m_threadRef);
        m_threadRef = -1;
    }
    m_snapshot = DebugSnapshot{};
    m_step = StepMode::None;
    m_stepDepth = -1;
    m_skipArmed = false;
    // The chunk references go with the VM they were taken in, so this is the
    // whole of "a parked coroutine cannot survive its own state".
    m_chunks.clear();
    for (Armed& armed : m_armed)
        armed.boundLine = 0;
}

} // namespace luaug::script
