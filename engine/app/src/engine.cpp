#include "luaug/app/engine.h"

#include <array>
#include <cmath>
#include <fstream>
#include <string>
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
#include "luaug/render/debug_draw.h"
#include "luaug/render/debug_renderer.h"
#include "luaug/render/shader_library.h"
#include "luaug/rhi/device.h"

#if LUAUG_RHI_CAPTURE
#include "luaug/rhi/capture.h"
#endif

namespace luaug::app
{
namespace
{

using core::f32;
using core::f64;
using core::I18nArg;
using core::LogLevel;

constexpr f64 kNanosPerSecond = 1'000'000'000.0;

// The format the headless target is created with, named once so the pipeline
// and the texture cannot disagree.
constexpr rhi::TextureFormat kOffscreenFormat = rhi::TextureFormat::Rgba8Unorm;

// A fixed camera looking at the origin from slightly above. Fixed on purpose:
// M1 has no camera Instance -- that is M4 -- and a moving camera would put a
// second source of change into a golden image whose whole value is that only
// one thing moves.
[[nodiscard]] core::Mat4 orbitCamera(core::u32 width, core::u32 height)
{
    const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
    // Far enough back and high enough that all three orbiting cubes are on
    // screen at every phase, rather than one of them being behind another for
    // part of the orbit. A deliverable that says "three cubes orbit" should
    // show three cubes.
    const core::Mat4 view = core::lookAt({0.0f, 5.5f, 8.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    const core::Mat4 projection = core::perspective(1.0472f, aspect, 0.1f, 100.0f);
    return projection * view;
}

// Three cubes orbiting the origin -- the M1 deliverable, and the thing that
// gives the screenshot gate something with edges in it to be right or wrong
// about (finding 9: a flat colour hides a one-tick shift inside the tolerance).
//
// Driven by the tick count for the same reason the clear colour is: a headless
// capture must produce the same pixels on any machine at any speed.
void submitOrbitingCubes(render::DebugDraw& draw, u64 tick, f64 fixedDt)
{
    constexpr int kCubeCount = 3;
    constexpr f32 kOrbitRadius = 2.5f;
    constexpr f32 kHalfExtent = 0.4f;

    // Fixed and saturated, NOT derived from the animation. A first version drove
    // the cube colours from the same sine the clear colour uses, and a cube
    // whose phase happened to land near the background's became invisible
    // against it -- geometry that can vanish is useless for debugging and worse
    // than useless in a golden image.
    constexpr std::array<render::DebugColor, kCubeCount> kCubeColors{
        render::DebugColor::fromLinear(1.0f, 0.15f, 0.15f),
        render::DebugColor::fromLinear(0.15f, 0.4f, 1.0f),
        render::DebugColor::fromLinear(1.0f, 1.0f, 1.0f),
    };

    const auto t = static_cast<f32>(static_cast<f64>(tick) * fixedDt);

    for (int i = 0; i < kCubeCount; ++i)
    {
        // A third of a turn apart, so all three are on screen from a fixed
        // camera at every point in the orbit rather than eclipsing each other.
        const f32 phase = t + static_cast<f32>(i) * (6.2832f / static_cast<f32>(kCubeCount));
        const core::Vec3 center{kOrbitRadius * std::cos(phase), 0.0f, kOrbitRadius * std::sin(phase)};

        draw.wireBox(center, {kHalfExtent, kHalfExtent, kHalfExtent},
            kCubeColors[static_cast<std::size_t>(i)]);
    }

    // A world triad at the origin, so a wrong view or projection matrix is
    // obvious rather than merely suspicious.
    draw.axes(core::Mat4{}, 1.0f);
}

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

// Writing the recorded stream is the app's job, not the backend's: the backend
// records into memory and knows nothing about files, which is what lets a test
// read a capture without touching a disk.
[[nodiscard]] std::optional<core::EngineError> writeCapture(
    const std::filesystem::path& path, const rhi::IDevice& device)
{
#if LUAUG_RHI_CAPTURE
    const std::string& stream = rhi::captureStream(device);
    if (stream.empty())
    {
        // An empty golden would match forever. Better to fail here than to
        // check in a file that can never catch anything.
        return core::makeError(LUAUG_TR("engine.capture.err.empty"));
    }

    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        const std::array<I18nArg, 1> args{I18nArg{"path", path.string()}};
        return core::makeError(LUAUG_TR("engine.capture.err.open_failed"), args);
    }

    // Binary mode on purpose: the stream is newline-terminated JSON lines, and
    // letting Windows translate them would make a golden recorded on one
    // platform differ from the same frame recorded on another.
    file.write(stream.data(), static_cast<std::streamsize>(stream.size()));
    file.close();

    if (!file)
    {
        const std::array<I18nArg, 1> args{I18nArg{"path", path.string()}};
        return core::makeError(LUAUG_TR("engine.capture.err.write_failed"), args);
    }

    return std::nullopt;
#else
    static_cast<void>(path);
    static_cast<void>(device);
    return core::makeError(LUAUG_TR("engine.capture.err.empty"));
#endif
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
            .format = kOffscreenFormat,
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

    // The debug pass is built on the first frame that has a target, not here,
    // because a graphics pipeline is compiled against one colour format and the
    // swapchain's is not known until it has been acquired. Headless knows its
    // own, but running both paths through the same lazy construction keeps them
    // from drifting.
    //
    // It is also optional by design rather than by accident: a device that
    // renders nothing -- capture, null -- has no pipeline to build, and a
    // machine whose content directory is missing its shaders should boot and
    // say why rather than refuse to start.
    render::ShaderLibrary shaders;
    render::DebugRenderer debugRenderer;
    render::DebugDraw debugDraw;
    bool debugPassAttempted = false;

    const auto ensureDebugPass = [&](rhi::TextureFormat colorFormat)
    {
        // Gated on "can this device take shaders", not on "will pixels come
        // out". They are different questions, and conflating them made the
        // capture backend -- the blocking render gate -- record a frame with no
        // draws in it at all, which is precisely the thing it exists to notice.
        if (debugPassAttempted || device->caps().shaderFormat == rhi::ShaderFormat::Unknown)
            return;
        debugPassAttempted = true;

        if (auto error = shaders.load(platform::paths().contentDir, device->caps().shaderFormat);
            error.has_value())
        {
            core::logText(LogLevel::Warn, error->message);
            return;
        }
        if (auto error = debugRenderer.create(*device, shaders, colorFormat); error.has_value())
            core::logText(LogLevel::Warn, error->message);
    };

    FrameScheduler scheduler;
    const auto headlessStepNs
        = static_cast<u64>(std::ceil(scheduler.timing().fixedDt * kNanosPerSecond));
    bool quit = false;

    while (!quit)
    {
        if (options.frames != 0 && scheduler.totalFrames() >= options.frames)
            break;

        // A headless run drives a synthetic clock: exactly one fixed step per
        // frame, as fast as the machine goes. Real time would make the tick
        // count -- and therefore the pixels -- depend on how busy the runner
        // was, which is the whole failure mode a golden gate must not have.
        //
        // The ceil is load-bearing. 1/60 s is 16666666.67 ns, and truncating it
        // leaves each frame a fraction short of the accumulator's threshold, so
        // ticks fire on some frames and not others -- deterministically, but
        // not the one-per-frame this comment claims. Rounding up costs 0.3 ns
        // of drift per frame and makes the claim true.
        const u64 nowNs = options.headless ? scheduler.totalFrames() * headlessStepNs
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
        rhi::TextureFormat targetFormat = kOffscreenFormat;
        core::u32 targetWidth = static_cast<core::u32>(options.width);
        core::u32 targetHeight = static_cast<core::u32>(options.height);

        if (!options.headless)
        {
            const rhi::Swapchain swapchain = device->acquireSwapchain(*window);
            target = swapchain.texture;
            targetFormat = swapchain.format;
            targetWidth = swapchain.width;
            targetHeight = swapchain.height;
        }

        if (target.valid())
        {
            ensureDebugPass(targetFormat);

            debugDraw.clear();
            submitOrbitingCubes(debugDraw, scheduler.totalTicks(), scheduler.timing().fixedDt);

            // Uploaded before the render pass opens, because a copy cannot run
            // inside one -- the seam says so and the backend enforces it.
            if (debugRenderer.valid())
                debugRenderer.upload(*device, *cmd, debugDraw);

            const std::array<rhi::ColorAttachment, 1> colors{rhi::ColorAttachment{
                .texture = target,
                .loadOp = rhi::LoadOp::Clear,
                .storeOp = rhi::StoreOp::Store,
                .clearColor = pulseColor(scheduler.totalTicks(), scheduler.timing().fixedDt),
            }};

            cmd->pushDebugGroup("frame");
            cmd->beginRenderPass({.colorAttachments = colors, .debugName = "clear"});

            if (debugRenderer.valid() && targetWidth > 0 && targetHeight > 0)
            {
                cmd->setViewport({
                    .width = static_cast<f32>(targetWidth),
                    .height = static_cast<f32>(targetHeight),
                });
                debugRenderer.render(*cmd, orbitCamera(targetWidth, targetHeight));
            }

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

    if (!options.capturePath.empty())
    {
        if (auto captureError = writeCapture(options.capturePath, *device); captureError.has_value())
            return captureError;

        const std::array<I18nArg, 1> captureArgs{I18nArg{"path", options.capturePath.string()}};
        core::log(LogLevel::Info, LUAUG_TR("engine.capture.info.written"), captureArgs);
    }

    const std::array<I18nArg, 2> summary{
        I18nArg{"frames", static_cast<core::i64>(scheduler.totalFrames())},
        I18nArg{"ticks", static_cast<core::i64>(scheduler.totalTicks())}};
    core::log(LogLevel::Info, LUAUG_TR("engine.frame.info.summary"), summary);

    debugRenderer.destroy(*device);
    if (offscreen.valid())
        device->destroy(offscreen);
    if (window != nullptr)
        device->releaseWindow(*window);

    return std::nullopt;
}

} // namespace luaug::app
