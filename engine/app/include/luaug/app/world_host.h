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
#include "luaug/audio/audio.h"
#include "luaug/core/error.h"
#include "luaug/core/name_atom.h"
#include "luaug/input/input.h"
#include "luaug/physics/backends.h"
#include "luaug/render/animation.h"
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

    // Whether the process has a window. The only thing the host does with it is
    // decide whether to open an audio device: a headless run has no reason to
    // hold one, and on a CI runner the attempt costs a second and a log line
    // nobody reads. The sound TIMELINE runs either way, which is what makes
    // `Ended` land on the same tick in both.
    bool headless = true;

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

    // `game`. Where an attribute a whole run has to agree about lives -- the
    // conformance report's counters, and the flag a replay scenario names.
    [[nodiscard]] core::InstanceId dataModel() const noexcept;

    // The `Lighting` service, which carries the environment `extract` reads.
    // Invalid in a build with no render module, which is not an error.
    [[nodiscard]] core::InstanceId lighting() const noexcept { return m_lighting; }

    // The parent of every `ScreenGui` (M6). A boot service for the same reason
    // `Lighting` is: the frame reads it whether or not a script asks for it,
    // and a service resolved after the host cached its id is how M4 spent four
    // milestones lighting scenes with defaults.
    [[nodiscard]] core::InstanceId uiService() const noexcept { return m_uiService; }

    // The mixer and the sound timeline (M6). Owned here beside the physics
    // mirror and the input system, and for the same reasons.
    [[nodiscard]] audio::AudioSystem& audio() noexcept { return m_audio; }

    // `Workspace.CurrentCamera`, which is the audio listener (§2.1). Resolved
    // per call rather than cached: it is a property a script may reassign, and a
    // cached id is how M4 spent four milestones lighting scenes with defaults.
    [[nodiscard]] core::InstanceId currentCamera() const noexcept
    {
        const scene::WorkspaceComponent* component = m_world->workspaces().find(m_workspace);
        return component == nullptr ? core::InstanceId{} : component->currentCamera;
    }

    // The physics mirror, or null in a build with no physics backend. The world
    // owns it because a hot reload rebuilds the world, and a simulation that
    // outlived the tree it mirrors would be holding bodies for parts that no
    // longer exist.
    // The Input Action System (M6). The host owns it for the reason it owns the
    // physics mirror: `scene` cannot hold it without L3 depending on
    // `platform`, and a process-global would make two worlds in one process
    // share a keyboard.
    //
    // `pumpInput` folds a frame's events into the device snapshot and is the
    // only caller that reads a device at all; `tick` and `preRender` dispatch.
    // A replay drives `input().setSnapshot` instead of pumping, which is what
    // makes the replay a replay of INPUT rather than of the API underneath it.
    void pumpInput(std::span<const platform::Event> events);
    [[nodiscard]] input::InputSystem& input() noexcept { return m_input; }

    // The skeletons and clips read out of each skinned glTF. Read-only from
    // outside: the host fills it, at the tick's own safe point, because
    // animation is SIMULATION -- it advances on the SimClock, a script reads
    // `TimePosition` off it, and it has to run in a headless replay where there
    // is no renderer at all.
    [[nodiscard]] const render::SkeletonLibrary& skeletons() const noexcept { return m_skeletons; }

    // The poses `render::extract` reads. Null before `boot`, which is the same
    // window in which there is no world to extract from.
    [[nodiscard]] const render::AnimationSystem* animation() const noexcept
    {
        return m_animation ? &*m_animation : nullptr;
    }

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

    // Reads the skeleton and clips of every `MeshPart` content the library does
    // not yet hold, once per URN. A parse and nothing else: no image is decoded
    // and no vertex is touched (`GltfImportOptions::skeletonOnly`).
    //
    // Synchronous, at the top of a tick, which is the same narrowing
    // `MeshLoader` made for the same reason: M7 is the milestone with a job pool
    // and something to stream, and a background loader with one caller and no
    // eviction policy is the speculative half of the design.
    void syncSkeletons();

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
    input::InputSystem m_input;

    // The skeletons the mesh loader reads out of each glTF, and the system that
    // plays their clips. Both live here rather than beside the renderer because
    // animation is SIMULATION: it advances on the SimClock, it has to run in a
    // headless replay, and a script reads `TimePosition` off it. What the
    // renderer takes is the pose it produces.
    //
    // Declared after `m_world` because the system holds a reference to it, and
    // before nothing: `m_animation` is destroyed first, which is the order its
    // own references need.
    render::SkeletonLibrary m_skeletons;
    // Content atoms already attempted, so a file with no skeleton is parsed once
    // rather than once a tick forever.
    std::vector<core::u32> m_skeletonsTried;
    std::optional<render::AnimationSystem> m_animation;

    std::filesystem::path m_root;
    core::InstanceId m_workspace;
    core::InstanceId m_lighting;
    core::InstanceId m_uiService;
    audio::AudioSystem m_audio;
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
