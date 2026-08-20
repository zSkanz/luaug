#include "luaug/app/bench.h"

#include "luaug/app/replay.h"
#include "luaug/app/world_host.h"
#include "luaug/core/json.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/platform.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace luaug::app {
namespace {

using core::I18nArg;
using core::LogLevel;
using core::usize;

constexpr f64 kNanosPerMs = 1'000'000.0;
constexpr f64 kBenchTimestep = 1.0 / 60.0;

struct Sample
{
    f64 meanTickMs = 0.0;
    f64 maxTickMs = 0.0;
    u64 instances = 0;
    // The three stages the physics mirror can separate, accumulated over the
    // run and reported as a per-tick mean. Zero for a scenario with no bodies,
    // which is most of them.
    f64 applyMs = 0.0;
    f64 stepMs = 0.0;
    f64 writebackMs = 0.0;
    u64 bodies = 0;
};

// Reads the one field a benchmark manifest has that a replay manifest does not.
// Parsed here rather than added to `ReplayScenario`, so that a determinism
// scenario cannot acquire a performance budget by accident.
[[nodiscard]] f64 budgetOf(const std::filesystem::path& directory)
{
    core::JsonDocument document;
    std::string source;
    if (std::ifstream file(directory / "scenario.json", std::ios::binary); file) {
        std::ostringstream buffer;
        buffer << file.rdbuf();
        source = buffer.str();
    }
    if (!document.parse(source, "scenario.json"))
        return 0.0;
    return document.root()["budgetMs"].asNumber(0.0);
}

[[nodiscard]] std::optional<core::EngineError> runOnce(const ReplayScenario& scenario, Sample& out)
{
    WorldHost host;
    if (auto error = host.boot({
            .projectPath = scenario.scriptPath,
            .seed = scenario.seed,
            .fixedTimestep = kBenchTimestep,
            .reloadState = nullptr,
            .isReload = false,
            .preserved = nullptr,
            .conformanceRoot = {},
        });
        error.has_value())
        return error;

    // The clock is read once before and once after each tick, and nothing
    // between them belongs to the harness -- no hashing, no reporting. A
    // benchmark that measures its own instrumentation is measuring the wrong
    // thing.
    u64 totalNs = 0;
    u64 worstNs = 0;
    f64 applySeconds = 0.0;
    f64 stepSeconds = 0.0;
    f64 writebackSeconds = 0.0;
    for (u64 tick = 0; tick < scenario.ticks; ++tick) {
        const u64 before = platform::nowNs();
        host.tick();
        const u64 elapsed = platform::nowNs() - before;
        totalNs += elapsed;
        worstNs = std::max(worstNs, elapsed);

        // Read after the tick rather than measured here: the mirror is the only
        // party that knows where its own stages begin and end, and a harness
        // timing them from outside would be timing the call, not the stage.
        if (const scene::PhysicsSync* physics = host.physics(); physics != nullptr) {
            applySeconds += physics->timings().apply;
            stepSeconds += physics->timings().step;
            writebackSeconds += physics->timings().writeback;
        }
    }

    out.instances = static_cast<u64>(host.world().instanceCount());
    if (const scene::PhysicsSync* physics = host.physics(); physics != nullptr) {
        const auto ticks = static_cast<f64>(scenario.ticks == 0 ? 1 : scenario.ticks);
        out.applyMs = applySeconds * 1000.0 / ticks;
        out.stepMs = stepSeconds * 1000.0 / ticks;
        out.writebackMs = writebackSeconds * 1000.0 / ticks;
        out.bodies = static_cast<u64>(physics->bodyCount());
    }
    out.meanTickMs =
        scenario.ticks == 0 ? 0.0 : static_cast<f64>(totalNs) / static_cast<f64>(scenario.ticks) / kNanosPerMs;
    out.maxTickMs = static_cast<f64>(worstNs) / kNanosPerMs;

    host.close();
    return std::nullopt;
}

[[nodiscard]] f64 median(std::vector<f64>& values)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

// Four significant figures at most, because a benchmark that prints
// 0.08333333333333333 ms invites someone to compare the last digit of two runs.
[[nodiscard]] std::string milliseconds(f64 value)
{
    std::array<char, 32> text{};
    const int written = std::snprintf(text.data(), text.size(), "%.4f", value);
    return written <= 0 ? std::string{"0"} : std::string{text.data(), static_cast<usize>(written)};
}

} // namespace

std::optional<core::EngineError> runBenchmarks(const std::filesystem::path& root, u64 repeats,
                                               std::vector<BenchResult>& out)
{
    out.clear();
    repeats = std::max<u64>(1, repeats);

    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        const std::array<I18nArg, 1> args{I18nArg{"path", root.string()}};
        return core::makeError(LUAUG_TR("engine.replay.err.open_failed"), args);
    }

    std::vector<std::filesystem::path> directories;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root, ec)) {
        if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / "scenario.json", ec))
            directories.push_back(entry.path());
    }
    std::sort(directories.begin(), directories.end());

    if (directories.empty()) {
        const std::array<I18nArg, 1> args{I18nArg{"path", root.string()}};
        return core::makeError(LUAUG_TR("engine.replay.err.no_scenarios"), args);
    }

    std::optional<core::EngineError> overBudget;
    for (const std::filesystem::path& directory : directories) {
        ReplayScenario scenario;
        if (auto error = loadScenario(directory, scenario); error.has_value())
            return error;

        std::vector<f64> means;
        std::vector<f64> maxima;
        std::vector<f64> applies;
        std::vector<f64> steps;
        std::vector<f64> writebacks;
        BenchResult result;
        result.name = scenario.name;
        result.ticks = scenario.ticks;
        result.budgetMs = budgetOf(directory);

        for (u64 repeat = 0; repeat < repeats; ++repeat) {
            Sample sample;
            if (auto error = runOnce(scenario, sample); error.has_value())
                return error;
            means.push_back(sample.meanTickMs);
            maxima.push_back(sample.maxTickMs);
            result.instances = sample.instances;
            applies.push_back(sample.applyMs);
            steps.push_back(sample.stepMs);
            writebacks.push_back(sample.writebackMs);
            result.bodies = sample.bodies;
        }

        result.meanTickMs = median(means);
        result.maxTickMs = median(maxima);
        result.applyMs = median(applies);
        result.stepMs = median(steps);
        result.writebackMs = median(writebacks);

        const std::array<I18nArg, 5> args{
            I18nArg{"name", result.name},
            I18nArg{"instances", static_cast<core::i64>(result.instances)},
            I18nArg{"ticks", static_cast<core::i64>(result.ticks)},
            I18nArg{"mean", milliseconds(result.meanTickMs)},
            I18nArg{"max", milliseconds(result.maxTickMs)},
        };
        core::log(LogLevel::Info, LUAUG_TR("engine.bench.info.result"), args);

        // The M5 gate's second item wants the physics budget "broken down"
        // (roadmap M5). These are the three stages this seam can separate, and
        // the line is emitted only for a scenario that has bodies -- a
        // breakdown of zeroes beside every other benchmark would be noise the
        // eye learns to skip.
        if (result.bodies > 0) {
            const std::array<I18nArg, 5> stageArgs{
                I18nArg{"name", result.name},
                I18nArg{"bodies", static_cast<core::i64>(result.bodies)},
                I18nArg{"apply", milliseconds(result.applyMs)},
                I18nArg{"step", milliseconds(result.stepMs)},
                I18nArg{"writeback", milliseconds(result.writebackMs)},
            };
            core::log(LogLevel::Info, LUAUG_TR("engine.bench.info.physics"), stageArgs);
        }

        // Recorded before the failure is raised, and every scenario runs even
        // after one blows its budget: the point of a run is the whole table, and
        // stopping at the first regression hides the other three.
        if (result.budgetMs > 0.0 && result.meanTickMs > result.budgetMs && !overBudget.has_value()) {
            const std::array<I18nArg, 3> budgetArgs{
                I18nArg{"name", result.name},
                I18nArg{"mean", milliseconds(result.meanTickMs)},
                I18nArg{"budget", milliseconds(result.budgetMs)},
            };
            overBudget = core::makeError(LUAUG_TR("engine.bench.err.over_budget"), budgetArgs);
        }

        out.push_back(std::move(result));
    }

    return overBudget;
}

} // namespace luaug::app
