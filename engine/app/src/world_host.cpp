#include "luaug/app/world_host.h"

#include "luaug/asset/gltf.h"
#include "luaug/audio/scene_types.h"
#include "luaug/core/build_info.h"
#include "luaug/core/json.h"
#include "luaug/core/log.h"
#include "luaug/input/scene_types.h"
#include "luaug/platform/file.h"
#include "luaug/platform/platform.h"
#include "luaug/render/debug_draw.h"
#include "luaug/render/scene_types.h"
#include "luaug/script/net_module.h"
#include "luaug/ui/scene_types.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

#include "class_descriptors.gen.h"

namespace luaug::app {
namespace {

using core::I18nArg;
using core::LogLevel;

// The `@luaug/…` modules that ship as content. One entry per module rather than
// a directory walk, because the set is the engine's own surface (ADR 0030) and
// discovering it from a directory would make an accidentally-shipped file part
// of the API.
constexpr std::string_view RuntimeModules[] = {"camera", "testing"};

// The conformance runner, as an ordinary entry script.
//
// Luau in a C++ string is not a thing to do lightly, and there are two reasons
// it is right here. It has to run FROM Luau: a case body may `task.wait`, and a
// `lua_call` from a C function cannot be resumed across a yield (U-34) -- so a
// C-side runner could only run suites that never wait, which is most of what
// there is to test. And it has to be an entry script rather than host
// machinery, because everything it does -- `game.Loaded`, attributes,
// `Shutdown` -- is something a project could do, and a runner with a private
// channel into the host would be testing a world no game will ever run in.
// The conformance runner is a real file staged with the rest of the runtime
// content, not a string literal here. It was written as a literal first, and
// the escaping alone made it unreadable -- as a file it is analysed by the gate
// and formatted like everything else.
constexpr std::string_view ConformanceRunnerPath = "runtime/conformance/runner.luau";

[[nodiscard]] bool readFile(const std::filesystem::path& path, std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

// Project-relative paths use '/' whatever the platform does, because they are
// the cache key `require` compares and a key that reads `a\b` on one machine and
// `a/b` on another is two modules where there is one.
[[nodiscard]] std::string toProjectPath(const std::filesystem::path& path)
{
    std::string text = path.generic_string();
    return text;
}

[[nodiscard]] std::string_view directoryOf(std::string_view path)
{
    const std::string::size_type slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash);
}

// Resolves `.` and `..` without touching the filesystem, so a specifier cannot
// escape the project root by spelling enough `..`s and so the answer does not
// depend on what happens to exist.
[[nodiscard]] bool normalisePath(std::string_view input, std::string& out)
{
    std::vector<std::string_view> stack;
    std::string::size_type start = 0;
    for (std::string::size_type index = 0; index <= input.size(); ++index) {
        if (index != input.size() && input[index] != '/')
            continue;

        const std::string_view segment = input.substr(start, index - start);
        start = index + 1;

        if (segment.empty() || segment == ".")
            continue;
        if (segment == "..") {
            if (stack.empty())
                return false; // above the project root, which is not a place
            stack.pop_back();
            continue;
        }
        stack.push_back(segment);
    }

    out.clear();
    for (std::string_view segment : stack) {
        if (!out.empty())
            out.push_back('/');
        out.append(segment);
    }
    return !out.empty();
}

} // namespace

// The `ModuleLoader` vtable, as a struct so its two functions can name the host
// without the host having to expose them.
struct WorldHostLoader
{
    static bool resolve(void* user, std::string_view fromPath, std::string_view specifier, std::string& outPath)
    {
        auto& host = *static_cast<WorldHost*>(user);
        if (host.m_root.empty() || specifier.empty())
            return false;

        std::string candidate;
        if (specifier.front() == '@') {
            // `@self` is the requiring file's own directory, and an alias is
            // whatever `.luaurc` said. Resolved here rather than through
            // `Luau::parseConfig` because that treats an unrecognised key as a
            // hard error that aborts the require (U-42) -- a `$schema` line
            // would break `require` at runtime.
            const std::string::size_type slash = specifier.find('/');
            const std::string_view head = specifier.substr(1, slash == std::string_view::npos ? slash : slash - 1);
            const std::string_view tail =
                slash == std::string_view::npos ? std::string_view{} : specifier.substr(slash + 1);

            if (head == "self") {
                candidate.assign(directoryOf(fromPath));
            }
            else {
                const auto alias = host.m_aliases.find(std::string(head));
                if (alias == host.m_aliases.end())
                    return false;
                candidate = alias->second;
            }

            if (!tail.empty()) {
                if (!candidate.empty())
                    candidate.push_back('/');
                candidate.append(tail);
            }
        }
        else if (specifier.starts_with("./") || specifier.starts_with("../")) {
            candidate.assign(directoryOf(fromPath));
            if (!candidate.empty())
                candidate.push_back('/');
            candidate.append(specifier);
        }
        else {
            // A bare specifier is project-root relative. Deliberately not a
            // search path: one place to look means one answer, and an ambiguity
            // a search path would resolve silently is a bug worth an error.
            candidate.assign(specifier);
        }

        std::string normalised;
        if (!normalisePath(candidate, normalised))
            return false;

        // The extension is added rather than required, and `init.luau` is the
        // directory form. Both are tried in a fixed order so the answer never
        // depends on which file was created first.
        const std::string withExtension = normalised.ends_with(".luau") ? normalised : normalised + ".luau";
        if (std::filesystem::is_regular_file(host.m_root / withExtension)) {
            outPath = withExtension;
            return true;
        }

        const std::string asDirectory = normalised + "/init.luau";
        if (std::filesystem::is_regular_file(host.m_root / asDirectory)) {
            outPath = asDirectory;
            return true;
        }
        return false;
    }

    static bool read(void* user, std::string_view path, std::string& outSource)
    {
        auto& host = *static_cast<WorldHost*>(user);
        return readFile(host.m_root / std::filesystem::path(path), outSource);
    }
};

WorldHost::WorldHost() = default;
WorldHost::~WorldHost() = default;

std::optional<core::EngineError> WorldHost::boot(const WorldHostOptions& options)
{
    // The order is load-bearing, not incidental. `scene` owns the registry and
    // the root of the hierarchy, and a class registered by a higher module
    // names its parent's `ClassId` -- which exists only after the module that
    // owns the parent has run. So the calls go in layer order, lowest first,
    // the same order api/generator/gen_cpp.luau emits the files in
    // (architecture.md §2, rule 3: higher modules register INTO scene's
    // registries). `app` is the only place that sees every module, which is why
    // it is the only place this sequence can be written down.
    scene::generated::registerClasses(m_classes, m_atoms);
    render::registerSceneTypes(m_classes, m_atoms);
    input::registerSceneTypes(m_classes, m_atoms);
    audio::registerSceneTypes(m_classes, m_atoms);
    ui::registerSceneTypes(m_classes, m_atoms);

    // Enums have one owner and no hierarchy, so they are independent of the
    // above; they stay with `scene`, which holds the registry.
    scene::generated::registerEnums(m_enums, m_atoms);

    m_world.emplace(m_classes, m_enums, m_atoms, options.seed);
    m_world->engineState().engineVersion = LUAUG_VERSION_STRING;
    m_world->engineState().luauVersion = LUAUG_LUAU_VERSION;
    m_world->engineState().fixedTimestep = options.fixedTimestep;
    // Both, so a read before any write gives what the scheduler is running on
    // rather than the struct's default.
    m_world->engineState().requestedFixedTimestep = options.fixedTimestep;

    m_runtime.emplace(*m_world);
    if (std::optional<core::EngineError> error = m_runtime->boot(); error.has_value())
        return error;

    m_runtime->setModuleLoader(script::ModuleLoader{
        .user = this,
        .resolve = &WorldHostLoader::resolve,
        .read = &WorldHostLoader::read,
    });

    // Before any script runs, because a script's file scope may call
    // `SaveState` and the bag it reaches has to be the host's by then.
    m_reloadState = options.reloadState;
    m_runtime->setReloadState(options.reloadState);
    if (options.reloadState != nullptr)
        options.reloadState->setIsReload(options.isReload);

    if (std::optional<core::EngineError> error = registerRuntimeModules(); error.has_value())
        return error;

    // Created by `registerServices` during the boot above, so this is a lookup
    // rather than a creation -- and it is cached because `extract` needs it
    // every frame.
    m_workspace = m_world->findFirstChildOfClass(m_runtime->dataModel(), m_classes.findId(m_atoms.lookup("Workspace")));
    // Cached for the same reason, and separately: `Lighting` is a sibling of
    // `Workspace` rather than a child of it, so `extract` cannot reach one from
    // the other.
    //
    // **This lookup is only a lookup because `Lighting` is a boot service.** It
    // was not, for the whole of M4: the service was created by its first
    // `GetService`, which is after this line, so the cache held an invalid id
    // for the life of the world and `extract` answered every frame with the
    // struct defaults. The renderer therefore drew a sun pinned straight up
    // over every scene, and the milestone's goldens recorded that faithfully.
    // `registerServices` now creates it at boot beside `Workspace`, which is
    // what makes the id correct by construction rather than by timing.
    //
    // An engine built without the render module registers no Lighting class at
    // all, and this stays invalid -- which `extract` reads as "no environment
    // state" and answers with the defaults, rather than as an error.
    m_lighting = m_world->findFirstChildOfClass(m_runtime->dataModel(), m_classes.findId(m_atoms.lookup("Lighting")));
    m_uiService = m_world->findFirstChildOfClass(m_runtime->dataModel(), m_classes.findId(m_atoms.lookup("UIService")));

    // The mixer opens a device on a windowed run and none on a headless one:
    // a headless run has no reason to hold one, and on a CI runner the attempt
    // costs a second and a log line nobody reads. The TIMELINE runs either way,
    // which is what makes `Ended` land on the same tick in both.
    (void)m_audio.start(options.headless);

    // Animation is created unconditionally, unlike the physics mirror: there is
    // no backend to be missing. A world whose meshes carry no skeleton simply
    // has an empty library, and every track it hands out plays nothing -- which
    // is the same answer a build with no render module gives.
    m_animation.emplace(*m_world, m_skeletons);
    m_runtime->setAnimation(&*m_animation);
    m_runtime->setInput(&m_input);

#if LUAUG_PHYSICS_JOLT
    // The one hand-written switch over what the build compiled in (ADR 0023),
    // the same shape the RHI backend is chosen with. A build with no physics
    // backend leaves the mirror null and every part stays where a script put
    // it -- which is what M0 through M4 were.
    m_backend = physics::createJoltPhysics();
    if (m_backend != nullptr) {
        m_physics.emplace(*m_world, *m_backend);
        // Handed over rather than looked up inside the mirror: `scene` has no
        // notion of the DataModel root. This assignment is the step M4.5's
        // lesson says to test -- an id resolved here and not there is exactly
        // how a renderer spent four milestones lighting scenes with defaults --
        // so `world_host_tests.cpp` asserts it on a world no script touched.
        m_physics->setWorkspace(m_workspace);
        // The same system that plays clips also owns the joints, and the mirror
        // is what knows when in the tick a pose may be committed.
        m_physics->setSkeleton(&*m_animation);
        // And the bindings, so `Workspace:Raycast` reads the same world the
        // tick steps rather than a second one.
        m_runtime->setPhysics(&*m_physics);
    }
#endif

    if (!options.projectPath.empty()) {
        if (std::optional<core::EngineError> error = mountProject(options.projectPath); error.has_value())
            return error;
    }

    if (!options.conformanceRoot.empty()) {
        if (std::optional<core::EngineError> error = mountConformance(options.conformanceRoot); error.has_value())
            return error;
    }

    // After the mount, so the tree is complete, and before the scripts are
    // deferred, so the first thing any of them can observe already includes
    // what the previous world was carrying (M3 brief Decision 5).
    if (options.preserved != nullptr)
        restorePreserved(*m_world, m_runtime->dataModel(), *options.preserved, m_preserveReport);

    // **The authored world, before the behaviour that runs on it** (ADR 0047).
    // After the mount, so a scene that names a class a project's own code
    // registers finds it; after the preserved restore, so a reload's carried
    // instances are in the tree the scene replaces around them; and before the
    // scripts, which is the half D067 was.
    //
    // A scene that will not read is reported and the boot continues. The world
    // then holds whatever the scripts build, which is the same world every
    // project before `06-scene` has -- a run that refused to start because a
    // file was malformed would be a worse answer than a run that says so.
    if (!options.bootScene.empty()) {
        // **The grid gets the scene before the world does** (ADR 0053). What
        // comes back is what stayed authored; the rest is cells, and the
        // streaming host has been told about them by the same call.
        std::filesystem::path sceneFile = options.bootScene;
        if (options.partitionScene) {
            if (std::filesystem::path partitioned = options.partitionScene(*m_world, options.bootScene);
                !partitioned.empty()) {
                sceneFile = std::move(partitioned);
            }
        }

        std::string sceneText;
        if (!readFile(sceneFile, sceneText)) {
            core::log(LogLevel::Warn, LUAUG_TR("scene.err.scene_unreadable"));
        }
        else if (const std::optional<core::EngineError> sceneError =
                     scene::readScene(*m_world, sceneText, &m_bootSceneReport, options.bootStamps);
                 sceneError.has_value()) {
            core::logText(LogLevel::Error, sceneError->message);
        }
        else {
            m_bootSceneApplied = true;
            const std::array<I18nArg, 1> args{I18nArg{"count", static_cast<core::i64>(m_bootSceneReport.instances)}};
            core::log(LogLevel::Info, LUAUG_TR("scene.info.scene_loaded"), args);
        }
    }

    // What the scene put under `Workspace`, counted before a line of script has
    // run -- see the warning after the drain.
    core::u32 authoredByScene = 0;
    if (m_bootSceneApplied) {
        for (core::InstanceId child = m_world->firstChild(m_workspace); child.valid();
             child = m_world->nextSibling(child)) {
            if (!m_world->generated(child))
                ++authoredByScene;
        }
    }

    // **`Instance.stamp` reads from the same place a scene load does**, so a
    // prefab named in code and one named in a file mean the same file (ADR
    // 0051). A project with no source gets none, and the binding says so
    // rather than handing back an empty prefab.
    m_stampSource = options.bootStamps;
    m_runtime->setStampSource(options.bootStamps);

    // **Mounted above; started here, and only if this run is one that starts
    // them** (ADR 0058). The editor is the one caller that says no: a project
    // opened in a tool shows what its scene holds, and behaviour begins when
    // somebody presses play.
    if (options.startScripts)
        script::startScripts(m_runtime->state());

    // The boot drain. api-design.md §3's lifecycle reads "start each Script on
    // its own coroutine via `task.defer` … → first frame", and the arrow is
    // load-bearing: the scripts have had their first resumption *before* the
    // first frame renders, which is also what makes `game.Loaded` a boot event
    // rather than a first-tick one.
    //
    // It advances no clock. `SimTime` is still zero here, so a script reading it
    // at file scope sees the same zero a `Heartbeat` handler would see on tick
    // one -- and the first frame shows a world that has been built rather than
    // an empty one.
    m_runtime->drain(core::Phase::FrameStart);
    m_world->retireDestroyed();

    // **Two sources for one world, said out loud** (D074).
    //
    // A scene is what a project STARTS with and a script is what it then does
    // (ADR 0047). A project that has both a scene and entry scripts which build
    // a world has two answers to the same question, and the engine cannot merge
    // them -- so a character saved into the scene and a character the script
    // makes are two characters, which is exactly what a person sees the first
    // time they save a scene of a world their code built.
    //
    // Counted rather than guessed, and reported with both numbers, because the
    // useful form of this is "the file gave you 6 and the scripts added 5" and
    // not "something may be duplicated". The resolution is the project's and
    // there are only two: stop building that world in code -- which is what
    // ADR 0047 asks projects to become and what `examples/06-scene` shows -- or
    // do not give this project a scene.
    if (m_bootSceneApplied) {
        core::u32 authoredNow = 0;
        for (core::InstanceId child = m_world->firstChild(m_workspace); child.valid();
             child = m_world->nextSibling(child)) {
            if (!m_world->generated(child))
                ++authoredNow;
        }
        if (authoredNow > authoredByScene) {
            const std::array<I18nArg, 2> args{
                I18nArg{"scene", static_cast<core::i64>(authoredByScene)},
                I18nArg{"scripts", static_cast<core::i64>(authoredNow - authoredByScene)},
            };
            core::log(LogLevel::Warn, LUAUG_TR("scene.warn.two_sources"), args);
        }
    }

    return std::nullopt;
}

std::optional<core::EngineError> WorldHost::restartRuntime()
{
    if (!m_runtime.has_value() || !m_world.has_value())
        return std::nullopt;

    // Read BEFORE the teardown, because both live in the VM that is about to go.
    const core::InstanceId dataModel = m_runtime->dataModel();
    std::vector<script::ModuleRegistry::Entry> mounted = script::mountedEntries(m_runtime->state());

    // Every connection, every required module, every queued resumption and every
    // timer goes with this line. That is the whole point of it.
    m_runtime.reset();

    m_runtime.emplace(*m_world);
    if (std::optional<core::EngineError> error = m_runtime->boot(dataModel); error.has_value())
        return error;

    m_runtime->setModuleLoader(script::ModuleLoader{
        .user = this,
        .resolve = &WorldHostLoader::resolve,
        .read = &WorldHostLoader::read,
    });
    m_runtime->setReloadState(m_reloadState);
    if (std::optional<core::EngineError> error = registerRuntimeModules(); error.has_value())
        return error;

    m_runtime->setStampSource(m_stampSource);
    script::adoptMountedEntries(m_runtime->state(), std::move(mounted));

    if (m_animation.has_value())
        m_runtime->setAnimation(&*m_animation);
    m_runtime->setInput(&m_input);
    if (m_physics.has_value())
        m_runtime->setPhysics(&*m_physics);

    // **The services are the ones that were already there.** `registerServices`
    // adopted the DataModel, and everything under it is found rather than made,
    // so these three ids are the same ids -- asserted rather than assumed,
    // because a service the host cached and the VM rebuilt would be exactly the
    // M4 lighting defect again (see `boot`).
    m_workspace = m_world->findFirstChildOfClass(dataModel, m_classes.findId(m_atoms.lookup("Workspace")));
    m_lighting = m_world->findFirstChildOfClass(dataModel, m_classes.findId(m_atoms.lookup("Lighting")));
    m_uiService = m_world->findFirstChildOfClass(dataModel, m_classes.findId(m_atoms.lookup("UIService")));

    // Consumed rather than queued: a rebuild is not a thing the world did, and
    // there is nothing connected yet that could observe it.
    (void)m_world->changes().take();
    return std::nullopt;
}

std::optional<core::EngineError> WorldHost::registerRuntimeModules()
{
    const std::filesystem::path base = platform::paths().contentDir / "runtime" / "luaug";
    for (const std::string_view name : RuntimeModules) {
        const std::filesystem::path path = base / std::filesystem::path(name) / "init.luau";
        std::string source;
        if (!readFile(path, source)) {
            const std::array<I18nArg, 1> args{I18nArg{"path", path.string()}};
            return core::makeError(LUAUG_TR("engine.cli.err.script_missing"), args);
        }
        script::registerModule(m_runtime->state(), std::string("@luaug/").append(name), source);
    }
    return std::nullopt;
}

std::optional<core::EngineError> WorldHost::mountProject(const std::filesystem::path& path)
{
    std::error_code ec;
    const bool isDirectory = std::filesystem::is_directory(path, ec);

    std::vector<script::MountedScript> entries;
    if (isDirectory) {
        // A directory is a project root and gets the full mount (M2 brief,
        // Decision 9).
        m_root = path;

        std::string config;
        if (readFile(m_root / ".luaurc", config)) {
            core::JsonDocument document;
            if (document.parse(config, ".luaurc")) {
                const core::JsonValue aliases = document.root()["aliases"];
                for (core::usize index = 0; index < aliases.size(); ++index) {
                    const std::string_view key = aliases.keyAt(index);
                    m_aliases.emplace(std::string(key), std::string(aliases[key].asString()));
                }
            }
            else {
                // Reported rather than fatal: a project with a malformed
                // `.luaurc` should still boot far enough to say so, which is not
                // what the vendored config reader does (U-42).
                core::logText(LogLevel::Warn, document.parse(config, ".luaurc").diagnostic);
            }
        }

        const std::filesystem::path scriptsRoot = m_root / "src" / "scripts";
        if (std::filesystem::is_directory(scriptsRoot, ec)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(scriptsRoot, ec)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".luau")
                    continue;
                std::string source;
                if (!readFile(entry.path(), source))
                    continue;
                entries.push_back(script::MountedScript{
                    .path = toProjectPath(std::filesystem::relative(entry.path(), m_root, ec)),
                    // Relative to `src/scripts`, so `enemy/patrol.luau` mounts
                    // as `ScriptService/enemy/patrol` rather than dragging the
                    // two directory levels that got it there into the tree.
                    .mountPath = toProjectPath(std::filesystem::relative(entry.path(), scriptsRoot, ec)),
                    .source = std::move(source),
                });
            }
        }
    }
    else {
        // A file is mounted as a single entry `Script`, which is what M0's and
        // M1's tests already do and will keep doing.
        std::string source;
        if (!readFile(path, source)) {
            const std::array<I18nArg, 1> args{I18nArg{"path", path.string()}};
            return core::makeError(LUAUG_TR("engine.cli.err.script_missing"), args);
        }

        m_root = path.parent_path();
        entries.push_back(script::MountedScript{
            .path = toProjectPath(path.filename()),
            .mountPath = toProjectPath(path.filename()),
            .source = std::move(source),
        });
    }

    // A project that mounts nothing boots, runs its frames and reports success
    // while doing absolutely nothing -- which is how `examples/01-instances`
    // came to render an empty screen with no diagnostic at all. Naming the
    // directory that was searched turns five minutes of confusion into none.
    //
    // A warning rather than an error: an empty project is a mistake, but it is
    // the user's mistake to make, and refusing to boot would also refuse the
    // legitimate act of starting an engine and building the tree from a console.
    if (entries.empty() && isDirectory) {
        const std::array<I18nArg, 1> args{I18nArg{"path", (m_root / "src" / "scripts").string()}};
        core::log(LogLevel::Warn, LUAUG_TR("engine.project.warn.no_scripts"), args);
    }

    script::mountScripts(m_runtime->state(), entries);
    return std::nullopt;
}

std::optional<core::EngineError> WorldHost::mountConformance(const std::filesystem::path& root)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        const std::array<I18nArg, 1> args{I18nArg{"path", root.string()}};
        return core::makeError(LUAUG_TR("engine.cli.err.script_missing"), args);
    }

    // The specs are mounted from their own root and the project's are not
    // touched: a conformance run is a run of the engine's own suite, and a
    // project's entry scripts have nothing to do with it.
    m_root = root;
    m_aliases.clear();

    std::vector<script::MountedScript> entries;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (!entry.is_regular_file() || !entry.path().filename().string().ends_with(".spec.luau"))
            continue;
        std::string source;
        if (!readFile(entry.path(), source))
            continue;

        const std::string relative = toProjectPath(std::filesystem::relative(entry.path(), root, ec));
        entries.push_back(script::MountedScript{relative, relative, std::move(source)});
    }

    // The runner's own path sorts wherever it sorts, and it does not matter: it
    // hangs off `game.Loaded`, which is raised after every entry script has had
    // its first resumption whatever order they ran in.
    std::string runnerSource;
    const std::filesystem::path runnerPath = platform::paths().contentDir / ConformanceRunnerPath;
    if (!readFile(runnerPath, runnerSource)) {
        const std::array<I18nArg, 1> args{I18nArg{"path", runnerPath.string()}};
        return core::makeError(LUAUG_TR("engine.cli.err.script_missing"), args);
    }

    entries.push_back(script::MountedScript{
        "__conformance_runner.luau",
        "__conformance_runner.luau",
        std::move(runnerSource),
    });

    script::mountScripts(m_runtime->state(), entries);
    return std::nullopt;
}

void WorldHost::firePreReload()
{
    m_runtime->fireHotReload(true);
    m_runtime->drain(core::Phase::FrameStart);
    m_world->retireDestroyed();
}

void WorldHost::firePostReload()
{
    m_runtime->fireHotReload(false);
    m_runtime->drain(core::Phase::FrameStart);
    m_world->retireDestroyed();
}

core::u64 WorldHost::mountedScriptCount() const
{
    return static_cast<core::u64>(script::mountedScriptCount(m_runtime->state()));
}

core::u64 WorldHost::scriptLoadFailures() const
{
    return static_cast<core::u64>(script::scriptLoadFailures(m_runtime->state()));
}

core::InstanceId WorldHost::dataModel() const noexcept
{
    return m_runtime.has_value() ? m_runtime->dataModel() : core::InstanceId{};
}

ConformanceReport WorldHost::conformanceReport() const
{
    const core::InstanceId root = dataModel();
    const auto attribute = [&](const char* name) -> core::i64 {
        const scene::Value value = m_world->getAttribute(root, m_world->atoms().lookup(name));
        const auto* number = std::get_if<f64>(&value);
        return number == nullptr ? -1 : static_cast<core::i64>(*number);
    };

    ConformanceReport report;
    report.total = attribute("ConformanceTotal");
    report.passed = attribute("ConformancePassed");
    report.failed = attribute("ConformanceFailed");
    report.ran = report.total >= 0;

    const scene::Value json = m_world->getAttribute(root, m_world->atoms().lookup("ConformanceReport"));
    if (const auto* text = std::get_if<std::string>(&json); text != nullptr)
        report.json = *text;

    return report;
}

void WorldHost::syncSkeletons()
{
    m_world->meshParts().forEach([&](core::InstanceId id, const scene::MeshPartComponent& meshPart) {
        (void)id;
        const core::NameAtom content = meshPart.meshContent;
        if (content.id == 0 || m_skeletons.find(content) != nullptr)
            return;
        if (std::find(m_skeletonsTried.begin(), m_skeletonsTried.end(), content.id) != m_skeletonsTried.end())
            return;
        // Remembered before anything can go wrong, so a file with no skeleton --
        // which is most of them -- costs one parse rather than one per tick
        // forever.
        m_skeletonsTried.push_back(content.id);

        const std::string urn(m_world->atoms().text(content));
        constexpr std::string_view scheme = "asset://";
        if (!urn.starts_with(scheme))
            return;

        const std::filesystem::path base = m_root.empty() ? platform::paths().contentDir : m_root / "content";
        const std::filesystem::path path = base / urn.substr(scheme.size());

        std::vector<std::byte> bytes;
        if (!platform::readFile(path, bytes))
            return;

        asset::Model model;
        if (asset::importGltf(bytes, path.parent_path(), {.skeletonOnly = true}, model).has_value())
            return;
        if (model.joints.empty())
            return;

        m_skeletons.set(content, render::SkeletonLibrary::Entry{std::move(model.joints), std::move(model.clips)});
    });
}

void WorldHost::tick()
{
    scene::EngineState& state = m_world->engineState();
    if (state.paused)
        return;

    // Before anything else in the tick: a track loaded by a script this tick has
    // to find the skeleton its mesh names, and a headless replay has no render
    // loop to have loaded it.
    syncSkeletons();

    state.tick += 1;
    state.simTime = static_cast<f64>(state.tick) * state.fixedTimestep;

    // Step 5a of architecture.md §3's frame: gameplay action signals, resolved
    // deterministically, BEFORE `PreAnimation`. Ahead of the first drain on
    // purpose -- a `Pressed` raised here is drained by the same drain the
    // phase's own handlers go through, so a script that jumps on a press sees
    // the press in the tick it happened rather than in the next one.
    m_input.dispatchSimTick(*m_world, state.tick);

    // The raw events the same dispatch produced (ADR 0041), enqueued right
    // beside the action signals it raised. They are what a caller reaching for
    // the familiar surface gets, and they come out of THIS dispatch rather than
    // from the OS -- so they sink like an action, replay like an action, and a
    // handler that writes to the world is deterministic.
    m_runtime->fireInputEvents(m_input.drainRawEvents());

    // The sound timeline, beside the input dispatch and for the same reason:
    // both are simulation state advanced by the tick, and both raise their
    // events into the drain the phases below go through.
    m_audio.tick(*m_world, state.fixedTimestep);

    // Each resumption point runs its engine phase, then drains (api-design.md
    // §3.1). `task` timers resume in their own phase between `PostSimulation`
    // and `Heartbeat`, and anything they defer drains at `Heartbeat`.
    //
    // The simulation sits between `PreSimulation` and `PostSimulation`, which is
    // architecture.md §3's order and not an arrangement of convenience: a script
    // pushes a part in `PreSimulation` and reads where it ended up in
    // `PostSimulation`, and the contacts the step produced are drained by the
    // `PostSimulation` drain rather than a frame later.
    m_runtime->firePhase(core::Phase::PreAnimation, state.fixedTimestep);
    m_runtime->drain(core::Phase::PreAnimation);

    // Tweens step here, in `PreAnimation`'s half of the tick, because that is
    // what they are: a property animated on the SimClock. After the drain, so a
    // tween started by a handler in this phase begins on the next tick rather
    // than half-advancing on the tick it was created in.
    m_runtime->stepTweens(state.fixedTimestep);

    // Skeletal animation, in the same half of the tick and after the drain for
    // the same reason a tween is: a track played by a `PreAnimation` handler
    // starts on the next tick rather than half-advancing on the one that
    // created it. `Ended` is enqueued here and drains with `PreSimulation`,
    // which is the deferred-signal rule (ADR 0015) and not a delay -- it is the
    // next resumption point either way.
    m_animation->sample(state.fixedTimestep);
    m_runtime->fireAnimationEnded(m_animation->drainEnded());
    m_animation->retire(*m_world);

    m_runtime->firePhase(core::Phase::PreSimulation, state.fixedTimestep);
    m_runtime->drain(core::Phase::PreSimulation);

    if (m_physics.has_value())
        m_physics->step(state.fixedTimestep);

    m_runtime->firePhase(core::Phase::PostSimulation, state.fixedTimestep);
    m_runtime->drain(core::Phase::PostSimulation);

    m_runtime->resumeTimers();
    m_runtime->firePhase(core::Phase::Heartbeat, state.fixedTimestep);
    m_runtime->drain(core::Phase::Heartbeat);
}

void WorldHost::pumpInput(std::span<const platform::Event> events)
{
    m_input.pumpFrame(events);

    // Losing focus releases everything held. An alt-tab that left W down is how
    // a character keeps walking into a wall while its window is in the
    // background, and the release has to happen HERE rather than at the next
    // dispatch: the window may stay unfocused for minutes, and the game should
    // not spend them running forward.
    for (const platform::Event& event : events) {
        if (event.type == platform::EventType::WindowFocusLost)
            m_input.releaseAll(*m_world);
    }
}

void WorldHost::publishNetworkResults()
{
    // At a FRAME SAFE POINT, not where the worker finished. A response arrives
    // on a thread of its own at a wall-clock moment; resuming the coroutine
    // there would put game code into the frame wherever the socket happened to
    // land it. Same rule and same reason as `resumeAreaWaiters` below (R10).
    script::resumeNetWaiters(m_runtime->state());
}

void WorldHost::publishStats(const script::FrameStats& stats)
{
    script::publishFrameStats(m_runtime->state(), stats);
}

void WorldHost::publishStreamingResults(const std::vector<core::InstanceId>& streamedOut,
                                        const std::function<bool(core::DVec3, f64)>& areaResident)
{
    for (const core::InstanceId instance : streamedOut) {
        script::fireStreamedOut(m_runtime->state(), instance);
    }
    script::resumeAreaWaiters(m_runtime->state(), areaResident);
}

void WorldHost::preRender(f64 renderDt)
{
    // Step 3 of the frame: the render-rate half of the dispatch split
    // (ADR 0039), before `PreRender` fires, so a camera handler reads the look
    // delta the frame it happened.
    m_input.dispatchRenderRate(*m_world);
    m_runtime->firePhase(core::Phase::PreRender, renderDt);
    m_runtime->drain(core::Phase::PreRender);
}

void WorldHost::setGizmoTarget(render::DebugDraw* draw)
{
    m_gizmos = draw;
    if (draw == nullptr) {
        m_runtime->setGizmoSink({});
        return;
    }

    m_runtime->setGizmoSink(script::GizmoSink{
        .user = this,
        .line =
            [](void* user, core::Vec3 a, core::Vec3 b, core::Color3 color) {
                auto& host = *static_cast<WorldHost*>(user);
                host.m_gizmos->line(a, b, render::DebugColor::fromLinear(color.r, color.g, color.b));
            },
        .box =
            [](void* user, const core::CFrameD& frame, core::Vec3 size, core::Color3 color) {
                auto& host = *static_cast<WorldHost*>(user);
                // Half-extents, because `DebugDraw` takes a centre and a radius and
                // halving at each call site is where the sign errors live.
                host.m_gizmos->wireBox(core::toRenderMatrix(frame, {}),
                                       core::Vec3{size.x * 0.5f, size.y * 0.5f, size.z * 0.5f},
                                       render::DebugColor::fromLinear(color.r, color.g, color.b));
            },
        .sphere =
            [](void* user, core::Vec3 position, f32 radius, core::Color3 color) {
                auto& host = *static_cast<WorldHost*>(user);
                host.m_gizmos->wireSphere(position, radius, render::DebugColor::fromLinear(color.r, color.g, color.b));
            },
    });
}

bool WorldHost::shutdownRequested()
{
    return script::shutdownRequested(m_runtime->state());
}

void WorldHost::close(core::f64 graceSeconds)
{
    script::runCloseHandlers(m_runtime->state());
    // One drain, so anything a close handler deferred already runs.
    m_runtime->drain(core::Phase::Heartbeat);

    // Then the grace period: keep ticking while a handler is still parked. A
    // handler that yields is the whole reason `BindToClose` takes a function
    // rather than being a signal, and cutting it off at the first drain made
    // the promise `architecture.md` §app carries untrue for five milestones
    // (D016).
    const auto graceNs = static_cast<core::u64>(std::max(0.0, graceSeconds) * 1'000'000'000.0);
    const core::u64 started = platform::nowNs();
    while (script::closeHandlersPending(m_runtime->state())) {
        if (platform::nowNs() - started >= graceNs) {
            const std::array<core::I18nArg, 1> args{core::I18nArg{"seconds", static_cast<core::i64>(graceSeconds)}};
            core::log(core::LogLevel::Warn, LUAUG_TR("engine.close.warn.grace_expired"), args);
            script::abandonCloseHandlers(m_runtime->state());
            break;
        }
        tick();
    }
}

} // namespace luaug::app
