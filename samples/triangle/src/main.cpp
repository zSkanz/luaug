// luaug-triangle -- a window, a clear, and one triangle through the SDL_GPU
// backend. Nothing else.
//
// This is the artifact the M4 Android checkpoint is passed with (roadmap M4,
// "The triangle sample and its Android package"). ADR 0005 records SDL3 GPU's
// Android support as officially "limited" and keeps bgfx as the hedge, and only
// a real phone can say which way that goes -- so this binary has to answer
// exactly one question, "does SDL3 GPU rasterize on this device", and must not
// be able to fail for any other reason.
//
// That is why it is not `luaug-host`. The host links the Luau VM, a world, a
// script mount and a project layout; on a device where none of that has ever
// run, a black screen would have a dozen candidate causes and every one would
// have to be excluded before the result meant anything. What is linked here is
// core + platform + rhi_api + rhi_sdlgpu + render + asset, and the whole render
// path is: load two shaders, build one pipeline, upload three vertices, draw.
//
// `--verify` is the point of the sample on a device with no CTest: it reads the
// rendered frame back and asserts on two pixels, so the phone can report a
// verdict rather than a photograph.
//
// Messages here are written in English at the call site rather than through the
// catalog. This is a developer diagnostic tool, not shipped product text, and
// its own strings must not enter `i18n/en.json` -- engine messages that reach
// it (device creation, shader loading) arrive already formatted through the
// catalog and are printed verbatim.

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "luaug/asset/image.h"
#include "luaug/core/build_info.h"
#include "luaug/core/error.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/core/types.h"
#include "luaug/platform/console.h"
#include "luaug/platform/event.h"
#include "luaug/platform/platform.h"
#include "luaug/platform/window.h"
#include "luaug/render/shader_library.h"
#include "luaug/rhi/backends.h"
#include "luaug/rhi/device.h"

namespace
{

using luaug::core::f32;
using luaug::core::i32;
using luaug::core::u32;
using luaug::core::u64;
using luaug::core::usize;
using luaug::core::I18nArg;
using luaug::core::LogLevel;

namespace platform = luaug::platform;
namespace render = luaug::render;
namespace rhi = luaug::rhi;

constexpr int kExitOk = 0;
constexpr int kExitFailed = 1;
constexpr int kExitUsage = 2;

// Distinct because "this machine has no usable GPU" is not a failure of
// anything under test -- the CTest driver maps it to a skip. Same value the
// host uses, so one convention covers both binaries.
constexpr int kExitNoGraphicsDevice = 4;

// Rgba8Unorm is linear and four bytes wide, which is what `asset::writePng` and
// the pixel probe below both assume. Named once so the target, the pipeline and
// the readback cannot drift apart.
constexpr rhi::TextureFormat kOffscreenFormat = rhi::TextureFormat::Rgba8Unorm;

// Neither black nor white, and no channel at either end of the range. M1's
// finding on the clear gate: a channel that is dropped, swizzled or written as
// a constant is invisible when the reference value happens to be 0 or 255, so
// the reference values are chosen to be neither.
constexpr rhi::ColorRgba kClearColor{.r = 0.12f, .g = 0.24f, .b = 0.44f, .a = 1.0f};
constexpr rhi::ColorRgba kTriangleColor{.r = 0.92f, .g = 0.36f, .b = 0.64f, .a = 1.0f};

// Per channel, on the 0..255 readback. GPUs round the last bit of a unorm
// conversion differently and a probe that fires on that is a probe that gets
// switched off; two is far below the distance between the two colours here.
constexpr int kChannelTolerance = 2;

struct Vertex
{
    std::array<f32, 3> clipPosition;
    std::array<f32, 4> color;
};

static_assert(offsetof(Vertex, clipPosition) == 0, "the shader reads the position at offset 0");
static_assert(offsetof(Vertex, color) == 12, "the shader reads the colour immediately after the position");

// Clip space directly: the shader applies no transform, so there is no camera,
// no projection and no matrix convention that could be wrong. Big enough that
// the centre of the frame is well inside it and every corner is well outside,
// which is what makes the two-pixel probe meaningful.
constexpr std::array<Vertex, 3> kTriangle{
    Vertex{{0.0f, 0.75f, 0.0f}, {kTriangleColor.r, kTriangleColor.g, kTriangleColor.b, kTriangleColor.a}},
    Vertex{{-0.75f, -0.75f, 0.0f}, {kTriangleColor.r, kTriangleColor.g, kTriangleColor.b, kTriangleColor.a}},
    Vertex{{0.75f, -0.75f, 0.0f}, {kTriangleColor.r, kTriangleColor.g, kTriangleColor.b, kTriangleColor.a}},
};

struct Options
{
    // `--help` is a successful run that has nothing to render, so it needs its
    // own answer: neither kExitOk (which would go on to open a device) nor
    // kExitUsage (which would report a mistake nobody made).
    bool helpRequested = false;

    // Zero means "until the window is closed", which only a windowed run can
    // honour; a headless run with no budget is rejected during parsing.
    u64 frames = 0;
    bool headless = false;
    bool verify = false;
    std::filesystem::path screenshotPath;
    i32 width = 1280;
    i32 height = 720;
};

void report(LogLevel level, std::string_view text)
{
    const auto stream = (level == LogLevel::Warn || level == LogLevel::Error) ? platform::ConsoleStream::Err
                                                                             : platform::ConsoleStream::Out;
    platform::writeConsole(stream, luaug::core::formatLogLine(level, text));
}

void reportError(const luaug::core::EngineError& error)
{
    report(LogLevel::Error, error.message);
    if (!error.detail.empty())
        report(LogLevel::Error, error.detail);
}

void printUsage()
{
    report(LogLevel::Info,
        "luaug-triangle -- draws one triangle through the SDL_GPU backend.\n"
        "\n"
        "  --frames N          stop after N frames (required with --headless)\n"
        "  --headless          render into an offscreen target instead of a window\n"
        "  --screenshot PATH   write the frame to PATH as PNG (implies a readback, so --headless)\n"
        "  --verify            assert the triangle is where it should be and report a verdict\n"
        "  --help              this text\n"
        "\n"
        "Both spellings of a valued flag are accepted: --frames 3 and --frames=3.");
}

struct Rgba8
{
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 0;
};

[[nodiscard]] Rgba8 toRgba8(const rhi::ColorRgba& color) noexcept
{
    const auto channel = [](f32 value) { return static_cast<int>(std::lround(value * 255.0f)); };
    return {channel(color.r), channel(color.g), channel(color.b), channel(color.a)};
}

[[nodiscard]] Rgba8 pixelAt(std::span<const std::byte> pixels, u32 width, u32 x, u32 y) noexcept
{
    const usize index = (static_cast<usize>(y) * width + x) * 4u;
    return {
        std::to_integer<int>(pixels[index]),
        std::to_integer<int>(pixels[index + 1]),
        std::to_integer<int>(pixels[index + 2]),
        std::to_integer<int>(pixels[index + 3]),
    };
}

[[nodiscard]] bool withinTolerance(const Rgba8& actual, const Rgba8& expected) noexcept
{
    return std::abs(actual.r - expected.r) <= kChannelTolerance
        && std::abs(actual.g - expected.g) <= kChannelTolerance
        && std::abs(actual.b - expected.b) <= kChannelTolerance
        && std::abs(actual.a - expected.a) <= kChannelTolerance;
}

[[nodiscard]] std::string describe(const Rgba8& color)
{
    return "rgba(" + std::to_string(color.r) + ", " + std::to_string(color.g) + ", " + std::to_string(color.b)
        + ", " + std::to_string(color.a) + ")";
}

// Two claims that no single failure can satisfy at once: the middle of the
// frame is the triangle's colour, and all four corners are still the clear
// colour. A pipeline that drew nothing fails the first; one that drew over the
// whole target -- a full-screen quad, a clear in the wrong colour, a shader
// returning a constant -- fails the second. Checking one pixel would catch only
// one of those.
//
// The corners are read at all four so that a flipped or rotated readback is a
// failure of the flip, not of this probe.
[[nodiscard]] bool verifyFrame(std::span<const std::byte> pixels, u32 width, u32 height)
{
    const Rgba8 expectedTriangle = toRgba8(kTriangleColor);
    const Rgba8 expectedClear = toRgba8(kClearColor);

    bool ok = true;

    const Rgba8 centre = pixelAt(pixels, width, width / 2u, height / 2u);
    if (withinTolerance(centre, expectedTriangle))
    {
        report(LogLevel::Info, "verify: centre is the triangle colour " + describe(centre));
    }
    else
    {
        report(LogLevel::Error,
            "verify: centre is " + describe(centre) + ", expected the triangle colour "
                + describe(expectedTriangle));
        ok = false;
    }

    const std::array<std::pair<u32, u32>, 4> corners{
        std::pair<u32, u32>{0u, 0u},
        std::pair<u32, u32>{width - 1u, 0u},
        std::pair<u32, u32>{0u, height - 1u},
        std::pair<u32, u32>{width - 1u, height - 1u},
    };

    for (const auto& [x, y] : corners)
    {
        const Rgba8 corner = pixelAt(pixels, width, x, y);
        if (withinTolerance(corner, expectedClear))
            continue;

        report(LogLevel::Error,
            "verify: corner (" + std::to_string(x) + ", " + std::to_string(y) + ") is " + describe(corner)
                + ", expected the clear colour " + describe(expectedClear));
        ok = false;
    }

    if (ok)
        report(LogLevel::Info, "verify: all four corners are the clear colour " + describe(expectedClear));

    return ok;
}

// Owns the shaders, the pipeline and the vertex buffer for one device. Built on
// the first frame that has a target rather than up front, because a graphics
// pipeline is compiled against one colour format and a swapchain's is not known
// until it has been acquired.
class TrianglePass
{
public:
    // Empty on success, otherwise a diagnostic ready to print. A string rather
    // than an EngineError because two of the failures below are this sample's
    // own and have no catalog key -- and inventing one would put a developer
    // tool's text into the engine's shipped message catalog. Engine-sourced
    // failures arrive already formatted through the catalog and are passed on
    // unchanged.
    [[nodiscard]] std::string create(rhi::IDevice& device, rhi::TextureFormat colorFormat)
    {
        render::ShaderLibrary shaders;
        if (auto loadError = shaders.load(platform::paths().contentDir, device.caps().shaderFormat);
            loadError.has_value())
            return loadError->message;

        luaug::core::EngineError error;

        vertexShader_ = shaders.create(device, "triangle", rhi::ShaderStage::Vertex, &error);
        if (!vertexShader_.valid())
            return error.message;

        fragmentShader_ = shaders.create(device, "triangle", rhi::ShaderStage::Fragment, &error);
        if (!fragmentShader_.valid())
            return error.message;

        const std::array<rhi::VertexBufferLayout, 1> buffers{
            rhi::VertexBufferLayout{.slot = 0, .strideBytes = static_cast<u32>(sizeof(Vertex))}};

        const std::array<rhi::VertexAttribute, 2> attributes{
            rhi::VertexAttribute{
                .location = 0,
                .bufferSlot = 0,
                .format = rhi::VertexFormat::Float3,
                .offsetBytes = offsetof(Vertex, clipPosition),
            },
            rhi::VertexAttribute{
                .location = 1,
                .bufferSlot = 0,
                .format = rhi::VertexFormat::Float4,
                .offsetBytes = offsetof(Vertex, color),
            },
        };

        const std::array<rhi::ColorTargetDesc, 1> targets{rhi::ColorTargetDesc{.format = colorFormat}};

        pipeline_ = device.createGraphicsPipeline({
            .vertexShader = vertexShader_,
            .fragmentShader = fragmentShader_,
            .vertexBuffers = buffers,
            .vertexAttributes = attributes,
            .primitive = rhi::PrimitiveType::TriangleList,
            // No culling. Winding is one more thing that can be wrong on an
            // untried device, and a sample whose failure mode is "the triangle
            // is facing away" answers the wrong question.
            .rasterizer = {.cullMode = rhi::CullMode::None},
            .colorTargets = targets,
            .debugName = "triangle",
        });

        if (!pipeline_.valid())
            return "the graphics device would not create the triangle pipeline";

        vertices_ = device.createBuffer({
            .usage = rhi::BufferUsage::Vertex,
            .sizeBytes = static_cast<u32>(sizeof(kTriangle)),
            .debugName = "triangle-vertices",
        });

        if (!vertices_.valid())
            return "the graphics device would not create the vertex buffer";

        return {};
    }

    [[nodiscard]] bool valid() const noexcept { return pipeline_.valid() && vertices_.valid(); }

    // Once, not per frame: the three vertices never change, and a sample that
    // re-staged them every frame would be modelling a problem it does not have.
    // Outside the render pass, because a copy cannot run inside one.
    void uploadOnce(rhi::ICmdList& cmd)
    {
        if (uploaded_ || !valid())
            return;

        cmd.upload(vertices_, std::as_bytes(std::span{kTriangle}), 0);
        uploaded_ = true;
    }

    void draw(rhi::ICmdList& cmd)
    {
        if (!valid() || !uploaded_)
            return;

        cmd.pushDebugGroup("triangle");
        cmd.setPipeline(pipeline_);
        const std::array<rhi::BufferHandle, 1> buffers{vertices_};
        cmd.bindVertexBuffers(0, buffers);
        cmd.draw(static_cast<u32>(kTriangle.size()), 1, 0, 0);
        cmd.popDebugGroup();
    }

    // The device is not a member, so releasing in a destructor is not possible
    // without holding one; this is called explicitly before the device dies.
    void destroy(rhi::IDevice& device)
    {
        if (vertices_.valid())
            device.destroy(vertices_);
        if (pipeline_.valid())
            device.destroy(pipeline_);
        if (fragmentShader_.valid())
            device.destroy(fragmentShader_);
        if (vertexShader_.valid())
            device.destroy(vertexShader_);

        vertexShader_ = {};
        fragmentShader_ = {};
        pipeline_ = {};
        vertices_ = {};
        uploaded_ = false;
    }

private:
    rhi::ShaderHandle vertexShader_{};
    rhi::ShaderHandle fragmentShader_{};
    rhi::PipelineHandle pipeline_{};
    rhi::BufferHandle vertices_{};
    bool uploaded_ = false;
};

// Accepts both `--frames=3` and `--frames 3`. The host spells its flags with
// `=`; somebody typing this into an adb shell will not, and a sample that
// answers a device question should not also be a spelling test.
[[nodiscard]] bool valueFor(
    std::span<const std::string_view> args, usize& index, std::string_view name, std::string_view& out)
{
    const std::string_view arg = args[index];
    if (arg == name)
    {
        if (index + 1 >= args.size())
            return false;
        ++index;
        out = args[index];
        return true;
    }

    if (arg.starts_with(name) && arg.size() > name.size() && arg[name.size()] == '=')
    {
        out = arg.substr(name.size() + 1);
        return !out.empty();
    }

    return false;
}

// kExitOk when the caller should proceed, otherwise the exit code to return.
[[nodiscard]] int parseOptions(std::span<const std::string_view> args, Options& options)
{
    for (usize i = 0; i < args.size(); ++i)
    {
        const std::string_view arg = args[i];
        std::string_view value;

        if (arg == "--help")
        {
            printUsage();
            options.helpRequested = true;
            return kExitOk;
        }
        if (arg == "--headless")
        {
            options.headless = true;
            continue;
        }
        if (arg == "--verify")
        {
            options.verify = true;
            continue;
        }
        if (arg.starts_with("--frames"))
        {
            if (!valueFor(args, i, "--frames", value))
            {
                report(LogLevel::Error, std::string("--frames needs a count: ") + std::string(arg));
                return kExitUsage;
            }
            const auto* const end = value.data() + value.size();
            if (const auto result = std::from_chars(value.data(), end, options.frames);
                result.ec != std::errc{} || result.ptr != end)
            {
                report(LogLevel::Error, std::string("--frames is not a number: ") + std::string(value));
                return kExitUsage;
            }
            continue;
        }
        if (arg.starts_with("--screenshot"))
        {
            if (!valueFor(args, i, "--screenshot", value))
            {
                report(LogLevel::Error, std::string("--screenshot needs a path: ") + std::string(arg));
                return kExitUsage;
            }
            options.screenshotPath = std::filesystem::path(value);
            continue;
        }

        report(LogLevel::Error, std::string("unknown option: ") + std::string(arg));
        printUsage();
        return kExitUsage;
    }

    // A headless run has no window to close, so with no budget it would run
    // until something killed it -- under CTest, until the job's timeout. Said
    // here rather than discovered there.
    if (options.headless && options.frames == 0)
    {
        report(LogLevel::Error, "--headless needs --frames N: there is no window to close.");
        return kExitUsage;
    }

    // A windowed frame is presented and gone before anything could read it
    // back. Headless renders into a target this process owns, which is why both
    // the screenshot and the probe require it -- the same rule the host has.
    if ((!options.screenshotPath.empty() || options.verify) && !options.headless)
    {
        report(LogLevel::Error, "--screenshot and --verify need --headless: a presented frame cannot be read back.");
        return kExitUsage;
    }

    return kExitOk;
}

int run(const Options& options)
{
    if (auto error = platform::init({.headless = options.headless}); error.has_value())
    {
        reportError(*error);
        return kExitFailed;
    }

    struct PlatformScope
    {
        ~PlatformScope() { platform::shutdown(); }
    } platformScope;

    // Declaration order below IS the shutdown order, reversed, and it is not
    // arbitrary: SDL_GPU requires a window to be released from its device
    // before the window is destroyed. Declaring the window first means the
    // device dies first -- releasing it -- on every path out of this function,
    // including the early returns.
    platform::WindowPtr window;
    luaug::core::EngineError error;

    const rhi::DeviceResult device = rhi::createSdlGpuDevice({.debug = true}, &error);
    if (device == nullptr)
    {
        reportError(error);
        return kExitNoGraphicsDevice;
    }

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
        {
            reportError(error);
            return kExitFailed;
        }

        if (!device->claimWindow(*window))
        {
            reportError(luaug::core::makeError(
                LUAUG_TR("rhi.err.window_claim_failed"), {}, "SDL_ClaimWindowForGPUDevice"));
            return kExitFailed;
        }
    }

    const auto targetWidth = static_cast<u32>(options.width);
    const auto targetHeight = static_cast<u32>(options.height);

    rhi::TextureHandle offscreen;
    if (options.headless)
    {
        offscreen = device->createTexture({
            .format = kOffscreenFormat,
            .usage = rhi::TextureUsage::ColorTarget,
            .width = targetWidth,
            .height = targetHeight,
            .debugName = "triangle-color",
        });
        if (!offscreen.valid())
        {
            reportError(luaug::core::makeError(LUAUG_TR("rhi.err.target_create_failed")));
            return kExitFailed;
        }
    }

    TrianglePass pass;
    bool passAttempted = false;
    std::string passError;

    u64 frame = 0;
    bool quit = false;

    while (!quit)
    {
        if (options.frames != 0 && frame >= options.frames)
            break;

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
        {
            report(LogLevel::Error, "the graphics device would not open a command buffer");
            return kExitFailed;
        }

        rhi::TextureHandle target = offscreen;
        rhi::TextureFormat targetFormat = kOffscreenFormat;
        u32 viewportWidth = targetWidth;
        u32 viewportHeight = targetHeight;

        if (!options.headless)
        {
            const rhi::Swapchain swapchain = device->acquireSwapchain(*window);
            target = swapchain.texture;
            targetFormat = swapchain.format;
            viewportWidth = swapchain.width;
            viewportHeight = swapchain.height;
        }

        // An invalid target is normal, not an error: a minimized window has no
        // backbuffer this frame. Submitting the empty command buffer keeps the
        // loop pumping instead of stalling on a window nobody can see.
        if (target.valid())
        {
            if (!passAttempted)
            {
                passAttempted = true;
                passError = pass.create(*device, targetFormat);
                if (!passError.empty())
                    quit = true;
            }

            pass.uploadOnce(*cmd);

            const std::array<rhi::ColorAttachment, 1> colors{rhi::ColorAttachment{
                .texture = target,
                .loadOp = rhi::LoadOp::Clear,
                .storeOp = rhi::StoreOp::Store,
                .clearColor = kClearColor,
            }};

            cmd->pushDebugGroup("frame");
            cmd->beginRenderPass({.colorAttachments = colors, .debugName = "triangle"});

            if (viewportWidth > 0 && viewportHeight > 0)
            {
                cmd->setViewport({
                    .width = static_cast<f32>(viewportWidth),
                    .height = static_cast<f32>(viewportHeight),
                });
                pass.draw(*cmd);
            }

            cmd->endRenderPass();
            cmd->popDebugGroup();
        }

        device->submitAndPresent();
        ++frame;
    }

    device->waitIdle();

    int exitCode = kExitOk;

    if (!passError.empty())
    {
        report(LogLevel::Error, passError);
        exitCode = kExitFailed;
    }

    if (exitCode == kExitOk && (options.verify || !options.screenshotPath.empty()))
    {
        std::vector<std::byte> pixels(static_cast<usize>(targetWidth) * targetHeight * 4u);
        if (!device->readTexture(offscreen, pixels))
        {
            reportError(luaug::core::makeError(LUAUG_TR("engine.screenshot.err.readback_failed")));
            exitCode = kExitFailed;
        }
        else
        {
            if (!options.screenshotPath.empty())
            {
                if (auto writeError
                    = luaug::asset::writePng(options.screenshotPath, pixels, targetWidth, targetHeight);
                    writeError.has_value())
                {
                    reportError(*writeError);
                    exitCode = kExitFailed;
                }
                else
                {
                    const std::array<I18nArg, 1> shotArgs{I18nArg{"path", options.screenshotPath.string()}};
                    luaug::core::log(LogLevel::Info, LUAUG_TR("engine.screenshot.info.written"), shotArgs);
                }
            }

            if (options.verify && !verifyFrame(pixels, targetWidth, targetHeight))
                exitCode = kExitFailed;
        }
    }

    report(LogLevel::Info, "Ran " + std::to_string(frame) + " frames.");

    pass.destroy(*device);
    if (offscreen.valid())
        device->destroy(offscreen);
    if (window != nullptr)
        device->releaseWindow(*window);

    return exitCode;
}

} // namespace

int main(int argc, char** argv)
{
    luaug::core::setLogSink([](LogLevel level, std::string_view text) { report(level, text); });

    // A warning rather than a failure, unlike the host. The catalog only
    // decides how legibly an engine message reads, and refusing to draw a
    // triangle because a translation file did not ship would defeat the one
    // thing this binary exists to find out.
    const auto catalogLoad
        = luaug::core::engineCatalog().loadFromFile(platform::paths().contentDir / "i18n" / "en.json");
    if (!catalogLoad)
        report(LogLevel::Warn, "no message catalog: " + catalogLoad.diagnostic);

    std::vector<std::string_view> args;
    args.reserve(static_cast<usize>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);

    Options options;
    if (const int usageExit = parseOptions(args, options); usageExit != kExitOk)
        return usageExit;

    if (options.helpRequested)
        return kExitOk;

    return run(options);
}
