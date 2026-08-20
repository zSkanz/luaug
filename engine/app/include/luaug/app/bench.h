// The simulation benchmark harness (roadmap M2 gate: "500-instance scene ticks
// under budget", and the 10k-parts / 1k-listeners property-churn threshold).
//
// It shares `ReplayScenario` with the determinism harness on purpose: a
// benchmark scene that is not also a deterministic scene is measuring something
// different on every run, and the number it reports would mean nothing.
//
// This is the one place in the engine allowed to read a wall clock as part of
// its purpose. R10 forbids *simulation* code from reading one; a harness that
// measures elapsed time has no other instrument.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "luaug/core/error.h"
#include "luaug/core/types.h"

namespace luaug::app
{

using core::f64;
using core::u64;

struct BenchResult
{
    std::string name;
    u64 ticks = 0;
    u64 instances = 0;
    // Median across repeats, which is what the baselines table records: a mean
    // over runs would let one descheduled run move the published number.
    f64 meanTickMs = 0.0;
    f64 maxTickMs = 0.0;
    // The scenario's own per-tick ceiling, from `budgetMs` in its manifest. Zero
    // means the scenario reports without gating.
    f64 budgetMs = 0.0;
};

// Runs every scenario under `root` `repeats` times and reports the median.
// Fails if any scenario exceeded its `budgetMs`.
//
// The threshold is a catastrophe detector, not an instrument. CI runners differ
// by more than a factor of two between one job and the next, so a budget tight
// enough to catch a 10% regression would be red every other week and would
// teach everyone to ignore it. The precise numbers live in
// `docs/perf-baselines.md`, measured on the reference machine; this catches the
// change that made a tick ten times slower.
[[nodiscard]] std::optional<core::EngineError> runBenchmarks(
    const std::filesystem::path& root, u64 repeats, std::vector<BenchResult>& out);

} // namespace luaug::app
