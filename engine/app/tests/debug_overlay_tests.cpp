// What is worth asserting about the overlay is not what it draws -- it draws a
// developer panel, and pixels of text are the screenshot gate's business, not a
// unit test's. It is the contract around it: that it is inert wherever there is
// nothing to draw with, that being inert costs the frame loop nothing, and that
// F3 is what turns it on where there is.
#include <doctest/doctest.h>

#include <array>
#include <string>

#include "luaug/app/backends.h"
#include "luaug/app/debug_overlay.h"
#include "luaug/app/frame_scheduler.h"
#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/event.h"
#include "luaug/platform/platform.h"
#include "luaug/platform/window.h"
#include "luaug/rhi/device.h"

using luaug::app::DebugOverlay;
using luaug::app::Frame;
using luaug::core::EngineError;

namespace
{

void seedRealCatalog()
{
    const auto result = luaug::core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// One F3 press, as the pump would have reported it.
[[nodiscard]] luaug::platform::Event pressF3()
{
    luaug::platform::Event event;
    event.type = luaug::platform::EventType::KeyDown;
    event.key = luaug::platform::Key::F3;
    return event;
}

} // namespace

TEST_CASE("the overlay is inert on a backend that draws nothing")
{
    seedRealCatalog();

    const auto initError = luaug::platform::init({.headless = true});
    REQUIRE_MESSAGE(!initError.has_value(), (initError ? initError->detail : std::string{}));

    EngineError error;
    const auto window = luaug::platform::createWindow(
        {.titleKey = LUAUG_TR("platform.window.title"), .width = 320, .height = 200, .visible = false}, &error);
    REQUIRE_MESSAGE(window != nullptr, error.detail);

    // `null` is the honest stand-in for every device that rasterizes nothing:
    // capture behaves the same way, and so does a shipping build, where the
    // overlay is inert for a different reason entirely.
    const luaug::rhi::DeviceResult device
        = luaug::app::createDevice({.backend = luaug::rhi::BackendId::Null}, &error);
    REQUIRE_MESSAGE(device != nullptr, error.detail);

    DebugOverlay overlay(*window, *device);

    CHECK_FALSE(overlay.active());
    CHECK_FALSE(overlay.visible());

    // Everything a frame loop would call, on an overlay that never started.
    // None of it may reach ImGui, and none of it may crash -- that is what
    // lets the loop call it unconditionally.
    const std::array<luaug::platform::Event, 1> events{pressF3()};
    overlay.handleEvents(events);
    CHECK_FALSE(overlay.visible());

    luaug::rhi::ICmdList* cmd = device->beginFrame();
    REQUIRE(cmd != nullptr);
    overlay.render(*cmd, luaug::rhi::TextureHandle{}, Frame{});
    device->submitAndPresent();

    luaug::platform::shutdown();
}

TEST_CASE("visibility is off until something asks for it")
{
    seedRealCatalog();

    const auto initError = luaug::platform::init({.headless = true});
    REQUIRE_MESSAGE(!initError.has_value(), (initError ? initError->detail : std::string{}));

    EngineError error;
    const auto window = luaug::platform::createWindow(
        {.titleKey = LUAUG_TR("platform.window.title"), .visible = false}, &error);
    REQUIRE_MESSAGE(window != nullptr, error.detail);

    const luaug::rhi::DeviceResult device
        = luaug::app::createDevice({.backend = luaug::rhi::BackendId::Null}, &error);
    REQUIRE_MESSAGE(device != nullptr, error.detail);

    DebugOverlay overlay(*window, *device);

    // A debug overlay that greets everyone who starts the engine is in the way,
    // so the default is off whether or not the build could show it.
    CHECK_FALSE(overlay.visible());

    overlay.setVisible(true);
    CHECK(overlay.visible());

    luaug::platform::shutdown();
}

// The path that needs a driver AND a display: a real SDL_GPU device with a
// claimed window is the only configuration where the overlay is not inert.
// Skipped where either is missing, for the reason the rhi tests give -- a red
// build meaning "this runner has no GPU" is a check nobody can act on.
TEST_CASE("on a real device, F3 flips the panel")
{
    seedRealCatalog();

    if (const auto initError = luaug::platform::init({.headless = false}); initError.has_value())
    {
        MESSAGE("no display on this machine, skipping: " << initError->detail);
        return;
    }

    EngineError error;
    const auto window = luaug::platform::createWindow(
        {.titleKey = LUAUG_TR("platform.window.title"), .width = 320, .height = 200, .visible = false}, &error);
    if (window == nullptr)
    {
        MESSAGE("no window on this machine, skipping: " << error.detail);
        luaug::platform::shutdown();
        return;
    }

    const luaug::rhi::DeviceResult device
        = luaug::app::createDevice({.backend = luaug::rhi::BackendId::SdlGpu, .debug = true}, &error);
    if (device == nullptr)
    {
        MESSAGE("no GPU device on this machine, skipping: " << error.detail);
        luaug::platform::shutdown();
        return;
    }
    if (!device->claimWindow(*window))
    {
        MESSAGE("the device would not claim a window on this machine, skipping");
        luaug::platform::shutdown();
        return;
    }

    {
        DebugOverlay overlay(*window, *device);

        // Dev builds compile ImGui in; shipping does not, and the same overlay
        // is then inert on the very device it was written for. Asserting both
        // halves is what makes ADR 0011's "compiled out of shipping" a claim
        // this suite can hold rather than a comment.
#if LUAUG_DEBUG_UI
        REQUIRE(overlay.active());

        const std::array<luaug::platform::Event, 1> events{pressF3()};

        overlay.handleEvents(events);
        CHECK(overlay.visible());

        // One real frame with the panel up, in the order the contract asks
        // for: the scene's pass closes, then the overlay opens its own. The
        // device was created with validation on, so a driver that objects to
        // what ImGui recorded fails here rather than in someone's screenshot.
        luaug::rhi::ICmdList* cmd = device->beginFrame();
        REQUIRE(cmd != nullptr);
        const luaug::rhi::Swapchain swapchain = device->acquireSwapchain(*window);
        if (swapchain.texture.valid())
        {
            const std::array<luaug::rhi::ColorAttachment, 1> colors{luaug::rhi::ColorAttachment{
                .texture = swapchain.texture,
                .loadOp = luaug::rhi::LoadOp::Clear,
                .storeOp = luaug::rhi::StoreOp::Store,
            }};
            cmd->beginRenderPass({.colorAttachments = colors, .debugName = "clear"});
            cmd->endRenderPass();

            overlay.render(*cmd, swapchain.texture, Frame{.index = 1, .renderDt = 1.0 / 60.0});
        }
        else
        {
            // Said out loud rather than passing quietly: a hidden window that
            // hands out no backbuffer means the draw half of this test did not
            // run, and a silent skip would read as coverage it does not have.
            MESSAGE("no swapchain for a hidden window on this machine; the overlay draw was not exercised");
        }
        device->submitAndPresent();
        device->waitIdle();

        overlay.handleEvents(events);
        CHECK_FALSE(overlay.visible());
#else
        CHECK_FALSE(overlay.active());
#endif
    }

    device->releaseWindow(*window);
    luaug::platform::shutdown();
}
