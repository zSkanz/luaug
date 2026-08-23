// What is worth asserting about the overlay is not what it draws -- it draws a
// developer panel, and pixels of text are the screenshot gate's business, not a
// unit test's. It is the contract around it: that it is inert wherever there is
// nothing to draw with, that being inert costs the frame loop nothing, and that
// F3 is what turns it on where there is.
#include "luaug/app/backends.h"
#include "luaug/app/debug_overlay.h"
#include "luaug/app/frame_scheduler.h"
#include "luaug/app/icons.h"
#include "luaug/app/inspector.h"
#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/event.h"
#include "luaug/platform/platform.h"
#include "luaug/platform/window.h"
#include "luaug/rhi/device.h"
#include "luaug/scene/world.h"

#include <array>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "icon_ids.gen.h"
#include "inspector_fixture.h"

using luaug::app::DebugOverlay;
using luaug::app::Frame;
using luaug::app::Inspector;
using luaug::core::EngineError;

namespace {

void seedRealCatalog()
{
    const auto result = luaug::core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// A world for the explorer to walk: a root, two children out of alphabetical
// order, and a grandchild, over the never-before-seen classes in
// `inspector_fixture.h`. Small on purpose -- what this exercises is the drawing
// code's ID stack and its Begin/End pairing, not the tree's size.
struct InspectedWorld
{
    luaug::app::testing::Fixture schema;
    luaug::scene::World world{schema.classes, schema.enums, schema.atoms, 1234u};
    luaug::core::InstanceId root;

    InspectedWorld()
    {
        root = schema.widget(world, "Root");
        const luaug::core::InstanceId zulu = schema.widget(world, "Zulu");
        const luaug::core::InstanceId alpha = schema.widget(world, "Alpha");
        const luaug::core::InstanceId leaf = schema.widget(world, "Leaf");
        static_cast<void>(world.setParent(zulu, root));
        static_cast<void>(world.setParent(alpha, root));
        static_cast<void>(world.setParent(leaf, zulu));

        // A reference for the Instance widget to render and offer to follow.
        static_cast<void>(world.setProperty(zulu, schema.atom("Link"), luaug::scene::Value{alpha}));
    }
};

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
    const luaug::rhi::DeviceResult device = luaug::app::createDevice({.backend = luaug::rhi::BackendId::Null}, &error);
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
    const auto window =
        luaug::platform::createWindow({.titleKey = LUAUG_TR("platform.window.title"), .visible = false}, &error);
    REQUIRE_MESSAGE(window != nullptr, error.detail);

    const luaug::rhi::DeviceResult device = luaug::app::createDevice({.backend = luaug::rhi::BackendId::Null}, &error);
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

    if (const auto initError = luaug::platform::init({.headless = false}); initError.has_value()) {
        MESSAGE("no display on this machine, skipping: " << initError->detail);
        return;
    }

    EngineError error;
    const auto window = luaug::platform::createWindow(
        {.titleKey = LUAUG_TR("platform.window.title"), .width = 320, .height = 200, .visible = false}, &error);
    if (window == nullptr) {
        MESSAGE("no window on this machine, skipping: " << error.detail);
        luaug::platform::shutdown();
        return;
    }

    const luaug::rhi::DeviceResult device =
        luaug::app::createDevice({.backend = luaug::rhi::BackendId::SdlGpu, .debug = true}, &error);
    if (device == nullptr) {
        MESSAGE("no GPU device on this machine, skipping: " << error.detail);
        luaug::platform::shutdown();
        return;
    }
    if (!device->claimWindow(*window)) {
        MESSAGE("the device would not claim a window on this machine, skipping");
        luaug::platform::shutdown();
        return;
    }

    {
        DebugOverlay overlay(*window, *device);

        // The panel's drawing half cannot be asserted on -- it is pixels of
        // text -- but it can be RUN, and running it is what catches an
        // unbalanced ImGui ID stack, a BeginChild without its EndChild, or a
        // widget reading a property the world will not give it. Without this
        // the explorer and the properties table are never executed by any test
        // at all, and `world` null is the only path the suite would cover.
        InspectedWorld inspected;
        Inspector inspector;
        inspector.select(inspected.root);
        overlay.setInspectionTarget(&inspected.world, inspected.root, &inspector);

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

        // **The icon atlas, on a real device.** After the swapchain is acquired
        // and before any render pass opens, which is the one window an upload
        // has: `uploadTexture` opens a copy pass, and SDL asserts outright if
        // a swapchain is acquired while one is open. That is the frame loop's
        // order too.
        //
        // It decodes sixty-seven PNGs, box-filters each to three sizes and
        // packs them, and none of that has a headless path: a decode that
        // returned nothing, a packer that overflowed its atlas or a format the
        // driver refuses all land here.
        luaug::app::IconAtlas atlas;
        const std::filesystem::path iconRoot = luaug::platform::paths().contentDir;
        if (std::filesystem::exists(iconRoot / "icons" / "default" / "theme.json")) {
            CHECK(atlas.load(*device, *cmd, iconRoot, {}));
            CHECK(atlas.ready());
            // The fallback resolves for an id no theme has, which is what makes
            // a class this build has never seen draw as something rather than
            // as a hole.
            CHECK(atlas.find("class.NoSuchClassExists", 16u).valid);
            CHECK_FALSE(atlas.has("class.NoSuchClassExists"));
            CHECK(atlas.has(luaug::app::icons::ClassPart));
            // The six that were drawn a day later than the rest. They were
            // ABSENT from the theme rather than present and broken, which is
            // what let the loader ship before them and what makes this a
            // one-line change now they exist.
            CHECK(atlas.has(luaug::app::icons::ActionDelete));
            CHECK(atlas.has(luaug::app::icons::ActionSave));
            // --- The palette ---------------------------------------------
            //
            // The tint says what KIND of thing an icon is and never which
            // thing: the shape carries the identity and the colour only makes a
            // long tree scannable.
            const std::optional<luaug::core::Color3> part =
                atlas.tintFor(luaug::app::icons::ClassPart, luaug::app::IconAtlas::Panel::Dark);
            REQUIRE(part.has_value());

            // Two values per role, and they are not the same one: a single
            // colour cannot clear 3:1 against both a near-white panel and a
            // dark one.
            const std::optional<luaug::core::Color3> partLight =
                atlas.tintFor(luaug::app::icons::ClassPart, luaug::app::IconAtlas::Panel::Light);
            REQUIRE(partLight.has_value());
            CHECK_FALSE(*part == *partLight);

            // An id NOT named in `roles` takes `defaultRole` rather than
            // nothing -- thirty of the shipped ids are, deliberately, because a
            // toolbar of ten colours is a fruit salad.
            const std::optional<luaug::core::Color3> verb =
                atlas.tintFor(luaug::app::icons::ActionUndo, luaug::app::IconAtlas::Panel::Dark);
            CHECK(verb.has_value());

            // And an id no theme has heard of resolves too, for the same
            // reason: a typo in somebody's manifest must not make an icon
            // vanish.
            CHECK(atlas.tintFor("class.NoSuchClassExists", luaug::app::IconAtlas::Panel::Dark).has_value());

            // **Tinting off is not a degraded mode.** Every icon takes the
            // panel's own foreground, which is exactly what they all did before
            // there was a palette -- one branch rather than a second path.
            atlas.setTinting(false);
            CHECK_FALSE(atlas.tintFor(luaug::app::icons::ClassPart, luaug::app::IconAtlas::Panel::Dark).has_value());
            atlas.setTinting(true);

            MESSAGE(atlas.status());

            // --- The compatibility case ----------------------------------
            //
            // **A theme with no `palette` at all must load and draw exactly as
            // it did before there was one.** Colour arrived after the set
            // shipped, and a loader that needed it would break every theme
            // written in between -- so this builds one on disk without it and
            // checks that every lookup still answers and no tint does.
            const std::filesystem::path plain =
                std::filesystem::temp_directory_path() / "luaug-plain-theme" / "icons" / "default";
            std::filesystem::remove_all(plain.parent_path().parent_path());
            std::filesystem::create_directories(plain / "class");
            std::filesystem::copy_file(iconRoot / "icons" / "default" / "class" / "Part.png",
                                       plain / "class" / "Part.png");
            {
                std::ofstream manifest(plain / "theme.json", std::ios::binary | std::ios::trunc);
                manifest << R"({"id":"plain","fallback":"class.Part","icons":{"class.Part":"class/Part.png"}})";
            }

            luaug::app::IconAtlas plainAtlas;
            CHECK(plainAtlas.load(*device, *cmd, plain.parent_path().parent_path(), {}));
            CHECK(plainAtlas.has(luaug::app::icons::ClassPart));
            CHECK(plainAtlas.find(luaug::app::icons::ClassPart, 16u).valid);
            // Nothing to tint with, so the caller uses its own foreground --
            // which is what every caller did before the palette existed.
            CHECK_FALSE(
                plainAtlas.tintFor(luaug::app::icons::ClassPart, luaug::app::IconAtlas::Panel::Dark).has_value());
            plainAtlas.destroy(*device);

            std::error_code plainError;
            std::filesystem::remove_all(plain.parent_path().parent_path(), plainError);
        }
        else {
            MESSAGE("no staged icon theme beside this test binary; the atlas was not exercised");
        }

        if (swapchain.texture.valid()) {
            const std::array<luaug::rhi::ColorAttachment, 1> colors{luaug::rhi::ColorAttachment{
                .texture = swapchain.texture,
                .loadOp = luaug::rhi::LoadOp::Clear,
                .storeOp = luaug::rhi::StoreOp::Store,
            }};
            cmd->beginRenderPass({.colorAttachments = colors, .debugName = "clear"});
            cmd->endRenderPass();

            overlay.render(*cmd, swapchain.texture, Frame{.index = 1, .renderDt = 1.0 / 60.0});

            // Every instance in turn, so each of the fixture's ValueTypes goes
            // through its own widget rather than only the root's. A type whose
            // editor crashed or left the ID stack unbalanced would show up
            // here and nowhere else.
            std::vector<luaug::app::TreeRow> rows;
            luaug::app::collectTree(inspected.world, inspected.root, rows);
            for (const luaug::app::TreeRow& row : rows) {
                inspector.select(row.id);
                overlay.render(*cmd, swapchain.texture, Frame{.index = 2, .renderDt = 1.0 / 60.0});
            }
        }
        else {
            // Said out loud rather than passing quietly: a hidden window that
            // hands out no backbuffer means the draw half of this test did not
            // run, and a silent skip would read as coverage it does not have.
            MESSAGE("no swapchain for a hidden window on this machine; the overlay draw was not exercised");
        }
        atlas.destroy(*device);
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
