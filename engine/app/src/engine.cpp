#include "luaug/app/engine.h"

#include <array>
#include <cmath>
#include <vector>

#include "luaug/app/backends.h"
#include "luaug/app/frame_scheduler.h"
#include "luaug/app/screenshot.h"
#include "luaug/app/script_host.h"
#include "luaug/core/build_info.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/event.h"
#include "luaug/platform/platform.h"
#include "luaug/platform/window.h"
#include "luaug/rhi/device.h"

namespace luaug::app
{
namespace
{

using core::f32;
using core::f64;
using core::I18nArg;
using core::LogLevel;

constexpr f64 kNanosPerSecond = 1'000'000'000.0;

// M1's stand-in for a renderer: a colour that moves, so a static frame and a
// running one are distinguishable in a screenshot. Derived from the tick count
// rather than the wall clock, which is what makes a headless capture
// reproducible -- the same frame number gives the same colour, on any machine,
// at any speed. That property is what allows a golden gate to exist at all
// (R10, architecture.md §9).
[[nodiscard]] rhi::ColorRgba pulseColor(u64 tick, f64 fixedDt) noexcept
{
    const f64 t = static_cast<f64>(tick) * fixedDt;
    const auto wave = [t](f64 phase) { return static_cast<f32>(0.5 + 0.5 * std::sin(t + phase)); };
    // Thirds of a turn apart, so the three channels never move together and a
    // channel that is stuck shows up.
    return {.r = wave(0.0), .g = wave(2.0944), .b = wave(4.1888), .a = 1.0f};
}

} // namespace

std::optional<core::EngineError> run(const EngineOptions& options)
{
    if (const auto error = platform::init({.headless = options.headless}); error.has_value())
        return error;

    // Declaration order below IS the shutdown order, reversed, and it is not
    // arbitrary: SDL_GPU requires a window to be released from its device
    // before the window is destroyed. Declaring the window first means the
    // device dies first -- releasing it -- on every path out of this function,
    // including the early returns.
    struct PlatformScope
    {
        ~PlatformScope() { platform::shutdown(); }
    } platformScope;

    platform::WindowPtr window;
    core::EngineError error;

    const rhi::DeviceResult device = createDevice({.backend = options.backend, .debug = true}, &error);
    if (device == nullptr)
        return error;

    if (!options.headless)
    {
        const std::array<I18nArg, 1> titleArgs{I18nArg{"version", LUAUG_VERSION_STRING}};
        window = platform::createWindow(
            {
                .titleKey = LUAUG_TR("platform.window.title"),
                .titleArgs = titleArgs,
                .width = options.width,
                .height = options.height,
            },
            &error);
        if (window == nullptr)
            return error;

        if (!device->claimWindow(*window))
            return core::makeError(LUAUG_TR("rhi.err.window_claim_failed"), {}, "SDL_ClaimWindowForGPUDevice");
    }

    // Headless has no swapchain, so it owns a target of its own. Everything
    // downstream is identical, which is the point: the harness exercises the
    // same path a windowed run does rather than a simplified one.
    rhi::TextureHandle offscreen;
    if (options.headless)
    {
        offscreen = device->createTexture({
            .format = rhi::TextureFormat::Rgba8Unorm,
            .usage = rhi::TextureUsage::ColorTarget,
            .width = static_cast<core::u32>(options.width),
            .height = static_cast<core::u32>(options.height),
            .debugName = "headless-color",
        });
        if (!offscreen.valid())
            return core::makeError(LUAUG_TR("rhi.err.target_create_failed"));
    }

    if (!options.scriptPath.empty())
    {
        ScriptHost host;
        if (std::optional<core::EngineError> scriptError = host.runFile(options.scriptPath);
            scriptError.has_value())
            return scriptError;
    }

    FrameScheduler scheduler;
    bool quit = false;

    while (!quit)
    {
        if (options.frames != 0 && scheduler.totalFrames() >= options.frames)
            break;

        // A headless run drives a synthetic clock: exactly one fixed step per
        // frame, as fast as the machine goes. Real time would make the tick
        // count -- and therefore the pixels -- depend on how busy the runner
        // was, which is the whole failure mode a golden gate must not have.
        const u64 nowNs = options.headless
            ? static_cast<u64>(static_cast<f64>(scheduler.totalFrames()) * scheduler.timing().fixedDt
                * kNanosPerSecond)
            : platform::nowNs();

        const Frame frame = scheduler.beginFrame(nowNs);

        if (!options.headless)
        {
            for (const platform::Event& event : platform::pumpEvents())
            {
                if (event.type == platform::EventType::Quit
                    || event.type == platform::EventType::WindowCloseRequested)
                    quit = true;
            }
        }

        rhi::ICmdList* cmd = device->beginFrame();
        if (cmd == nullptr)
            continue;

        // An invalid target is normal, not an error: a minimized window has no
        // backbuffer this frame. Submitting the empty command buffer keeps the
        // loop pumping instead of stalling on a window nobody can see.
        rhi::TextureHandle target = offscreen;
        if (!options.headless)
            target = device->acquireSwapchain(*window).texture;

        if (target.valid())
        {
            const std::array<rhi::ColorAttachment, 1> colors{rhi::ColorAttachment{
                .texture = target,
                .loadOp = rhi::LoadOp::Clear,
                .storeOp = rhi::StoreOp::Store,
                .clearColor = pulseColor(scheduler.totalTicks(), scheduler.timing().fixedDt),
            }};

            cmd->pushDebugGroup("frame");
            cmd->beginRenderPass({.colorAttachments = colors, .debugName = "clear"});
            cmd->endRenderPass();
            cmd->popDebugGroup();
        }

        device->submitAndPresent();

        if (options.frames != 0 && options.exitAfterFrames && frame.index + 1 >= options.frames)
            quit = true;
    }

    device->waitIdle();

    if (!options.screenshotPath.empty() && offscreen.valid())
    {
        const auto pixelCount = static_cast<core::usize>(options.width) * static_cast<core::usize>(options.height);
        std::vector<std::byte> pixels(pixelCount * 4u);

        if (!device->readTexture(offscreen, pixels))
            return core::makeError(LUAUG_TR("engine.screenshot.err.readback_failed"));

        if (auto writeError = writePng(options.screenshotPath, pixels, static_cast<core::u32>(options.width),
                static_cast<core::u32>(options.height));
            writeError.has_value())
            return writeError;

        const std::array<I18nArg, 1> shotArgs{I18nArg{"path", options.screenshotPath.string()}};
        core::log(LogLevel::Info, LUAUG_TR("engine.screenshot.info.written"), shotArgs);
    }

    const std::array<I18nArg, 2> summary{
        I18nArg{"frames", static_cast<core::i64>(scheduler.totalFrames())},
        I18nArg{"ticks", static_cast<core::i64>(scheduler.totalTicks())}};
    core::log(LogLevel::Info, LUAUG_TR("engine.frame.info.summary"), summary);

    if (offscreen.valid())
        device->destroy(offscreen);
    if (window != nullptr)
        device->releaseWindow(*window);

    return std::nullopt;
}

} // namespace luaug::app
