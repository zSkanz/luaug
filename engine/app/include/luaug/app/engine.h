// Subsystem bring-up and the frame loop (architecture.md §2 "app", §3).
#pragma once

#include <filesystem>
#include <optional>

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

    rhi::BackendId backend = rhi::BackendId::SdlGpu;

    i32 width = 1280;
    i32 height = 720;
};

// Runs to completion. Returns the first error that stopped it, or nothing on a
// clean exit.
[[nodiscard]] std::optional<core::EngineError> run(const EngineOptions& options);

} // namespace luaug::app
