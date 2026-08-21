// luaug-host -- the M0 engine host: boot a sandboxed Luau VM, run one script,
// report failures as structured, key-identified engine errors.

#include "luaug/app/backends.h"
#include "luaug/app/bench.h"
#include "luaug/app/engine.h"
#include "luaug/app/replay.h"
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
int parseOptions(std::span<const std::string_view> args, luaug::app::EngineOptions& options)
{
    const auto numericValue = [](std::string_view text, luaug::core::u64& out) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
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
    if (!options.replayRoot.empty() || !options.benchRoot.empty())
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

    if (args.empty()) {
        luaug::core::log(LogLevel::Error, LUAUG_TR("engine.cli.err.no_script"));
        return kExitUsage;
    }

    if (args[0] == "--version") {
        printVersion();
        return kExitOk;
    }

    if (args[0] == "--help") {
        luaug::core::log(LogLevel::Info, LUAUG_TR("engine.cli.usage"));
        return kExitOk;
    }

    luaug::app::EngineOptions options;
    if (const int usageExit = parseOptions(args, options); usageExit != kExitOk)
        return usageExit;

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

    if (!options.replayRoot.empty()) {
        if (const std::optional<luaug::core::EngineError> error =
                luaug::app::runReplayGate(options.replayRoot, options.replayRecord)) {
            luaug::core::logText(LogLevel::Error, error->message);
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
