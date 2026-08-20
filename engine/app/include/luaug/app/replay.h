// Record/replay harness v1 (ADR 0025, architecture.md §9 "Determinism tests").
//
// A replay is pure simulation. It creates no device, no window and no swapchain,
// because none of those may influence the world -- and a harness that boots them
// anyway would be unable to say so. What it produces is a *trace*: the world
// hash sampled at a fixed tick interval, which is a far more useful failure
// report than a single number at the end. Two runs that diverge at tick 4200
// tell you where to look; two final hashes that differ tell you nothing.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "luaug/core/error.h"
#include "luaug/core/types.h"

namespace luaug::app
{

using core::u64;

// The whole of a scenario's input. ADR 0025's claim is that this is *all* of it:
// if a run depends on anything not written here, that is the defect the harness
// exists to find.
struct ReplayScenario
{
    // Absolute, so a trace does not encode where the repository was checked out.
    std::filesystem::path scriptPath;
    std::string name;
    u64 seed = 1;
    u64 ticks = 0;
    // Every Nth tick is sampled, and the last tick always is. Sampling costs a
    // full walk of the world, so this is a real dial rather than a formality:
    // the gate's 10,000 ticks at every 500 is 20 samples.
    u64 checkpointEvery = 1;
};

struct ReplayCheckpoint
{
    u64 tick = 0;
    u64 hash = 0;
};

struct ReplayTrace
{
    std::vector<ReplayCheckpoint> checkpoints;

    [[nodiscard]] u64 finalHash() const noexcept
    {
        return checkpoints.empty() ? 0u : checkpoints.back().hash;
    }
};

// Parses `<directory>/scenario.json`. The script path in it is resolved against
// the directory, so a scenario is relocatable.
[[nodiscard]] std::optional<core::EngineError> loadScenario(
    const std::filesystem::path& directory, ReplayScenario& out);

// Boots a world, runs `scenario.ticks` fixed steps and samples the hash. Nothing
// here reads a clock: the tick is the only time there is.
[[nodiscard]] std::optional<core::EngineError> runScenario(const ReplayScenario& scenario, ReplayTrace& out);

// Text, one `<tick> <hash>` line per checkpoint in hex. Not JSON, deliberately:
// a trace is reviewed as a diff when it changes, and a diff of 20 short lines
// says which checkpoint moved. It is also the format `--record-replay` writes,
// so the golden and the run agree by construction.
[[nodiscard]] std::optional<core::EngineError> writeTrace(const std::filesystem::path& path, const ReplayTrace& trace);
[[nodiscard]] std::optional<core::EngineError> readTrace(const std::filesystem::path& path, ReplayTrace& out);

// Reports the FIRST checkpoint at which two traces disagree, which is the one
// worth reporting -- everything after a divergence is a consequence of it.
[[nodiscard]] std::optional<core::EngineError> compareTraces(
    const ReplayTrace& expected, const ReplayTrace& actual, std::string_view expectedLabel,
    std::string_view actualLabel);

// The gate. Runs every scenario under `root` twice in this process and compares
// the two traces against each other and against the recorded `trace.txt` -- and
// since the process itself is fresh each time CTest starts it, that third
// comparison is the cross-process leg architecture.md §9 asks for.
//
// `record` rewrites each `trace.txt` from the run instead of comparing, which is
// the only way a legitimate semantic change gets a new golden.
[[nodiscard]] std::optional<core::EngineError> runReplayGate(const std::filesystem::path& root, bool record);

} // namespace luaug::app
