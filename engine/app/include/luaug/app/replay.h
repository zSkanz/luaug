// Record/replay harness v1 (ADR 0025, architecture.md §9 "Determinism tests").
//
// A replay is pure simulation. It creates no device, no window and no swapchain,
// because none of those may influence the world -- and a harness that boots them
// anyway would be unable to say so. What it produces is a *trace*: the world
// hash sampled at a fixed tick interval, which is a far more useful failure
// report than a single number at the end. Two runs that diverge at tick 4200
// tell you where to look; two final hashes that differ tell you nothing.
#pragma once

#include "luaug/core/error.h"
#include "luaug/core/types.h"
#include "luaug/platform/event.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace luaug::app {

using core::u64;

// The whole of a scenario's input. ADR 0025's claim is that this is *all* of it:
// if a run depends on anything not written here, that is the defect the harness
// exists to find.
// One key changing state at one tick. The whole of a scenario's INPUT, as
// opposed to its script -- which is the distinction that makes "input replay"
// mean anything (roadmap M5's first gate item).
//
// Before M5 a scenario's script WAS its input, which was honest while nothing
// could be steered. A bot that calls `Move` directly proves the simulation is
// deterministic and proves nothing about the path a keystroke takes to reach
// it; this replays the keystroke.
// One recorded transition. `keyCode` is an `Enum.KeyCode` value, which spans
// the keyboard, the mouse and the gamepad -- M5 recorded `platform::Key` alone,
// because the keyboard scaffold was the only path input had.
//
// An analogue line sets `axis` instead of a press: `900 = LeftStickX -0.5`.
// Both shapes are one struct because a recording is read in tick order and a
// second vector would have to be merged back into that order anyway.
struct ReplayInput
{
    u64 tick = 0;
    core::i32 keyCode = 0;
    bool down = false;
    bool analog = false;
    float value = 0.0f;
};

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

    // An attribute the DataModel must carry, truthy, when the last tick has run.
    // Empty for a scenario that only checks determinism.
    //
    // **This is what turns a replay into an END-TO-END gate.** M6's own gate is
    // "an input replay of a full obby run completes to the finish flag", and a
    // hash comparison cannot say whether the flag was reached -- three runs that
    // all fall in the same hole agree perfectly. The scenario names the fact it
    // is really asserting, and the game sets it.
    std::string requireAttribute;

    // Read from `inputs.txt` beside the manifest, and empty when there is none.
    // Sorted by tick, and stable within a tick: the order two keys change in on
    // the same tick is part of the recording (R10).
    std::vector<ReplayInput> inputs;

    // **This scenario's hash is only comparable within one build**, so it
    // carries no committed trace and is verified by running it three times and
    // requiring the three to agree.
    //
    // That is ADR 0025's level B stated exactly: "same engine BUILD + same
    // platform ... same observable simulation result". Every scenario before M5
    // was integer and tree state, which happens to survive a change of
    // compiler, so committing a trace and checking it on CI worked and was
    // worth more than self-consistency -- it catches a behaviour change, which
    // running something three times cannot.
    //
    // A physics scenario is different in kind: its state is floating point, its
    // divergence amplifies over ticks, and the CI runner's compiler is not this
    // machine's. Verified by CI, at tick 600 of a 3,600-tick replay, against a
    // trace recorded here.
    //
    // What replaces the behaviour half for such a scenario is assertions inside
    // the scene, with tolerances -- which is what `tests/determinism/character`
    // has seven of, and which survive a compiler change precisely because they
    // are not bit comparisons.
    bool sameBuildOnly = false;
};

struct ReplayCheckpoint
{
    u64 tick = 0;
    u64 hash = 0;
};

struct ReplayTrace
{
    std::vector<ReplayCheckpoint> checkpoints;

    [[nodiscard]] u64 finalHash() const noexcept { return checkpoints.empty() ? 0u : checkpoints.back().hash; }
};

// Parses `<directory>/scenario.json`. The script path in it is resolved against
// the directory, so a scenario is relocatable.
[[nodiscard]] std::optional<core::EngineError> loadScenario(const std::filesystem::path& directory,
                                                            ReplayScenario& out);

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
[[nodiscard]] std::optional<core::EngineError> compareTraces(const ReplayTrace& expected, const ReplayTrace& actual,
                                                             std::string_view expectedLabel,
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
