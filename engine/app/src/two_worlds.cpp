#include "luaug/app/two_worlds.h"

#include "luaug/app/backends.h"
#include "luaug/app/screenshot.h"
#include "luaug/app/world_host.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/jobs/jobs.h"
#include "luaug/platform/platform.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/mesh_loader.h"
#include "luaug/render/render_world.h"
#include "luaug/render/renderer.h"
#include "luaug/render/shader_library.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace luaug::app {
namespace {

using core::f32;
using core::f64;
using core::I18nArg;
using core::LogLevel;
using core::u32;
using core::u64;

constexpr rhi::TextureFormat kColorFormat = rhi::TextureFormat::Rgba8Unorm;
constexpr f64 kTimestep = 1.0 / 60.0;

// One world's whole render path: the host, and the per-world GPU state a
// renderer needs to draw it.
//
// **The mesh cache and the mesh library are per world**, which is the half of
// this gate a reader should look at hardest. They hold geometry keyed by the
// instances that asked for it, so one shared between two worlds would be
// exactly the coupling this proof exists to refuse -- and it would be invisible
// as long as both worlds happened to want the same shapes.
struct Session
{
    WorldHost host;
    render::MeshCache meshes;
    render::MeshLoader loader;
    render::MeshLibrary library;
    rhi::TextureHandle target;

    // **Per world, and this is the first thing the proof taught.** A renderer
    // is not stateless per view: it carries the exposure it has adapted to and
    // the environment chain it has been baking a level at a time, both of which
    // are properties of the world it has been looking at. One renderer shared
    // between two worlds does not leak scene DATA -- it leaks scene HISTORY,
    // which is enough to make every image differ. `renderer.h` already says the
    // shape of the answer for a different reason ("a caller that renders into
    // two formats needs two renderers"); a caller rendering two worlds is the
    // same sentence.
    std::unique_ptr<render::IRenderer> renderer;

    // What came off the GPU. Compared in memory; the PNG is for a human.
    std::vector<std::byte> pixels;
};

[[nodiscard]] std::optional<core::EngineError> openSession(Session& session, rhi::IDevice& device,
                                                           const render::ShaderLibrary& shaders,
                                                           const std::filesystem::path& project,
                                                           const TwoWorldsOptions& options, u64 seed)
{
    session.target = device.createTexture({
        .format = kColorFormat,
        .usage = rhi::TextureUsage::ColorTarget,
        .width = static_cast<u32>(options.width),
        .height = static_cast<u32>(options.height),
        .debugName = "two-worlds-color",
    });
    if (!session.target.valid())
        return core::makeError(LUAUG_TR("rhi.err.target_create_failed"));

    if (auto error = session.meshes.create(device); error.has_value())
        return error;

    session.renderer = render::createDefaultRenderer();
    if (auto error = session.renderer->create(device, shaders, kColorFormat); error.has_value())
        return error;

    session.loader.setContentRoot(project / "content");

    // Each world gets its own seed, so a world that reads one is still telling
    // the truth about which world it is. Both are literals: a seed drawn from
    // anything but a constant would make the comparison depend on the order the
    // sessions were opened in.
    const WorldHostOptions worldOptions{
        .projectPath = project,
        .seed = seed,
        .fixedTimestep = kTimestep,
        .reloadState = nullptr,
        .isReload = false,
        .preserved = nullptr,
        .conformanceRoot = {},
    };
    return session.host.boot(worldOptions);
}

void closeSession(Session& session, rhi::IDevice& device)
{
    if (session.renderer != nullptr) {
        session.renderer->destroy(device);
        session.renderer.reset();
    }
    session.loader.destroy(device);
    session.meshes.destroy(device);
    if (session.target.valid())
        device.destroy(session.target);
    session.target = {};
}

// Ticks the world and draws it into its own target, leaving the pixels in
// `session.pixels`. Everything a frame of `engine.cpp` does that can influence
// an image, in the same order, minus the parts a headless run does not have.
[[nodiscard]] std::optional<core::EngineError> renderSession(Session& session, rhi::IDevice& device,
                                                             const TwoWorldsOptions& options)
{
    // Every session runs the same number of frames, and that is a requirement
    // rather than a convenience: exposure adapts towards the frame before it and
    // the environment chain bakes one level per frame, so a world rendered once
    // and the same world rendered four times are two different pictures. The
    // comparison is only about isolation if the frame COUNT is held equal.
    const f32 aspect = options.height == 0 ? 1.0f : static_cast<f32>(options.width) / static_cast<f32>(options.height);
    render::RenderWorld snapshot;

    for (u64 frame = 0; frame < options.ticks; ++frame) {
        session.host.tick();

        rhi::ICmdList* cmd = device.beginFrame();
        if (cmd == nullptr)
            return core::makeError(LUAUG_TR("rhi.err.target_create_failed"));

        session.meshes.beginFrame(device);
        session.loader.syncPrimitives(device, *cmd, session.host.world(), session.meshes, session.library);
        (void)session.loader.sync(device, *cmd, session.host.world(), session.host.workspace(), session.meshes,
                                  session.library);

        // No interpolation: this proof renders exactly at the tick, which is
        // what every headless run does and what makes two of its frames
        // comparable at all (D047).
        render::extract(session.host.world(), session.host.workspace(), session.host.lighting(), session.library,
                        aspect, session.renderer->shadowRadius(), session.host.animation(), 0.0f, nullptr, snapshot);

        session.renderer->render(device, *cmd,
                                 {
                                     .color = session.target,
                                     .colorFormat = kColorFormat,
                                     .width = static_cast<u32>(options.width),
                                     .height = static_cast<u32>(options.height),
                                 },
                                 snapshot, session.meshes);

        device.submitAndPresent();
    }

    device.waitIdle();

    session.pixels.assign(static_cast<std::size_t>(options.width) * static_cast<std::size_t>(options.height) * 4,
                          std::byte{});
    if (!device.readTexture(session.target, session.pixels))
        return core::makeError(LUAUG_TR("engine.twoworlds.err.readback_failed"));

    return std::nullopt;
}

[[nodiscard]] std::optional<core::EngineError> writeEvidence(const TwoWorldsOptions& options, std::string_view name,
                                                             const std::vector<std::byte>& pixels)
{
    if (options.outputDir.empty())
        return std::nullopt;

    std::error_code ec;
    std::filesystem::create_directories(options.outputDir, ec);
    const std::filesystem::path path = options.outputDir / (std::string(name) + ".png");
    return writePng(path, pixels, static_cast<u32>(options.width), static_cast<u32>(options.height));
}

// How many bytes of two images differ. A count rather than a bool, because both
// halves of this gate are about a magnitude: "identical" is zero and "different"
// has to be large enough that nobody can mistake it for a rounding difference
// in the last bit of a unorm conversion.
[[nodiscard]] std::size_t differingBytes(const std::vector<std::byte>& left, const std::vector<std::byte>& right)
{
    if (left.size() != right.size())
        return left.size() + right.size();
    std::size_t count = 0;
    for (std::size_t index = 0; index < left.size(); ++index)
        count += left[index] == right[index] ? 0u : 1u;
    return count;
}

} // namespace

std::optional<core::EngineError> runTwoWorldsGate(const TwoWorldsOptions& options)
{
    const std::filesystem::path projectA = options.root / "a";
    const std::filesystem::path projectB = options.root / "b";
    std::error_code ec;
    if (!std::filesystem::is_directory(projectA, ec) || !std::filesystem::is_directory(projectB, ec)) {
        const std::array<I18nArg, 1> args{I18nArg{"path", options.root.string()}};
        return core::makeError(LUAUG_TR("engine.twoworlds.err.no_projects"), args);
    }

    jobs::init();
    if (const auto error = platform::init({.headless = true}); error.has_value())
        return error;

    struct PlatformScope
    {
        ~PlatformScope()
        {
            platform::shutdown();
            jobs::shutdown();
        }
    } platformScope;

    core::EngineError deviceError;
    const rhi::DeviceResult device = createDevice({.backend = options.backend, .debug = true}, &deviceError);
    if (device == nullptr)
        return deviceError;

    render::ShaderLibrary shaders;
    if (auto error = shaders.load(platform::paths().contentDir, device->caps().shaderFormat); error.has_value())
        return error;

    std::vector<std::byte> soloA;
    std::vector<std::byte> soloB;

    // Phase one: each world alone, which is the control. Scoped so that the
    // host, its VM and its GPU state are all gone before the next one opens --
    // a solo render that shared anything with the pair phase would be comparing
    // a world against itself.
    {
        Session session;
        if (auto error = openSession(session, *device, shaders, projectA, options, 1); error.has_value())
            return error;
        if (auto error = renderSession(session, *device, options); error.has_value())
            return error;
        soloA = session.pixels;
        closeSession(session, *device);
    }
    {
        Session session;
        if (auto error = openSession(session, *device, shaders, projectB, options, 2); error.has_value())
            return error;
        if (auto error = renderSession(session, *device, options); error.has_value())
            return error;
        soloB = session.pixels;
        closeSession(session, *device);
    }

    // Phase two: BOTH alive at once. Two `WorldHost`s, two `ScriptRuntime`s,
    // two Luau VMs, two render targets -- which is the sentence ADR 0017's
    // condition reduces to.
    Session pairA;
    Session pairB;
    if (auto error = openSession(pairA, *device, shaders, projectA, options, 1); error.has_value())
        return error;
    if (auto error = openSession(pairB, *device, shaders, projectB, options, 2); error.has_value())
        return error;

    // Interleaved rather than run to completion one after the other: a static
    // that the second boot overwrites would still be the second world's by the
    // time anything drew, and this is the order an editor actually ticks in.
    if (auto error = renderSession(pairA, *device, options); error.has_value())
        return error;
    if (auto error = renderSession(pairB, *device, options); error.has_value())
        return error;

    if (auto error = writeEvidence(options, "solo-a", soloA); error.has_value())
        return error;
    if (auto error = writeEvidence(options, "solo-b", soloB); error.has_value())
        return error;
    if (auto error = writeEvidence(options, "pair-a", pairA.pixels); error.has_value())
        return error;
    if (auto error = writeEvidence(options, "pair-b", pairB.pixels); error.has_value())
        return error;

    const std::size_t driftA = differingBytes(soloA, pairA.pixels);
    const std::size_t driftB = differingBytes(soloB, pairB.pixels);
    const std::size_t between = differingBytes(pairA.pixels, pairB.pixels);

    closeSession(pairA, *device);
    closeSession(pairB, *device);

    if (driftA != 0 || driftB != 0) {
        const std::array<I18nArg, 2> args{I18nArg{"a", static_cast<core::i64>(driftA)},
                                          I18nArg{"b", static_cast<core::i64>(driftB)}};
        return core::makeError(LUAUG_TR("engine.twoworlds.err.not_isolated"), args);
    }

    // The vacuous pass, refused. Two worlds that draw nothing are byte-identical
    // to each other and to their own solo renders, and every assertion above
    // would be green -- which is D043's failure exactly, one milestone later.
    if (between == 0)
        return core::makeError(LUAUG_TR("engine.twoworlds.err.indistinguishable"));

    const std::array<I18nArg, 1> okArgs{I18nArg{"different", static_cast<core::i64>(between)}};
    core::log(LogLevel::Info, LUAUG_TR("engine.twoworlds.info.ok"), okArgs);
    return std::nullopt;
}

} // namespace luaug::app
