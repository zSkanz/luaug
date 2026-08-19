#include <doctest/doctest.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "luaug/core/error.h"
#include "luaug/core/log.h"

using luaug::core::Catalog;
using luaug::core::EngineError;
using luaug::core::engineCatalog;
using luaug::core::I18nArg;
using luaug::core::LogLevel;

namespace
{

// Each case seeds the process-wide catalog itself rather than relying on
// another test having done so, so the file has no hidden ordering.
void seedEngineCatalog()
{
    const Catalog::LoadResult result = engineCatalog().loadFromJson(R"({
        "assets.err.not_found": "Asset not found: {content}.",
        "engine.boot.hello": "LuauG {version} - engine initialized."
    })",
        "test");
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

// The sink holds a reference to a local; if a REQUIRE aborted the test case
// before an explicit reset, the next test would call through to freed memory.
struct ScopedLogSink
{
    explicit ScopedLogSink(luaug::core::LogSink sink) { luaug::core::setLogSink(std::move(sink)); }
    ~ScopedLogSink() { luaug::core::resetLogSink(); }

    ScopedLogSink(const ScopedLogSink&) = delete;
    ScopedLogSink& operator=(const ScopedLogSink&) = delete;
};

} // namespace

TEST_CASE("engine errors are identified by key, not prose")
{
    seedEngineCatalog();

    SUBCASE("the message is prefixed with the key so tests can match on it")
    {
        const std::array<I18nArg, 1> args{I18nArg{"content", "asset://models/tree.glb"}};
        const EngineError error = luaug::core::makeError(LUAUG_TR("assets.err.not_found"), args);

        CHECK(error.key == LUAUG_TR("assets.err.not_found"));
        CHECK(error.message == "[assets.err.not_found] Asset not found: asset://models/tree.glb.");
    }

    SUBCASE("detail is carried verbatim and never localised")
    {
        const EngineError error =
            luaug::core::makeError(LUAUG_TR("assets.err.not_found"), {}, "stack traceback:\n  boot.luau:1");
        CHECK(error.detail == "stack traceback:\n  boot.luau:1");
    }

    SUBCASE("an unregistered key still yields a traceable prefix")
    {
        const std::string message = luaug::core::formatKeyPrefixed(LUAUG_TR("no.such.key"));
        CHECK(message.find("i18n:missing") != std::string::npos);
    }
}

TEST_CASE("logging routes through the catalog")
{
    seedEngineCatalog();

    std::vector<std::pair<LogLevel, std::string>> captured;
    const ScopedLogSink guard([&](LogLevel level, std::string_view text)
        { captured.emplace_back(level, std::string{text}); });

    SUBCASE("a keyed message is formatted before reaching the sink")
    {
        const std::array<I18nArg, 1> args{I18nArg{"version", "0.0.1"}};
        luaug::core::log(LogLevel::Info, LUAUG_TR("engine.boot.hello"), args);

        REQUIRE(captured.size() == 1);
        CHECK(captured[0].first == LogLevel::Info);
        CHECK(captured[0].second == "LuauG 0.0.1 - engine initialized.");
    }

    SUBCASE("script-authored text passes through untranslated")
    {
        // Text originating in Luau must not be looked up as a key.
        luaug::core::logText(LogLevel::Info, "hello from boot.luau");

        REQUIRE(captured.size() == 1);
        CHECK(captured[0].second == "hello from boot.luau");
    }
}
