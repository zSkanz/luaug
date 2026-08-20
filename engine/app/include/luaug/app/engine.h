// Subsystem bring-up and the frame loop (architecture.md §2 "app", §3).
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "luaug/core/error.h"
#include "luaug/core/types.h"
#include "luaug/rhi/types.h"

namespace luaug::app
{

using core::i32;
using core::u64;

struct EngineOptions
{
    // A directory is a project root and gets the full mount; a file is mounted
    // as a single entry `Script` (M2 brief, Decision 9). The scripts do not run
    // before the loop any more -- they are deferred, and their first resumption
    // is the first drain of the first tick.
    std::filesystem::path scriptPath;

    // The world's deterministic stream. Fixed rather than drawn from a clock,
    // because a replay stores the seed and nothing else (ADR 0025) -- and
    // because two runs of the same script must produce the same world hash,
    // which is the M2 gate.
    u64 worldSeed = 1;

    // No window. The frame loop, the device and the render target all still
    // exist -- this is the CI harness and the shape a dedicated server would
    // take, not a mode where rendering is skipped.
    bool headless = false;

    // Zero means run until the window is closed. Any other value is a frame
    // budget, which is the only thing that makes a headless run terminate.
    u64 frames = 0;

    // Exit when the frame budget is spent instead of continuing to run.
    bool exitAfterFrames = false;

    // Empty means take no screenshot. Requires `headless`: a windowed frame
    // renders into the swapchain, which has been presented and is gone by the
    // time anyone could read it.
    std::filesystem::path screenshotPath;

    // Where to write the recorded command stream. Only the capture backend
    // records one; asking any other backend for it is a usage error rather
    // than an empty file, because an empty golden would pass forever.
    std::filesystem::path capturePath;

    // Runs the conformance suite from this directory instead of a normal
    // session: every `*.spec.luau` under it is mounted as an entry `Script` and
    // the run ends by itself once the suite reports (api-design.md §3).
    std::filesystem::path conformanceRoot;

    // Runs the record/replay determinism gate over this directory instead of a
    // session. A replay creates no device and no window (see `replay.h`), so it
    // is a mode rather than a flag on a normal run.
    std::filesystem::path replayRoot;

    // Rewrites each scenario's `trace.txt` from this build instead of comparing
    // against it. The only way a legitimate semantic change gets a new golden.
    bool replayRecord = false;

    // Runs the simulation benchmarks over this directory. Like a replay it
    // opens no device: what it measures is the tick, and a tick that depended
    // on a swapchain would be the finding rather than the measurement.
    std::filesystem::path benchRoot;
    u64 benchRepeats = 3;

    // Where a conformance run writes its machine-readable per-case report.
    // Empty writes none. `luaug test` reads this rather than the console,
    // because every console line is catalog-resolved (M3 brief, Decision 6).
    std::filesystem::path testReportPath;

    // `ws://127.0.0.1:<port>/<path>` -- the dev server this engine dials out to
    // (ADR 0035). Empty means no watcher is attached and no control code runs
    // at all. The engine never listens, in any profile.
    std::string devControlUrl;
    // Proves this connection is the engine `luaug dev` launched: a loopback
    // listener is reachable by every process on the machine.
    std::string devControlToken;

    rhi::BackendId backend = rhi::BackendId::SdlGpu;

    i32 width = 1280;
    i32 height = 720;
};

// Runs to completion. Returns the first error that stopped it, or nothing on a
// clean exit.
[[nodiscard]] std::optional<core::EngineError> run(const EngineOptions& options);

} // namespace luaug::app
