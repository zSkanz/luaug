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
    // M1 still runs one script and then the frame loop; M2's kernel is what
    // makes the script drive the loop instead of preceding it.
    std::filesystem::path scriptPath;

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

    rhi::BackendId backend = rhi::BackendId::SdlGpu;

    i32 width = 1280;
    i32 height = 720;
};

// Runs to completion. Returns the first error that stopped it, or nothing on a
// clean exit.
[[nodiscard]] std::optional<core::EngineError> run(const EngineOptions& options);

} // namespace luaug::app
