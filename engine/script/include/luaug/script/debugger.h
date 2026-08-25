// Breakpoints, stepping and inspection against the live VM (ADR 0057).
//
// **The VM has had this API since it was vendored and nothing has ever called
// it**: `lua_breakpoint`, `lua_singlestep`, `lua_atbreakpoint` and the
// `lua_Callbacks` hooks `debugbreak`/`debugstep` (`lua.h:555-559`, `:609-612`).
// Scripts are already compiled with `debugLevel = 2` (`modules.cpp:45-47`),
// which is what puts local names and line information in the bytecode. And
// `signals.cpp:507-513` already anticipates the parked state in as many words:
// *"a debugger break, neither error nor yield. Treated like a yield -- the
// thread is still resumable and something else will resume it."*
//
// That something is this, and it has to be. Both resume sites drop their
// bookkeeping BEFORE the resume returns, so nothing else roots a broken
// coroutine -- the hook takes a registry reference and that reference is the
// only thing keeping the parked script alive.
//
// ## A hit breakpoint parks the script, not the engine
//
// The frame loop keeps drawing at sixty frames a second while a coroutine sits
// on `LUA_BREAK`: panels stay live, the camera still flies, and the world is
// inspectable. What stops is the SIMULATION -- `Editor::allowedTicks` answers
// zero while something is parked, which is the same function that already
// refuses ticks to a paused world.
//
// That gate is not a convenience, it is what keeps R10 true. ADR 0025's
// guarantee is indexed by TICKS, and `task.wait` deadlines are tick indices --
// so if ticks ran while a script was parked, the script would resume N ticks
// later than it would have without a debugger attached and the run would
// diverge. No tick begins while parked, so the tick sequence is byte-identical
// to the one without a debugger, and the pause happens between ticks as far as
// the simulation can tell.
//
// ## One call per script, not one per function
//
// `luaG_breakpoint` recurses into every nested proto
// (`VM/src/ldebug.cpp:427-430`), so `lua_breakpoint` on a chunk's top-level
// closure reaches every line in the file -- including inside closures that do
// not exist yet. There is no "the function has not been created" problem, which
// is what would otherwise have been the hardest part of this.
//
// It also answers the line it really landed on (`getnextline`, `:503-537`,
// "the next closest line with valid instructions"), so a marker clicked on a
// comment is drawn where it will actually stop rather than where the click was.
//
// ## What this deliberately does not do
//
// **Write a variable.** `lua_setlocal` is two lines of binding and a divergence
// generator: a value changed from a panel is a world the replay harness can no
// longer reproduce. If it is ever wanted it needs the treatment a hot reload
// gets, not a checkbox.
//
// **Work with native codegen on.** `luaG_breakpoint` skips a proto that has
// `execdata` unless the disable callback is set (`ldebug.cpp:389`), and
// `lua_getlocal` answers null for a native frame (`:84-85`). The engine links
// `Luau.CodeGen` and never calls it, so this holds today -- and whoever turns
// codegen on for speed must read this paragraph first, because stepping will
// stop working silently.
#pragma once

#include "luaug/core/id.h"
#include "luaug/core/types.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct lua_State;

namespace luaug::script {

// One local or upvalue, copied out. **Copied, and that is the rule rather than
// a habit**: a `const char*` from `lua_getlocal` and a `lua_Debug::short_src`
// both point into storage that does not outlive the pause.
struct DebugValue
{
    std::string name;
    std::string type;
    // What the panel shows without expanding: a number, a string, `Part
    // "Baseplate"`. Never the whole of a table.
    std::string preview;
};

struct DebugFrame
{
    std::string function;
    // The chunk name, which is what a breakpoint is keyed on.
    std::string chunk;
    // ONE-based, as Luau reports it, and converted where a human reads it
    // rather than at every comparison.
    core::u32 line = 0;
    std::vector<DebugValue> locals;
    std::vector<DebugValue> upvalues;
};

enum class BreakReason : core::u8
{
    Breakpoint,
    Step,
};

// What the panel draws. Filled once, at the moment of the break.
struct DebugSnapshot
{
    bool parked = false;
    BreakReason reason = BreakReason::Breakpoint;
    std::string chunk;
    core::u32 line = 0;
    // Innermost first.
    std::vector<DebugFrame> frames;
};

// Caps rather than guesses: a recursive script has twenty thousand frames and
// the panel draws eight rows.
inline constexpr core::u32 kMaxDebugFrames = 32;
inline constexpr core::u32 kMaxDebugValues = 64;

class Debugger
{
public:
    // Installs the hooks. Called once, from `ScriptRuntime::boot`, and
    // unconditionally -- the no-op build has the same signature so the caller
    // carries no `#ifdef` (ADR 0011's rule).
    void install(lua_State* L);

    // Arms a line. Returns the line the VM actually put it on, or 0 when the
    // chunk is not loaded yet or has no instructions at or after that line.
    //
    // **Zero is a normal answer and the panel says so**: a breakpoint set before
    // the world runs, or one on a comment, is drawn hollow rather than filled.
    core::u32 setBreakpoint(lua_State* L, std::string_view chunk, core::u32 line);
    void clearBreakpoint(lua_State* L, std::string_view chunk, core::u32 line);

    // Remembers a chunk's top-level closure so breakpoints can be applied to it,
    // and applies every breakpoint already asked for on that chunk. Called
    // immediately after the chunk loads, with the closure on `co`'s stack.
    void bindChunk(lua_State* L, lua_State* co, std::string_view chunk, int closureIndex);

    [[nodiscard]] bool parked() const noexcept { return m_snapshot.parked; }
    [[nodiscard]] const DebugSnapshot& snapshot() const noexcept { return m_snapshot; }

    // Lets the parked script go. Re-enqueued through the deferred queue rather
    // than resumed here, so it starts again inside a drain and in the
    // scheduler's order rather than out of band.
    void resume(lua_State* L);
    // Runs to the next line in the same frame or shallower.
    void stepOver(lua_State* L);
    // Runs to the next line anywhere, including into a call.
    void stepInto(lua_State* L);
    // Runs until the current frame returns.
    void stepOut(lua_State* L);

    // Forgets a parked thread without resuming it. **Before a reload**, because
    // a parked coroutine cannot survive the VM it lives in and pretending
    // otherwise is a dangling `lua_State*`.
    void detach(lua_State* L);

    // **Called by the VM's own hook and by nothing else.** Public because the
    // hook is a C function pointer and cannot be a member; the alternative was
    // friending a signature with `lua_Debug` in it, which would have put a Luau
    // type in this header for one line.
    void onBreak(lua_State* co, BreakReason reason);
    // Whether a step is in flight, so the single-step hook can decide without
    // reaching into the members.
    [[nodiscard]] bool stepping() const noexcept { return m_step != StepMode::None; }
    void onStep(lua_State* co);

private:
    struct Armed
    {
        std::string chunk;
        core::u32 line = 0;
        core::u32 boundLine = 0;
    };

    struct Chunk
    {
        std::string name;
        // A registry reference to the top-level closure. **Load-bearing**:
        // without a function on the stack there is nothing to hand
        // `lua_breakpoint`, and recompiling to get one would produce a
        // different `Proto` and patch opcodes nothing is executing -- a
        // breakpoint that is set, shown, and never hits.
        int closureRef = -1;
    };

    std::vector<Armed> m_armed;
    std::vector<Chunk> m_chunks;

    // The parked thread's registry reference, and the only root it has.
    int m_threadRef = -1;
    DebugSnapshot m_snapshot;

    // **Where we were when Continue was pressed, skipped exactly once.**
    //
    // `LOP_BREAK` re-executes on resume -- `pc` still points at the patched
    // instruction and `lua_resume` clears the status the hook set, so the hook
    // fires again at once and a naive Continue parks on the same line forever.
    // One skip is exactly right: the instruction being re-entered is the one
    // already stopped on, and a genuine second visit to that line arrives
    // through a different execution of it.
    std::string m_skipChunk;
    core::u32 m_skipLine = 0;
    bool m_skipArmed = false;

    // Stepping: the stack depth and the line the step began at, so "over" and
    // "out" know when they have arrived and so the first step hook -- which
    // fires before anything has moved -- does not stop where it started.
    int m_stepDepth = -1;
    core::u32 m_stepFromLine = 0;
    enum class StepMode : core::u8
    {
        None,
        Over,
        Into,
        Out,
    } m_step = StepMode::None;
};

} // namespace luaug::script
