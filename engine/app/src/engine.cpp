#include "luaug/app/engine.h"

#include <lua.h>

// `std::sort` for the frame-time median, and `std::ptrdiff_t` beside it. Both
// were reached transitively for a milestone: this file compiles on Windows and
// in the Tier-2 container without either include, and fails on the CI runner's
// libstdc++, which is a different version with a different transitive graph.
// A header a translation unit uses is a header it includes.
#include "luaug/app/backends.h"
#include "luaug/app/debug_overlay.h"
#include "luaug/app/dev_control.h"
#include "luaug/app/frame_scheduler.h"
#include "luaug/app/inspector.h"
#include "luaug/app/reload.h"
#include "luaug/app/screenshot.h"
#include "luaug/app/soak.h"
#include "luaug/app/streaming_host.h"
#include "luaug/app/ui_text.h"
#include "luaug/app/world_host.h"
#include "luaug/asset/content.h"
#include "luaug/core/build_info.h"
#include "luaug/core/json_writer.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/event.h"
#include "luaug/platform/platform.h"
#include "luaug/platform/window.h"
#include "luaug/render/debug_draw.h"
#include "luaug/render/debug_renderer.h"
#include "luaug/render/mesh_loader.h"
#include "luaug/render/render_world.h"
#include "luaug/render/renderer.h"
#include "luaug/render/shader_library.h"
#include "luaug/render/ui_renderer.h"
#include "luaug/rhi/device.h"
#include "luaug/ui/ui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#if LUAUG_RHI_CAPTURE
#include "luaug/rhi/capture.h"
#endif

namespace luaug::app {
namespace {

using core::f32;
using core::f64;
using core::I18nArg;
using core::LogLevel;

constexpr f64 kNanosPerSecond = 1'000'000'000.0;

// The format the headless target is created with, named once so the pipeline
// and the texture cannot disagree.
constexpr rhi::TextureFormat kOffscreenFormat = rhi::TextureFormat::Rgba8Unorm;

// The physics backend's wireframe, forwarded into the engine's debug draw
// (roadmap M5, "Jolt debug-draw bridge").
//
// A sink rather than a returned buffer, and the seam is the reason: the backend
// walks shapes it already holds, and copying that into a vector so this could
// walk it again would double the cost of a view whose whole job is to be cheap
// enough to leave on.
class PhysicsWireframe final : public physics::IDebugDrawSink
{
public:
    explicit PhysicsWireframe(render::DebugDraw& draw) noexcept : m_draw(draw) {}

    void line(core::DVec3 from, core::DVec3 to, core::u32 color) override
    {
        // Rebased the same way every other debug line is: `DebugDraw` holds
        // camera-relative f32, and a world-space line drawn through a
        // camera-relative view-projection is displaced by the camera's distance
        // from the origin -- which is D011, found by looking at a screenshot.
        m_draw.line(core::toVec3(from), core::toVec3(to),
                    render::DebugColor::fromLinear(static_cast<f32>((color >> 16) & 0xff) / 255.0f,
                                                   static_cast<f32>((color >> 8) & 0xff) / 255.0f,
                                                   static_cast<f32>(color & 0xff) / 255.0f));
    }

private:
    render::DebugDraw& m_draw;
};

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

// What the engine draws for the world itself: one wire box per part that is
// under `Workspace`, from the extracted snapshot rather than from the ECS
// (ADR 0027). The real renderer is M4; until then this is how 500 scripted
// instances are seen at all, and it is what `examples/01-instances` is
// visualized with.
// The UI's draw list into the renderer's vertices, plus one run per contiguous
// span sharing a clip rectangle.
//
// A copy per frame over a few hundred quads, and it is what keeps the layering
// honest: `render` is L4 and `ui` is L5, so the renderer cannot see a
// `ui::DrawQuad` and does not need to (architecture.md §2 rule 3).
//
// Six vertices a quad rather than four and an index buffer. The geometry is the
// smallest thing in the frame; an index buffer would save a third of its
// bandwidth and cost a second upload.
void buildUiGeometry(const ui::DrawList& list, core::Vec2 viewport, std::vector<render::UiVertex>& vertices,
                     std::vector<render::UiScissorRun>& runs, std::span<const rhi::TextureHandle> textures)
{
    vertices.clear();
    runs.clear();

    const auto toByte = [](f32 value) {
        const f32 clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<core::u8>(clamped * 255.0f + 0.5f);
    };

    // A run breaks on EITHER a clip change or a texture change: a draw carries
    // one scissor and one texture, so either is a new draw. The draw list is
    // already ordered by the tree, so runs stay contiguous rather than becoming
    // buckets.
    core::u32 currentScissor = 0xffffffffu;
    core::u32 currentTexture = 0xffffffffu;
    for (const ui::DrawQuad& quad : list.quads) {
        if (quad.scissor != currentScissor || quad.texture != currentTexture) {
            currentScissor = quad.scissor;
            currentTexture = quad.texture;
            const core::Rect& clip = list.scissors[currentScissor];
            // Clamped to the target, because a scissor outside it is a
            // validation error on every backend rather than an empty draw.
            const f32 left = std::fmax(0.0f, clip.min.x);
            const f32 top = std::fmax(0.0f, clip.min.y);
            const f32 right = std::fmin(viewport.x, clip.max.x);
            const f32 bottom = std::fmin(viewport.y, clip.max.y);
            runs.push_back(render::UiScissorRun{
                .scissor = {static_cast<core::i32>(left), static_cast<core::i32>(top),
                            static_cast<core::i32>(std::fmax(0.0f, right - left)),
                            static_cast<core::i32>(std::fmax(0.0f, bottom - top))},
                .firstVertex = static_cast<core::u32>(vertices.size()),
                .vertexCount = 0,
                // Index zero is "no texture" and resolves to the renderer's own
                // white pixel, so an out-of-range index degrades to an untinted
                // quad rather than to an unbound read.
                .texture = currentTexture < textures.size() ? textures[currentTexture] : rhi::TextureHandle{},
            });
        }

        // The quad's own frame, so the fragment stage can measure a corner
        // without knowing where on screen the quad is (D030).
        const f32 halfX = (quad.max.x - quad.min.x) * 0.5f;
        const f32 halfY = (quad.max.y - quad.min.y) * 0.5f;

        const auto corner = [&](f32 x, f32 y, f32 u, f32 v) {
            return render::UiVertex{x,
                                    y,
                                    toByte(quad.color.r),
                                    toByte(quad.color.g),
                                    toByte(quad.color.b),
                                    toByte(quad.alpha),
                                    x - (quad.min.x + halfX),
                                    y - (quad.min.y + halfY),
                                    halfX,
                                    halfY,
                                    quad.cornerRadius,
                                    u,
                                    v};
        };

        const render::UiVertex a = corner(quad.min.x, quad.min.y, quad.uvMin.x, quad.uvMin.y);
        const render::UiVertex b = corner(quad.max.x, quad.min.y, quad.uvMax.x, quad.uvMin.y);
        const render::UiVertex c = corner(quad.max.x, quad.max.y, quad.uvMax.x, quad.uvMax.y);
        const render::UiVertex d = corner(quad.min.x, quad.max.y, quad.uvMin.x, quad.uvMax.y);
        vertices.insert(vertices.end(), {a, b, c, a, c, d});
        runs.back().vertexCount += 6;
    }
}

void submitWorld(const render::RenderWorld& snapshot, render::DebugDraw& draw)
{
    for (const render::RenderPart& part : snapshot.parts) {
        // Fully transparent is not drawn. A debug wireframe has no blending, so
        // the alternative is a box that a script asked to be invisible and that
        // is nonetheless the most visible thing on screen.
        if (part.transparency >= 1.0f)
            continue;

        draw.wireBox(core::toRenderMatrix(part.cframe, {}),
                     core::Vec3{part.size.x * 0.5f, part.size.y * 0.5f, part.size.z * 0.5f},
                     render::DebugColor::fromLinear(part.color.r, part.color.g, part.color.b));
    }
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
[[nodiscard]] std::optional<core::EngineError> writeCapture(const std::filesystem::path& path,
                                                            const rhi::IDevice& device)
{
#if LUAUG_RHI_CAPTURE
    const std::string& stream = rhi::captureStream(device);
    if (stream.empty()) {
        // An empty golden would match forever. Better to fail here than to
        // check in a file that can never catch anything.
        return core::makeError(LUAUG_TR("engine.capture.err.empty"));
    }

    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        const std::array<I18nArg, 1> args{I18nArg{"path", path.string()}};
        return core::makeError(LUAUG_TR("engine.capture.err.open_failed"), args);
    }

    // Binary mode on purpose: the stream is newline-terminated JSON lines, and
    // letting Windows translate them would make a golden recorded on one
    // platform differ from the same frame recorded on another.
    file.write(stream.data(), static_cast<std::streamsize>(stream.size()));
    file.close();

    if (!file) {
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

    if (!options.headless) {
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
    if (options.headless) {
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

    // Dev builds only, windowed only, and after the claim: the overlay's
    // pipeline is built against the swapchain's colour format, which does not
    // exist until the device owns the window. Compiled out entirely in shipping
    // (ADR 0011), where the constructor is a no-op and active() is false.
    std::optional<DebugOverlay> overlay;
    if (window != nullptr)
        overlay.emplace(*window, *device);

    // The explorer's selection and the queue its edits wait in. Held by the
    // frame loop rather than by the overlay because it outlives a hot reload
    // and the world does not, and because the drain below has to happen in
    // every profile -- including the shipping one, where nothing ever fills it
    // and the call costs an empty loop (ADR 0011, and why this line carries no
    // #ifdef).
    Inspector inspector;

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
    // Empty until something loads a mesh into it. `extract` skips a `MeshPart`
    // whose content is not here, so an unpopulated library renders the debug
    // path and nothing else -- which is exactly the state a world with no
    // MeshParts is in.
    // What the last frame actually submitted. Reported beside frame time because
    // the roadmap asks for the *why* next to the *what*: a frame that got slower
    // with the same draw count is a different problem from one that got slower
    // because it drew more.
    core::u32 frameDrawCalls = 0;
    core::u64 frameTriangles = 0;
    core::u32 frameLodDraws = 0;
    std::vector<f64> frameTimesMs;
    // Sixty warm-up frames rather than `--frame-stats`'s ten. A soak is minutes
    // long, so a second of startup costs it nothing -- and the streamed world
    // has not finished its first ring of chunks inside ten frames, which would
    // put the whole materialisation burst in the measured window.
    SoakRecorder soak(60);
    core::u64 lastFrameNs = 0;

    render::MeshLibrary meshLibrary;
    render::MeshCache meshCache;
    render::MeshLoader meshLoader;

    // Where `asset://` resolves from. Two mounts and the order is the rule:
    // the project's content DIRECTORY first, then its pack if one has been
    // built, so a compiled asset wins over the source it was compiled from and
    // a developer can still drop a loose file in to try something.
    //
    // A project with no pack behaves exactly as it did before M7 -- the loose
    // path is not a fallback here, it is the dev-mode path ADR 0010 keeps
    // forever.
    asset::ContentMounts contentMounts;

    // The streamed world, if the project has one. Inactive and free for every
    // project that does not, which is every example before this milestone.
    StreamingHost streaming;
    std::unique_ptr<render::IRenderer> renderer;
    render::ShaderLibrary shaders;
    render::DebugRenderer debugRenderer;
    render::UiRenderer uiRenderer;
    UiText uiText;
    // The size the UI was last laid out against, so a target that changes size
    // dirties every tree. Held here rather than derived, because "did this
    // change" is a question about the previous frame.
    core::Vec2 lastUiViewport;
    // The pointer and keyboard facts the UI needs, gathered from this frame's
    // events. Edges rather than states: a press is a frame on which the button
    // went down, and the UI needs the edge to tell a click from a hold.
    bool uiPointerDown = false;
    bool lastUiPointerDown = false;
    std::string uiTypedText;
    bool uiBackspace = false;
    bool uiSubmit = false;
    render::DebugDraw debugDraw;
    ui::DrawList uiDrawList;
    std::vector<render::UiVertex> uiVertices;
    std::vector<render::UiScissorRun> uiRuns;
    std::vector<rhi::TextureHandle> uiTextures;
    bool debugPassAttempted = false;

    // Mounts and the streamed world are set up HERE, outside `ensureDebugPass`,
    // and that placement is a defect this milestone had to find twice. They
    // lived inside it, so a device that could not take shaders -- `--rhi=null`,
    // or any machine whose shader load failed -- booted with NO content mounted
    // and NO chunk index, silently. The soak gate ran in 0.17 s over a world of
    // eleven instances and reported a pass.
    //
    // Nothing below reads a shader, a format or a device. Content addressing and
    // streaming residency are decisions about files and distances; the renderer
    // is downstream of them, and coupling the two made a gate vacuous rather
    // than red -- the worse of the two failures.
    // The PROJECT's content directory, not the engine's. `asset://` is a
    // URN in the game's namespace -- `asset://models/tree.glb` means the
    // file the developer put in their own `content/models/`, and resolving
    // it beside `luaug-host` would mean every project shipped its meshes
    // into the engine's install. The engine's own content directory is for
    // shaders and the message catalog, which are the engine's.
    //
    // `scriptPath` is a directory when it is a project root and a file when
    // it is a bare script (engine.h), so a lone script gets the engine's
    // content directory -- which is right: it has no project to have one.
    std::error_code pathError;
    const bool isProject = !options.scriptPath.empty() && std::filesystem::is_directory(options.scriptPath, pathError);
    const std::filesystem::path contentRoot = isProject ? options.scriptPath / "content" : platform::paths().contentDir;
    meshLoader.setContentRoot(contentRoot);

    contentMounts.clear();
    contentMounts.mountDirectory(contentRoot);
    if (isProject) {
        const std::filesystem::path pack = options.scriptPath / ".luaug" / "content.lpack";
        if (std::filesystem::exists(pack, pathError)) {
            if (auto mountError = contentMounts.mountPack(pack); mountError.has_value()) {
                // Named and survivable: a pack that will not open leaves the
                // loose mount standing, so a broken build is a message and a
                // slower load rather than a world with no meshes in it.
                core::logText(LogLevel::Warn, mountError->message);
            }
            else {
                const std::array<core::I18nArg, 1> mountArgs{core::I18nArg{"path", pack.string()}};
                core::log(LogLevel::Info, LUAUG_TR("app.info.pack_mounted"), mountArgs);
            }
        }
    }
    meshLoader.setContentMounts(&contentMounts);
    // The same mounts the meshes come from, so `TextLabel.Font` can name a face
    // out of the project the same way `MeshPart.MeshContent` names a model.
    uiText.setMounts(&contentMounts);

    if (isProject) {
        // Mounted after the pack so a loose chunk overrides a built one,
        // which is the same dev-mode override rule the content directory
        // gets, and the index sits beside them both.
        const std::filesystem::path built = options.scriptPath / ".luaug" / "content";
        if (std::filesystem::is_directory(built, pathError)) {
            contentMounts.mountDirectory(built);
        }
        if (!platform::initIo()) {
            core::log(LogLevel::Warn, LUAUG_TR("app.warn.io_unavailable"), {});
        }
        (void)streaming.load(contentMounts, options.scriptPath / ".luaug" / "content.chunks.json");
    }

    const auto ensureDebugPass = [&](rhi::TextureFormat colorFormat) {
        // Gated on "can this device take shaders", not on "will pixels come
        // out". They are different questions, and conflating them made the
        // capture backend -- the blocking render gate -- record a frame with no
        // draws in it at all, which is precisely the thing it exists to notice.
        if (debugPassAttempted || device->caps().shaderFormat == rhi::ShaderFormat::Unknown)
            return;
        debugPassAttempted = true;

        if (auto error = shaders.load(platform::paths().contentDir, device->caps().shaderFormat); error.has_value()) {
            core::logText(LogLevel::Warn, error->message);
            return;
        }
        if (auto error = debugRenderer.create(*device, shaders, colorFormat); error.has_value())
            core::logText(LogLevel::Warn, error->message);

        // Built beside the debug one and, like it, not required: a machine
        // whose content directory is missing the 2D shader boots and draws the
        // world with no UI over it, which is a better failure than refusing to
        // start.
        if (auto error = uiRenderer.create(*device, shaders, colorFormat); error.has_value())
            core::logText(LogLevel::Warn, error->message);

        // The real renderer is built beside the debug one and neither is
        // required. A machine whose content directory has the debug shader and
        // not the PBR set boots and draws wire boxes, which is a far better
        // failure than refusing to start.
        if (auto error = meshCache.create(*device); error.has_value()) {
            core::logText(LogLevel::Warn, error->message);
            return;
        }
        renderer = render::createDefaultRenderer();
        if (auto error = renderer->create(*device, shaders, colorFormat); error.has_value()) {
            core::logText(LogLevel::Warn, error->message);
            renderer.reset();
        }
    };

    FrameScheduler scheduler;

    // The world and the VM. Booted before the loop because every entry script's
    // first resumption is a deferred callback, and the first drain is inside the
    // first tick -- so a script that fails to compile says so here rather than
    // one frame later.
    //
    // Held by pointer rather than by value because a hot reload replaces it
    // wholesale (ADR 0024, `reload.h`): everything above this line -- the
    // window, the device, the renderer, the shader cache -- outlives the swap,
    // and that is what "engine-side content survives" means in C++.
    //
    // The bag outlives the host by construction: it is what a reload carries
    // across, and the host is what a reload destroys (ADR 0024).
    script::ReloadState reloadState;

    WorldHostOptions worldOptions{
        .projectPath = options.scriptPath,
        .seed = options.worldSeed,
        .fixedTimestep = scheduler.timing().fixedDt,
        .reloadState = &reloadState,
        .isReload = false,
        .headless = options.headless,
        .preserved = nullptr,
        .conformanceRoot = options.conformanceRoot,
    };

    auto host = std::make_unique<WorldHost>();
    if (std::optional<core::EngineError> bootError = host->boot(worldOptions); bootError.has_value())
        return bootError;

    // `game`, which is where the tree the explorer walks starts. Re-pointed
    // after every reload, because a reload destroys this world and builds
    // another (ADR 0024).
    if (overlay.has_value()) {
        overlay->setInspectionTarget(&host->world(), host->runtime().dataModel(), &inspector);
        overlay->setScriptTarget(&host->runtime());
        // After the console sink is installed, so the shell chains to it rather
        // than replacing it (D017).
        overlay->captureLog();
    }

    // The dev-server connection, if `luaug dev` started this process. Nothing
    // listens here: the engine dials out (ADR 0035), and the whole path is
    // absent when the flag is.
    DevControl control;
    std::vector<DevCommand> commands;
    // `sample` answers once the world has advanced, so the request outlives the
    // frame that received it.
    struct PendingSample
    {
        u64 id = 0;
        u64 tick = 0;
    };
    std::vector<PendingSample> pendingSamples;

    if (!options.devControlUrl.empty()) {
        if (std::optional<core::EngineError> attachError = control.start({
                .url = options.devControlUrl,
                .token = options.devControlToken,
            });
            attachError.has_value())
            return attachError;

        const std::array<I18nArg, 1> attached{I18nArg{"url", options.devControlUrl}};
        core::log(LogLevel::Info, LUAUG_TR("engine.dev.info.attached"), attached);
    }

    const auto replyOk = [&control](std::string_view type, u64 id, const auto& fill) {
        core::JsonWriter writer;
        writer.beginObject();
        writer.field("type", type);
        writer.field("id", id);
        fill(writer);
        writer.endObject();
        control.post(writer.text());
    };

    render::RenderWorld snapshot;

    auto headlessStepNs = static_cast<u64>(std::ceil(scheduler.timing().fixedDt * kNanosPerSecond));
    bool quit = false;

    while (!quit) {
        if (options.frames != 0 && scheduler.totalFrames() >= options.frames)
            break;

        // The FrameStart safe point for `PhysicsService.FixedTimestep`
        // (api-design.md §2.1). Here and nowhere else: the accumulator below,
        // the `task` timer wheel and the solver all read the tick, and a value
        // that changed between two of those reads inside one frame is a class of
        // bug worth designing out rather than debugging. A script's write lands
        // in `requestedFixedTimestep` and takes effect on the frame after it.
        if (const f64 requested = host->world().engineState().requestedFixedTimestep;
            requested != scheduler.timing().fixedDt) {
            scheduler.setFixedDt(requested);
            host->world().engineState().fixedTimestep = requested;
            headlessStepNs = static_cast<u64>(std::ceil(requested * kNanosPerSecond));
        }

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
        // A headless run drives the synthetic clock; a headless DEV SESSION does
        // not. The synthetic clock exists so a golden capture does not depend on
        // how busy the runner was, and a dev session has no golden -- what it
        // has is a developer, or a test, watching a world advance. Left
        // synthetic it runs tens of thousands of ticks per second, so "the hash
        // at tick 40" is a tick the world blew past before the request for it
        // finished crossing the socket.
        const bool syntheticClock = options.headless && options.devControlUrl.empty();
        const u64 nowNs = syntheticClock ? scheduler.totalFrames() * headlessStepNs : platform::nowNs();

        const Frame frame = scheduler.beginFrame(nowNs);

        // The gizmo target is armed BEFORE the ticks, not with the rest of the
        // rendering. `DebugService:DrawLine` is documented as drawing "for one
        // frame", and the handler that calls it runs inside a tick -- so a
        // target armed after the ticks would collect nothing, which is exactly
        // what happened the first time this was written the other way round.
        debugDraw.clear();
        host->setGizmoTarget(&debugDraw);

        // Published between frames, before anything this frame can read one.
        // Derived from the wall clock and therefore never legal in simulation
        // code (R10) -- they exist for a human looking at an overlay.
        host->publishStats({
            .fps = frame.renderDt > 0.0 ? 1.0 / frame.renderDt : 0.0,
            .frameTimeMs = frame.renderDt * 1000.0,
            .drawCalls = static_cast<f64>(frameDrawCalls),
            // Real from M5. It read zero for four milestones because there
            // were no bodies to count; a stat that says zero when it means
            // "not implemented" is the shape of every unbacked property this
            // repository has had to find later.
            .physicsBodies = host->physics() != nullptr ? static_cast<f64>(host->physics()->bodyCount()) : 0.0,
            .luaMemoryKb = static_cast<f64>(lua_totalbytes(host->runtime().state(), 0)) / 1024.0,
            .audioUnderruns = static_cast<f64>(host->audio().stats().underruns),
            .audioVoices = static_cast<f64>(host->audio().stats().activeVoices),
            .meshLodDraws = static_cast<f64>(frameLodDraws),
        });

        if (options.frameStats || !options.soakReportPath.empty()) {
            // The WALL clock, not `frame.renderDt`. Headless drives the frame
            // loop from a synthetic 1/60 s step so a golden capture cannot
            // depend on how busy the machine was (M1 Finding 8) -- which makes
            // `renderDt` exactly 16.666667 ms every frame and a perf baseline
            // built on it a measurement of the constant.
            //
            // R10 is not in the way: it forbids SIMULATION reading a wall clock.
            // A profiler is the one thing that has to.
            const core::u64 sampleNs = platform::nowNs();
            if (lastFrameNs != 0) {
                const f64 frameMs = static_cast<f64>(sampleNs - lastFrameNs) / 1'000'000.0;
                frameTimesMs.push_back(frameMs);
                // Resident size is read per frame rather than sampled, because
                // the number the gate wants is a PEAK and a peak between two
                // samples is a peak nobody saw.
                soak.sample({.frameMs = frameMs,
                             // The PREVIOUS frame's pump, because this sample is
                             // taken before this frame's. Off by one frame and
                             // deliberately so: every pump is counted exactly
                             // once, which is the property a histogram needs.
                             .streamingMs = streaming.lastPumpMilliseconds(),
                             .residentBytes = platform::residentBytes(),
                             .instanceCount = static_cast<core::u64>(host->world().instanceCount())});
            }
            lastFrameNs = sampleNs;
        }

        // The FrameStart safe point. Overlay edits are applied HERE and not
        // where they were typed (M4 brief, Decision 15): the panel draws at the
        // end of the frame, after the sim has ticked and after `extract`, so a
        // write applied there would land after the tick the drawn frame came
        // from -- the mid-frame mutation the reload below is forbidden for the
        // same reason. One frame of latency on a typed value; a replay that is
        // still a replay.
        //
        // Before the reload, deliberately. A write queued against the outgoing
        // world is dropped by `onWorldChanged` rather than replayed against a
        // world that never issued the ids it names.
        inspector.applyPending(host->world());

        // The streamed world advances at the SAME safe point, and for the same
        // reason: materialising instances mid-tick is the mutation the reload
        // below is forbidden for. Two milliseconds is architecture.md §10's
        // budget, and it is denominated in time rather than in chunks because
        // a chunk's cost varies with what is in it.
        if (streaming.active()) {
            streaming.setWorld(&host->world(), host->workspace());
            streaming.setPhysics(host->physics());
            streaming.pump(2.0);
        }

        // `@std/net` completions land here, at the same safe point, and for the
        // same reason streaming advances here: a response arrives on a worker
        // thread at a wall-clock moment, and resuming the coroutine there would
        // put game code into the frame wherever the socket happened to land it.
        host->publishNetworkResults();

        // Published UNCONDITIONALLY, and the conformance suite is what settled
        // it: a `LoadAreaAsync` in a project with no streamed world parked a
        // coroutine nothing would ever resume. With no chunks there is nothing
        // to wait for, so the honest answer is "loaded" on the next pump -- and
        // it has to be given, because a call that hangs forever is worse than
        // one that refuses.
        host->publishStreamingResults(streaming.drainStreamedOut(), [&streaming](core::DVec3 position, f64 radius) {
            return streaming.areaResident(position, radius);
        });

        // The only place a reload happens, and for the same reason: a world
        // swapped mid-tick would break within-run determinism, which is the
        // rule architecture.md §4 states (and the reason the connection hands
        // its messages over here rather than acting on them itself).
        if (!options.devControlUrl.empty()) {
            control.takeCommands(commands);
            for (const DevCommand& command : commands) {
                switch (command.kind) {
                case DevCommand::Kind::Reload: {
                    const ReloadReport reloaded = reloadWorld(host, worldOptions);
                    host->setGizmoTarget(&debugDraw);

                    // Both halves matter. The selection and anything still
                    // queued name ids the outgoing world minted, and slot
                    // indices restart from zero -- so replaying them would
                    // write to whatever moved into the same slot rather than
                    // to nothing. The overlay's pointer is re-aimed for the
                    // blunter reason: the world it held has been destroyed.
                    inspector.onWorldChanged();
                    if (overlay.has_value()) {
                        overlay->setInspectionTarget(&host->world(), host->runtime().dataModel(), &inspector);
                        // And the VM, for the same blunt reason: the runtime the
                        // console evaluated in has been destroyed with the world.
                        overlay->setScriptTarget(&host->runtime());
                    }
                    replyOk("reloaded", command.id, [&reloaded, &host](core::JsonWriter& writer) {
                        writer.field("ok", reloaded.ok);
                        writer.field("ms", reloaded.spanMs);
                        writer.field("scripts", reloaded.mountedScripts);
                        writer.field("preserved", reloaded.preserve.restored);
                        writer.field("tick", host->world().engineState().tick);
                        writer.field("hash", host->world().worldHash());
                        if (!reloaded.ok && reloaded.error.has_value())
                            writer.field("detail", reloaded.error->message);
                    });
                    break;
                }
                case DevCommand::Kind::Sample:
                    // A tick already past is answered at once rather than never:
                    // the reply carries the tick it was actually taken at, so a
                    // caller can tell the difference.
                    pendingSamples.push_back(PendingSample{command.id, command.atTick});
                    break;
                case DevCommand::Kind::Ping:
                    replyOk("pong", command.id, [](core::JsonWriter&) {});
                    break;
                case DevCommand::Kind::Shutdown:
                    quit = true;
                    break;
                case DevCommand::Kind::Unsupported:
                    // Answered rather than ignored: `asset-changed` and `eval`
                    // are reserved by the protocol and a caller that gets
                    // silence cannot tell "not yet" from "lost".
                    replyOk("error", command.id, [&command](core::JsonWriter& writer) {
                        writer.field("key", "dev.err.not_implemented");
                        writer.field("of", command.type);
                    });
                    break;
                }
            }
        }

        // The simulation, before anything is drawn: rendering shows the state a
        // tick settled on, never one being written.
        for (u32 step = 0; step < frame.simTicks; ++step)
            host->tick();

        // What the speakers do is a consequence of the simulation and never an
        // input to it (M6 brief, Decision 9), which is why this is after the
        // ticks rather than inside one. The listener is
        // `Workspace.CurrentCamera` -- a game with two ideas about where the
        // player is hearing from is a game with a bug.
        host->audio().update(host->world(), host->currentCamera());

        // The physics wireframe (roadmap M5, "Jolt debug-draw bridge"): what the
        // SOLVER thinks the world looks like, which is the only picture that can
        // disagree with the rendered one and therefore the only one worth
        // having. Drawn after the ticks, because it describes the state the
        // frame is about to show.
        //
        // Behind `DebugService:ShowPanel("Physics")` rather than always on: it
        // is a line per shape edge for every body in the world, which is a frame
        // cost nobody should pay without asking.
        if (scene::PhysicsSync* physics = host->physics();
            physics != nullptr && script::panelOpen(host->runtime().state(), "Physics")) {
            PhysicsWireframe sink(debugDraw);
            physics->backend().debugDraw(physics->worldHandle(), sink);
        }

        if (!pendingSamples.empty()) {
            const u64 tick = host->world().engineState().tick;
            const u64 hash = host->world().worldHash();
            std::erase_if(pendingSamples, [&](const PendingSample& sample) {
                if (sample.tick > tick)
                    return false;
                replyOk("sample", sample.id, [tick, hash](core::JsonWriter& writer) {
                    writer.field("tick", tick);
                    writer.field("hash", hash);
                });
                return true;
            });
        }

        if (host->shutdownRequested())
            quit = true;

        // A dev session that has lost its dev server has nobody left to tell it
        // to stop -- and headless it has no window to close either, so it would
        // run until something else killed it. That is the orphaned-process
        // failure the M3 brief lists as entering risk 4, and this is the whole
        // of the fix. A WINDOWED session keeps running on purpose: closing
        // `luaug dev` should not take the window with it.
        if (!options.devControlUrl.empty() && options.headless && !control.connected())
            quit = true;

        if (!options.headless) {
            const std::span<const platform::Event> events = platform::pumpEvents();
            for (const platform::Event& event : events) {
                if (event.type == platform::EventType::Quit || event.type == platform::EventType::WindowCloseRequested)
                    quit = true;
            }

            // The Input Action System folds this frame's events into the device
            // snapshot the ticks below read. Accumulated rather than replaced,
            // because a key stays down between the press and the release, and
            // handed over BEFORE the ticks so that every tick this frame sees
            // one snapshot.
            host->pumpInput(events);

            // The UI's own reading of the same events. Gathered here rather
            // than from the device snapshot because two of the three are
            // EVENTS with no resting state: a typed character and a Return do
            // not persist, and a snapshot cannot express them.
            uiTypedText.clear();
            uiBackspace = false;
            uiSubmit = false;
            for (const platform::Event& event : events) {
                switch (event.type) {
                case platform::EventType::MouseButtonDown:
                    if (event.button == platform::MouseButton::Left)
                        uiPointerDown = true;
                    break;
                case platform::EventType::MouseButtonUp:
                    if (event.button == platform::MouseButton::Left)
                        uiPointerDown = false;
                    break;
                case platform::EventType::TextInput:
                    uiTypedText.append(event.text);
                    break;
                case platform::EventType::KeyDown:
                    if (event.key == platform::Key::Backspace)
                        uiBackspace = true;
                    else if (event.key == platform::Key::Return)
                        uiSubmit = true;
                    break;
                default:
                    break;
                }
            }

            // After the pump and with the span it returned: the overlay reads
            // the untranslated stream behind these, which is only valid until
            // the next pump.
            if (overlay.has_value())
                overlay->handleEvents(events);
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

        if (!options.headless) {
            const rhi::Swapchain swapchain = device->acquireSwapchain(*window);
            target = swapchain.texture;
            targetFormat = swapchain.format;
            targetWidth = swapchain.width;
            targetHeight = swapchain.height;
        }

        if (target.valid()) {
            ensureDebugPass(targetFormat);

            if (!options.headless)
                host->preRender(frame.renderDt);

            // Loading comes BEFORE extraction, and the order is load-bearing:
            // `extract` reads the mesh library, so a MeshPart whose file has not
            // been read yet contributes nothing. Running the loader afterwards
            // made every newly created MeshPart invisible for exactly one frame
            // -- which a golden records faithfully and a person notices as a
            // flicker they cannot reproduce.
            meshCache.beginFrame(*device);
            if (renderer != nullptr && renderer->valid())
                meshLoader.syncPrimitives(*device, *cmd, host->world(), meshCache, meshLibrary);
            if (renderer != nullptr && renderer->valid())
                (void)meshLoader.sync(*device, *cmd, host->world(), host->workspace(), meshCache, meshLibrary);

            // Extraction happens once, at a known moment, from a world that is
            // between ticks (ADR 0027). Rendering never walks the ECS.
            // The aspect comes from the target rather than from the camera:
            // a `Camera` has no ViewportSize in this release (it needs a
            // Vector2, which the UI brings at M6), and the renderer is the one
            // that knows how many pixels it is filling.
            const f32 aspect =
                targetHeight == 0 ? 1.0f : static_cast<f32>(targetWidth) / static_cast<f32>(targetHeight);
            const f32 shadowRadius = renderer != nullptr && renderer->valid() ? renderer->shadowRadius() : 0.0f;
            render::extract(host->world(), host->workspace(), host->lighting(), meshLibrary, aspect, shadowRadius,
                            host->animation(), snapshot);
            // The UI is laid out against the TARGET's size rather than the
            // window's: an offscreen render at 640x360 has to produce the
            // layout that resolution would, which is the whole of what the
            // two-resolution goldens check.
            const core::Vec2 uiViewport{static_cast<f32>(targetWidth), static_cast<f32>(targetHeight)};
            if (uiViewport != lastUiViewport) {
                // A scale is a fraction of something that just changed, so
                // every tree is stale. Marked here rather than in the resize
                // handler because an offscreen target can change size with no
                // window event at all.
                host->world().screenGuis().forEach(
                    [](core::InstanceId, scene::ScreenGuiComponent& screen) { screen.layoutDirty = true; });
                lastUiViewport = uiViewport;
            }
            ui::layout(host->world(), host->uiService(), uiViewport);

            // Interaction reads the rectangles the layout just produced, and it
            // runs here rather than beside the event pump for that reason: a hit
            // test against last frame's rectangles is a click that lands where a
            // button used to be.
            const input::DeviceState& devices = host->input().snapshot();
            ui::InteractionInput interaction;
            interaction.pointer = devices.pointer;
            interaction.pressed = uiPointerDown && !lastUiPointerDown;
            interaction.released = !uiPointerDown && lastUiPointerDown;
            interaction.text = uiTypedText;
            interaction.backspace = uiBackspace;
            interaction.submit = uiSubmit;
            lastUiPointerDown = uiPointerDown;
            const ui::InteractionResult uiResult = ui::updateInteraction(host->world(), host->uiService(), interaction);
            host->input().setPointerCapturedByUi(uiResult.pointerOverUi);
            // The keyboard half of the same claim (ADR 0041): a focused
            // `TextInput` eats the keys, so typing into a chat box does not also
            // drive the character.
            host->input().setKeyboardCapturedByUi(uiResult.textInputFocused);

            ui::buildDrawList(host->world(), host->uiService(), uiDrawList);
            // Index 0 is "no texture" and every entry after it is a texture the
            // UI can name. The glyph atlas is index 1 when a face has been
            // rasterised; images follow it.
            // Index 0 is "no texture" and resolves to the renderer's white
            // pixel; index 1 is the glyph atlas and everything after it is an
            // image. The table is rebuilt each frame because a texture handle
            // is four bytes and a stale one is a picture from a world that has
            // been unloaded.
            uiTextures.clear();
            uiTextures.push_back(rhi::TextureHandle{});
            uiTextures.push_back(uiText.atlasTexture());
            buildUiGeometry(uiDrawList, uiViewport, uiVertices, uiRuns, uiTextures);

            frameDrawCalls = 0;
            frameTriangles = 0;
            frameLodDraws = 0;
            // The SAME level the renderer will choose, from the same function.
            // Counting level zero here while the backend drew level two would
            // be a triangle count that describes a frame nobody rendered, and a
            // stat that lies is worse than one that is missing.
            const f32 lodPixelsPerUnit = snapshot.camera.valid && uiViewport.y > 0.0f
                                             ? 0.5f * uiViewport.y * snapshot.camera.projection.m[1][1]
                                             : 0.0f;
            for (const render::DrawItem& draw : snapshot.draws) {
                // Counted from the snapshot rather than from the backend: it is
                // the same number, it costs nothing, and it is available on a
                // device that rasterizes nothing.
                if (!draw.inCameraFrustum)
                    continue;
                const render::MeshCache::Resolved* resolved = meshCache.resolve(draw.mesh);
                if (resolved == nullptr || resolved->lods.empty())
                    continue;
                const core::u32 level = render::selectMeshLod(*resolved, draw.transform, lodPixelsPerUnit);
                const render::MeshLodRange& range = resolved->lods[level];
                if (draw.section >= range.sectionCount ||
                    range.firstSection + draw.section >= resolved->sections.size())
                    continue;
                ++frameDrawCalls;
                if (level > 0)
                    ++frameLodDraws;
                frameTriangles += resolved->sections[range.firstSection + draw.section].indexCount / 3u;
            }

            submitWorld(snapshot, debugDraw);

            // Everything in the buffer is in world coordinates until here: the
            // wire boxes above, and whatever `DebugService` recorded during the
            // tick, which happened before extraction had decided where the
            // camera was. The overlay is drawn with the renderer's
            // camera-relative view-projection, so the buffer has to be moved
            // into that space -- and for the whole of M4 it was not, which put
            // every debug line the camera's own distance away from where it
            // belonged. `origin` is zero when no camera resolved, which is
            // exactly the M1 path this must not disturb.
            debugDraw.rebaseTo(snapshot.camera.origin);

            // Uploaded before the render pass opens, because a copy cannot run
            // inside one -- the seam says so and the backend enforces it. The
            // mesh loader is here for the same reason and one more: it is the
            // FrameStart safe point, so a file read cannot land mid-tick.
            if (debugRenderer.valid())
                debugRenderer.upload(*device, *cmd, debugDraw);
            if (uiRenderer.valid())
                // The atlas first: `buildUiGeometry` has already written UVs
                // into it, and uploading after the draw would show this frame's
                // new glyphs as last frame's pixels.
                uiText.sync(*device, *cmd);
            uiRenderer.upload(*device, *cmd, uiVertices, uiRuns);

            // The real renderer owns the target when there is a camera to look
            // through. Without one -- an empty project, a world booting, a
            // camera nobody assigned -- the M1 debug path still draws, which is
            // what keeps every earlier example and the capture golden working.
            const bool useRenderer = renderer != nullptr && renderer->valid() && snapshot.camera.valid;
            if (useRenderer) {
                renderer->render(
                    *device, *cmd,
                    {.color = target, .colorFormat = targetFormat, .width = targetWidth, .height = targetHeight},
                    snapshot, meshCache);

                // Debug geometry goes on top, in a pass that LOADS rather than
                // clears: it is an overlay on the rendered frame, not a
                // replacement for it.
                if (debugRenderer.valid() && !debugDraw.vertices().empty()) {
                    const std::array<rhi::ColorAttachment, 1> overlayColors{rhi::ColorAttachment{
                        .texture = target,
                        .loadOp = rhi::LoadOp::Load,
                        .storeOp = rhi::StoreOp::Store,
                    }};
                    cmd->pushDebugGroup("debug-draw");
                    cmd->beginRenderPass({.colorAttachments = overlayColors, .debugName = "debug-draw"});
                    cmd->setViewport({
                        .width = static_cast<f32>(targetWidth),
                        .height = static_cast<f32>(targetHeight),
                    });
                    debugRenderer.render(*cmd, snapshot.camera.viewProjection);
                    cmd->endRenderPass();
                    cmd->popDebugGroup();
                }
            }
            else {

                const std::array<rhi::ColorAttachment, 1> colors{rhi::ColorAttachment{
                    .texture = target,
                    .loadOp = rhi::LoadOp::Clear,
                    .storeOp = rhi::StoreOp::Store,
                    .clearColor = pulseColor(scheduler.totalTicks(), scheduler.timing().fixedDt),
                }};

                cmd->pushDebugGroup("frame");
                cmd->beginRenderPass({.colorAttachments = colors, .debugName = "clear"});

                if (debugRenderer.valid() && targetWidth > 0 && targetHeight > 0) {
                    cmd->setViewport({
                        .width = static_cast<f32>(targetWidth),
                        .height = static_cast<f32>(targetHeight),
                    });
                    debugRenderer.render(*cmd, orbitCamera(targetWidth, targetHeight));
                }

                cmd->endRenderPass();
                cmd->popDebugGroup();
            }

            // The UI, in its own pass that LOADS: it is drawn over the finished
            // frame whatever produced it, so a project with no camera still has
            // a menu. Before the debug overlay and after everything else, which
            // is the order api-design.md §2.2 implies -- game UI is part of the
            // game, and the ImGui overlay is on top of the game.
            if (uiRenderer.valid() && !uiVertices.empty()) {
                const std::array<rhi::ColorAttachment, 1> uiColors{rhi::ColorAttachment{
                    .texture = target,
                    .loadOp = rhi::LoadOp::Load,
                    .storeOp = rhi::StoreOp::Store,
                }};
                cmd->pushDebugGroup("ui");
                cmd->beginRenderPass({.colorAttachments = uiColors, .debugName = "ui"});
                cmd->setViewport({
                    .width = static_cast<f32>(targetWidth),
                    .height = static_cast<f32>(targetHeight),
                });
                uiRenderer.render(*cmd, uiViewport);
                cmd->endRenderPass();
                cmd->popDebugGroup();
            }

            // Its own pass, on top of the finished frame, after ours closed and
            // before submit -- the ordering the overlay's contract asks for.
            if (overlay.has_value())
                overlay->render(*cmd, target, frame);
        }

        // Cleared once the frame is over. A `DrawLine` from a task resumed
        // outside a frame has nowhere to go and is the silent no-op the headless
        // contract already describes.
        host->setGizmoTarget(nullptr);

        device->submitAndPresent();

        if (options.frames != 0 && options.exitAfterFrames && frame.index + 1 >= options.frames)
            quit = true;
    }

    control.stop();
    host->close();
    device->waitIdle();

    if (!options.screenshotPath.empty() && offscreen.valid()) {
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

    if (!options.capturePath.empty()) {
        if (auto captureError = writeCapture(options.capturePath, *device); captureError.has_value())
            return captureError;

        const std::array<I18nArg, 1> captureArgs{I18nArg{"path", options.capturePath.string()}};
        core::log(LogLevel::Info, LUAUG_TR("engine.capture.info.written"), captureArgs);
    }

    if (!options.conformanceRoot.empty()) {
        const ConformanceReport report = host->conformanceReport();
        if (!report.ran)
            return core::makeError(LUAUG_TR("engine.tests.err.never_ran"));

        const std::array<I18nArg, 3> args{I18nArg{"total", report.total}, I18nArg{"passed", report.passed},
                                          I18nArg{"failed", report.failed}};
        core::log(LogLevel::Info, LUAUG_TR("engine.tests.info.summary"), args);

        // Written before the failure check, because a run that failed is
        // exactly the one whose per-case detail somebody wants.
        if (!options.testReportPath.empty()) {
            std::error_code ec;
            if (options.testReportPath.has_parent_path())
                std::filesystem::create_directories(options.testReportPath.parent_path(), ec);

            std::ofstream file(options.testReportPath, std::ios::binary | std::ios::trunc);
            if (!file) {
                const std::array<I18nArg, 1> path{I18nArg{"path", options.testReportPath.string()}};
                return core::makeError(LUAUG_TR("engine.tests.err.report_failed"), path);
            }
            file << report.json;
        }

        if (report.failed != 0)
            return core::makeError(LUAUG_TR("engine.tests.err.failed"), args);

        // A spec that does not compile is not a spec that passed. Before this,
        // a syntax error in one file logged a line and the run reported "955
        // passed, 0 failed" over a suite that had silently lost seventeen cases
        // -- a gate that can pass while doing nothing, which is the twelfth
        // instance of that shape in six milestones and the one that had to be
        // found by noticing a number did not move.
        if (const core::u64 failures = host->scriptLoadFailures(); failures != 0) {
            const std::array<I18nArg, 1> loadArgs{I18nArg{"count", static_cast<core::i64>(failures)}};
            return core::makeError(LUAUG_TR("engine.tests.err.load_failed"), loadArgs);
        }
    }

    const std::array<I18nArg, 2> summary{I18nArg{"frames", static_cast<core::i64>(scheduler.totalFrames())},
                                         I18nArg{"ticks", static_cast<core::i64>(scheduler.totalTicks())}};
    core::log(LogLevel::Info, LUAUG_TR("engine.frame.info.summary"), summary);

    if (options.frameStats && !frameTimesMs.empty()) {
        // The first frames are warm-up -- shader creation, the first mesh load,
        // the swapchain settling -- and including them makes a median that
        // describes startup rather than the scene. Dropped rather than averaged
        // away, because averaging a spike in is exactly how a baseline stops
        // being comparable.
        constexpr core::usize kWarmupFrames = 10;
        std::vector<f64> measured = frameTimesMs;
        if (measured.size() > kWarmupFrames * 2)
            measured.erase(measured.begin(), measured.begin() + static_cast<std::ptrdiff_t>(kWarmupFrames));
        std::sort(measured.begin(), measured.end());

        const f64 median = measured[measured.size() / 2];
        const f64 worst = measured.back();
        const std::array<I18nArg, 5> stats{
            I18nArg{"frames", static_cast<core::i64>(measured.size())},
            I18nArg{"median", median},
            I18nArg{"worst", worst},
            I18nArg{"draws", static_cast<core::i64>(frameDrawCalls)},
            I18nArg{"triangles", static_cast<core::i64>(frameTriangles)},
        };
        core::log(LogLevel::Info, LUAUG_TR("engine.frame.info.stats"), stats);
    }

    // The soak verdict is computed BEFORE teardown, because teardown frees the
    // very memory the peak was measured against -- and after the frame loop,
    // because a gate that could stop a run early would report on a run that did
    // not happen.
    std::optional<core::EngineError> soakFailure;
    if (!options.soakReportPath.empty()) {
        const SoakThresholds thresholds{.memoryCeilingBytes = options.soakCeilingBytes,
                                        .minimumInstances = options.soakMinimumInstances};
        const SoakVerdict verdict = soak.evaluate(thresholds);

        std::ofstream report(options.soakReportPath, std::ios::binary);
        report << soak.report(thresholds);
        if (!report.good()) {
            const std::array<I18nArg, 1> writeArgs{I18nArg{"path", options.soakReportPath.string()}};
            soakFailure = core::makeError(LUAUG_TR("engine.soak.err.report_write_failed"), writeArgs);
        }
        report.close();

        const std::array<I18nArg, 8> reportArgs{
            I18nArg{"frames", static_cast<core::i64>(verdict.frames)},
            I18nArg{"median", verdict.medianMs},
            I18nArg{"p99", verdict.p99Ms},
            I18nArg{"worst", verdict.worstMs},
            I18nArg{"peak", static_cast<core::i64>(verdict.peakResidentBytes / (1024 * 1024))},
            I18nArg{"early", static_cast<core::i64>(verdict.earlyInstances)},
            I18nArg{"late", static_cast<core::i64>(verdict.lateInstances)},
            I18nArg{"path", options.soakReportPath.string()},
        };
        core::log(LogLevel::Info, LUAUG_TR("engine.soak.info.report"), reportArgs);

        // Every failure is logged and then ONE error is returned. A gate that
        // reports only its first complaint makes the second one cost another
        // five minutes.
        for (const core::EngineError& failure : verdict.failures) {
            core::logText(LogLevel::Error, failure.message);
        }
        if (!verdict.ok && !soakFailure.has_value()) {
            const std::array<I18nArg, 2> failArgs{I18nArg{"failures", static_cast<core::i64>(verdict.failures.size())},
                                                  I18nArg{"path", options.soakReportPath.string()}};
            soakFailure = core::makeError(LUAUG_TR("engine.soak.err.failed"), failArgs);
        }
    }

    uiText.destroy(*device);
    uiRenderer.destroy(*device);
    debugRenderer.destroy(*device);
    if (offscreen.valid())
        device->destroy(offscreen);
    if (window != nullptr)
        device->releaseWindow(*window);

    return soakFailure;
}

} // namespace luaug::app
