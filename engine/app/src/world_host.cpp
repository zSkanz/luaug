#include "luaug/app/world_host.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

#include "class_descriptors.gen.h"
#include "luaug/core/build_info.h"
#include "luaug/core/json.h"
#include "luaug/core/log.h"
#include "luaug/platform/platform.h"
#include "luaug/render/debug_draw.h"

namespace luaug::app
{
namespace
{

using core::I18nArg;
using core::LogLevel;

// The `@luaug/…` modules that ship as content. One entry per module rather than
// a directory walk, because the set is the engine's own surface (ADR 0030) and
// discovering it from a directory would make an accidentally-shipped file part
// of the API.
constexpr std::string_view RuntimeModules[] = {"testing"};

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
    for (std::string::size_type index = 0; index <= input.size(); ++index)
    {
        if (index != input.size() && input[index] != '/')
            continue;

        const std::string_view segment = input.substr(start, index - start);
        start = index + 1;

        if (segment.empty() || segment == ".")
            continue;
        if (segment == "..")
        {
            if (stack.empty())
                return false; // above the project root, which is not a place
            stack.pop_back();
            continue;
        }
        stack.push_back(segment);
    }

    out.clear();
    for (std::string_view segment : stack)
    {
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
        if (specifier.front() == '@')
        {
            // `@self` is the requiring file's own directory, and an alias is
            // whatever `.luaurc` said. Resolved here rather than through
            // `Luau::parseConfig` because that treats an unrecognised key as a
            // hard error that aborts the require (U-42) -- a `$schema` line
            // would break `require` at runtime.
            const std::string::size_type slash = specifier.find('/');
            const std::string_view head = specifier.substr(1, slash == std::string_view::npos ? slash : slash - 1);
            const std::string_view tail =
                slash == std::string_view::npos ? std::string_view{} : specifier.substr(slash + 1);

            if (head == "self")
            {
                candidate.assign(directoryOf(fromPath));
            }
            else
            {
                const auto alias = host.m_aliases.find(std::string(head));
                if (alias == host.m_aliases.end())
                    return false;
                candidate = alias->second;
            }

            if (!tail.empty())
            {
                if (!candidate.empty())
                    candidate.push_back('/');
                candidate.append(tail);
            }
        }
        else if (specifier.starts_with("./") || specifier.starts_with("../"))
        {
            candidate.assign(directoryOf(fromPath));
            if (!candidate.empty())
                candidate.push_back('/');
            candidate.append(specifier);
        }
        else
        {
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
        if (std::filesystem::is_regular_file(host.m_root / withExtension))
        {
            outPath = withExtension;
            return true;
        }

        const std::string asDirectory = normalised + "/init.luau";
        if (std::filesystem::is_regular_file(host.m_root / asDirectory))
        {
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
    scene::generated::registerClasses(m_classes, m_atoms);
    scene::generated::registerEnums(m_enums, m_atoms);

    m_world.emplace(m_classes, m_enums, m_atoms, options.seed);
    m_world->engineState().engineVersion = LUAUG_VERSION_STRING;
    m_world->engineState().luauVersion = LUAUG_LUAU_VERSION;
    m_world->engineState().fixedTimestep = options.fixedTimestep;

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
    m_runtime->setReloadState(options.reloadState);
    if (options.reloadState != nullptr)
        options.reloadState->setIsReload(options.isReload);

    if (std::optional<core::EngineError> error = registerRuntimeModules(); error.has_value())
        return error;

    // Created by `registerServices` during the boot above, so this is a lookup
    // rather than a creation -- and it is cached because `extract` needs it
    // every frame.
    m_workspace =
        m_world->findFirstChildOfClass(m_runtime->dataModel(), m_classes.findId(m_atoms.lookup("Workspace")));

    if (!options.projectPath.empty())
    {
        if (std::optional<core::EngineError> error = mountProject(options.projectPath); error.has_value())
            return error;
    }

    if (!options.conformanceRoot.empty())
    {
        if (std::optional<core::EngineError> error = mountConformance(options.conformanceRoot); error.has_value())
            return error;
    }

    // After the mount, so the tree is complete, and before the scripts are
    // deferred, so the first thing any of them can observe already includes
    // what the previous world was carrying (M3 brief Decision 5).
    if (options.preserved != nullptr)
        restorePreserved(*m_world, m_runtime->dataModel(), *options.preserved, m_preserveReport);

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
    return std::nullopt;
}

std::optional<core::EngineError> WorldHost::registerRuntimeModules()
{
    const std::filesystem::path base = platform::paths().contentDir / "runtime" / "luaug";
    for (const std::string_view name : RuntimeModules)
    {
        const std::filesystem::path path = base / std::filesystem::path(name) / "init.luau";
        std::string source;
        if (!readFile(path, source))
        {
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
    if (isDirectory)
    {
        // A directory is a project root and gets the full mount (M2 brief,
        // Decision 9).
        m_root = path;

        std::string config;
        if (readFile(m_root / ".luaurc", config))
        {
            core::JsonDocument document;
            if (document.parse(config, ".luaurc"))
            {
                const core::JsonValue aliases = document.root()["aliases"];
                for (core::usize index = 0; index < aliases.size(); ++index)
                {
                    const std::string_view key = aliases.keyAt(index);
                    m_aliases.emplace(std::string(key), std::string(aliases[key].asString()));
                }
            }
            else
            {
                // Reported rather than fatal: a project with a malformed
                // `.luaurc` should still boot far enough to say so, which is not
                // what the vendored config reader does (U-42).
                core::logText(LogLevel::Warn, document.parse(config, ".luaurc").diagnostic);
            }
        }

        const std::filesystem::path scriptsRoot = m_root / "src" / "scripts";
        if (std::filesystem::is_directory(scriptsRoot, ec))
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(scriptsRoot, ec))
            {
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
    else
    {
        // A file is mounted as a single entry `Script`, which is what M0's and
        // M1's tests already do and will keep doing.
        std::string source;
        if (!readFile(path, source))
        {
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
    if (entries.empty() && isDirectory)
    {
        const std::array<I18nArg, 1> args{I18nArg{"path", (m_root / "src" / "scripts").string()}};
        core::log(LogLevel::Warn, LUAUG_TR("engine.project.warn.no_scripts"), args);
    }

    script::mountScripts(m_runtime->state(), entries);
    return std::nullopt;
}

std::optional<core::EngineError> WorldHost::mountConformance(const std::filesystem::path& root)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
    {
        const std::array<I18nArg, 1> args{I18nArg{"path", root.string()}};
        return core::makeError(LUAUG_TR("engine.cli.err.script_missing"), args);
    }

    // The specs are mounted from their own root and the project's are not
    // touched: a conformance run is a run of the engine's own suite, and a
    // project's entry scripts have nothing to do with it.
    m_root = root;
    m_aliases.clear();

    std::vector<script::MountedScript> entries;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
    {
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
    if (!readFile(runnerPath, runnerSource))
    {
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

ConformanceReport WorldHost::conformanceReport() const
{
    const core::InstanceId root = m_runtime->dataModel();
    const auto attribute = [&](const char* name) -> core::i64
    {
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

void WorldHost::tick()
{
    scene::EngineState& state = m_world->engineState();
    if (state.paused)
        return;

    state.tick += 1;
    state.simTime = static_cast<f64>(state.tick) * state.fixedTimestep;

    // Each resumption point runs its engine phase, then drains (api-design.md
    // §3.1). `task` timers resume in their own phase between `PostSimulation`
    // and `Heartbeat`, and anything they defer drains at `Heartbeat`.
    for (const core::Phase phase :
         {core::Phase::PreAnimation, core::Phase::PreSimulation, core::Phase::PostSimulation})
    {
        m_runtime->firePhase(phase, state.fixedTimestep);
        m_runtime->drain(phase);
    }

    m_runtime->resumeTimers();
    m_runtime->firePhase(core::Phase::Heartbeat, state.fixedTimestep);
    m_runtime->drain(core::Phase::Heartbeat);
}

void WorldHost::publishStats(const script::FrameStats& stats)
{
    script::publishFrameStats(m_runtime->state(), stats);
}

void WorldHost::preRender(f64 renderDt)
{
    m_runtime->firePhase(core::Phase::PreRender, renderDt);
    m_runtime->drain(core::Phase::PreRender);
}

void WorldHost::setGizmoTarget(render::DebugDraw* draw)
{
    m_gizmos = draw;
    if (draw == nullptr)
    {
        m_runtime->setGizmoSink({});
        return;
    }

    m_runtime->setGizmoSink(script::GizmoSink{
        .user = this,
        .line =
            [](void* user, core::Vec3 a, core::Vec3 b, core::Color3 color)
        {
            auto& host = *static_cast<WorldHost*>(user);
            host.m_gizmos->line(a, b, render::DebugColor::fromLinear(color.r, color.g, color.b));
        },
        .box =
            [](void* user, const core::CFrameD& frame, core::Vec3 size, core::Color3 color)
        {
            auto& host = *static_cast<WorldHost*>(user);
            // Half-extents, because `DebugDraw` takes a centre and a radius and
            // halving at each call site is where the sign errors live.
            host.m_gizmos->wireBox(
                core::toRenderMatrix(frame, {}),
                core::Vec3{size.x * 0.5f, size.y * 0.5f, size.z * 0.5f},
                render::DebugColor::fromLinear(color.r, color.g, color.b));
        },
        .sphere =
            [](void* user, core::Vec3 position, f32 radius, core::Color3 color)
        {
            auto& host = *static_cast<WorldHost*>(user);
            host.m_gizmos->wireSphere(position, radius, render::DebugColor::fromLinear(color.r, color.g, color.b));
        },
    });
}

bool WorldHost::shutdownRequested()
{
    return script::shutdownRequested(m_runtime->state());
}

void WorldHost::close()
{
    script::runCloseHandlers(m_runtime->state());
    // One last drain, so anything a close handler deferred still runs. After
    // this the VM is going away and nothing else will.
    m_runtime->drain(core::Phase::Heartbeat);
}

} // namespace luaug::app
