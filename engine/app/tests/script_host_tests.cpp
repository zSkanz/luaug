#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "luaug/app/script_host.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"

using luaug::app::ScriptHost;
using luaug::core::EngineError;
using luaug::core::engineCatalog;
using luaug::core::LogLevel;

namespace
{

// Loads the real shipped catalog, so these tests also prove that every key the
// host raises actually exists in i18n/en.json.
void seedRealCatalog()
{
    const auto result = engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
}

struct ScopedLogSink
{
    explicit ScopedLogSink(luaug::core::LogSink sink) { luaug::core::setLogSink(std::move(sink)); }
    ~ScopedLogSink() { luaug::core::resetLogSink(); }

    ScopedLogSink(const ScopedLogSink&) = delete;
    ScopedLogSink& operator=(const ScopedLogSink&) = delete;
};

} // namespace

TEST_CASE("the VM boots sandboxed")
{
    seedRealCatalog();
    ScriptHost host;

    SUBCASE("a script cannot mutate a builtin library table")
    {
        // The M0 gate: environment mutation from a script must fail. This is
        // luaL_sandbox marking every library table readonly (rule R4).
        const std::optional<EngineError> error = host.run("local lib: any = string; lib.rep = nil", "sandbox.luau");

        REQUIRE(error.has_value());
        CHECK(error->key == LUAUG_TR("script.err.runtime"));
    }

    SUBCASE("a script cannot mutate the shared global table")
    {
        const std::optional<EngineError> error = host.run("local g: any = _G; g.leak = 1", "sandbox.luau");
        REQUIRE(error.has_value());
        CHECK(error->key == LUAUG_TR("script.err.runtime"));
    }

    SUBCASE("but a script may write its own globals")
    {
        // Not a leak: luaL_sandboxthread gives each script a private globals
        // table that proxies reads to the readonly originals, so this write is
        // invisible to every other script.
        const std::optional<EngineError> error = host.run("mine = 1", "sandbox.luau");
        CHECK_FALSE(error.has_value());
    }
}

TEST_CASE("script failures surface as structured engine errors")
{
    seedRealCatalog();
    ScriptHost host;

    SUBCASE("a runtime error carries the runtime key and a traceback")
    {
        const std::optional<EngineError> error = host.run("error('boom')", "bad.luau");

        REQUIRE(error.has_value());
        CHECK(error->key == LUAUG_TR("script.err.runtime"));
        // Prefixed with the key so tests and CI match on a stable identifier
        // rather than on prose that translation is free to change.
        CHECK(error->message.rfind("[script.err.runtime]", 0) == 0);
        CHECK(error->message.find("bad.luau") != std::string::npos);
        CHECK(error->message.find("boom") != std::string::npos);
        CHECK_FALSE(error->detail.empty());
    }

    SUBCASE("a syntax error is reported distinctly from a runtime error")
    {
        const std::optional<EngineError> error = host.run("this is not luau", "broken.luau");

        REQUIRE(error.has_value());
        CHECK(error->key == LUAUG_TR("script.err.syntax"));
        CHECK(error->message.rfind("[script.err.syntax]", 0) == 0);
    }

    SUBCASE("a script that succeeds reports no error")
    {
        CHECK_FALSE(host.run("local x = 1 + 1", "ok.luau").has_value());
    }

    SUBCASE("a missing file is reported by key, not by crashing")
    {
        const std::optional<EngineError> error = host.runFile("definitely/not/here.luau");
        REQUIRE(error.has_value());
        CHECK(error->key == LUAUG_TR("engine.cli.err.script_missing"));
    }
}

TEST_CASE("script print is bridged to the engine log verbatim")
{
    seedRealCatalog();

    std::vector<std::string> captured;
    const ScopedLogSink guard([&](LogLevel, std::string_view text) { captured.emplace_back(text); });

    ScriptHost host;

    SUBCASE("text reaches the log unchanged and untranslated")
    {
        // Script-authored text must never be looked up as an i18n key.
        REQUIRE_FALSE(host.run("print('engine.boot.hello')", "print.luau").has_value());

        REQUIRE(captured.size() == 1);
        CHECK(captured[0] == "engine.boot.hello");
    }

    SUBCASE("multiple arguments are tab separated")
    {
        REQUIRE_FALSE(host.run("print('a', 1, true)", "print.luau").has_value());

        REQUIRE(captured.size() == 1);
        CHECK(captured[0] == "a\t1\ttrue");
    }
}
