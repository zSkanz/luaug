// What is worth asserting about the overlay is not what it draws -- it draws a
// developer panel, and pixels of text are the screenshot gate's business, not a
// unit test's. It is the contract around it: that it is inert wherever there is
// nothing to draw with, that being inert costs the frame loop nothing, and that
// F3 is what turns it on where there is.
//
// And what the shell DECIDES, which is a different thing from what it draws.
// The one case below that builds the shell needs a display and a GPU device,
// and where either is missing it returns before it asserts -- so a check
// written inside it reports a pass it never ran. Every decision this shell
// makes with arithmetic rather than with pixels is therefore a function, and
// the cases at the foot of this file call those on any machine at all.
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
#include <string_view>
#include <vector>

#include "icon_ids.gen.h"
#include "inspector_fixture.h"

using luaug::app::DebugOverlay;
using luaug::app::EditorCommands;
using luaug::app::EditorDialogs;
using luaug::app::Frame;
using luaug::app::Inspector;
using luaug::core::EngineError;

// **Declared here rather than included from `debug_overlay.h`.** Both are
// defined in `debug_overlay.cpp`, outside the anonymous namespace the rest of
// that file lives in, and the header is where the declarations belong -- the
// change that added them owned the shell's source and this suite and not its
// header. `inspector.h` carries `collectTree` for exactly this reason and is
// the shape to move these into.
namespace luaug::app {

// Ask, or act: the one gate in front of every verb that empties the scene.
void issueOrAsk(EditorDialogs::Pending what, bool unsavedWork, std::string_view scene, EditorDialogs& dialogs,
                EditorCommands& commands);

// How tall the Console's log child is, given the room the panel has left.
[[nodiscard]] luaug::core::f32 consoleLogHeight(luaug::core::f32 available, luaug::core::f32 reservedBelow,
                                                luaug::core::f32 minimum) noexcept;

} // namespace luaug::app

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
    luaug::core::InstanceId zulu;
    luaug::core::InstanceId alpha;
    luaug::core::InstanceId leaf;
    // A class unrelated to the other three, so that a selection spanning both
    // has an intersection to take rather than a superset to draw.
    luaug::core::InstanceId gadget;

    InspectedWorld()
    {
        root = schema.widget(world, "Root");
        zulu = schema.widget(world, "Zulu");
        alpha = schema.widget(world, "Alpha");
        leaf = schema.widget(world, "Leaf");
        gadget = schema.gadget(world, "Gadget");
        static_cast<void>(world.setParent(zulu, root));
        static_cast<void>(world.setParent(alpha, root));
        static_cast<void>(world.setParent(leaf, zulu));
        static_cast<void>(world.setParent(gadget, root));

        // A reference for the Instance widget to render and offer to follow.
        static_cast<void>(world.setProperty(zulu, schema.atom("Link"), luaug::scene::Value{alpha}));

        // **Deliberately different from Alpha's**, so that a selection of the
        // two takes the MIXED branch of every widget that has one -- the
        // checkbox's indeterminate square, the drags that hide their number,
        // the text field that starts empty. Those branches exist only for a
        // selection of more than one and are otherwise executed by nothing.
        static_cast<void>(world.setProperty(zulu, schema.atom("Flag"), luaug::scene::Value{true}));
        static_cast<void>(world.setProperty(zulu, schema.atom("Count"), luaug::scene::Value{luaug::core::f64{7.0}}));
        static_cast<void>(world.setProperty(zulu, schema.atom("Label"), luaug::scene::Value{std::string("zulu")}));
        static_cast<void>(
            world.setProperty(zulu, schema.atom("Offset"), luaug::scene::Value{luaug::core::Vec3{1.0f, 2.0f, 3.0f}}));
        static_cast<void>(
            world.setProperty(zulu, schema.atom("Tint"), luaug::scene::Value{luaug::core::Color3{1.0f, 0.0f, 0.0f}}));
        static_cast<void>(world.setProperty(zulu, schema.atom("Mood"),
                                            luaug::scene::Value{luaug::scene::EnumValue{schema.moodEnum, 7}}));
        // M6's screen-space four, which nothing in this suite drew until the
        // `UDim` widget was rewritten and the gap turned up.
        static_cast<void>(
            world.setProperty(zulu, schema.atom("Anchor"), luaug::scene::Value{luaug::core::Vec2{0.5f, 0.5f}}));
        static_cast<void>(
            world.setProperty(zulu, schema.atom("Pad"), luaug::scene::Value{luaug::core::UDim{0.25f, 12.0f}}));
        static_cast<void>(world.setProperty(
            zulu, schema.atom("Extent"),
            luaug::scene::Value{luaug::core::UDim2{luaug::core::UDim{1.0f, -8.0f}, luaug::core::UDim{0.0f, 40.0f}}}));
        static_cast<void>(world.setProperty(
            zulu, schema.atom("Slice"),
            luaug::scene::Value{luaug::core::Rect{luaug::core::Vec2{1.0f, 2.0f}, luaug::core::Vec2{3.0f, 4.0f}}}));
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

    // Shutdown last, after the device and the window that outlive it in
    // declaration order (D147). The backend here destroys nothing, so this is
    // the shape rather than a live fault -- and the shape is what bit.
    struct PlatformScope
    {
        ~PlatformScope() { luaug::platform::shutdown(); }
    } platformScope;

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
}

TEST_CASE("visibility is off until something asks for it")
{
    seedRealCatalog();

    const auto initError = luaug::platform::init({.headless = true});
    REQUIRE_MESSAGE(!initError.has_value(), (initError ? initError->detail : std::string{}));

    // Shutdown last, after the device and the window that outlive it in
    // declaration order (D147). The backend here destroys nothing, so this is
    // the shape rather than a live fault -- and the shape is what bit.
    struct PlatformScope
    {
        ~PlatformScope() { luaug::platform::shutdown(); }
    } platformScope;

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
}

// The path that needs a driver AND a display: a real SDL_GPU device with a
// claimed window is the only configuration where the overlay is not inert.
// Skipped where either is missing, for the reason the rhi tests give -- a red
// build meaning "this runner has no GPU" is a check nobody can act on.
// **The `LUAUG_TEST_SKIP` token in every message below is load-bearing**
// (S7.8, S7.9), and it is the one this repository already uses for exactly this.
// This case is registered with CTest a second time on its own, as `editor_shell`
// with a `SKIP_REGULAR_EXPRESSION` matching that token, so a machine with no
// display reports SKIPPED instead of passed. It reported passed for the whole of
// v1: the Linux tier has a Vulkan device through lavapipe and no display at all,
// so the one test that enters the shell returned green there without entering
// it, and the summary said nothing.
TEST_CASE("on a real device, F3 flips the panel")
{
    seedRealCatalog();

    if (const auto initError = luaug::platform::init({.headless = false}); initError.has_value()) {
        MESSAGE("LUAUG_TEST_SKIP: no display on this machine: " << initError->detail);
        return;
    }

    // **Declaration order is shutdown order reversed**, which is the idiom both
    // `run` and `runLauncher` document and this case did not have. SDL_GPU needs
    // the window released from its device before the window dies, and the device
    // needs SDL still standing when IT dies. Calling `shutdown()` by hand at the
    // end did neither: `window` and `device` are locals declared after it, so
    // they were destroyed after SDL was already gone.
    //
    // It crashed in SDL3's own `VULKAN_DestroyDevice`, on Linux only, and only
    // once this case was given a display to run on at all -- for the whole of v1
    // the Linux tier had a Vulkan device and no screen, so the case returned at
    // the first line and reported a pass (S7.8, D147).
    struct PlatformScope
    {
        ~PlatformScope() { luaug::platform::shutdown(); }
    } platformScope;

    EngineError error;
    // **Visible, and that is the whole difference between drawing the shell and
    // not** (S7.8). A hidden window gets a backbuffer on Windows and gets none
    // under lavapipe, so `.visible = false` -- which was here to keep a window
    // from flashing during a test run -- silently cost the Linux tier every
    // assertion past the icon atlas. It is 320x200 for a tenth of a second, and
    // buying the entire ImGui recording path a second driver and a second
    // compiler with validation on is worth a flash.
    const auto window = luaug::platform::createWindow(
        {.titleKey = LUAUG_TR("platform.window.title"), .width = 320, .height = 200, .visible = true}, &error);
    if (window == nullptr) {
        MESSAGE("LUAUG_TEST_SKIP: no window on this machine: " << error.detail);
        return;
    }

    const luaug::rhi::DeviceResult device =
        luaug::app::createDevice({.backend = luaug::rhi::BackendId::SdlGpu, .debug = true}, &error);
    if (device == nullptr) {
        MESSAGE("LUAUG_TEST_SKIP: no GPU device on this machine: " << error.detail);
        return;
    }
    if (!device->claimWindow(*window)) {
        MESSAGE("LUAUG_TEST_SKIP: the device would not claim a window on this machine");
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
        std::size_t shellFrames = 0;

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
            // **The add menu leans on exactly this.** It draws `class.<Name>`
            // for every creatable class without asking whether the theme has
            // one, because this fallback is what makes the answer always a
            // picture -- and a menu with a hole in one row is worse than a menu
            // with no pictures at all.
            CHECK(atlas.find("class.Part", 16u).valid);
            CHECK(atlas.find("class.Folder", 16u).valid);
            CHECK(atlas.has(luaug::app::icons::ClassPart));
            // The six that were drawn a day later than the rest. They were
            // ABSENT from the theme rather than present and broken, which is
            // what let the loader ship before them and what makes this a
            // one-line change now they exist.
            CHECK(atlas.has(luaug::app::icons::ActionDelete));
            CHECK(atlas.has(luaug::app::icons::ActionSave));
            // **The badge is two ids and needs both**, because it is two draws:
            // a solid silhouette that punches a hole and the same silhouette
            // with the mark cut out. One without the other is a badge that
            // disappears on 37 of the 42 class icons.
            CHECK(atlas.has(luaug::app::icons::OverlayStamp));
            CHECK(atlas.has(luaug::app::icons::OverlayStampBase));
            CHECK(atlas.find(luaug::app::icons::OverlayStampBase, 16u).valid);
            // The theme's own numbers, read rather than assumed.
            CHECK(static_cast<double>(atlas.overlay().scale) == doctest::Approx(0.40));

            // --- Falling back UP the hierarchy ----------------------------
            //
            // **A leaf class does not owe the art set a drawing.** A project
            // that writes `MyDoor extends Part` should see a part, not the
            // generic instance -- which is the icon for "I have no idea what
            // this is", and the tree does have an idea.
            {
                luaug::core::AtomTable atoms;
                luaug::scene::ClassRegistry classes;
                const luaug::scene::ClassId partClass =
                    classes.registerClass({.name = atoms.intern("Part"), .defaultName = atoms.intern("Part")});
                const luaug::scene::ClassId doorClass = classes.registerClass(
                    {.name = atoms.intern("MyDoor"), .super = partClass, .defaultName = atoms.intern("MyDoor")});
                // Nothing in between draws either, so the walk has to pass
                // through a class the theme has never heard of.
                const luaug::scene::ClassId trapClass =
                    classes.registerClass({.name = atoms.intern("MyTrapDoor"),
                                           .super = doorClass,
                                           .defaultName = atoms.intern("MyTrapDoor")});

                CHECK(luaug::app::classIconFor(&atlas, classes, atoms, partClass) == "class.Part");
                CHECK(luaug::app::classIconFor(&atlas, classes, atoms, doorClass) == "class.Part");
                CHECK(luaug::app::classIconFor(&atlas, classes, atoms, trapClass) == "class.Part");

                // A class with nothing drawn anywhere above it lands on the
                // generic instance, which is the honest end of the walk.
                const luaug::scene::ClassId loneClass =
                    classes.registerClass({.name = atoms.intern("MyThing"), .defaultName = atoms.intern("MyThing")});
                CHECK(luaug::app::classIconFor(&atlas, classes, atoms, loneClass) == luaug::app::icons::ClassInstance);

                // From a NAME, which is what a stamp file carries. A name this
                // build has no class for keeps its own id: a theme may draw a
                // class a plugin adds, and the atlas falls back if it does not.
                CHECK(luaug::app::classIconFor(&atlas, &classes, &atoms, "MyDoor") == "class.Part");
                CHECK(luaug::app::classIconFor(&atlas, &classes, &atoms, "NotAClassHere") == "class.NotAClassHere");
                // And with no registry at all -- the F3 overlay over a running
                // game hands one panel a world and another none.
                CHECK(luaug::app::classIconFor(&atlas, nullptr, nullptr, "Part") == "class.Part");
            }
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
            // **A theme with no `overlay` block draws the badge at the
            // documented defaults**, which is what makes the numbers living in
            // data rather than in code safe: a plugin that only recolours does
            // not have to restate the badge's geometry to keep it.
            CHECK(static_cast<double>(plainAtlas.overlay().scale) == doctest::Approx(0.40));
            CHECK(static_cast<double>(plainAtlas.overlay().haloScale) == doctest::Approx(1.22));
            CHECK(plainAtlas.overlay().corner == luaug::app::IconAtlas::Overlay::Corner::BottomRight);
            CHECK(plainAtlas.has(luaug::app::icons::ClassPart));
            CHECK(plainAtlas.find(luaug::app::icons::ClassPart, 16u).valid);
            // Nothing to tint with, so the caller uses its own foreground --
            // which is what every caller did before the palette existed.
            CHECK_FALSE(
                plainAtlas.tintFor(luaug::app::icons::ClassPart, luaug::app::IconAtlas::Panel::Dark).has_value());
            plainAtlas.destroy(*device);

            std::error_code plainError;
            std::filesystem::remove_all(plain.parent_path().parent_path(), plainError);

            // --- And a theme that MOVES it ------------------------------
            //
            // The other half of the same claim: a badge whose geometry is data
            // is a badge a plugin can move, and a test that only proved the
            // defaults would prove the constants were reachable rather than
            // that they were read.
            const std::filesystem::path moved =
                std::filesystem::temp_directory_path() / "luaug-moved-badge" / "icons" / "default";
            std::filesystem::remove_all(moved.parent_path().parent_path());
            std::filesystem::create_directories(moved / "class");
            std::filesystem::copy_file(iconRoot / "icons" / "default" / "class" / "Part.png",
                                       moved / "class" / "Part.png");
            {
                std::ofstream manifest(moved / "theme.json", std::ios::binary | std::ios::trunc);
                manifest << R"({"id":"moved","fallback":"class.Part",)"
                         << R"("overlay":{"scale":0.6,"haloScale":1.5,"corner":"top-left"},)"
                         << R"("icons":{"class.Part":"class/Part.png"}})";
            }

            luaug::app::IconAtlas movedAtlas;
            CHECK(movedAtlas.load(*device, *cmd, moved.parent_path().parent_path(), {}));
            CHECK(static_cast<double>(movedAtlas.overlay().scale) == doctest::Approx(0.6));
            CHECK(static_cast<double>(movedAtlas.overlay().haloScale) == doctest::Approx(1.5));
            CHECK(movedAtlas.overlay().corner == luaug::app::IconAtlas::Overlay::Corner::TopLeft);
            movedAtlas.destroy(*device);

            std::error_code movedError;
            std::filesystem::remove_all(moved.parent_path().parent_path(), movedError);
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

            // **And the panel over more than one**, which is a different set of
            // branches in every widget it draws and in the sweep above them.
            // Two instances of one class that disagree about every property
            // takes the mixed path; a selection spanning two unrelated classes
            // takes the intersection, which is a shorter table with a
            // read-only row in it. Neither is reachable by selecting one thing.
            const std::array<luaug::core::InstanceId, 2> pair{inspected.zulu, inspected.alpha};
            inspector.select(pair);
            overlay.render(*cmd, swapchain.texture, Frame{.index = 3, .renderDt = 1.0 / 60.0});

            const std::array<luaug::core::InstanceId, 2> across{inspected.zulu, inspected.gadget};
            inspector.select(across);
            overlay.render(*cmd, swapchain.texture, Frame{.index = 4, .renderDt = 1.0 / 60.0});
            shellFrames += 4 + rows.size();

            // **The claim, stated as an assertion instead of as a comment.**
            // Everything above draws and asserts nothing, so a refactor that
            // returned early anywhere inside this block would leave a test that
            // passes having entered the shell zero times -- which is the exact
            // failure the skip token exists to make visible, arriving through
            // the one door that token does not cover.
            CHECK(shellFrames >= 4);
        }
        else {
            // Said out loud rather than passing quietly: a hidden window that
            // hands out no backbuffer means the draw half of this test did not
            // run, and a silent skip would read as coverage it does not have.
            MESSAGE("LUAUG_TEST_SKIP: no swapchain for a hidden window; the overlay draw was not exercised");
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
}

// --- The decisions, on any machine ------------------------------------------

// **Five verbs lead to the same loss, and a person learns the rule from the
// doors that knock.** File > New Scene, opening another scene, making a
// project, leaving for one, and quitting all throw away every edit since the
// last save. The toolbar's New was a sixth door onto the second of them and it
// acted on the spot, so the one door a hand reaches for without opening a menu
// was the one that never asked.
//
// A table rather than one case, because the claim is about the SET: a verb
// added tomorrow is a row here, and a row that fails is a door wired past the
// gate.
TEST_CASE("every verb that empties the scene asks before it acts")
{
    const std::array<EditorDialogs::Pending, 5> doors{
        EditorDialogs::Pending::Quit,       EditorDialogs::Pending::NewScene,    EditorDialogs::Pending::OpenScene,
        EditorDialogs::Pending::NewProject, EditorDialogs::Pending::OpenProject,
    };

    for (const EditorDialogs::Pending door : doors) {
        CAPTURE(static_cast<int>(door));

        EditorDialogs dialogs;
        EditorCommands commands;
        luaug::app::issueOrAsk(door, /*unsavedWork=*/true, "levels/one.scene.json", dialogs, commands);

        // Parked as a question, and nothing issued: the frame loop must see no
        // command at all this frame, whichever door was used.
        CHECK(dialogs.pending == door);
        CHECK_FALSE(commands.quit);
        CHECK_FALSE(commands.newScene);
        CHECK_FALSE(commands.newProject);
        CHECK_FALSE(commands.openProject);
        CHECK(commands.openScene.empty());
    }
}

// The other half of the same rule, and the reason the gate is not simply a
// dialog: a question with nothing behind it is one people learn to dismiss
// without reading, so a clean scene acts immediately.
TEST_CASE("with nothing to lose, the same verbs act at once")
{
    const auto issued = [](EditorDialogs::Pending door) {
        EditorDialogs dialogs;
        EditorCommands commands;
        luaug::app::issueOrAsk(door, /*unsavedWork=*/false, "levels/one.scene.json", dialogs, commands);
        CHECK(dialogs.pending == EditorDialogs::Pending::None);
        CHECK(dialogs.pendingScene.empty());
        return commands;
    };

    CHECK(issued(EditorDialogs::Pending::Quit).quit);
    CHECK(issued(EditorDialogs::Pending::NewScene).newScene);
    CHECK(issued(EditorDialogs::Pending::NewProject).newProject);
    CHECK(issued(EditorDialogs::Pending::OpenProject).openProject);
    CHECK(issued(EditorDialogs::Pending::OpenScene).openScene == "levels/one.scene.json");
}

// The scene a double-click asked for has to survive the question, because the
// answer arrives frames later and by then the browser is looking elsewhere.
TEST_CASE("the scene an open was asking for is carried across the question")
{
    EditorDialogs dialogs;
    EditorCommands commands;
    luaug::app::issueOrAsk(EditorDialogs::Pending::OpenScene, /*unsavedWork=*/true, "levels/two.scene.json", dialogs,
                           commands);
    CHECK(dialogs.pending == EditorDialogs::Pending::OpenScene);
    CHECK(dialogs.pendingScene == "levels/two.scene.json");

    // And the answer re-issues it through the same gate, which is what makes
    // the question one place rather than five.
    luaug::app::issueOrAsk(dialogs.pending, /*unsavedWork=*/false, dialogs.pendingScene, dialogs, commands);
    CHECK(commands.openScene == "levels/two.scene.json");
}

// **The Console is a docked, resizable panel, so its log has to be the part
// that grows.** It was a child of a fixed 160 px, which meant dragging the
// panel taller added empty room UNDER the log rather than showing more of it --
// the one thing making a console bigger is ever for.
TEST_CASE("the console's log takes the room the panel has, less the line below it")
{
    // One REPL line and its spacing sit under the log; everything else the
    // panel has is the log's.
    const luaug::core::f32 repl = 26.0f;
    const luaug::core::f32 floorHeight = 160.0f;

    CHECK(static_cast<double>(luaug::app::consoleLogHeight(600.0f, repl, floorHeight)) == doctest::Approx(574.0));

    // Enlarging the panel enlarges the LOG, one pixel for one pixel. This is
    // the assertion the fixed height fails.
    CHECK(static_cast<double>(luaug::app::consoleLogHeight(900.0f, repl, floorHeight)) == doctest::Approx(874.0));
    CHECK(static_cast<double>(luaug::app::consoleLogHeight(900.0f, repl, floorHeight) -
                              luaug::app::consoleLogHeight(600.0f, repl, floorHeight)) == doctest::Approx(300.0));

    // **A floor, and it is not tidiness.** The same console is drawn at the
    // foot of the F3 overlay's scrolling window, where what is left can be a
    // few pixels or none -- and ImGui reads a non-positive child height as
    // "fill the rest, less this much", so an unclamped subtraction would make
    // the log grow as the room for it shrank.
    CHECK(static_cast<double>(luaug::app::consoleLogHeight(40.0f, repl, floorHeight)) == doctest::Approx(160.0));
    CHECK(static_cast<double>(luaug::app::consoleLogHeight(0.0f, repl, floorHeight)) == doctest::Approx(160.0));
    CHECK(static_cast<double>(luaug::app::consoleLogHeight(-120.0f, repl, floorHeight)) == doctest::Approx(160.0));
}
