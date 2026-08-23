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
#include "luaug/app/editor.h"
#include "luaug/app/frame_scheduler.h"
#include "luaug/app/icons.h"
#include "luaug/app/inspector.h"
#include "luaug/app/reload.h"
#include "luaug/app/screenshot.h"
#include "luaug/app/soak.h"
#include "luaug/app/streaming_host.h"
#include "luaug/app/ui_text.h"
#include "luaug/app/world_host.h"
#include "luaug/asset/content.h"
#include "luaug/asset/image.h"
#include "luaug/core/build_info.h"
#include "luaug/core/json_writer.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/jobs/jobs.h"
#include "luaug/platform/event.h"
#include "luaug/platform/file.h"
#include "luaug/platform/platform.h"
#include "luaug/platform/window.h"
#include "luaug/render/debug_draw.h"
#include "luaug/render/debug_renderer.h"
#include "luaug/render/mesh_loader.h"
#include "luaug/render/render_world.h"
#include "luaug/render/renderer.h"
#include "luaug/render/shader_library.h"
#include "luaug/render/transform_history.h"
#include "luaug/render/ui_renderer.h"
#include "luaug/rhi/device.h"
#include "luaug/scene/scene_file.h"
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

// The selected instances, outlined in the viewport.
//
// A selection that exists only in a tree view is not a selection in a 3D
// editor: the whole reason to click a thing in the world is to see which thing
// you clicked. Drawn from the world rather than from the render snapshot
// because the snapshot is a filtered, culled, LOD-selected view of it -- a part
// behind the camera or past the far plane is still selected, and an outline
// that vanished when you turned around would be worse than none.
//
// Orange because nothing in a PBR scene is, and because it stays legible
// against both the lit and the shadowed halves of a frame. The primary is
// brighter than the rest, because a manipulator anchors to it and which one
// that is has to be visible.
//
// **Submitted CAMERA-RELATIVE, which is why it takes an origin and why it is
// called after `rebaseTo` rather than with the rest of the debug submissions.**
// `DebugDraw::rebaseTo` subtracts in f32 and its own header says so: a box
// recorded at an absolute world position is quantised to a float BEFORE the
// camera comes off it, which is about half a millimetre four kilometres out and
// worse beyond. `toRenderMatrix` does the subtraction in f64, and an outline
// somebody is trying to drag a handle on cannot afford the other one.
void submitSelection(const scene::World& world, std::span<const core::InstanceId> selection, core::DVec3 cameraOrigin,
                     render::DebugDraw& draw)
{
    for (usize index = 0; index < selection.size(); ++index) {
        const core::InstanceId id = selection[index];
        if (!id.valid() || !world.alive(id))
            continue;

        const scene::PartComponent* part = world.parts().find(id);
        if (part == nullptr)
            continue;

        const bool primary = index + 1 == selection.size();
        // A hair larger than the part, so the outline sits outside the surface
        // rather than fighting it for the same depth -- a box drawn exactly on
        // a face z-fights along every edge, which reads as a flicker rather
        // than as a selection.
        constexpr f32 kOutlineMargin = 1.01f;
        draw.wireBox(core::toRenderMatrix(part->cframe, cameraOrigin),
                     core::Vec3{part->size.x * 0.5f * kOutlineMargin, part->size.y * 0.5f * kOutlineMargin,
                                part->size.z * 0.5f * kOutlineMargin},
                     primary ? render::DebugColor::fromLinear(1.0f, 0.45f, 0.05f)
                             : render::DebugColor::fromLinear(0.75f, 0.30f, 0.03f));
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
    // **The job pool, started here, and it had no caller until M7.5.** M7 built
    // it -- work stealing, dependencies, `parallelFor`, `StableCommit` -- and
    // nothing in the engine ever called `init`, so every `parallelFor` in it had
    // been taking `jobs.h`'s documented serial path. That is a legitimate mode
    // and it is not the shipping one; the environment prefilter was the first
    // caller to notice, at two and a half milliseconds a frame.
    //
    // Zero means one worker per hardware thread less this one (jobs.h). Nothing
    // sim-visible runs on it, and the render-side work that does partitions by
    // DATA -- so the answer does not depend on how many workers this machine
    // has, which is what R10 asks of a pool at all.
    jobs::init();

    if (const auto error = platform::init({.headless = options.headless}); error.has_value())
        return error;

    // Declaration order below IS the shutdown order, reversed, and it is not
    // arbitrary: SDL_GPU requires a window to be released from its device
    // before the window is destroyed. Declaring the window first means the
    // device dies first -- releasing it -- on every path out of this function,
    // including the early returns.
    struct PlatformScope
    {
        ~PlatformScope()
        {
            platform::shutdown();
            jobs::shutdown();
        }
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
                .title = options.windowTitle,
                .width = options.width,
                .height = options.height,
            },
            &error);
        if (window == nullptr)
            return error;

        // The window wears whatever icon this executable carries in its own
        // resources (roadmap M8). Nothing is configured and nothing is
        // installed: a packaged game had that resource replaced by
        // `luaug build`, so the same three lines dress the engine's dev host in
        // the LuauG mark and a shipped game in its own.
        //
        // Every failure here is silent and survivable -- a platform with no
        // per-window icon, an icon that will not decode, a build with no
        // resource at all. A window with the default icon is a cosmetic loss;
        // refusing to open one is not.
        if (const std::vector<std::byte> iconBytes = platform::applicationIconBytes(); !iconBytes.empty()) {
            asset::Image icon;
            if (!asset::decodeImage(iconBytes, icon).has_value() && icon.valid()) {
                (void)platform::setWindowIcon(*window, icon.pixels, static_cast<i32>(icon.width),
                                              static_cast<i32>(icon.height));
            }
        }

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
    if (window != nullptr) {
        // The layout lives inside the project it belongs to, not beside the
        // executable: two projects open in turn should not fight over one
        // arrangement of panels.
        const std::filesystem::path layout =
            options.editor ? options.scriptPath / ".luaug" / "editor-layout.ini" : std::filesystem::path{};
        overlay.emplace(*window, *device, options.editor ? Shell::Editor : Shell::Overlay,
                        options.editor ? layout.string() : std::string{});
    }

    // The editor's model and the texture its viewport is drawn into. Both are
    // inert unless `--edit` asked for them, and the target is not created until
    // a panel has said how big it is.
    Editor editor;
    ViewportTarget viewportTarget;
    // The editor's icons, built once from `content/icons` on the first frame
    // that has a command list -- uploading a texture is one, so this cannot be
    // done before the loop.
    IconAtlas iconAtlas;
    // What was last written to `.luaug/editor.json`, so the write happens on a
    // change rather than every frame.
    std::string rememberedScene;
    // This frame's relative pointer motion, accumulated from the events.
    core::Vec2 editorLookDelta;
    // Where the pointer was when it was last free, so a hold can put it back.
    core::Vec2 editorPointerAnchor;
    // Whether the game was taking input last frame, so the release happens once
    // on the way out rather than every frame the editor is editing.
    bool gameHadInput = true;

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
    // How many objects the camera could see, which stopped being the same
    // number as `frameDrawCalls` at M7.5: a run of objects sharing a mesh and a
    // material is now one call. The roadmap's gate for the instanced path is
    // exactly these two side by side -- if they are equal, it did nothing.
    core::u32 frameVisibleObjects = 0;
    core::u32 frameInstancedDraws = 0;
    core::u64 frameTriangles = 0;
    core::u32 frameLodDraws = 0;
    std::vector<f64> frameTimesMs;
    // Sixty warm-up frames rather than `--frame-stats`'s ten. A soak is minutes
    // long, so a second of startup costs it nothing -- and the streamed world
    // has not finished its first ring of chunks inside ten frames, which would
    // put the whole materialisation burst in the measured window.
    SoakRecorder soak(60);
    core::u64 lastFrameNs = 0;

    // Where everything was one tick ago (D047). Owned by the frame loop rather
    // than by the world, because it is not world state: a reload replaces the
    // host and this simply starts again, which is right -- a reloaded world has
    // no previous frame to have come from.
    // What the window has been TOLD, so a change is applied once. The engine
    // state carries what the game wants; these carry what SDL was last asked
    // for, and the two are different questions.
    bool pointerLocked = false;
    bool pointerVisible = true;
    // And the same question for the overlay, which has a second author: F3.
    // `DebugService.OverlayVisible` and the panel's own state are synced in
    // both directions below, and this is which of the two moved (D055).
    bool overlayVisible = false;

    render::TransformHistory transformHistory;
    // What `transformHistory` was captured against. A restore, an undo, a scene
    // load or a reload all replace the world under it, and `world.h` says in so
    // many words what that costs: the history is "rebuilt from the tree rather
    // than restored ... safe order: restore, then rebuild". Nothing rebuilt it,
    // and a snapshot preserves generations precisely so an id means the same
    // thing afterwards -- which is what let a stale entry go on answering.
    //
    // The inspector's counter is the signal because it already IS one: every
    // path that replaces the world calls `onWorldChanged`, and inventing a
    // second notion of "the world is not the one it was" would be inventing
    // somewhere for the two to disagree.
    core::u64 transformHistoryWorld = 0;

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

    // **The content directory is the asset manager, and a scene is one of the
    // assets in it** (human decision, 2026-08-22). The browser shows the same
    // root the engine mounts -- the SOURCE tree, not the packed archive, which
    // is what a person authors and what `ContentMounts` already resolves over
    // the pack for exactly this reason.
    if (options.editor)
        editor.openContent(contentRoot);
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
        // Before `create`, because the shadow atlas is sized by the settings and
        // building it twice on the first frame would be a wasted allocation the
        // size of the whole map.
        renderer->setSettings(options.graphics);
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

    // **The scene, if the project has one** (ADR 0047), chosen here and applied
    // by `WorldHost::boot` BEFORE it starts the scripts -- which is the order
    // the ADR describes and `scene_file.h` states, and which D067 is the cost
    // of having had backwards.
    //
    // **Which scene opens, in order: what this person had open, what the
    // project declares, then nothing.** The first is per-person state in
    // `.luaug/`; the second is `[project] scene` in `luaug.toml`, which is the
    // decision a project makes about what a RUN of it starts with. Nothing is
    // an untitled world, which is what an editor opened on an empty project
    // should be.
    //
    // A project with no scene file is not an error and never logs one. Every
    // example before `06-scene` is exactly that.
    std::string sceneRelative;
    if (options.editor)
        sceneRelative = Editor::recallOpenScene(options.scriptPath / ".luaug");
    if (sceneRelative.empty() || !platform::fileExists(contentRoot / std::filesystem::path(sceneRelative)))
        sceneRelative = options.startupScene;

    std::filesystem::path bootScene;
    if (!options.scriptPath.empty() && !sceneRelative.empty()) {
        const std::filesystem::path candidate = contentRoot / std::filesystem::path(sceneRelative);
        if (platform::fileExists(candidate))
            bootScene = candidate;
        else
            sceneRelative.clear();
    }

    WorldHostOptions worldOptions{
        .projectPath = options.scriptPath,
        .seed = options.worldSeed,
        .fixedTimestep = scheduler.timing().fixedDt,
        .reloadState = &reloadState,
        .isReload = false,
        .headless = options.headless,
        .preserved = nullptr,
        .conformanceRoot = options.conformanceRoot,
        .bootScene = bootScene,
    };

    auto host = std::make_unique<WorldHost>();
    if (std::optional<core::EngineError> bootError = host->boot(worldOptions); bootError.has_value())
        return bootError;

    // The editor is told which scene the world holds, so its save writes back
    // to that one rather than refusing for want of an open scene.
    if (options.editor && host->bootSceneApplied())
        editor.adoptOpenScene(sceneRelative);

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
            .audioClipsLoaded = static_cast<f64>(host->audio().stats().clipsLoaded),
            .audioClipsMissing = static_cast<f64>(host->audio().stats().clipsMissing),
            .meshLodDraws = static_cast<f64>(frameLodDraws),
            .visibleObjects = static_cast<f64>(frameVisibleObjects),
            .instancedDraws = static_cast<f64>(frameInstancedDraws),
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
        // **Recorded before the write, because undo restores what was there.**
        // A whole frame's queued edits are one step: they were typed in one
        // frame and a person undoing thinks of them as one thing.
        //
        // The key says what counts as ONE edit, and `coalesceKeyFor` owns that
        // question -- it used to be four lines here, which is four lines no
        // test could reach. An open gesture is one drag however many writes it
        // made; without one the old rule still applies, so a caller that never
        // learned about gestures behaves exactly as it did.
        if (options.editor && inspector.pendingCount() > 0)
            editor.history().record(host->world(), "Edit", coalesceKeyFor(inspector.gesture(), inspector.pending()));

        inspector.applyPending(host->world());

        // A click resolves here too, and AFTER the drain rather than before:
        // whatever was typed into the old selection lands before the selection
        // becomes something else. Walking the world for a pick is only a read,
        // but doing it here rather than inside the UI callback that noticed the
        // click is what keeps what a click selects independent of where in the
        // panel tree it happened to be handled -- the same discipline the
        // writes above are under, for the same reason.
        //
        // The click was noticed at the END of the previous frame, so a
        // selection is one frame behind the mouse. That is the same frame of
        // latency a typed value already has and it is not felt at sixty hertz.
        if (options.editor) {
            // The shell's buttons, acted on HERE and not where they were
            // pressed: play snapshots the world, stop replaces it and save walks
            // all of it, and none of those may happen while a panel is drawing
            // from the same world.
            if (overlay.has_value()) {
                const EditorCommands editorCommands = overlay->takeCommands();
                if (editorCommands.play.has_value()) {
                    if (*editorCommands.play)
                        editor.play(host->world());
                    else
                        editor.stop(host->world(), inspector);
                }
                if (editorCommands.pause.has_value())
                    editor.setPaused(*editorCommands.pause);
                if (!editorCommands.openScene.empty()) {
                    // Out of play mode first. Loading a scene while playing
                    // would leave the snapshot describing a world that no longer
                    // exists, and stop would restore into it.
                    if (editor.inPlayMode())
                        editor.stop(host->world(), inspector);
                    (void)editor.openScene(host->world(), editorCommands.openScene, inspector);
                }
                if (!editorCommands.createFolder.empty())
                    (void)editor.content().createFolder(editorCommands.createFolder);

                // Before the delete and the duplicate, so a frame that somehow
                // carried both acts on a world the create has already finished
                // with rather than on one halfway through it.
                if (editorCommands.createClass != scene::InvalidClass && editorCommands.createParent.valid()) {
                    (void)editor.createInstance(host->world(), editorCommands.createClass, editorCommands.createParent,
                                                host->runtime().dataModel(), inspector);
                }
                if (editorCommands.deleteInstance.valid())
                    (void)editor.deleteInstance(host->world(), editorCommands.deleteInstance,
                                                host->runtime().dataModel(), inspector);
                if (editorCommands.duplicateInstance.valid())
                    (void)editor.duplicateInstance(host->world(), editorCommands.duplicateInstance,
                                                   host->runtime().dataModel(), inspector);
                if (editorCommands.renameInstance.valid())
                    (void)editor.renameInstance(host->world(), editorCommands.renameInstance,
                                                host->runtime().dataModel(), editorCommands.renameInstanceTo);

                // Content actions resolve the entry by path, because the tree
                // may have been re-read between the click and here -- and an
                // index into a list that moved is how a delete hits the row
                // below the one somebody chose.
                if (!editorCommands.deleteContent.empty() || !editorCommands.renameContent.empty()) {
                    const std::string& wanted = editorCommands.deleteContent.empty() ? editorCommands.renameContent
                                                                                     : editorCommands.deleteContent;
                    for (const ContentEntry& entry : editor.content().entries()) {
                        if (entry.path != wanted)
                            continue;
                        if (!editorCommands.deleteContent.empty())
                            (void)editor.content().remove(entry);
                        else
                            (void)editor.content().rename(entry, editorCommands.renameContentTo);
                        break;
                    }
                }
                // File > Exit. The same door the window's own close button is,
                // so a person who reached for the menu gets the same shutdown.
                if (editorCommands.quit)
                    quit = true;
                if (editorCommands.undo)
                    (void)editor.undo(host->world(), inspector);
                if (editorCommands.redo)
                    (void)editor.redo(host->world(), inspector);
                if (editorCommands.clearSelection)
                    inspector.select(core::InstanceId{});
                if (editorCommands.newScene) {
                    // Out of play mode first, for the reason opening a scene is:
                    // the snapshot would describe a world that no longer exists.
                    if (editor.inPlayMode())
                        editor.stop(host->world(), inspector);
                    editor.newScene(host->world(), inspector);
                }
                if (editorCommands.save)
                    (void)editor.saveOpenScene(host->world());
                if (!editorCommands.saveAs.empty())
                    (void)editor.saveSceneAs(host->world(), editorCommands.saveAs);

                // Remembered on CHANGE rather than at exit: an editor that only
                // wrote this on a clean shutdown would forget everything the one
                // time somebody most wants it -- after a crash.
                if (editor.openScenePath() != rememberedScene) {
                    rememberedScene = editor.openScenePath();
                    editor.rememberOpenScene(options.scriptPath / ".luaug");
                }
            }

            // **The manipulator gets the pointer first.** A press that lands on
            // a handle is not a press asking to select whatever is behind it,
            // and losing the selection on the frame you grab its gizmo is the
            // commonest way a first manipulator is unusable.
            const bool gizmoTook = editor.driveGizmo(host->world(), inspector);

            const core::InstanceId wasSelected = inspector.selection();
            if (!gizmoTook)
                editor.resolvePick(host->world(), inspector);

            // An editor says what you picked. It is the cheapest confirmation
            // that a click landed on the thing under the cursor rather than on
            // the thing behind it, and it is the only such confirmation that
            // survives into a log somebody can read afterwards -- which matters
            // here because the ImGui shell cannot render headlessly and so
            // cannot be asserted by any test that does not have a person in it.
            if (inspector.selection() != wasSelected) {
                if (inspector.selection().valid()) {
                    const scene::ClassDescriptor* descriptor =
                        host->world().classes().find(host->world().classOf(inspector.selection()));
                    const core::I18nArg args[] = {
                        {"name", host->world().atoms().text(host->world().name(inspector.selection()))},
                        {"class",
                         descriptor != nullptr ? host->world().atoms().text(descriptor->name) : std::string_view{"?"}},
                    };
                    core::log(core::LogLevel::Info, LUAUG_TR("engine.editor.info.selected"), args);
                }
                else {
                    core::log(core::LogLevel::Info, LUAUG_TR("engine.editor.info.deselected"));
                }
            }

            // The editor's camera reaches the world here, at the same safe
            // point as every other write, and only while paused -- `driveCamera`
            // returns the transform unchanged once the world is playing, so the
            // game takes its camera back on the first tick without this having
            // to know it did.
            //
            // Seeded from wherever the world's camera already is, so pressing
            // pause does not teleport the view somewhere nobody asked for.
            // **The editor's camera is the editor's**, and the world never
            // learns about it (ADR 0046's rule taken the rest of the way). It is
            // seeded once from whatever the world is looking through so that
            // opening the editor does not teleport the view, and after that the
            // renderer is TOLD which view to draw -- see `render::ViewOverride`.
            //
            // Writing `Workspace.CurrentCamera` was the first design and it was
            // wrong: it made the tool and the game two authors of one transform,
            // which is a disagreement no arbitration settles (D061).
            if (!editor.cameraAdopted()) {
                if (const core::InstanceId cameraId = host->currentCamera(); cameraId.valid()) {
                    if (const scene::CameraComponent* camera = host->world().cameras().find(cameraId);
                        camera != nullptr)
                        editor.adoptCamera(camera->cframe);
                }
            }
        }

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

        // **The streaming pause, which is a property that had a reader waiting
        // for it (D055).** `StreamingManager::minimumRingResident()` exists,
        // its own comment says it "is what `StreamingService.PauseOutsideLoadedArea`
        // reads", and nothing had ever called it -- so a game that turned the
        // property on got no pause and a character walking into unloaded ground
        // fell through it, which is the exact failure the property names.
        //
        // **It does not weaken R10.** What this changes is WHEN a tick runs,
        // never what a tick computes: the same ticks happen in the same order
        // with the same inputs, and the world hash after n ticks is the hash
        // after n ticks. The pump below keeps running while the world is
        // paused, which is what ends the pause.
        const bool waitingForGround = streaming.active() &&
                                      host->world().engineState().streamingPauseOutsideLoadedArea &&
                                      !streaming.minimumRingResident();
        u32 simTicks = waitingForGround ? 0u : frame.simTicks;

        // **The editor's transport, and it gates ticks by exactly the same
        // argument the streaming pause above makes** (D058): what this changes
        // is WHEN a tick runs, never what a tick computes. Paused takes none,
        // a step takes one, playing takes what the frame owed and this line
        // does nothing at all.
        //
        // A paused editor still shows a built world: `WorldHost::boot` runs
        // every entry script in its own drain before the first frame and
        // advances no clock, so tick zero is a world rather than an absence.
        if (options.editor)
            simTicks = editor.allowedTicks(simTicks);

        // **The history is dropped when the world it describes has been
        // replaced**, and here because here is downstream of every way that
        // happens -- a stop, an undo, a redo, a scene load, a new scene and a
        // hot reload are all behind us and the first `capture` of this frame is
        // ahead. `world.h` states the obligation in so many words: these
        // caches are "rebuilt from the tree rather than restored ... safe
        // order: restore, then rebuild". Nothing rebuilt this one (D070).
        //
        // A snapshot preserves generations precisely so that an id means the
        // same thing afterwards, which is what let a stale entry go on
        // answering `previous()` for an instance that had moved metres.
        if (transformHistoryWorld != inspector.worldGeneration()) {
            transformHistoryWorld = inspector.worldGeneration();
            transformHistory.clear();
        }

        // The simulation, before anything is drawn: rendering shows the state a
        // tick settled on, never one being written.
        for (u32 step = 0; step < simTicks; ++step) {
            // BEFORE the tick, so that once the loop is done the history holds
            // where everything was one tick ago and the world holds where it is
            // now -- the two ends `render::extract` interpolates between (D047).
            transformHistory.capture(host->world());
            host->tick();
        }

        // **The pointer's state, applied to the window it belongs to.** Both
        // properties were stored and read by nothing until M8 (D049): a script
        // could set `InputService.PointerLocked` and the cursor kept wandering
        // across the desktop, which is a look control that stops at the edge of
        // the screen.
        //
        // Applied on CHANGE rather than every frame, because SDL's relative mode
        // warps the cursor when it is entered and re-entering it every frame
        // would fight anything else on the machine that moves a pointer.
        // Windowed only: a headless run has no window to lock a pointer to, and
        // the property still round-trips there, which is what makes a replay of
        // a game that locks its pointer legal.
        if (window != nullptr) {
            scene::EngineState& engineState = host->world().engineState();

            // **In the editor the cursor belongs to the person, not to the
            // game** (D059). `examples/10-open-world` locks the pointer at file
            // scope for its mouse look, and the boot drain runs that before the
            // first frame -- so an editor opened on it started with no cursor
            // and no way to click a panel. The rule is the one Unity and Unreal
            // use and it is the same rule the transport applies to time: while
            // the world is paused the editor owns the device, and pressing play
            // hands it back.
            //
            // The game's property is not overwritten, only overridden. A script
            // that reads `InputService.PointerLocked` sees what it wrote, which
            // keeps the property honest and keeps a replay of a game that locks
            // its pointer legal.
            const bool editorOwnsPointer = options.editor && editing(editor.runState());
            // **While turning the camera the pointer is hidden and held**, which
            // is SDL's relative mode and is what puts the cursor back exactly
            // where it was when the button is released. Without it a right-drag
            // walks the cursor across the desktop and out of the window, and the
            // turn stops when it leaves.
            const bool editorLooking = editorOwnsPointer && editor.lookInput().active;
            const bool wantLocked = editorOwnsPointer ? editorLooking : engineState.pointerLocked;
            const bool wantVisible = editorOwnsPointer ? !editorLooking : engineState.pointerVisible;

            // **Handed back means handed back** (D069). The pointer belongs to
            // whoever holds it, and while that is the GAME the panels must not
            // see the mouse at all -- relative mode keeps posting motion with a
            // logical position SDL accumulates, and the invisible cursor walks
            // across the explorer hovering and clicking things a player turning
            // their head cannot see. Told here rather than inferred there,
            // because this is where the question is already answered, and told
            // BEFORE `handleEvents` runs later in this same frame, so there is
            // no frame of lag on either edge.
            if (overlay.has_value())
                overlay->setGameHoldsPointer(wantLocked && !editorOwnsPointer);

            if (wantLocked != pointerLocked) {
                pointerLocked = wantLocked;
                // Logged on the transition, not per frame. It separates two
                // failures that look identical from the outside: an editor that
                // never asked to hold the pointer, and a window that refused.
                if (options.editor) {
                    const core::I18nArg args[] = {
                        {"state", pointerLocked ? std::string_view{"held"} : std::string_view{"released"}}};
                    core::log(core::LogLevel::Info, LUAUG_TR("app.info.pointer_lock"), args);
                }
                // The result is checked rather than discarded. A pointer lock
                // that silently did not happen looks exactly like one that did
                // -- the cursor is hidden either way -- and the difference only
                // shows up as a camera that stops turning at the edge of the
                // screen, which is a thing somebody reports and nobody can
                // explain.
                if (!platform::setPointerLocked(*window, pointerLocked))
                    core::log(core::LogLevel::Warn, LUAUG_TR("app.warn.pointer_lock_refused"));

                // **Anchored back to where the hold began** (D063). SDL
                // accumulates a logical position from the relative motion and
                // warps the real cursor there on the way out, so without this a
                // look that turned the camera around leaves the cursor against
                // a window edge. Every editor puts it back under the hand that
                // was holding the button.
                if (!pointerLocked)
                    platform::setPointerPosition(*window, editorPointerAnchor.x, editorPointerAnchor.y);
            }
            if (wantVisible != pointerVisible) {
                pointerVisible = wantVisible;
                platform::setPointerVisible(pointerVisible);
            }

            // **What the WINDOW knows, written where a script can read it
            // (D055).** Both of these are properties of the display rather than
            // of the world, `UIService` publishes them, and until the lint was
            // widened to sweep `EngineState` nothing wrote either -- so
            // `DisplayScale` was 1 on a doubled display and `SafeAreaInsets`
            // was zero on a device with a notch, which are exactly the two
            // machines a game gets them for. Every frame rather than on change,
            // because they change with a window drag between two monitors and
            // there is no event for that worth subscribing to at this price.
            engineState.displayScale = platform::windowDisplayScale(*window);
            const platform::WindowInsets insets = platform::windowSafeAreaInsets(*window);
            engineState.safeAreaInsets = core::Rect{
                .min = core::Vec2{static_cast<f32>(insets.left), static_cast<f32>(insets.top)},
                .max = core::Vec2{static_cast<f32>(insets.right), static_cast<f32>(insets.bottom)},
            };

            // **The overlay and its property, in both directions.** A script
            // writing `DebugService.OverlayVisible` opens the panel; F3 opening
            // the panel writes the property back, so a game that draws its own
            // hint from it does not go stale (D055). Without the write-back the
            // two would disagree the moment anybody pressed the key, which is
            // the state D030 named and this lint exists to find.
            if (overlay.has_value()) {
                if (engineState.overlayVisible != overlayVisible) {
                    overlayVisible = engineState.overlayVisible;
                    overlay->setVisible(overlayVisible);
                }
                else if (overlay->visible() != overlayVisible) {
                    overlayVisible = overlay->visible();
                    engineState.overlayVisible = overlayVisible;
                }
            }
        }

        // Where this frame sits between those two ticks.
        //
        // **Zero on the synthetic clock, by construction and not by accident.**
        // A headless run drives exactly one fixed step per frame, so its frames
        // ARE the ticks -- and every golden in this repository was recorded that
        // way. Interpolating a headless frame by the accumulator's rounding
        // residue would move every recorded uniform in its last bits for no
        // gain at all.
        //
        // **And zero whenever the world is not advancing** (D070). Interpolation
        // blends where a part was at the START of the last tick with where it
        // is now, and a frame that runs no tick is not between those two things
        // -- there is one state and the frame is on it. The accumulator does not
        // stop for a paused editor: `FrameScheduler` drains it every frame
        // whether or not the editor let a tick through, so `alpha` goes on
        // sweeping the whole of [0, 1) at render rate. Against a history that
        // stopped moving, that is a part drawn somewhere different every frame,
        // which is what "the capsule flickers after stop" is.
        const bool worldAdvancing = !options.editor || advancing(editor.runState());
        const f32 renderAlpha = syntheticClock || !worldAdvancing ? 0.0f : frame.alpha;

        // What the speakers do is a consequence of the simulation and never an
        // input to it (M6 brief, Decision 9), which is why this is after the
        // ticks rather than inside one. The listener is
        // `Workspace.CurrentCamera` -- a game with two ideas about where the
        // player is hearing from is a game with a bug.
        // The same mounts the meshes and the UI read from, so `Sound.Content`
        // names a file the same way everything else does. Set every frame
        // rather than at boot because the world -- and with it the audio system
        // -- can be replaced by a reload; the call is an identity check when
        // nothing changed.
        host->audio().setContentMounts(&contentMounts);
        // Silent while the editor is paused (D060). A world that is not ticking
        // should not be audible, and hearing a game's ambience while editing it
        // is the same wrong-owner mistake as an editor whose cursor belongs to
        // the game.
        // Silent whenever the world is not advancing -- editing OR paused in
        // play mode. A paused game that kept humming would be the same
        // half-stopped state the three-state model exists to remove.
        host->audio().setSuspended(options.editor && !advancing(editor.runState()));
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
            // **The game does not receive input while the editor is editing**
            // (D062). Same rule as the tick, the cursor, the audio and the
            // camera, and the same reason: while the tool owns the machine, WASD
            // is a fly camera and not a character. The events still reach ImGui
            // -- it takes the untranslated SDL stream of its own -- so the shell
            // is fully live while the game is not.
            //
            // `releaseAll` on the way in rather than on the way out, so a key
            // held when play stopped is not still held when play starts again.
            // That is the alt-tab case the function was written for, arriving
            // through a different door.
            // **The motion, taken relative rather than differenced.** Once the
            // pointer is locked its POSITION stops moving, so a camera driven
            // from two positions stops turning at exactly the moment it is being
            // asked to. `platform::Event` says so at the field itself.
            editorLookDelta = {};
            for (const platform::Event& event : events) {
                if (event.type == platform::EventType::MouseMoved) {
                    editorLookDelta.x += event.pointerDeltaX;
                    editorLookDelta.y += event.pointerDeltaY;
                    // Only while the pointer is free. In relative mode the
                    // position SDL reports is a logical one it accumulates from
                    // the motion, so recording it here would anchor the cursor
                    // to wherever the camera took it -- which is the defect this
                    // is the other half of.
                    if (!pointerLocked)
                        editorPointerAnchor = core::Vec2{event.pointerX, event.pointerY};
                }
            }

            // Driven HERE rather than at the frame's safe point, because this is
            // where the motion arrives: the loop pumps events after the ticks,
            // and a camera fed at the safe point would be turning on the
            // previous frame's mouse. It moves on the RENDER clock and not the
            // tick, because a world that is not ticking is exactly when somebody
            // is flying it.
            if (options.editor && editing(editor.runState()) && editor.cameraAdopted()) {
                const Editor::LookInput& look = editor.lookInput();
                (void)editor.driveCamera(look.active ? editorLookDelta : core::Vec2{}, look.move,
                                         static_cast<f32>(frame.renderDt));
            }

            const bool gameTakesInput = !options.editor || editor.inPlayMode();
            if (gameTakesInput) {
                host->pumpInput(events);
            }
            else if (gameHadInput) {
                host->input().releaseAll(host->world());
            }
            gameHadInput = gameTakesInput;

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

        // In the editor the world is not drawn to the screen: it is drawn into
        // the viewport panel's own texture, and the shell is drawn to the
        // screen on top. Everything between here and the overlay's pass
        // therefore renders at the PANEL's resolution -- which is what makes
        // the aspect ratio, the UI layout and the picking ray all agree with
        // the image somebody is looking at.
        const rhi::TextureHandle present = target;
        if (options.editor && target.valid()) {
            const ViewportRect& panel = editor.viewport();
            const core::u32 wantWidth = static_cast<core::u32>(panel.width > 1.0f ? panel.width : 1.0f);
            const core::u32 wantHeight = static_cast<core::u32>(panel.height > 1.0f ? panel.height : 1.0f);
            if (viewportTarget.resize(*device, wantWidth, wantHeight)) {
                target = viewportTarget.texture();
                targetFormat = kOffscreenFormat;
                targetWidth = viewportTarget.width();
                targetHeight = viewportTarget.height();
            }
        }

        if (target.valid()) {
            ensureDebugPass(targetFormat);

            // **A paused world runs no script phases, not even the render-rate
            // one** (D061). `PreRender` fires on the render clock rather than
            // the tick, so it kept firing while the editor was paused -- and
            // `examples/10-open-world` drives its follow camera from there, so
            // the script and the editor wrote `CurrentCamera` on alternate
            // frames and the view flickered between two answers.
            //
            // Pause means the game is not running. A phase that still fires is
            // the game still running, and no amount of arbitrating who wins the
            // camera would make that untrue.
            const bool worldIsRunning = !options.editor || advancing(editor.runState());
            if (!options.headless && worldIsRunning)
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
                if (meshLoader.sync(*device, *cmd, host->world(), host->workspace(), meshCache, meshLibrary) > 0 &&
                    host->physics() != nullptr) {
                    // A mesh finished loading, so the physics mirror can be told
                    // what it collides as. Pushed from here because this is the
                    // one place that can see both the render library and the
                    // mirror -- `render` is L4 and `scene` is L3, and neither is
                    // allowed to reach the other.
                    meshLibrary.forEach([&](core::NameAtom content, const render::MeshLibrary::Entry& entry) {
                        if (!entry.positions.empty())
                            host->physics()->setCollisionPoints(content, entry.positions);
                    });
                }

            // Extraction happens once, at a known moment, from a world that is
            // between ticks (ADR 0027). Rendering never walks the ECS.
            // The aspect comes from the target rather than from the camera:
            // a `Camera` has no ViewportSize in this release (it needs a
            // Vector2, which the UI brings at M6), and the renderer is the one
            // that knows how many pixels it is filling.
            const f32 aspect =
                targetHeight == 0 ? 1.0f : static_cast<f32>(targetWidth) / static_cast<f32>(targetHeight);
            const f32 shadowRadius = renderer != nullptr && renderer->valid() ? renderer->shadowRadius() : 0.0f;
            // Paused: the editor's own view. Playing: the game's, unchanged --
            // which is what makes the viewport show the GAME when you press play
            // rather than a tool's idea of it.
            const render::ViewOverride editorView{editor.cameraCFrame(), 70.0f, 0.1f, 5000.0f};
            const bool useEditorView = options.editor && editing(editor.runState()) && editor.cameraAdopted();
            // **The selection, drawn as a silhouette rather than as a box.** The
            // wire box E1 shipped says where a thing's BOUNDS are, which for
            // anything that is not a cube is a shape the object does not have --
            // and around a tree or a character it is a box floating in the air
            // near the thing you clicked. What the renderer gets is a flag per
            // draw; what it makes of it is a mask and an outline of its edge.
            //
            // Only while EDITING. In play mode the world belongs to the game and
            // a tool's mark on it would be in every screenshot somebody takes of
            // their own game.
            const std::span<const core::InstanceId> outlined = options.editor && editing(editor.runState())
                                                                   ? inspector.selectionSet()
                                                                   : std::span<const core::InstanceId>{};
            render::extract(host->world(), host->workspace(), host->lighting(), meshLibrary, aspect, shadowRadius,
                            host->animation(), renderAlpha, &transformHistory, snapshot,
                            useEditorView ? &editorView : nullptr, outlined);
            // The UI is laid out against the TARGET's size rather than the
            // window's: an offscreen render at 640x360 has to produce the
            // layout that resolution would, which is the whole of what the
            // two-resolution goldens check.
            // The matrices the image was drawn with, not the ones the next
            // frame will use. A pick taken against a fresher camera lands
            // wherever the camera moved to between the click and the walk,
            // which is invisible standing still and wrong while walking.
            if (options.editor && snapshot.camera.valid)
                editor.setCamera(snapshot.camera.projection, snapshot.camera.view, snapshot.camera.origin);

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
            for (const rhi::TextureHandle image : uiText.images())
                uiTextures.push_back(image);
            buildUiGeometry(uiDrawList, uiViewport, uiVertices, uiRuns, uiTextures);

            frameVisibleObjects = 0;
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
                ++frameVisibleObjects;
                if (level > 0)
                    ++frameLodDraws;
                frameTriangles += resolved->sections[range.firstSection + draw.section].indexCount / 3u;
            }

            submitWorld(snapshot, debugDraw);

            // Read AFTER submission, because that is when the renderer knows.
            // Counted by the renderer rather than derived from the snapshot: a
            // draw call is a thing a backend issues, and inferring it from the
            // draw list is exactly the assumption instancing invalidated.
            const render::RendererStats rendererStats =
                renderer != nullptr && renderer->valid() ? renderer->stats() : render::RendererStats{};
            frameDrawCalls = rendererStats.drawCalls;
            frameInstancedDraws = rendererStats.instancedDraws;

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

            // After the rebase and not before it, because these are already in
            // the space it converts to -- see `submitSelection`. Everything the
            // editor draws over the world goes here for the same reason.
            if (options.editor) {
                submitSelection(host->world(), inspector.selectionSet(), snapshot.camera.origin, debugDraw);
                // The manipulator over the outline, because the outline says
                // WHAT is selected and the manipulator is the thing being
                // aimed at.
                if (const std::optional<GizmoFrame> gizmo = editor.gizmoFrame(host->world(), inspector);
                    gizmo.has_value()) {
                    submitGizmo(*gizmo, editor.gizmoMode(), editor.gizmoHandle(), snapshot.camera.origin, debugDraw);
                }
            }

            // Uploaded before the render pass opens, because a copy cannot run
            // inside one -- the seam says so and the backend enforces it. The
            // mesh loader is here for the same reason and one more: it is the
            // FrameStart safe point, so a file read cannot land mid-tick.
            if (debugRenderer.valid())
                debugRenderer.upload(*device, *cmd, debugDraw);
            // Beside the other uploads and for the same reason: this is the
            // only place in the frame where a copy is legal. Idempotent after
            // the first success, so the cost is one branch a frame.
            if (options.editor && !iconAtlas.ready()) {
                if (iconAtlas.load(*device, *cmd, platform::paths().contentDir, options.scriptPath))
                    core::logText(core::LogLevel::Info, iconAtlas.status());
                else
                    core::logText(core::LogLevel::Warn, iconAtlas.status());
            }
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
            //
            // In the editor it goes to the SCREEN while everything above went
            // to the panel's texture, and the screen has had nothing written to
            // it this frame -- so it is cleared first. Without that, whatever
            // the dockspace leaves transparent shows a previous frame or worse.
            if (options.editor && present.valid() && present != target) {
                const std::array<rhi::ColorAttachment, 1> clear{rhi::ColorAttachment{
                    .texture = present,
                    .loadOp = rhi::LoadOp::Clear,
                    .storeOp = rhi::StoreOp::Store,
                    .clearColor = {0.06f, 0.06f, 0.07f, 1.0f},
                }};
                cmd->beginRenderPass({.colorAttachments = clear, .debugName = "editor-backdrop"});
                cmd->endRenderPass();
            }

            if (overlay.has_value()) {
                if (options.editor) {
                    overlay->setEditorTarget(&editor, viewportTarget.texture());
                    overlay->setIcons(&iconAtlas);
                }
                overlay->render(*cmd, options.editor && present.valid() ? present : target, frame);
            }
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
        const std::array<I18nArg, 6> stats{
            I18nArg{"frames", static_cast<core::i64>(measured.size())},
            I18nArg{"median", median},
            I18nArg{"worst", worst},
            I18nArg{"draws", static_cast<core::i64>(frameDrawCalls)},
            I18nArg{"objects", static_cast<core::i64>(frameVisibleObjects)},
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
        // Loud, and not fatal. A quarantined check that stopped saying anything
        // would be a deleted check with extra steps (D066, §12).
        for (const core::EngineError& quarantined : verdict.quarantined) {
            core::logText(LogLevel::Warn, quarantined.message);
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
    iconAtlas.destroy(*device);
    if (offscreen.valid())
        device->destroy(offscreen);
    if (window != nullptr)
        device->releaseWindow(*window);

    return soakFailure;
}

} // namespace luaug::app
