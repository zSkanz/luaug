// luaug-host -- the M0 engine host: boot a sandboxed Luau VM, run one script,
// report failures as structured, key-identified engine errors.

#include "luaug/app/backends.h"
#include "luaug/app/bench.h"
#include "luaug/app/engine.h"
#include "luaug/app/project_config.h"
#include "luaug/app/replay.h"
#include "luaug/app/two_worlds.h"
#include "luaug/core/build_info.h"
#include "luaug/core/error.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/platform/console.h"
#include "luaug/platform/crash.h"
#include "luaug/platform/platform.h"

#include <Luau/Bytecode.h>
#include <lua.h>

#include <array>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using luaug::core::I18nArg;
using luaug::core::LogLevel;

constexpr int kExitOk = 0;
constexpr int kExitUsage = 2;
constexpr int kExitScriptError = 1;
constexpr int kExitNoCatalog = 3;

// Distinct because "this machine has no usable GPU" is not a failure of
// anything under test. CTest maps it to SKIP, so a runner without a driver
// reports a skipped render test instead of a red build nobody can act on.
constexpr int kExitNoGraphicsDevice = 4;

// Catalogs are UTF-8 (ADR 0019); a Windows console decodes raw byte writes with
// its own codepage and mangles anything non-ASCII, which would quietly reduce
// "adding a locale is adding a file" to "adding a locale nobody on Windows can
// read". `platform::writeConsole` is the fix, and routing the log through it is
// how every engine message gets it.
void installConsoleLogSink()
{
    luaug::core::setLogSink([](LogLevel level, std::string_view text) {
        // Warnings and errors go to stderr so a headless CI run can
        // separate them from ordinary output without parsing.
        const auto stream = (level == LogLevel::Warn || level == LogLevel::Error) ? luaug::platform::ConsoleStream::Err
                                                                                  : luaug::platform::ConsoleStream::Out;
        luaug::platform::writeConsole(stream, luaug::core::formatLogLine(level, text));
    });
}

// The one place a user-facing string may be hardcoded: if the catalog itself
// failed to load there is, by definition, nothing to translate through. Kept
// deliberately to stderr and to this single call site (ADR 0019).
void reportCatalogFailure(const std::string& diagnostic)
{
    std::fprintf(stderr, "luaug-host: cannot load the message catalog: %s\n", diagnostic.c_str());
}

void printVersion()
{
    const std::array<I18nArg, 2> engineArgs{I18nArg{"version", LUAUG_VERSION_STRING},
                                            I18nArg{"profile", LUAUG_PROFILE_NAME}};
    luaug::core::log(LogLevel::Info, LUAUG_TR("engine.cli.version.engine"), engineArgs);

    // Version and commit come from third_party/manifest.json via the generated
    // provenance header (ADR 0031) -- Luau itself ships no version constant.
    const std::array<I18nArg, 2> luauArgs{I18nArg{"version", LUAUG_LUAU_VERSION}, I18nArg{"commit", LUAUG_LUAU_COMMIT}};
    luaug::core::log(LogLevel::Info, LUAUG_TR("engine.cli.version.luau"), luauArgs);

    // These come from the vendored headers at compile time, so they describe
    // the VM actually linked into this binary rather than what we intended.
    const std::array<I18nArg, 4> abiArgs{
        I18nArg{"bytecode", static_cast<luaug::core::i64>(LBC_VERSION_TARGET)},
        I18nArg{"types", static_cast<luaug::core::i64>(LBC_TYPE_VERSION_TARGET)},
        I18nArg{"vectorSize", static_cast<luaug::core::i64>(LUA_VECTOR_SIZE)},
        I18nArg{"vectorPrecision", LUA_VECTOR_DOUBLE ? std::string_view{"f64"} : std::string_view{"f32"}}};
    luaug::core::log(LogLevel::Info, LUAUG_TR("engine.cli.version.abi"), abiArgs);
}

// Fills `options` from the command line. Returns kExitOk when the caller should
// proceed, or the exit code to return.
//
// Deliberately hand-rolled and small: a getopt-style dependency for six flags
// would be a dependency (R5) bought for nothing, and the engine's real CLI is
// `luaug` (M3), which lives in Lute and is the thing users will actually type.
// This is the host's own switchboard.
int parseOptions(std::span<const std::string_view> args, luaug::app::EngineOptions& options,
                 luaug::app::GraphicsOverrides& graphics, bool& sizeFromFlags)
{
    const auto numericValue = [](std::string_view text, luaug::core::u64& out) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    };

    // `strtod` rather than `from_chars`, for the reason `core/json.cpp` records:
    // the floating-point overloads of from_chars are missing from one of the
    // standard libraries this engine builds against. The WHOLE token has to
    // convert, so `--render-scale=0.75x` is a usage error rather than 0.75.
    const auto decimalValue = [](std::string_view text, double& out) {
        const std::string buffer(text);
        char* end = nullptr;
        const double value = std::strtod(buffer.c_str(), &end);
        if (end != buffer.c_str() + buffer.size())
            return false;
        out = value;
        return true;
    };

    for (const std::string_view arg : args) {
        if (!arg.empty() && arg.front() != '-') {
            options.scriptPath = std::filesystem::path(arg);
            continue;
        }

        if (arg == "--headless") {
            options.headless = true;
            continue;
        }
        if (arg == "--exit") {
            options.exitAfterFrames = true;
            continue;
        }
        if (arg == "--launcher") {
            options.launcher = true;
            continue;
        }
        if (arg == "--edit") {
            options.editor = true;
            continue;
        }
        if (arg == "--frame-stats") {
            options.frameStats = true;
            continue;
        }

        // M7's gate. The report path turns the recorder on; the ceiling is
        // separate because a soak that only wants the histogram should not have
        // to invent a memory number to get one.
        if (arg.starts_with("--soak-report=")) {
            options.soakReportPath = std::filesystem::path(arg.substr(arg.find('=') + 1));
            continue;
        }
        if (arg.starts_with("--soak-ceiling-mb=")) {
            const std::string_view value = arg.substr(arg.find('=') + 1);
            luaug::core::u64 parsed = 0;
            if (!numericValue(value, parsed) || parsed == 0) {
                const std::array<I18nArg, 2> badValue{I18nArg{"option", arg}, I18nArg{"value", value}};
                luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.bad_value"), badValue);
                return kExitUsage;
            }
            options.soakCeilingBytes = parsed * 1024 * 1024;
            continue;
        }
        if (arg.starts_with("--soak-return-radius=")) {
            // Whole metres, like every other numeric flag here parses: a radius
            // in centimetres is not a thing a fly-through declares, and the
            // shared `numericValue` is what keeps a bad value a NAMED error
            // rather than a silent zero -- which for this option would turn the
            // check off instead of failing.
            const std::string_view value = arg.substr(arg.find('=') + 1);
            luaug::core::u64 parsed = 0;
            if (!numericValue(value, parsed) || parsed == 0) {
                const std::array<I18nArg, 2> badValue{I18nArg{"option", arg}, I18nArg{"value", value}};
                luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.bad_value"), badValue);
                return kExitUsage;
            }
            options.soakReturnRadiusMetres = static_cast<luaug::core::f32>(parsed);
            continue;
        }
        if (arg.starts_with("--soak-min-instances=")) {
            const std::string_view value = arg.substr(arg.find('=') + 1);
            luaug::core::u64 parsed = 0;
            if (!numericValue(value, parsed) || parsed == 0) {
                const std::array<I18nArg, 2> badValue{I18nArg{"option", arg}, I18nArg{"value", value}};
                luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.bad_value"), badValue);
                return kExitUsage;
            }
            options.soakMinimumInstances = parsed;
            continue;
        }
        // The render target's size. Windowed it is the window; headless it is the
        // offscreen texture. The M4 gate records a frame-time baseline at 1080p
        // and the host had no way to be asked for one.
        if (arg.starts_with("--width=") || arg.starts_with("--height=")) {
            const std::string_view value = arg.substr(arg.find('=') + 1);
            luaug::core::u64 parsed = 0;
            // Bounded rather than merely positive: a target larger than any GPU
            // will allocate fails inside the backend with a message about
            // memory, which is a long way from "you typed a silly number".
            if (!numericValue(value, parsed) || parsed == 0 || parsed > 16384) {
                const std::array<I18nArg, 2> badValue{I18nArg{"option", arg}, I18nArg{"value", value}};
                luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.bad_value"), badValue);
                return kExitUsage;
            }
            (arg.starts_with("--width=") ? options.width : options.height) = static_cast<luaug::core::i32>(parsed);
            // Remembered so `[window] size` in the project file can fill in for
            // a size nobody asked for and stay out of the way of one somebody
            // did.
            sizeFromFlags = true;
            continue;
        }
        if (arg.starts_with("--frames=")) {
            if (!numericValue(arg.substr(9), options.frames)) {
                const std::array<I18nArg, 1> badValue{I18nArg{"option", arg}};
                luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.bad_value"), badValue);
                return kExitUsage;
            }
            continue;
        }
        if (arg.starts_with("--screenshot=")) {
            options.screenshotPath = std::filesystem::path(arg.substr(13));
            continue;
        }
        if (arg.starts_with("--save-scene=")) {
            options.saveScenePath = std::filesystem::path(arg.substr(13));
            continue;
        }
        if (arg == "--partition") {
            // Partition the project's scene into the cache and stop. Headless
            // and windowless for the same reason `--save-scene` is: it is a
            // build step, not a session.
            options.partitionOnly = true;
            options.headless = true;
            continue;
        }
        if (arg.starts_with("--capture-out=")) {
            options.capturePath = std::filesystem::path(arg.substr(14));
            continue;
        }
        if (arg.starts_with("--run-tests=")) {
            options.conformanceRoot = std::filesystem::path(arg.substr(12));
            // A conformance run has no window and ends when the suite does, so
            // the two flags a headless run needs are implied rather than typed
            // out at every call site.
            options.headless = true;
            options.exitAfterFrames = true;
            continue;
        }
        if (arg.starts_with("--test-report=")) {
            options.testReportPath = std::filesystem::path(arg.substr(14));
            continue;
        }
        if (arg.starts_with("--dev-control=")) {
            options.devControlUrl = std::string(arg.substr(14));
            continue;
        }
        if (arg.starts_with("--dev-token=")) {
            options.devControlToken = std::string(arg.substr(12));
            continue;
        }
        if (arg.starts_with("--replay=")) {
            options.replayRoot = std::filesystem::path(arg.substr(9));
            continue;
        }
        if (arg == "--record-replay") {
            options.replayRecord = true;
            continue;
        }
        if (arg.starts_with("--two-worlds=")) {
            options.twoWorldsRoot = std::filesystem::path(arg.substr(arg.find('=') + 1));
            continue;
        }
        if (arg.starts_with("--two-worlds-out=")) {
            options.twoWorldsOutDir = std::filesystem::path(arg.substr(arg.find('=') + 1));
            continue;
        }
        if (arg.starts_with("--bench=")) {
            options.benchRoot = std::filesystem::path(arg.substr(8));
            continue;
        }
        if (arg.starts_with("--bench-repeats=")) {
            if (!numericValue(arg.substr(16), options.benchRepeats)) {
                const std::array<I18nArg, 1> badValue{I18nArg{"option", arg}};
                luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.bad_value"), badValue);
                return kExitUsage;
            }
            continue;
        }
        // --- The graphics settings family (roadmap M8, ADR 0044) -----------
        //
        // The outermost of the three layers: a preset, then the project file,
        // then these. Each is an OVERRIDE rather than a value, so that "nobody
        // said anything" and "somebody asked for the default" stay different
        // answers -- see `project_config.h`.
        if (arg.starts_with("--quality=")) {
            const std::string_view value = arg.substr(arg.find('=') + 1);
            const std::optional<luaug::render::QualityLevel> level = luaug::render::parseQuality(value);
            if (!level.has_value()) {
                const std::array<I18nArg, 2> badValue{I18nArg{"option", arg}, I18nArg{"value", value}};
                luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.bad_value"), badValue);
                return kExitUsage;
            }
            graphics.quality = *level;
            continue;
        }
        if (arg.starts_with("--render-scale=") || arg.starts_with("--shadow-distance=")) {
            const std::string_view value = arg.substr(arg.find('=') + 1);
            double parsed = 0.0;
            if (!decimalValue(value, parsed) || parsed <= 0.0) {
                const std::array<I18nArg, 2> badValue{I18nArg{"option", arg}, I18nArg{"value", value}};
                luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.bad_value"), badValue);
                return kExitUsage;
            }
            if (arg.starts_with("--render-scale="))
                graphics.renderScale = static_cast<luaug::core::f32>(parsed);
            else
                graphics.shadowDistance = static_cast<luaug::core::f32>(parsed);
            continue;
        }
        if (arg.starts_with("--shadow-resolution=") || arg.starts_with("--shadow-cascades=") ||
            arg.starts_with("--light-budget=")) {
            const std::string_view value = arg.substr(arg.find('=') + 1);
            luaug::core::u64 parsed = 0;
            // Zero is legal for two of the three -- no cascades is "the sun
            // casts no shadow" and no lights is a scene lit by the sky alone --
            // so only the resolution refuses it.
            if (!numericValue(value, parsed) || (arg.starts_with("--shadow-resolution=") && parsed == 0)) {
                const std::array<I18nArg, 2> badValue{I18nArg{"option", arg}, I18nArg{"value", value}};
                luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.bad_value"), badValue);
                return kExitUsage;
            }
            if (arg.starts_with("--shadow-resolution="))
                graphics.shadowResolution = static_cast<luaug::core::u32>(parsed);
            else if (arg.starts_with("--shadow-cascades="))
                graphics.shadowCascades = static_cast<luaug::core::u32>(parsed);
            else
                graphics.lightBudget = static_cast<luaug::core::u32>(parsed);
            continue;
        }
        // Both directions, because a preset is a starting point rather than a
        // menu: somebody on `low` may still want bloom, and somebody measuring
        // may want the frame held still with `--no-auto-exposure`.
        if (arg == "--bloom" || arg == "--no-bloom") {
            graphics.bloom = arg == "--bloom";
            continue;
        }
        if (arg == "--ambient-occlusion" || arg == "--no-ambient-occlusion") {
            graphics.ambientOcclusion = arg == "--ambient-occlusion";
            continue;
        }
        if (arg == "--anti-aliasing" || arg == "--no-anti-aliasing") {
            graphics.antiAliasing = arg == "--anti-aliasing";
            continue;
        }
        if (arg == "--auto-exposure" || arg == "--no-auto-exposure") {
            graphics.autoExposure = arg == "--auto-exposure";
            continue;
        }

        if (arg.starts_with("--rhi=")) {
            const std::optional<luaug::rhi::BackendId> backend = luaug::app::parseBackendId(arg.substr(6));
            if (!backend.has_value()) {
                const std::array<I18nArg, 2> unknownBackend{I18nArg{"name", arg.substr(6)},
                                                            I18nArg{"available", luaug::app::availableBackendNames()}};
                luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.unknown_backend"), unknownBackend);
                return kExitUsage;
            }
            options.backend = *backend;
            continue;
        }

        const std::array<I18nArg, 1> unknownArgs{I18nArg{"option", arg}};
        luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.unknown_option"), unknownArgs);
        return kExitUsage;
    }

    // A replay answers to none of the checks below: it has no window, no frame
    // budget and no backend, because it never opens a device at all. Validating
    // it as though it were a session would demand `--headless --frames=N` for a
    // mode in which neither means anything.
    //
    // `--partition` is the same shape one step along: it boots, writes the
    // cache and returns without ever reaching the loop, so a frame budget would
    // be a ceiling on a loop that does not run.
    // `--launcher` is the third: it has its own loop, no world and no frame
    // budget, and it ends when somebody chooses a project or closes the window.
    if (!options.replayRoot.empty() || !options.benchRoot.empty() || !options.twoWorldsRoot.empty() ||
        options.partitionOnly || options.launcher)
        return kExitOk;

    // A conformance run needs a ceiling for the same reason, and a generous one:
    // it ends when the suite calls `Shutdown`, and the budget is only there so a
    // suite that hangs fails rather than running until CI gives up.
    if (!options.conformanceRoot.empty() && options.frames == 0)
        options.frames = 100000;

    // A dev session is driven by its dev server and ends when that server says
    // so, so a frame budget would be a timer on a loop nobody asked to time.
    // Requiring one for `--headless --dev-control` -- which is what the E2E
    // gate runs -- would mean guessing how long a test needs.
    if (!options.devControlUrl.empty() && options.headless && options.frames == 0)
        return kExitOk;

    // A headless run with no frame budget would never terminate and nothing
    // could tell you why, since there is no window to close. Saying so beats
    // hanging a CI job until its timeout.
    if (options.headless && options.frames == 0) {
        luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.headless_needs_frames"));
        return kExitUsage;
    }

    // A windowed frame renders into the swapchain, which has been presented and
    // is gone before anything could read it back. Headless renders into a
    // target the engine owns, which is why the harness uses it.
    if (!options.screenshotPath.empty() && !options.headless) {
        luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.screenshot_needs_headless"));
        return kExitUsage;
    }

    // An editor with no window is not an editor. Refusing beats quietly
    // starting a headless session that draws the world into a texture nobody
    // will ever see.
    if (options.editor && options.headless) {
        luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.editor_needs_window"));
        return kExitUsage;
    }

    // Only the capture backend records a stream. Asking any other one for it
    // would produce an empty file, and an empty golden matches forever.
    if (!options.capturePath.empty() && options.backend != luaug::rhi::BackendId::Capture) {
        luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.capture_needs_backend"));
        return kExitUsage;
    }

    return kExitOk;
}

} // namespace

int main(int argc, char** argv)
{
    installConsoleLogSink();

    const auto& paths = luaug::platform::paths();
    const auto catalogLoad = luaug::core::engineCatalog().loadFromFile(paths.contentDir / "i18n" / "en.json");
    if (!catalogLoad) {
        reportCatalogFailure(catalogLoad.diagnostic);
        return kExitNoCatalog;
    }

    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);

    // **A packaged game is a folder that runs** (roadmap M8). `luaug build`
    // produces `<Game>.exe` beside a `game/` directory, and the player is this
    // same host: given no script, it mounts the project next to itself. That is
    // the whole of what makes the artifact double-clickable, and it is a
    // convention rather than a configuration file, because a configuration file
    // would be a second thing that can go missing.
    const std::filesystem::path packagedProject = luaug::platform::paths().executableDir / "game";
    std::error_code packagedError;
    const bool hasPackagedProject = std::filesystem::is_directory(packagedProject, packagedError);

    // **No project is no longer a usage error** (ADR 0055): given nothing at all,
    // the host shows the project browser, which is what makes the engine
    // something a person can double-click. Decided here and DISPATCHED at the
    // bottom, so the launcher gets the log file and the crash handler like every
    // other session -- a launcher whose failure went only to a console nobody
    // opened would be the hardest thing here to diagnose.
    //
    // A host given a path that is not a project still refuses exactly as it did.
    // This is the case where there is no path, not a fallback that swallows a
    // bad one.
    const bool noProjectGiven = args.empty() && !hasPackagedProject;

    if (!args.empty() && args[0] == "--version") {
        printVersion();
        return kExitOk;
    }

    if (!args.empty() && args[0] == "--help") {
        luaug::core::log(LogLevel::Info, LUAUG_TR("engine.cli.usage"));
        return kExitOk;
    }

    luaug::app::EngineOptions options;
    if (hasPackagedProject)
        options.scriptPath = packagedProject;
    luaug::app::GraphicsOverrides graphicsOverrides;
    bool sizeFromFlags = false;
    if (const int usageExit = parseOptions(args, options, graphicsOverrides, sizeFromFlags); usageExit != kExitOk)
        return usageExit;
    if (noProjectGiven)
        options.launcher = true;

    // The project file, and the three-layer resolution it completes. A bare
    // script has no project and gets the preset plus the flags, which is the
    // same code path with an empty root.
    {
        std::error_code projectError;
        const bool isProject =
            !options.scriptPath.empty() && std::filesystem::is_directory(options.scriptPath, projectError);
        std::string configDiagnostic;
        const luaug::app::ProjectConfig config = luaug::app::loadProjectConfig(
            isProject ? options.scriptPath : std::filesystem::path{}, graphicsOverrides, &configDiagnostic);

        if (!configDiagnostic.empty()) {
            // Named and survivable, like a content pack that will not open: a
            // malformed project file leaves the engine's own defaults standing
            // rather than refusing to start, and says which line stopped it.
            const std::array<I18nArg, 1> configArgs{I18nArg{"reason", configDiagnostic}};
            luaug::core::log(LogLevel::Warn, LUAUG_TR("app.warn.project_config"), configArgs);
        }

        options.graphics = config.graphics;
        options.windowTitle = config.windowTitle;
        options.startupScene = config.scene;

        // Before any window exists, because the shell reads a process's
        // identity when it first shows one -- and a pinned shortcut that lost
        // its icon is not something a later call can undo.
        luaug::platform::setApplicationId(config.id);

        if (!sizeFromFlags && config.windowWidth > 0 && config.windowHeight > 0) {
            options.width = config.windowWidth;
            options.height = config.windowHeight;
        }
    }

    // The two artifacts `architecture.md` §app has promised since M0, and the
    // reason they are HERE: `core` is L0 and cannot ask where a file belongs,
    // `platform::paths()` is L1, and this is the layer that sees both. The
    // directory is the process's own, so `run.bat` leaves them beside the
    // example a person was running.
    //
    // Neither failure is fatal. An engine that refuses to start because it could
    // not open a log is an engine that a read-only directory takes away
    // entirely, which is a worse trade than losing the log.
    const std::filesystem::path artifactDir = std::filesystem::current_path();
    const std::filesystem::path logPath = artifactDir / "luaug.log";
    const bool logOpened = luaug::core::openLogFile(logPath);
    const bool handlerInstalled = luaug::platform::installCrashHandler(artifactDir);

    const std::array<I18nArg, 1> bootArgs{I18nArg{"version", LUAUG_VERSION_STRING}};
    luaug::core::log(LogLevel::Info, LUAUG_TR("engine.boot.hello"), bootArgs);

    // Printed, not assumed. The whole failure this closes is a human reporting a
    // crash from memory, and a log whose path nobody knows is a log nobody
    // sends. It goes out at Info so it is in the log file as well -- the first
    // line of which then says where the log file is, which sounds circular and
    // is not: the copy in the terminal is the one a person reads.
    if (logOpened) {
        const std::array<I18nArg, 1> logArgs{I18nArg{"path", logPath.string()}};
        luaug::core::log(LogLevel::Info, LUAUG_TR("engine.boot.info.log_file"), logArgs);
    }
    else {
        const std::array<I18nArg, 1> logArgs{I18nArg{"path", logPath.string()}};
        luaug::core::log(LogLevel::Warn, LUAUG_TR("engine.boot.warn.log_file_failed"), logArgs);
    }
    if (handlerInstalled) {
        const std::array<I18nArg, 1> crashArgs{I18nArg{"path", luaug::platform::crashArtifactPath().string()}};
        luaug::core::log(LogLevel::Info, LUAUG_TR("engine.boot.info.crash_artifact"), crashArgs);
    }

    if (!options.benchRoot.empty()) {
        std::vector<luaug::app::BenchResult> results;
        if (const std::optional<luaug::core::EngineError> error =
                luaug::app::runBenchmarks(options.benchRoot, options.benchRepeats, results)) {
            luaug::core::logText(LogLevel::Error, error->message);
            return kExitScriptError;
        }
        return kExitOk;
    }

    if (!options.twoWorldsRoot.empty()) {
        if (const std::optional<luaug::core::EngineError> error = luaug::app::runTwoWorldsGate({
                .root = options.twoWorldsRoot,
                .outputDir = options.twoWorldsOutDir,
                .backend = options.backend,
                .ticks = options.frames == 0 ? 8 : options.frames,
                .width = options.width,
                .height = options.height,
            })) {
            luaug::core::logText(LogLevel::Error, error->message);

            // Same mapping the session path uses, and for the same reason: a
            // runner with no driver has not found anything about the seam.
            if (error->key.hash == LUAUG_TR("rhi.err.device_create_failed").hash)
                return kExitNoGraphicsDevice;
            return kExitScriptError;
        }
        return kExitOk;
    }

    if (!options.replayRoot.empty()) {
        if (const std::optional<luaug::core::EngineError> error =
                luaug::app::runReplayGate(options.replayRoot, options.replayRecord)) {
            luaug::core::logText(LogLevel::Error, error->message);
            return kExitScriptError;
        }
        return kExitOk;
    }

    if (options.launcher) {
        if (const std::optional<luaug::core::EngineError> error = luaug::app::runLauncher(options)) {
            luaug::core::logText(LogLevel::Error, error->message);
            if (!error->detail.empty())
                luaug::core::logText(LogLevel::Error, error->detail);
            if (error->key.hash == LUAUG_TR("rhi.err.device_create_failed").hash)
                return kExitNoGraphicsDevice;
            return kExitScriptError;
        }
        return kExitOk;
    }

    if (const std::optional<luaug::core::EngineError> error = luaug::app::run(options)) {
        luaug::core::logText(LogLevel::Error, error->message);
        if (!error->detail.empty())
            luaug::core::logText(LogLevel::Error, error->detail);

        // The key IS the identity of an engine error (ADR 0019), so matching on
        // it is the intended way to tell one failure from another -- no second
        // channel, no parsing of prose that translation would break.
        if (error->key.hash == LUAUG_TR("rhi.err.device_create_failed").hash)
            return kExitNoGraphicsDevice;

        return kExitScriptError;
    }

    return kExitOk;
}
