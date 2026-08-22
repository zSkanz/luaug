#include "luaug/app/replay.h"

#include "luaug/app/world_host.h"
#include "luaug/core/json.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/input/input.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace luaug::app {
namespace {

using core::I18nArg;
using core::LogLevel;
using core::usize;

// The scenario's own timestep. Fixed here rather than taken from
// `FrameScheduler`, because a replay is defined by its tick count and a change
// to the default frame rate must not silently redefine every recorded trace.
constexpr f64 kReplayTimestep = 1.0 / 60.0;

// Where the host's sink is parked while a scenario's counting sink is installed.
// A function-local static rather than a capture, because the lambda that reads
// it is the one that replaced it.
[[nodiscard]] core::LogSink& hostSink()
{
    static core::LogSink sink;
    return sink;
}

[[nodiscard]] std::optional<core::EngineError> readFile(const std::filesystem::path& path, std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        const std::array<I18nArg, 1> args{I18nArg{"path", path.string()}};
        return core::makeError(LUAUG_TR("engine.replay.err.open_failed"), args);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return std::nullopt;
}

// Which platform's recorded trace this build should be compared against.
//
// Per-platform because the guarantee is per-platform, and architecture.md §9
// says so in as many words: "same build + same platform + same seed/inputs/
// tick-config ⇒ same WorldHash", with cross-platform comparison a tracked
// non-blocking concern. The reason is `sin`. A script that calls `math.sin` gets
// MSVC's CRT on Windows and glibc's on Linux, the two disagree in the last ULP,
// and one ULP compounded over 500 ticks of accumulated transforms is a different
// world. Making that agree means shipping our own transcendentals, which is a
// real option and is not M2's.
//
// One file per platform is therefore the honest shape: each tier gates against
// what it actually recorded, so a Linux regression is caught on Linux, and
// nobody is asked to reconcile two libms to merge a patch.
[[nodiscard]] std::string_view platformName() noexcept
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

[[nodiscard]] std::filesystem::path tracePathFor(const std::filesystem::path& directory)
{
    return directory / (std::string("trace.").append(platformName()).append(".txt"));
}

[[nodiscard]] std::string hex(u64 value)
{
    std::array<char, 16> digits{};
    for (usize i = 0; i < digits.size(); ++i)
        digits[digits.size() - 1 - i] = "0123456789abcdef"[(value >> (i * 4)) & 0xFu];
    return std::string{digits.data(), digits.size()};
}

} // namespace

std::optional<core::EngineError> loadScenario(const std::filesystem::path& directory, ReplayScenario& out)
{
    const std::filesystem::path manifest = directory / "scenario.json";
    std::string source;
    if (auto error = readFile(manifest, source); error.has_value())
        return error;

    core::JsonDocument document;
    if (const core::JsonDocument::ParseResult result = document.parse(source, manifest.string()); !result) {
        const std::array<I18nArg, 2> args{I18nArg{"path", manifest.string()}, I18nArg{"reason", result.diagnostic}};
        return core::makeError(LUAUG_TR("engine.replay.err.bad_manifest"), args);
    }

    const core::JsonValue root = document.root();
    out.name = std::string{root["name"].asString(directory.filename().string())};
    out.seed = static_cast<u64>(root["seed"].asInteger(1));
    out.ticks = static_cast<u64>(std::max<core::i64>(0, root["ticks"].asInteger(0)));
    out.checkpointEvery = static_cast<u64>(std::max<core::i64>(1, root["checkpointEvery"].asInteger(1)));
    out.sameBuildOnly = root["sameBuildOnly"].asBool(false);
    out.requireAttribute = std::string(root["requireAttribute"].asString(""));

    const std::string_view script = root["script"].asString("init.luau");
    out.scriptPath = std::filesystem::absolute(directory / script);

    if (out.ticks == 0) {
        const std::array<I18nArg, 1> args{I18nArg{"path", manifest.string()}};
        return core::makeError(LUAUG_TR("engine.replay.err.no_ticks"), args);
    }

    std::error_code ec;
    if (!std::filesystem::exists(out.scriptPath, ec)) {
        const std::array<I18nArg, 1> args{I18nArg{"path", out.scriptPath.string()}};
        return core::makeError(LUAUG_TR("engine.replay.err.open_failed"), args);
    }

    // The input stream, optional and text. Text for the same reason the trace
    // is: a recording is reviewed as a diff when it changes, and
    // `900 + Space` is a line a person can read and edit.
    const std::filesystem::path inputPath = directory / "inputs.txt";
    if (std::filesystem::exists(inputPath, ec)) {
        std::string inputSource;
        if (auto error = readFile(inputPath, inputSource); error.has_value())
            return error;

        std::istringstream lines(inputSource);
        std::string line;
        u64 lineNumber = 0;
        while (std::getline(lines, line)) {
            ++lineNumber;
            std::istringstream fields(line);
            std::string tickText;
            std::string edge;
            std::string keyName;
            if (!(fields >> tickText >> edge >> keyName))
                continue; // Blank lines and trailing newlines.
            if (!tickText.empty() && tickText[0] == '#')
                continue; // A comment, so a recording can say what it is doing.

            ReplayInput input;
            input.tick = static_cast<u64>(std::strtoull(tickText.c_str(), nullptr, 10));
            input.down = edge == "+";
            input.analog = edge == "=";
            input.keyCode = input::keyCodeFromName(keyName);

            // An analogue line carries a fourth field. `900 = LeftStickX -0.5`
            // is a stick held half left for as long as the recording says --
            // held, like a key, because an axis has a value rather than an
            // edge.
            bool wellFormed = input.keyCode != 0 && (edge == "+" || edge == "-" || input.analog);
            if (input.analog) {
                std::string valueText;
                if (fields >> valueText)
                    input.value = std::strtof(valueText.c_str(), nullptr);
                else
                    wellFormed = false;
            }

            if (!wellFormed) {
                // Refused rather than skipped. A recording with a typo in it
                // would otherwise replay as a different recording and still
                // pass, which is the failure mode this whole harness exists to
                // rule out.
                const std::array<I18nArg, 2> args{I18nArg{"path", inputPath.string()},
                                                  I18nArg{"line", static_cast<core::i64>(lineNumber)}};
                return core::makeError(LUAUG_TR("engine.replay.err.bad_input"), args);
            }
            out.inputs.push_back(input);
        }

        // Stable within a tick: the order two keys change in on the same tick is
        // part of the recording, so the sort must not reorder them (R10).
        std::stable_sort(out.inputs.begin(), out.inputs.end(),
                         [](const ReplayInput& a, const ReplayInput& b) { return a.tick < b.tick; });
    }
    return std::nullopt;
}

std::optional<core::EngineError> runScenario(const ReplayScenario& scenario, ReplayTrace& out)
{
    out.checkpoints.clear();

    // A script error is not fatal to a run -- a deferred handler that throws is
    // contained, by design (api-design.md §3.1) -- so without this the harness
    // would happily record a golden for a scenario that died on its first tick
    // and reproduce that emptiness forever. Which is exactly what happened the
    // first time this was recorded.
    //
    // The host's own sink is wrapped rather than replaced, so the error is still
    // printed and the reason is visible above the failure.
    u64 errors = 0;
    core::LogSink previous = core::setLogSink([&errors](LogLevel level, std::string_view text) {
        if (level == LogLevel::Error)
            ++errors;
        if (const core::LogSink& host = hostSink(); host)
            host(level, text);
    });
    hostSink() = std::move(previous);
    const struct SinkScope
    {
        ~SinkScope() { core::setLogSink(std::move(hostSink())); }
    } sinkScope;

    WorldHost host;
    if (auto error = host.boot({
            .projectPath = scenario.scriptPath,
            .seed = scenario.seed,
            .fixedTimestep = kReplayTimestep,
            .reloadState = nullptr,
            .isReload = false,
            .preserved = nullptr,
            .conformanceRoot = {},
            .bootScene = {},
        });
        error.has_value())
        return error;

    // Tick 0 is the world as boot left it -- entry scripts deferred, nothing
    // resumed. Sampling it separates "the scenario built a different world" from
    // "the scenario simulated it differently", and those have different causes.
    out.checkpoints.push_back({0, host.world().worldHash()});

    // The recorded device, applied before each tick and held between them -- a
    // key stays down until the recording says it came up, and an axis keeps its
    // value until the recording moves it.
    //
    // Handed to the Input Action System as a SNAPSHOT rather than pumped as
    // events, which is the seam `InputSystem::setSnapshot` exists for: what the
    // replay drives is the state a tick would have seen, so everything from the
    // resolver down runs exactly as it does with a keyboard attached. That is
    // what makes this a replay of INPUT rather than of the API underneath it.
    input::DeviceState device;
    usize nextInput = 0;

    for (u64 tick = 1; tick <= scenario.ticks; ++tick) {
        while (nextInput < scenario.inputs.size() && scenario.inputs[nextInput].tick <= tick) {
            const ReplayInput& recorded = scenario.inputs[nextInput];
            const auto slot = static_cast<usize>(recorded.keyCode);
            if (recorded.analog)
                device.axis[slot] = recorded.value;
            else
                device.held[slot] = recorded.down;
            ++nextInput;
        }
        if (!scenario.inputs.empty())
            host.input().setSnapshot(device);

        host.tick();

        // The counters a script can read, published in a replay too. Most of
        // them are zero here and honestly so -- there is no render frame -- but
        // the audio ones are real, and M6's soak gate is a scenario that reads
        // `AudioUnderruns` after an hour of ticks. A stat that only existed in
        // a windowed run would be a stat no gate could assert.
        host.publishStats({
            .audioUnderruns = static_cast<f64>(host.audio().stats().underruns),
            .audioVoices = static_cast<f64>(host.audio().stats().activeVoices),
        });

        if (tick % scenario.checkpointEvery == 0 || tick == scenario.ticks)
            out.checkpoints.push_back({tick, host.world().worldHash()});
    }

    // The attribute the scenario says the run has to end with, read BEFORE
    // `close`: a `BindToClose` handler is a chance to finish, and a game that
    // only set its flag on the way out would be claiming a finish it did not
    // reach during play.
    bool reached = scenario.requireAttribute.empty();
    if (!reached) {
        const scene::Value value =
            host.world().getAttribute(host.dataModel(), host.world().atoms().lookup(scenario.requireAttribute));
        if (const auto* flag = std::get_if<bool>(&value); flag != nullptr)
            reached = *flag;
        else if (const auto* number = std::get_if<f64>(&value); number != nullptr)
            reached = *number != 0.0;
    }

    host.close();

    if (!reached) {
        const std::array<I18nArg, 2> args{I18nArg{"name", scenario.name},
                                          I18nArg{"attribute", scenario.requireAttribute}};
        return core::makeError(LUAUG_TR("engine.replay.err.attribute_missing"), args);
    }

    if (errors != 0) {
        const std::array<I18nArg, 2> args{I18nArg{"name", scenario.name},
                                          I18nArg{"count", static_cast<core::i64>(errors)}};
        return core::makeError(LUAUG_TR("engine.replay.err.script_errors"), args);
    }
    return std::nullopt;
}

std::optional<core::EngineError> writeTrace(const std::filesystem::path& path, const ReplayTrace& trace)
{
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);

    // Binary, and '\n' written explicitly: a trace recorded on Windows and one
    // recorded on Linux have to be the same bytes, or the cross-platform tier
    // fails on line endings and says "hashes differ".
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        const std::array<I18nArg, 1> args{I18nArg{"path", path.string()}};
        return core::makeError(LUAUG_TR("engine.replay.err.open_failed"), args);
    }

    for (const ReplayCheckpoint& checkpoint : trace.checkpoints)
        file << checkpoint.tick << ' ' << hex(checkpoint.hash) << '\n';

    file.close();
    if (!file) {
        const std::array<I18nArg, 1> args{I18nArg{"path", path.string()}};
        return core::makeError(LUAUG_TR("engine.replay.err.write_failed"), args);
    }
    return std::nullopt;
}

std::optional<core::EngineError> readTrace(const std::filesystem::path& path, ReplayTrace& out)
{
    std::string source;
    if (auto error = readFile(path, source); error.has_value())
        return error;

    out.checkpoints.clear();
    std::istringstream lines(source);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;

        std::istringstream fields(line);
        u64 tick = 0;
        std::string digits;
        if (!(fields >> tick >> digits)) {
            const std::array<I18nArg, 2> args{I18nArg{"path", path.string()}, I18nArg{"reason", line}};
            return core::makeError(LUAUG_TR("engine.replay.err.bad_trace"), args);
        }

        u64 hashValue = 0;
        const char* first = digits.data();
        const char* last = first + digits.size();
        if (std::from_chars(first, last, hashValue, 16).ptr != last) {
            const std::array<I18nArg, 2> args{I18nArg{"path", path.string()}, I18nArg{"reason", line}};
            return core::makeError(LUAUG_TR("engine.replay.err.bad_trace"), args);
        }
        out.checkpoints.push_back({tick, hashValue});
    }
    return std::nullopt;
}

std::optional<core::EngineError> compareTraces(const ReplayTrace& expected, const ReplayTrace& actual,
                                               std::string_view expectedLabel, std::string_view actualLabel)
{
    const usize shared = std::min(expected.checkpoints.size(), actual.checkpoints.size());
    for (usize i = 0; i < shared; ++i) {
        const ReplayCheckpoint& lhs = expected.checkpoints[i];
        const ReplayCheckpoint& rhs = actual.checkpoints[i];
        if (lhs.tick == rhs.tick && lhs.hash == rhs.hash)
            continue;

        const std::array<I18nArg, 5> args{
            I18nArg{"tick", static_cast<core::i64>(rhs.tick)},
            I18nArg{"expectedLabel", expectedLabel},
            I18nArg{"expected", hex(lhs.hash)},
            I18nArg{"actualLabel", actualLabel},
            I18nArg{"actual", hex(rhs.hash)},
        };
        return core::makeError(LUAUG_TR("engine.replay.err.diverged"), args);
    }

    if (expected.checkpoints.size() != actual.checkpoints.size()) {
        const std::array<I18nArg, 4> args{
            I18nArg{"expectedLabel", expectedLabel},
            I18nArg{"expected", static_cast<core::i64>(expected.checkpoints.size())},
            I18nArg{"actualLabel", actualLabel},
            I18nArg{"actual", static_cast<core::i64>(actual.checkpoints.size())},
        };
        return core::makeError(LUAUG_TR("engine.replay.err.length_mismatch"), args);
    }
    return std::nullopt;
}

std::optional<core::EngineError> runReplayGate(const std::filesystem::path& root, bool record)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        const std::array<I18nArg, 1> args{I18nArg{"path", root.string()}};
        return core::makeError(LUAUG_TR("engine.replay.err.open_failed"), args);
    }

    // Sorted, so the run order is the same everywhere. `directory_iterator` has
    // no defined order, and a harness that exists to prove determinism must not
    // depend on the filesystem's.
    std::vector<std::filesystem::path> directories;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root, ec)) {
        if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / "scenario.json", ec))
            directories.push_back(entry.path());
    }
    std::sort(directories.begin(), directories.end());

    if (directories.empty()) {
        // An empty gate passes forever, which is worse than no gate: it reports
        // success for a suite that has silently stopped existing.
        const std::array<I18nArg, 1> args{I18nArg{"path", root.string()}};
        return core::makeError(LUAUG_TR("engine.replay.err.no_scenarios"), args);
    }

    for (const std::filesystem::path& directory : directories) {
        ReplayScenario scenario;
        if (auto error = loadScenario(directory, scenario); error.has_value())
            return error;

        ReplayTrace first;
        if (auto error = runScenario(scenario, first); error.has_value())
            return error;

        const std::filesystem::path tracePath = tracePathFor(directory);

        // A scenario whose hash is only comparable within one build carries no
        // committed trace: it is run three times and the three must agree.
        //
        // Three rather than two because that is the roadmap's wording for the
        // M5 gate -- "identical final world hash across 3 runs in CI" -- and
        // because the run that catches leaked state is the one after the first,
        // so a third costs one more and covers a leak that only shows up on a
        // later repeat. What it does NOT cover is a behaviour change, and the
        // scenario is expected to carry its own assertions for that.
        if (scenario.sameBuildOnly) {
            if (record) {
                const std::array<I18nArg, 1> args{I18nArg{"name", scenario.name}};
                core::log(LogLevel::Info, LUAUG_TR("engine.replay.info.same_build_only"), args);
                continue;
            }

            for (int repeat = 2; repeat <= 3; ++repeat) {
                ReplayTrace again;
                if (auto error = runScenario(scenario, again); error.has_value())
                    return error;
                const std::string label = "run " + std::to_string(repeat);
                if (auto error = compareTraces(first, again, "run 1", label); error.has_value())
                    return error;
            }

            const std::array<I18nArg, 3> selfArgs{
                I18nArg{"name", scenario.name},
                I18nArg{"ticks", static_cast<core::i64>(scenario.ticks)},
                I18nArg{"hash", hex(first.finalHash())},
            };
            core::log(LogLevel::Info, LUAUG_TR("engine.replay.info.same_build_ok"), selfArgs);
            continue;
        }

        if (record) {
            if (auto error = writeTrace(tracePath, first); error.has_value())
                return error;

            const std::array<I18nArg, 2> args{I18nArg{"name", scenario.name}, I18nArg{"path", tracePath.string()}};
            core::log(LogLevel::Info, LUAUG_TR("engine.replay.info.recorded"), args);
            continue;
        }

        // The second run is in the SAME process, on a second `WorldHost`. That
        // is the leg that catches state left in a global, in a static, or in an
        // allocator whose addresses leaked into an iteration order -- none of
        // which a fresh process would ever show.
        ReplayTrace second;
        if (auto error = runScenario(scenario, second); error.has_value())
            return error;

        if (auto error = compareTraces(first, second, "run 1", "run 2"); error.has_value())
            return error;

        // A missing trace for THIS platform is an error, never a skip. A gate
        // that quietly degrades to "the two in-process runs agreed" is the
        // weaker half of the check reporting success for the whole of it, and
        // the in-process half is exactly the one that missed the padding bug.
        std::error_code traceEc;
        if (!std::filesystem::exists(tracePath, traceEc)) {
            const std::array<I18nArg, 2> args{I18nArg{"path", tracePath.string()}, I18nArg{"platform", platformName()}};
            return core::makeError(LUAUG_TR("engine.replay.err.no_trace_for_platform"), args);
        }

        ReplayTrace golden;
        if (auto error = readTrace(tracePath, golden); error.has_value())
            return error;

        if (auto error = compareTraces(golden, first, "recorded", "this build"); error.has_value())
            return error;

        const std::array<I18nArg, 3> args{
            I18nArg{"name", scenario.name},
            I18nArg{"ticks", static_cast<core::i64>(scenario.ticks)},
            I18nArg{"hash", hex(first.finalHash())},
        };
        core::log(LogLevel::Info, LUAUG_TR("engine.replay.info.scenario_ok"), args);
    }

    const std::array<I18nArg, 1> args{I18nArg{"count", static_cast<core::i64>(directories.size())}};
    core::log(LogLevel::Info, LUAUG_TR("engine.replay.info.summary"), args);
    return std::nullopt;
}

} // namespace luaug::app
