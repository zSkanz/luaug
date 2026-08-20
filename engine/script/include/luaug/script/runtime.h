// The game VM and everything that crosses into it (architecture.md §5).
//
// One `lua_State` on the main thread for all gameplay. The `VmPool` shape that
// architecture.md sketches for future actor VMs is deliberately NOT built here:
// v1 has one VM, and inventing a pool around it now would be inventing the
// seams M2 is meant to design from a position of having built nothing.
//
// `ScriptRuntime` owns the boot order, and the boot order is the part that is
// easy to get wrong invisibly -- see `binding.h` for the four VM properties
// that constrain it.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "luaug/core/error.h"
#include "luaug/core/phase.h"
#include "luaug/scene/world.h"
#include "luaug/script/binding.h"
#include "luaug/script/instance_binding.h"
#include "luaug/script/modules.h"
#include "luaug/script/services.h"

struct lua_State;

namespace luaug::script
{

class ScriptRuntime
{
public:
    explicit ScriptRuntime(scene::World& world);
    ~ScriptRuntime();

    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    // Opens the VM in the one order that works: allocator, `useratom`, standard
    // libraries, removals, tag metatables, globals, then `luaL_sandbox`. Every
    // step after the sandbox is a step that silently does nothing.
    [[nodiscard]] std::optional<core::EngineError> boot();

    // Compiles and starts `source` on its own sandboxed thread. Returns the
    // structured error that ended it, or nullopt -- including when the script
    // merely yielded, which is the normal outcome for anything that calls
    // `task.wait`.
    [[nodiscard]] std::optional<core::EngineError> runSource(std::string_view source, std::string_view chunkName);

    // Runs the engine phase's deferred work: converts the scene's POD facts
    // into signal fires, then drains the queue to fixpoint (api-design.md
    // §3.1). Every resumption point calls this.
    void drain(core::Phase phase);

    // Resumes whatever `task.wait` and `task.delay` are due at the world's
    // current tick. Between `PostSimulation` and `Heartbeat`, per
    // architecture.md §3. Takes no time argument: a deadline is a tick index and
    // the tick lives on `EngineState`, so passing seconds here would be handing
    // the scheduler a float to re-derive an integer from.
    void resumeTimers();

    // Fires an engine-raised event on a service instance -- `Heartbeat` and the
    // other phase signals. Enqueues; the drain is what runs the handlers.
    void fireEvent(core::InstanceId instance, core::NameAtom event, f64 argument);

    // `RunService`'s phase signal for this resumption point, carrying `delta` in
    // seconds -- the fixed tick duration for the four sim phases, and the
    // variable time since the last render for `PreRender`. A phase with no
    // signal of its own is a no-op, so the scheduler can call this at every
    // resumption point without a table of which ones have one.
    void firePhase(core::Phase phase, f64 delta);

    // Fires `game.Loaded`, once, after every entry script has had its first
    // resumption (api-design.md §3).
    void fireLoaded();

    // How deep the current drain has gone. Exposed for the tests that pin the
    // re-entrancy cap, which is otherwise only observable through a log line.
    [[nodiscard]] u32 deferredDepth() const noexcept;

    // Where `DebugService`'s gizmos go. Null in a headless run, which is why
    // those calls are documented no-ops there rather than errors.
    void setGizmoSink(const GizmoSink& sink);

    // Where `require` gets file-backed module source. Unset means only the
    // registered `@luaug/…` modules resolve, which is what a test wants.
    void setModuleLoader(const ModuleLoader& loader);

    // What the boot-time method cross-check found. Zeroed until `boot` runs.
    // Exposed rather than logged so that a test can assert the two halves
    // rather than a human having to read a startup line.
    [[nodiscard]] MethodCoverage methodCoverage() const noexcept;

    [[nodiscard]] lua_State* state() const noexcept;

    // The DataModel `game` names. Invalid until `boot` runs; the host needs it
    // to find `Workspace`, which is the root `render::extract` reads from.
    [[nodiscard]] core::InstanceId dataModel() const noexcept;
    [[nodiscard]] scene::World& world() noexcept { return m_world; }

private:
    struct Impl;

    scene::World& m_world;
    // Pimpl because every member worth having names a Luau type, and
    // architecture.md §2 rule 2 keeps `lua.h` out of any header a lower module
    // could include.
    std::unique_ptr<Impl> m_impl;
};

} // namespace luaug::script
