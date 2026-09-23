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
#include "luaug/replication/extract.h"
#include "luaug/replication/replication.h"

#if LUAUG_ENABLE_REPLICATION
#include "luaug/net/memory_transport.h"
#endif

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
    // **Synchronous, explicitly** (D125). This harness compares two worlds
    // frame for frame, so a mesh or a map that arrived on a different frame in
    // one of them would be the difference it is looking for -- and the loader's
    // default is already synchronous, which is exactly why the intent has to be
    // written down rather than left to be true by accident.
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

[[nodiscard]] std::optional<core::EngineError>
openSession(Session& session, rhi::IDevice& device, const render::ShaderLibrary& shaders,
            const std::filesystem::path& project, const TwoWorldsOptions& options, u64 seed,
            scene::NetworkTopology topology = scene::NetworkTopology::Solo)
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
    // Said rather than inherited: see the member's own comment. A default that
    // happens to be right is a default somebody can change.
    session.loader.setDeferredMeshes(false);
    session.loader.setDeferredTextures(false);

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
        // No scene, so no stamps to read: this path boots a project's scripts
        // and nothing else (ADR 0049).
        .bootStamps = {},
        .bootScene = {},
        .networkTopology = topology,
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
// Draws the world as it stands into the session's target: one frame of
// `engine.cpp` minus the parts a headless run does not have.
[[nodiscard]] std::optional<core::EngineError> drawFrame(Session& session, rhi::IDevice& device,
                                                         const TwoWorldsOptions& options, render::RenderWorld& snapshot)
{
    const f32 aspect = options.height == 0 ? 1.0f : static_cast<f32>(options.width) / static_cast<f32>(options.height);
    {
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
    return std::nullopt;
}

// What the session's target holds, into `session.pixels`.
[[nodiscard]] std::optional<core::EngineError> readBack(Session& session, rhi::IDevice& device,
                                                        const TwoWorldsOptions& options)
{
    device.waitIdle();
    session.pixels.assign(static_cast<std::size_t>(options.width) * static_cast<std::size_t>(options.height) * 4,
                          std::byte{});
    if (!device.readTexture(session.target, session.pixels))
        return core::makeError(LUAUG_TR("engine.twoworlds.err.readback_failed"));
    return std::nullopt;
}

[[nodiscard]] std::optional<core::EngineError> renderSession(Session& session, rhi::IDevice& device,
                                                             const TwoWorldsOptions& options)
{
    // Every session runs the same number of frames, and that is a requirement
    // rather than a convenience: exposure adapts towards the frame before it and
    // the environment chain bakes one level per frame, so a world rendered once
    // and the same world rendered four times are two different pictures. The
    // comparison is only about isolation if the frame COUNT is held equal.
    render::RenderWorld snapshot;
    for (u64 frame = 0; frame < options.ticks; ++frame) {
        session.host.tick();
        if (auto error = drawFrame(session, device, options, snapshot); error.has_value())
            return error;
    }
    return readBack(session, device, options);
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

#if LUAUG_ENABLE_REPLICATION
namespace {

// Pixels that differ in any channel by more than `threshold` levels. A pixel
// count rather than a byte count, and a threshold rather than zero, for the
// reason `ReplicaGateOptions` gives.
[[nodiscard]] std::size_t pixelsApart(const std::vector<std::byte>& left, const std::vector<std::byte>& right,
                                      int threshold)
{
    if (left.size() != right.size())
        return left.size() / 4 + right.size() / 4;
    std::size_t count = 0;
    for (std::size_t pixel = 0; pixel + 3 < left.size(); pixel += 4) {
        bool apart = false;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const int a = std::to_integer<int>(left[pixel + channel]);
            const int b = std::to_integer<int>(right[pixel + channel]);
            apart = apart || (a > b ? a - b : b - a) > threshold;
        }
        count += apart ? 1u : 0u;
    }
    return count;
}

} // namespace
#endif

std::optional<core::EngineError> runReplicaGate(const ReplicaGateOptions& options)
{
#if LUAUG_ENABLE_REPLICATION
    std::error_code ec;
    if (!std::filesystem::is_directory(options.project, ec)) {
        const std::array<I18nArg, 1> args{I18nArg{"path", options.project.string()}};
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

    const TwoWorldsOptions frameOptions{
        .root = {},
        .outputDir = options.outputDir,
        .backend = options.backend,
        .ticks = options.ticks,
        .width = options.width,
        .height = options.height,
    };

    // One seed for both: the replica's own script draws the same numbers the
    // authority's does, so nothing but replication can make the pictures agree
    // or disagree.
    Session authority;
    Session replica;
    if (auto error =
            openSession(authority, *device, shaders, options.project, frameOptions, 1, scene::NetworkTopology::Host);
        error.has_value())
        return error;
    if (auto error =
            openSession(replica, *device, shaders, options.project, frameOptions, 1, scene::NetworkTopology::Replica);
        error.has_value())
        return error;
    // What the replica's own copy of the scene would have duplicated -- the
    // same call the engine makes after a replica boots.
    (void)replication::clearReplicated(replica.host.world(), replica.host.workspace());

    // The memory transport: what arrives is a function of the calls made, so
    // this gate is as repeatable as the determinism traces.
    constexpr core::u16 Port = 7777;
    auto network = net::createMemoryNetwork();
    std::optional<core::EngineError> netError;
    replication::Config hostConfig;
    hostConfig.topology = replication::Topology::Host;
    hostConfig.port = Port;
    hostConfig.ticksPerSnapshot = 1;
    auto hostNet = replication::createReplicationOver(net::createMemoryTransport(network), hostConfig, netError);
    if (hostNet == nullptr)
        return netError;
    replication::Config joinConfig;
    joinConfig.topology = replication::Topology::Replica;
    joinConfig.port = Port;
    joinConfig.address = "memory";
    // Pixel for pixel against the host, so each snapshot is shown as it
    // arrives rather than a few ticks behind it.
    joinConfig.interpolationDelayTicks = 0;
    auto joinNet = replication::createReplicationOver(net::createMemoryTransport(network), joinConfig, netError);
    if (joinNet == nullptr)
        return netError;

    render::RenderWorld authoritySnapshot;
    render::RenderWorld replicaSnapshot;
    std::vector<std::byte> replicaFirst;
    for (u64 frame = 0; frame < options.ticks; ++frame) {
        // Lockstep, in the frame's own order on each side: receive, tick, send.
        // The authority sends tick T after it; the replica applies it before its
        // own tick T, so both draw the state of tick T.
        hostNet->receive(authority.host.world(), authority.host.workspace());
        authority.host.tick();
        hostNet->send(authority.host.world(), authority.host.workspace(), authority.host.world().engineState().tick);
        joinNet->receive(replica.host.world(), replica.host.workspace());
        replica.host.tick();
        joinNet->send(replica.host.world(), replica.host.workspace(), replica.host.world().engineState().tick);

        if (auto error = drawFrame(authority, *device, frameOptions, authoritySnapshot); error.has_value())
            return error;
        if (auto error = drawFrame(replica, *device, frameOptions, replicaSnapshot); error.has_value())
            return error;
        if (frame == 0) {
            if (auto error = readBack(replica, *device, frameOptions); error.has_value())
                return error;
            replicaFirst = replica.pixels;
        }
        // **Drained every frame.** Two sessions submitting hundreds of frames
        // back to back never let the backend retire a command buffer, so its
        // descriptor pools only grew -- and lavapipe, the software device the
        // Linux tier renders on, died inside one of those allocations. A real
        // frame loop presents and waits; this one has to say so.
        device->waitIdle();
    }
    if (auto error = readBack(authority, *device, frameOptions); error.has_value())
        return error;
    if (auto error = readBack(replica, *device, frameOptions); error.has_value())
        return error;

    if (auto error = writeEvidence(frameOptions, "authority", authority.pixels); error.has_value())
        return error;
    if (auto error = writeEvidence(frameOptions, "replica", replica.pixels); error.has_value())
        return error;
    if (auto error = writeEvidence(frameOptions, "replica-first", replicaFirst); error.has_value())
        return error;

    const std::size_t total = static_cast<std::size_t>(options.width) * static_cast<std::size_t>(options.height);
    const std::size_t apart = pixelsApart(authority.pixels, replica.pixels, 6);
    const std::size_t arrived = pixelsApart(replicaFirst, replica.pixels, 6);
    const replication::Status joined = joinNet->status();

    hostNet->shutdown();
    joinNet->shutdown();
    closeSession(authority, *device);
    closeSession(replica, *device);

    // Half a percent of the frame: a crate is ten times that at this size.
    if (apart * 200 > total) {
        const std::array<I18nArg, 2> args{I18nArg{"apart", static_cast<core::i64>(apart)},
                                          I18nArg{"total", static_cast<core::i64>(total)}};
        return core::makeError(LUAUG_TR("engine.replicagate.err.diverged"), args);
    }
    // The vacuous pass: a world that never arrived. Five percent of the frame
    // has to have changed since the replica's first, empty, frame.
    if (arrived * 20 < total || joined.serverTick == 0)
        return core::makeError(LUAUG_TR("engine.replicagate.err.vacuous"));

    const std::array<I18nArg, 2> okArgs{I18nArg{"apart", static_cast<core::i64>(apart)},
                                        I18nArg{"tick", static_cast<core::i64>(joined.serverTick)}};
    core::log(LogLevel::Info, LUAUG_TR("engine.replicagate.info.ok"), okArgs);
    return std::nullopt;
#else
    (void)options;
    return core::makeError(LUAUG_TR("engine.cli.err.no_replication"));
#endif
}

} // namespace luaug::app
