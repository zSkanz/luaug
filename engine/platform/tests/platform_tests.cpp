#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"
#include "luaug/platform/event.h"
#include "luaug/platform/file.h"
#include "luaug/platform/platform.h"
#include "luaug/platform/sdl_interop.h"
#include "luaug/platform/window.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using luaug::core::engineCatalog;
using luaug::core::EngineError;

namespace {

void seedRealCatalog()
{
    const auto result = engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// Every test here runs headless. A CI runner has no display, so asking for a
// real video driver would make these tests pass on the dev machine and fail on
// Linux -- which is the failure mode the offscreen driver exists to remove.
struct HeadlessPlatform
{
    HeadlessPlatform()
    {
        const auto error = luaug::platform::init({.headless = true});
        // Parenthesised: `<<` binds tighter than `?:`, so an unwrapped ternary
        // here is a compile error rather than a message.
        REQUIRE_MESSAGE(!error.has_value(), (error ? error->detail : std::string{}));
    }

    ~HeadlessPlatform() { luaug::platform::shutdown(); }

    HeadlessPlatform(const HeadlessPlatform&) = delete;
    HeadlessPlatform& operator=(const HeadlessPlatform&) = delete;
};

} // namespace

TEST_CASE("paths resolve without bringing up a video subsystem")
{
    // Deliberately no init(): a tool that only wants to find content should not
    // have to open a display connection to do it.
    const auto& paths = luaug::platform::paths();

    CHECK(paths.executableDir.is_absolute());
    CHECK(paths.contentDir == paths.executableDir / "content");
}

TEST_CASE("the clock runs before init")
{
    // Regression guard: SDL's tick clock counts from SDL_Init and reads 0
    // before it, which made this test pass standalone and fail under CTest
    // depending on which test case happened to run first.
    REQUIRE_FALSE(luaug::platform::isInitialized());

    const auto first = luaug::platform::nowNs();
    const auto second = luaug::platform::nowNs();

    CHECK(first > 0);
    CHECK(second >= first);
}

TEST_CASE("init is idempotent and shutdown is safe without it")
{
    CHECK_FALSE(luaug::platform::isInitialized());

    // Safe before any init -- the host's error paths unwind through here.
    luaug::platform::shutdown();

    {
        HeadlessPlatform platform;
        CHECK(luaug::platform::isInitialized());

        const auto again = luaug::platform::init({.headless = true});
        CHECK_FALSE(again.has_value());
        CHECK(luaug::platform::isInitialized());
    }

    CHECK_FALSE(luaug::platform::isInitialized());
}

TEST_CASE("a window cannot be created before init")
{
    seedRealCatalog();
    REQUIRE_FALSE(luaug::platform::isInitialized());

    EngineError error;
    const auto window = luaug::platform::createWindow({.titleKey = LUAUG_TR("platform.window.title")}, &error);

    CHECK(window == nullptr);
    CHECK_FALSE(error.message.empty());
}

TEST_CASE("a window reports an id and a drawable size")
{
    seedRealCatalog();
    HeadlessPlatform platform;

    EngineError error;
    const auto window = luaug::platform::createWindow(
        {.titleKey = LUAUG_TR("platform.window.title"), .width = 640, .height = 360, .visible = false}, &error);

    REQUIRE_MESSAGE(window != nullptr, error.detail);
    CHECK(luaug::platform::windowId(*window) != 0);

    const auto size = luaug::platform::windowPixelSize(*window);
    CHECK(size.width == 640);
    CHECK(size.height == 360);
}

TEST_CASE("the pump translates what the engine models and drops the rest")
{
    using luaug::platform::EventType;
    using luaug::platform::Key;

    HeadlessPlatform platform;
    // Drain whatever bringing up a driver queued.
    static_cast<void>(luaug::platform::pumpEvents());

    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    REQUIRE(SDL_PushEvent(&quit));

    SDL_Event overlayKey{};
    overlayKey.type = SDL_EVENT_KEY_DOWN;
    overlayKey.key.scancode = SDL_SCANCODE_F3;
    overlayKey.key.down = true;
    REQUIRE(SDL_PushEvent(&overlayKey));

    // A key the engine does not model. It must survive in the raw stream --
    // that is the contract sdl_interop.h promises the ImGui backend -- while
    // staying out of the translated one.
    //
    // It was SDL_SCANCODE_A until M6, and by then it had stopped being unmapped:
    // M5 grew `Key` to a whole keyboard, so this case had spent a milestone
    // pushing a key the engine DOES model and asserting nothing. Insert is
    // unmodelled on purpose -- nothing in the surface names it, and if something
    // ever does, this line is where the test says so.
    SDL_Event unmappedKey{};
    unmappedKey.type = SDL_EVENT_KEY_DOWN;
    unmappedKey.key.scancode = SDL_SCANCODE_INSERT;
    unmappedKey.key.down = true;
    REQUIRE(SDL_PushEvent(&unmappedKey));

    const auto events = luaug::platform::pumpEvents();
    const auto raw = luaug::platform::rawEvents();

    CHECK(std::ranges::any_of(events, [](const auto& e) { return e.type == EventType::Quit; }));
    CHECK(std::ranges::any_of(events, [](const auto& e) { return e.type == EventType::KeyDown && e.key == Key::F3; }));
    CHECK_FALSE(std::ranges::any_of(
        events, [](const auto& e) { return e.type == EventType::KeyDown && e.key == Key::Unknown; }));

    CHECK(std::ranges::any_of(
        raw, [](const SDL_Event& e) { return e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_INSERT; }));
}

// --- The device layer (M6) ---------------------------------------------------
//
// The Input Action System binds mouse buttons, wheel notches and gamepad
// buttons and axes as first-class inputs (ADR 0029), so the translation of each
// is a contract rather than a convenience. These cases push synthetic SDL
// events, which is the only way to test a device layer with no device.

TEST_CASE("the pump translates pointer, wheel and text events")
{
    using luaug::platform::EventType;
    using luaug::platform::MouseButton;

    HeadlessPlatform platform;
    static_cast<void>(luaug::platform::pumpEvents());

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 120.0f;
    motion.motion.y = 48.0f;
    motion.motion.xrel = -3.0f;
    motion.motion.yrel = 7.0f;
    REQUIRE(SDL_PushEvent(&motion));

    SDL_Event press{};
    press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    press.button.button = SDL_BUTTON_RIGHT;
    press.button.down = true;
    press.button.x = 120.0f;
    press.button.y = 48.0f;
    REQUIRE(SDL_PushEvent(&press));

    // FLIPPED is what a natural-scrolling trackpad reports, and SDL documents
    // the fix as multiplying by -1. The engine undoes it so that one convention
    // reaches every consumer -- otherwise a binding saved on one machine scrolls
    // the wrong way on another.
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.x = 0.0f;
    wheel.wheel.y = 2.0f;
    wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    REQUIRE(SDL_PushEvent(&wheel));

    const auto events = luaug::platform::pumpEvents();

    const auto moved = std::ranges::find_if(events, [](const auto& e) { return e.type == EventType::MouseMoved; });
    REQUIRE(moved != events.end());
    CHECK(moved->pointerX == doctest::Approx(120.0));
    CHECK(moved->pointerY == doctest::Approx(48.0));
    CHECK(moved->pointerDeltaX == doctest::Approx(-3.0));
    CHECK(moved->pointerDeltaY == doctest::Approx(7.0));

    const auto down = std::ranges::find_if(events, [](const auto& e) { return e.type == EventType::MouseButtonDown; });
    REQUIRE(down != events.end());
    CHECK(down->button == MouseButton::Right);

    const auto scrolled = std::ranges::find_if(events, [](const auto& e) { return e.type == EventType::MouseWheel; });
    REQUIRE(scrolled != events.end());
    CHECK(scrolled->wheelY == doctest::Approx(-2.0));
}

TEST_CASE("a gamepad axis is normalized by what the axis IS")
{
    using luaug::platform::EventType;
    using luaug::platform::GamepadAxis;
    using luaug::platform::GamepadButton;

    HeadlessPlatform platform;
    static_cast<void>(luaug::platform::pumpEvents());

    // A stick at its most negative. SDL's range is -32768..32767, so dividing
    // by the positive half overshoots -1 -- which is why the translation
    // clamps, and why this case pushes the extreme rather than a round number.
    SDL_Event stick{};
    stick.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    stick.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
    stick.gaxis.value = -32768;
    REQUIRE(SDL_PushEvent(&stick));

    // A trigger at rest. Over the same divisor a stick uses this would read
    // -1: a resting input indistinguishable from a fully held one.
    SDL_Event trigger{};
    trigger.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    trigger.gaxis.axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    trigger.gaxis.value = 0;
    REQUIRE(SDL_PushEvent(&trigger));

    SDL_Event button{};
    button.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    button.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    button.gbutton.down = true;
    REQUIRE(SDL_PushEvent(&button));

    // A paddle: a real button on a minority of pads, and deliberately outside
    // the engine's enum. It must be dropped rather than translated to Unknown.
    SDL_Event paddle{};
    paddle.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    paddle.gbutton.button = SDL_GAMEPAD_BUTTON_LEFT_PADDLE1;
    paddle.gbutton.down = true;
    REQUIRE(SDL_PushEvent(&paddle));

    const auto events = luaug::platform::pumpEvents();

    const auto axes =
        std::ranges::count_if(events, [](const auto& e) { return e.type == EventType::GamepadAxisMoved; });
    REQUIRE(axes == 2);

    for (const auto& event : events) {
        if (event.type != EventType::GamepadAxisMoved)
            continue;
        if (event.gamepadAxis == GamepadAxis::LeftX)
            CHECK(event.axisValue == doctest::Approx(-1.0));
        if (event.gamepadAxis == GamepadAxis::LeftTrigger)
            CHECK(event.axisValue == doctest::Approx(0.0));
    }

    CHECK(std::ranges::count_if(events, [](const auto& e) { return e.type == EventType::GamepadButtonDown; }) == 1);
    CHECK(std::ranges::any_of(events, [](const auto& e) {
        return e.type == EventType::GamepadButtonDown && e.gamepadButton == GamepadButton::South;
    }));
}

TEST_CASE("every device enumerator round-trips through its name")
{
    using luaug::platform::GamepadAxis;
    using luaug::platform::GamepadButton;
    using luaug::platform::Key;
    using luaug::platform::MouseButton;

    // The recorded input stream the determinism gate replays is written in
    // these names. A name table that fell behind its enum would produce a trace
    // that reads fine and replays a different key -- so the tables are walked
    // rather than spot-checked, and `Unknown` is required to name nothing.
    for (int raw = 1; raw < static_cast<int>(Key::Count); ++raw) {
        const auto key = static_cast<Key>(raw);
        CAPTURE(raw);
        const std::string_view name = luaug::platform::keyName(key);
        CHECK_FALSE(name.empty());
        CHECK(luaug::platform::keyFromName(name) == key);
    }

    for (int raw = 1; raw < static_cast<int>(MouseButton::Count); ++raw) {
        const auto button = static_cast<MouseButton>(raw);
        CAPTURE(raw);
        const std::string_view name = luaug::platform::mouseButtonName(button);
        CHECK_FALSE(name.empty());
        CHECK(luaug::platform::mouseButtonFromName(name) == button);
    }

    for (int raw = 1; raw < static_cast<int>(GamepadButton::Count); ++raw) {
        const auto button = static_cast<GamepadButton>(raw);
        CAPTURE(raw);
        const std::string_view name = luaug::platform::gamepadButtonName(button);
        CHECK_FALSE(name.empty());
        CHECK(luaug::platform::gamepadButtonFromName(name) == button);
    }

    for (int raw = 1; raw < static_cast<int>(GamepadAxis::Count); ++raw) {
        const auto axis = static_cast<GamepadAxis>(raw);
        CAPTURE(raw);
        const std::string_view name = luaug::platform::gamepadAxisName(axis);
        CHECK_FALSE(name.empty());
        CHECK(luaug::platform::gamepadAxisFromName(name) == axis);
    }

    CHECK(luaug::platform::keyName(Key::Unknown).empty());
    CHECK(luaug::platform::mouseButtonName(MouseButton::Unknown).empty());
    CHECK(luaug::platform::gamepadButtonName(GamepadButton::Unknown).empty());
    CHECK(luaug::platform::gamepadAxisName(GamepadAxis::Unknown).empty());

    // The four namespaces share one spelling space in a recorded trace and in
    // `Enum.KeyCode`, so a collision between them would make a name ambiguous.
    std::vector<std::string_view> names;
    for (int raw = 1; raw < static_cast<int>(Key::Count); ++raw)
        names.push_back(luaug::platform::keyName(static_cast<Key>(raw)));
    for (int raw = 1; raw < static_cast<int>(MouseButton::Count); ++raw)
        names.push_back(luaug::platform::mouseButtonName(static_cast<MouseButton>(raw)));
    for (int raw = 1; raw < static_cast<int>(GamepadButton::Count); ++raw)
        names.push_back(luaug::platform::gamepadButtonName(static_cast<GamepadButton>(raw)));
    for (int raw = 1; raw < static_cast<int>(GamepadAxis::Count); ++raw)
        names.push_back(luaug::platform::gamepadAxisName(static_cast<GamepadAxis>(raw)));

    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
}

// --- platform::readFile ------------------------------------------------------
//
// The seam ShaderLibrary reads content through. On this tier it is an ordinary
// file read; the reason it exists at all is Android, where the same call lands
// on AAssetManager instead -- and that half cannot be tested without a device.
// What IS testable here is the contract every caller relies on: exact bytes,
// including embedded NULs and CR, and a clean false for anything unreadable.

TEST_CASE("readFile returns the file's bytes exactly")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "luaug-platform-readfile.bin";

    // A NUL in the middle and a bare CR: a reader that went through a text-mode
    // FILE* or treated the buffer as a C string would lose one or the other.
    const std::string payload("ab\0cd\r\n", 7);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }

    std::vector<std::byte> bytes;
    REQUIRE(luaug::platform::readFile(path, bytes));
    REQUIRE(bytes.size() == payload.size());
    CHECK(std::memcmp(bytes.data(), payload.data(), payload.size()) == 0);

    std::string text;
    REQUIRE(luaug::platform::readTextFile(path, text));
    CHECK(text == payload);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("readFile fails without touching the caller's buffer")
{
    const std::filesystem::path missing = std::filesystem::temp_directory_path() / "luaug-platform-does-not-exist.bin";

    std::vector<std::byte> bytes{std::byte{0x7f}};
    CHECK_FALSE(luaug::platform::readFile(missing, bytes));
    CHECK(bytes.size() == 1);

    // A directory is not a file. It is worth pinning because the platform
    // layers disagree: opening one succeeds on POSIX and fails on Windows, and
    // a caller that got a zero-byte "success" would report a corrupt shader
    // instead of a missing one.
    std::string text{"kept"};
    CHECK_FALSE(luaug::platform::readTextFile(std::filesystem::temp_directory_path(), text));
    CHECK(text == "kept");
}

// --- Application identity (roadmap M8) ---------------------------------------
//
// This test binary links `branding/luaug.rc`, the same resource `luaug-host`
// carries, precisely so this claim can be made at all: an icon is a property of
// an EXECUTABLE, and no library can be asked whether it has one.

TEST_CASE("an executable can read its own icon back out of itself")
{
    const std::vector<std::byte> icon = luaug::platform::applicationIconBytes();

#if defined(_WIN32)
    // The whole failure this closes is an icon that is wrong and breaks
    // nothing: the program runs, the window opens, and it wears the wrong face.
    // Nothing fails, so nothing reports it -- unless something reads it back.
    REQUIRE_FALSE(icon.empty());

    // Every entry of `branding/icon/luaug.ico` is PNG-compressed, which is not
    // decoration: the window icon is decoded by stb_image, and a BMP-encoded
    // icon entry would need a DIB reader nothing in this engine has. If the
    // artwork is ever regenerated with BMP entries, this is where it says so
    // rather than in a window that quietly has no icon.
    REQUIRE(icon.size() > 8);
    CHECK(static_cast<unsigned char>(icon[0]) == 0x89);
    CHECK(static_cast<char>(icon[1]) == 'P');
    CHECK(static_cast<char>(icon[2]) == 'N');
    CHECK(static_cast<char>(icon[3]) == 'G');
#else
    // Empty rather than an error: a Linux window's icon comes from a `.desktop`
    // entry and a macOS one from the bundle, so there is nothing in the
    // executable to find and that is not a failure.
    CHECK(icon.empty());
#endif
}

TEST_CASE("setting the application id is safe to call with nothing to set")
{
    // Idempotent and total, because it runs before anything else on a path where
    // a project may not name itself. The assertion is that none of these faults.
    luaug::platform::setApplicationId("");
    luaug::platform::setApplicationId("dev.local.luaug-test");
    luaug::platform::setApplicationId("dev.local.luaug-test");
}
