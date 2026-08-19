#include <doctest/doctest.h>

#include <array>
#include <string>

#include "luaug/core/i18n.h"

using luaug::core::Catalog;
using luaug::core::i64;
using luaug::core::I18nArg;

namespace
{

Catalog loadOrFail(std::string_view json)
{
    Catalog catalog;
    const Catalog::LoadResult result = catalog.loadFromJson(json, "test");
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
    return catalog;
}

} // namespace

TEST_CASE("catalog parsing")
{
    SUBCASE("loads flat entries and ignores $-prefixed metadata")
    {
        const Catalog catalog = loadOrFail(R"({
            "$comment": "not a message",
            "engine.boot.hello": "LuauG {version} initialized.",
            "engine.boot.shutdown": "Shutting down."
        })");

        CHECK(catalog.size() == 2);
        CHECK(catalog.contains(LUAUG_TR("engine.boot.hello")));
        CHECK_FALSE(catalog.contains(LUAUG_TR("$comment")));
        CHECK(catalog.keyName(LUAUG_TR("engine.boot.shutdown")) == "engine.boot.shutdown");
    }

    SUBCASE("accepts an empty catalog")
    {
        const Catalog catalog = loadOrFail("{}");
        CHECK(catalog.size() == 0);
    }

    SUBCASE("decodes escapes")
    {
        const Catalog catalog = loadOrFail(R"({"k": "a\"b\\c\nd\te\/f"})");
        CHECK(catalog.format(LUAUG_TR("k")) == "a\"b\\c\nd\te/f");
    }

    SUBCASE("decodes unicode escapes including surrogate pairs")
    {
        // Written with escaped backslashes rather than as literal UTF-8: a
        // literal character would pass straight through the parser and prove
        // nothing about the \u path.
        const Catalog catalog = loadOrFail("{\"bmp\": \"\\u00e9\", \"astral\": \"\\ud83d\\ude00\"}");
        CHECK(catalog.format(LUAUG_TR("bmp")) == "\xC3\xA9");            // U+00E9
        CHECK(catalog.format(LUAUG_TR("astral")) == "\xF0\x9F\x98\x80"); // U+1F600
    }

    SUBCASE("reports malformed input instead of silently accepting it")
    {
        struct Case
        {
            const char* json;
            const char* what;
        };
        const std::array<Case, 5> cases{{
            {"[]", "top level must be an object"},
            {R"({"k": 1})", "numbers are not catalog values"},
            {R"({"k": "v",})", "trailing comma"},
            {R"({"k": "unterminated})", "unterminated string"},
            {R"({"k": "v"} extra)", "trailing content"},
        }};

        for (const Case& testCase : cases)
        {
            Catalog catalog;
            const Catalog::LoadResult result = catalog.loadFromJson(testCase.json, "test");
            CHECK_MESSAGE(!result.ok, testCase.what);
            CHECK_FALSE(result.diagnostic.empty());
        }
    }

    SUBCASE("a failed load leaves the previous contents intact")
    {
        Catalog catalog = loadOrFail(R"({"k": "original"})");
        const Catalog::LoadResult result = catalog.loadFromJson("{ bad", "test");
        CHECK_FALSE(result.ok);
        CHECK(catalog.format(LUAUG_TR("k")) == "original");
    }
}

TEST_CASE("catalog formatting")
{
    const Catalog catalog = loadOrFail(R"({
        "greeting": "LuauG {version} - engine initialized.",
        "twice": "{a} then {b} then {a}",
        "plain": "no placeholders"
    })");

    SUBCASE("substitutes named placeholders")
    {
        const std::array<I18nArg, 1> args{I18nArg{"version", "0.0.1"}};
        CHECK(catalog.format(LUAUG_TR("greeting"), args) == "LuauG 0.0.1 - engine initialized.");
    }

    SUBCASE("substitutes a repeated placeholder every time")
    {
        const std::array<I18nArg, 2> args{I18nArg{"a", "1"}, I18nArg{"b", "2"}};
        CHECK(catalog.format(LUAUG_TR("twice"), args) == "1 then 2 then 1");
    }

    SUBCASE("leaves an unsupplied placeholder visible")
    {
        // A hole in a sentence must look like a bug, not like finished prose.
        CHECK(catalog.format(LUAUG_TR("greeting")) == "LuauG {version} - engine initialized.");
    }

    SUBCASE("ignores extra arguments")
    {
        const std::array<I18nArg, 1> args{I18nArg{"unused", "x"}};
        CHECK(catalog.format(LUAUG_TR("plain"), args) == "no placeholders");
    }

    SUBCASE("formats integer arguments")
    {
        const std::array<I18nArg, 1> args{I18nArg{"version", static_cast<i64>(42)}};
        CHECK(catalog.format(LUAUG_TR("greeting"), args) == "LuauG 42 - engine initialized.");
    }

    SUBCASE("trims trailing zeros from floating point arguments")
    {
        const std::array<I18nArg, 1> args{I18nArg{"version", 1.5}};
        CHECK(catalog.format(LUAUG_TR("greeting"), args) == "LuauG 1.5 - engine initialized.");
    }

    SUBCASE("a missing key yields a visible, traceable marker")
    {
        const std::string out = catalog.format(LUAUG_TR("nope.not.here"));
        CHECK(out.find("i18n:missing") != std::string::npos);
    }
}

TEST_CASE("catalog plurals")
{
    const Catalog catalog = loadOrFail(R"({
        "reloaded": { "one": "Reloaded {count} script.", "other": "Reloaded {count} scripts." }
    })");

    SUBCASE("selects the 'one' category for a count of 1")
    {
        const std::array<I18nArg, 1> args{I18nArg{"count", static_cast<i64>(1)}};
        CHECK(catalog.format(LUAUG_TR("reloaded"), args) == "Reloaded 1 script.");
    }

    SUBCASE("selects 'other' for every other count")
    {
        for (const i64 count : {static_cast<i64>(0), static_cast<i64>(2), static_cast<i64>(17)})
        {
            const std::array<I18nArg, 1> args{I18nArg{"count", count}};
            CHECK(catalog.format(LUAUG_TR("reloaded"), args) == "Reloaded " + std::to_string(count) + " scripts.");
        }
    }

    SUBCASE("falls back to 'other' when no count is supplied")
    {
        CHECK(catalog.format(LUAUG_TR("reloaded")) == "Reloaded {count} scripts.");
    }
}
