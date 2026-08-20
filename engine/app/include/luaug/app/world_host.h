// The world, the VM, and the project they run (architecture.md §2 "app", §3).
//
// This is what `engine/app/src/script_host.cpp` and `preview_api.cpp` were
// standing in for through M1: the reflection registries, a `scene::World`, a
// booted `script::ScriptRuntime`, and the file-backed half of `require`.
//
// `app` owns it rather than `script` for one reason that decides the rest:
// resolution policy belongs with the filesystem. `script` never opens a file --
// it asks a `ModuleLoader` for source at a canonical project-relative path --
// and that is what keeps a Luau require deterministic under R10 and lets a test
// mount a project that exists only in memory.
#pragma once

#include "luaug/app/preserved.h"
#include "luaug/core/error.h"
#include "luaug/core/name_atom.h"
#include "luaug/physics/backends.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/physics_sync.h"
#include "luaug/scene/world.h"
#include "luaug/script/modules.h"
#include "luaug/script/runtime.h"
#include "luaug/script/services.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace luaug::render {
class DebugDraw;
}

namespace luaug::app {

using core::f32;
using core::f64;

struct WorldHostOptions
{
    // A directory is a project root and gets the full mount; a file is mounted
    // as a single entry `Script` (M2 brief, Decision 9). Empty runs an empty
    // world, which is what `luaug-host --version` and the render gates need.
    std::filesystem::path projectPath;

    // The world's own deterministic stream. Recorded in a replay, because a
    // replay stores seeds and never draws (ADR 0025).
    core::u64 seed = 1;

    f64 fixedTimestep = 1.0 / 60.0;

    // The hot-reload bag, owned by whoever outlives this host -- which is the
    // point of it (ADR 0024). Null runs the world with the runtime's own bag,
    // which dies with the VM and is right for a world nobody will reload.
    script::ReloadState* reloadState = nullptr;

    // Whether this world is the product of a reload, which is the whole of what
    // `HotReloadService:IsReload` answers.
    bool isReload = false;

    // The `PreserveOnReload` instances the outgoing world was carrying. They go
    // into the tree after the project is mounted and **before** the entry
    // scripts are deferred, so a script that looks for what it left behind
    // finds it already there (M3 brief Decision 5). Null for a cold boot.
    const std::vector<PreservedTree>* preserved = nullptr;

    // Every `*.spec.luau` under this directory is mounted as an entry `Script`
    // alongside the project's own, and one more synthesized entry runs the
    // suite once `game.Loaded` has fired (api-design.md §3). Empty means an
    // ordinary run.
    std::filesystem::path conformanceRoot;
};

// What the conformance run reported. Read after the loop, because the run ends
// by calling `game:Shutdown()` and the host notices that the way it notices any
// other shutdown.
struct ConformanceReport
{
    bool ran = false;
    core::i64 total = 0;
    core::i64 passed = 0;
    core::i64 failed = 0;

    // The per-case results, already JSON, as the runner produced them. Written
    // verbatim to `--test-report=PATH`; `luaug test` turns it into TAP or JUnit
    // rather than parsing a console whose every line is catalog-resolved
    // (M3 brief, Decision 6).
    std::string json;
};

class WorldHost
{
public:
    WorldHost();
    ~WorldHost();

    WorldHost(const WorldHost&) = delete;
    WorldHost& operator=(const WorldHost&) = delete;

    // Boots the VM, mounts the project and defers every entry script. The
    // scripts do not run here: they run at the first drain, which is what makes
    // their first resumption a scheduled event like any other (api-design.md
    // §3).
    [[nodiscard]] std::optional<core::EngineError> boot(const WorldHostOptions& options);

    // One simulation tick: the four phase signals with a drain after each, the
    // task-resume phase between `PostSimulation` and `Heartbeat`, and the
    // retirement of everything destroyed during it. This is architecture.md §3's
    // resumption order, and it is the only place it is written down as code.
    void tick();

    // Publishes the engine's own instrumentation for this frame. Called between
    // frames, so a stat never changes halfway through a tick that reads it.
    void publishStats(const script::FrameStats& stats);

    // The render-rate phase. Never fires headless -- headless is the same
    // scheduler minus the render steps, and this is one of them.
    void preRender(f64 renderDt);

    // Where `DebugService`'s gizmos go this frame. Null clears it, which is what
    // a headless run leaves it as: the calls become silent no-ops rather than
    // errors, so debug drawing left in shared code cannot fail a headless test.
    void setGizmoTarget(render::DebugDraw* draw);

    // Entry scripts this world mounted, and how many of them failed to
    // compile. The reload reads both: a reload that mounted nothing, or that
    // mounted something it could not compile, is a gate passing while doing
    // nothing (M2 Finding 19).
    // What the restore did, filled during `boot`. Zeroes on a cold boot.
    [[nodiscard]] const PreserveReport& preserveReport() const noexcept { return m_preserveReport; }

    [[nodiscard]] core::u64 mountedScriptCount() const;
    [[nodiscard]] core::u64 scriptLoadFailures() const;

    [[nodiscard]] scene::World& world() noexcept { return *m_world; }

    // `Workspace`, which is what `render::extract` treats as the world root:
    // whatever is parented under it is in the world and whatever is not, is not.
    [[nodiscard]] core::InstanceId workspace() const noexcept { return m_workspace; }

    // The `Lighting` service, which carries the environment `extract` reads.
    // Invalid in a build with no render module, which is not an error.
    [[nodiscard]] core::InstanceId lighting() const noexcept { return m_lighting; }

    // The physics mirror, or null in a build with no physics backend. The world
    // owns it because a hot reload rebuilds the world, and a simulation that
    // outlived the tree it mirrors would be holding bodies for parts that no
    // longer exist.
    // The keyboard the next tick reads (M5's scaffold). The host owns the
    // snapshot because it owns the event pump; the replay harness hands in a
    // recorded one instead, which is the whole reason this is a setter rather
    // than a device read inside the binding.
    void setKeyboard(std::span<const bool> down);

    [[nodiscard]] scene::PhysicsSync* physics() noexcept { return m_physics ? &*m_physics : nullptr; }
    [[nodiscard]] const scene::PhysicsSync* physics() const noexcept { return m_physics ? &*m_physics : nullptr; }
    [[nodiscard]] script::ScriptRuntime& runtime() noexcept { return *m_runtime; }
    [[nodiscard]] bool shutdownRequested();

    // Read off `game`'s attributes, which is where the runner script puts them.
    // Attributes rather than a private channel, because the runner is an
    // ordinary entry script and everything it does should be something a
    // project could do.
    [[nodiscard]] ConformanceReport conformanceReport() const;

    // Runs the `BindToClose` callbacks and WAITS for them, up to
    // `graceSeconds` of wall clock (`architecture.md` §app: "wait <= 30 s
    // (configurable)").
    //
    // Waiting means advancing the world: a handler that yields on `task.wait`
    // resumes on the SimClock, so a shutdown that drained once and left would
    // cut off every handler that saved anything asynchronously -- which is
    // exactly what it did until M5 (D016).
    //
    // The cap is wall clock rather than sim time, because its job is to stop a
    // handler that never finishes from holding the process open, and a handler
    // that never finishes never advances sim time either.
    void close(core::f64 graceSeconds = 30.0);

    // `HotReloadService.PreReload` on the outgoing world and `PostReload` on
    // the incoming one, each fired and then drained -- the drain is the point,
    // because a handler that has not run yet has not saved anything yet.
    void firePreReload();
    void firePostReload();

private:
    // Reads `@luaug/*` out of the content directory and registers each. They are
    // shipped content rather than compiled-in strings so that a project can read
    // the same file its editor does.
    [[nodiscard]] std::optional<core::EngineError> registerRuntimeModules();

    [[nodiscard]] std::optional<core::EngineError> mountProject(const std::filesystem::path& path);
    [[nodiscard]] std::optional<core::EngineError> mountConformance(const std::filesystem::path& root);

    core::AtomTable m_atoms;
    scene::ClassRegistry m_classes;
    scene::EnumRegistry m_enums;
    // Constructed in `boot`, because the registries have to be populated first
    // and a `World` holds references to them.
    std::optional<scene::World> m_world;
    std::optional<script::ScriptRuntime> m_runtime;
    // Declared before the mirror, and destroyed after it: the mirror holds a
    // reference to this and tears its world down in its own destructor.
    physics::PhysicsResult m_backend;
    std::optional<scene::PhysicsSync> m_physics;

    std::filesystem::path m_root;
    core::InstanceId m_workspace;
    core::InstanceId m_lighting;
    PreserveReport m_preserveReport;
    render::DebugDraw* m_gizmos = nullptr;

    // Aliases from the project's `.luaurc`, read once at boot. Parsed with
    // `core::json` rather than `Luau::parseConfig`, which treats any key it does
    // not recognise as a hard error that aborts the whole require (U-42) -- a
    // `$schema` line would break `require` at runtime.
    std::unordered_map<std::string, std::string> m_aliases;

    friend struct WorldHostLoader;
};

} // namespace luaug::app
