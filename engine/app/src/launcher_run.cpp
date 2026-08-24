// The launcher's own loop (ADR 0055).
//
// **Everything `run` brings up that a project browser does not need is absent
// here**, and that is the shape rather than an economy: no world, no Luau VM,
// no physics, no scheduler, no job pool. What it shares with `run` is the
// window, the device and the overlay -- the part that draws -- and it shares it
// by doing the same six calls rather than by threading a flag through two
// thousand lines of frame loop.
//
// It ends one of two ways: somebody closed the window, or somebody chose a
// project. The second starts the editor as a new process and returns, because a
// project decides the content mounts, the Luau VM, the `.luaurc`, the partition
// cache and the editor layout -- all resolved at boot.
#include "luaug/app/backends.h"
#include "luaug/app/debug_overlay.h"
#include "luaug/app/engine.h"
#include "luaug/app/launcher.h"
#include "luaug/asset/image.h"
#include "luaug/core/build_info.h"
#include "luaug/core/log.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/event.h"
#include "luaug/platform/platform.h"
#include "luaug/platform/window.h"
#include "luaug/rhi/device.h"

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace luaug::app {
namespace {

using core::I18nArg;
using core::LogLevel;

// The file the project list lives in, under this user's own directory.
[[nodiscard]] std::filesystem::path projectListPath()
{
    const std::filesystem::path& userDir = platform::paths().userDir;
    // Empty is a real answer, not a failure: a platform with nowhere to write
    // gets a list that lasts one session, and `ProjectList::save` says so by
    // returning false rather than by pretending.
    return userDir.empty() ? std::filesystem::path{} : userDir / "projects.json";
}

// This executable, as something `startDetached` can run. `executableDir` is what
// the platform layer resolves; the name is ours because the launcher is only
// ever the engine's own host and never a renamed packaged game -- a packaged
// game has a `game/` beside it and never reaches this file.
[[nodiscard]] std::filesystem::path selfPath()
{
#if defined(_WIN32)
    return platform::paths().executableDir / "luaug-host.exe";
#else
    return platform::paths().executableDir / "luaug-host";
#endif
}

} // namespace

std::optional<core::EngineError> runLauncher(const EngineOptions& options)
{
    if (const auto error = platform::init({.headless = false}); error.has_value())
        return error;

    // Declaration order is shutdown order reversed, exactly as `run` documents:
    // SDL_GPU needs the window released from the device before the window dies.
    struct PlatformScope
    {
        ~PlatformScope() { platform::shutdown(); }
    } platformScope;

    platform::WindowPtr window;
    core::EngineError error;

    const rhi::DeviceResult device = createDevice({.backend = options.backend, .debug = true}, &error);
    if (device == nullptr)
        return error;

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

    // The same three lines that dress every other window this engine opens.
    if (const std::vector<std::byte> iconBytes = platform::applicationIconBytes(); !iconBytes.empty()) {
        asset::Image icon;
        if (!asset::decodeImage(iconBytes, icon).has_value() && icon.valid()) {
            (void)platform::setWindowIcon(*window, icon.pixels, static_cast<core::i32>(icon.width),
                                          static_cast<core::i32>(icon.height));
        }
    }

    if (!device->claimWindow(*window))
        return core::makeError(LUAUG_TR("rhi.err.window_claim_failed"), {}, "SDL_ClaimWindowForGPUDevice");

    DebugOverlay overlay(*window, *device, Shell::Launcher);
    if (!overlay.active()) {
        // A build with no ImGui cannot draw a launcher, and reaching here means
        // the host was started with no project in a profile that has no shell to
        // show. Said rather than presented as an empty window.
        return core::makeError(LUAUG_TR("engine.cli.err.no_script"));
    }
    overlay.setVisible(true);

    ProjectList projects;
    projects.load(projectListPath());

    LauncherView view;
    view.projects = &projects;
    view.templatesDir = platform::paths().contentDir / "templates";
    view.definitions = platform::paths().contentDir / "runtime" / "types" / "engine.d.luau";
    // **Documents, not the application's own directory.** They are different
    // questions and the wrong answer is invisible until somebody goes looking
    // for the game they made: the first version of this seeded the field from
    // `userDir` and put a new project inside `AppData`, where nobody would ever
    // find it. Blank when the platform has no such notion.
    view.defaultParent = platform::paths().documentsDir;
    view.canBrowse = platform::canPickFolder();
    overlay.setLauncherTarget(&view);

    // What the folder picker answered, written by a callback that arrives while
    // events are pumped and read by the loop below.
    std::filesystem::path picked;
    bool pickPending = false;

    bool quit = false;
    Frame frame;
    while (!quit) {
        const std::span<const platform::Event> events = platform::pumpEvents();
        for (const platform::Event& event : events) {
            if (event.type == platform::EventType::Quit || event.type == platform::EventType::WindowCloseRequested)
                quit = true;
        }
        overlay.handleEvents(events);

        if (!picked.empty()) {
            const std::filesystem::path chosen = std::exchange(picked, {});
            pickPending = false;
            if (isProjectDirectory(chosen))
                view.open = chosen;
            else
                view.message = chosen.string() + " is not a project: it has no luaug.toml and no src/scripts.";
        }

        if (view.browse) {
            view.browse = false;
            // One at a time: a second picker while the first is open is not a
            // state this has to represent.
            if (!pickPending && platform::canPickFolder()) {
                pickPending = true;
                platform::pickFolder(*window, view.defaultParent.string(),
                                     [&picked, &pickPending](std::filesystem::path chosen) {
                                         picked = std::move(chosen);
                                         if (picked.empty())
                                             pickPending = false;
                                     });
            }
        }

        if (!view.open.empty()) {
            const std::filesystem::path project = std::exchange(view.open, {});
            projects.remember(project);
            (void)projects.save();

            const std::array<I18nArg, 1> args{I18nArg{"path", project.string()}};
            core::log(LogLevel::Info, LUAUG_TR("app.info.launcher_opening"), args);

            if (platform::startDetached({selfPath().string(), project.string(), "--edit"})) {
                quit = true;
                continue;
            }
            // The list still remembers it, which is right: the person did choose
            // it, and the next launch should offer it first.
            view.message = core::engineCatalog().format(LUAUG_TR("app.err.launcher_start_failed"), {});
        }

        if (view.quit)
            quit = true;

        rhi::ICmdList* cmd = device->beginFrame();
        if (cmd == nullptr)
            continue;

        const rhi::Swapchain swapchain = device->acquireSwapchain(*window);
        if (swapchain.texture.valid()) {
            // The launcher has nothing behind its panel, so the backdrop IS the
            // frame -- the same clear the editor's screen gets, for the same
            // reason: whatever ImGui leaves transparent must not show a previous
            // frame.
            const std::array<rhi::ColorAttachment, 1> clear{rhi::ColorAttachment{
                .texture = swapchain.texture,
                .loadOp = rhi::LoadOp::Clear,
                .storeOp = rhi::StoreOp::Store,
                .clearColor = {0.06f, 0.06f, 0.07f, 1.0f},
            }};
            cmd->beginRenderPass({.colorAttachments = clear, .debugName = "launcher-backdrop"});
            cmd->endRenderPass();

            overlay.render(*cmd, swapchain.texture, frame);
        }

        device->submitAndPresent();
        ++frame.index;
    }

    return std::nullopt;
}

} // namespace luaug::app
