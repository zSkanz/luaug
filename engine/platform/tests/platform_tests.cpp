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
    SDL_Event unmappedKey{};
    unmappedKey.type = SDL_EVENT_KEY_DOWN;
    unmappedKey.key.scancode = SDL_SCANCODE_A;
    unmappedKey.key.down = true;
    REQUIRE(SDL_PushEvent(&unmappedKey));

    const auto events = luaug::platform::pumpEvents();
    const auto raw = luaug::platform::rawEvents();

    CHECK(std::ranges::any_of(events, [](const auto& e) { return e.type == EventType::Quit; }));
    CHECK(std::ranges::any_of(events, [](const auto& e) { return e.type == EventType::KeyDown && e.key == Key::F3; }));
    CHECK_FALSE(std::ranges::any_of(
        events, [](const auto& e) { return e.type == EventType::KeyDown && e.key == Key::Unknown; }));

    CHECK(std::ranges::any_of(
        raw, [](const SDL_Event& e) { return e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_A; }));
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
