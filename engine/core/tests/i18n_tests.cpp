#include "luaug/core/i18n.h"

#include <array>
#include <doctest/doctest.h>
#include <string>

using luaug::core::Catalog;
using luaug::core::I18nArg;
using luaug::core::i64;

namespace {

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

        for (const Case& testCase : cases) {
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
        for (const i64 count : {static_cast<i64>(0), static_cast<i64>(2), static_cast<i64>(17)}) {
            const std::array<I18nArg, 1> args{I18nArg{"count", count}};
            CHECK(catalog.format(LUAUG_TR("reloaded"), args) == "Reloaded " + std::to_string(count) + " scripts.");
        }
    }

    SUBCASE("falls back to 'other' when no count is supplied")
    {
        CHECK(catalog.format(LUAUG_TR("reloaded")) == "Reloaded {count} scripts.");
    }
}

// --- Plural rules beyond English (S6.9) --------------------------------------
//
// **A catalog knew its own text and did not know its own language**, so every
// locale was pluralised by English's rule. That is right for about half the
// languages anybody translates a game into and wrong for the rest in a way a
// translator cannot work around: a catalog that offers `few` and never selects
// it reads as broken grammar to the people it was written for.
//
// The numbers below are the published CLDR cardinal categories, checked at the
// counts where each rule turns over -- which is where a hand-written rule goes
// wrong, and never at 1 and 2.

namespace {

// A catalog for `locale` whose one entry names every category, so the selected
// one is what comes back.
[[nodiscard]] Catalog pluralCatalog(std::string_view locale)
{
    Catalog catalog;
    catalog.setLocale(locale);
    const Catalog::LoadResult loaded = catalog.loadFromJson(R"({
        "count": {
            "zero": "zero",
            "one": "one",
            "two": "two",
            "few": "few",
            "many": "many",
            "other": "other"
        }
    })",
                                                            "<plural>");
    REQUIRE_MESSAGE(loaded.ok, loaded.diagnostic);
    return catalog;
}

[[nodiscard]] std::string categoryOf(const Catalog& catalog, i64 count)
{
    const I18nArg args[] = {{"count", count}};
    return catalog.format(LUAUG_TR("count"), args);
}

} // namespace

TEST_CASE("English is one and other, and so is every language the subset does not name")
{
    const Catalog english = pluralCatalog("en");
    CHECK(categoryOf(english, 0) == "other");
    CHECK(categoryOf(english, 1) == "one");
    CHECK(categoryOf(english, 2) == "other");

    // German, Spanish, Swedish -- the shape most of Europe shares, reached by
    // the fallback rather than by being listed.
    const Catalog german = pluralCatalog("de");
    CHECK(categoryOf(german, 1) == "one");
    CHECK(categoryOf(german, 5) == "other");
}

TEST_CASE("a language with no plural gets one form for every count")
{
    // The most visible of the lot to get wrong: the translator has ONE string
    // and the engine asks for a category that is not in the file.
    const Catalog japanese = pluralCatalog("ja");
    CHECK(categoryOf(japanese, 0) == "other");
    CHECK(categoryOf(japanese, 1) == "other");
    CHECK(categoryOf(japanese, 7) == "other");
}

TEST_CASE("French and Brazilian Portuguese put zero with one")
{
    // "0 jour" rather than "0 jours", which is the one difference from English
    // and the one nobody remembers.
    const Catalog french = pluralCatalog("fr");
    CHECK(categoryOf(french, 0) == "one");
    CHECK(categoryOf(french, 1) == "one");
    CHECK(categoryOf(french, 2) == "other");

    // The region is dropped: `pt-BR` is `pt`.
    const Catalog brazilian = pluralCatalog("pt-BR");
    CHECK(categoryOf(brazilian, 0) == "one");
    CHECK(categoryOf(brazilian, 3) == "other");
}

TEST_CASE("Russian turns over at the last digit and again at the teens")
{
    // The published rule, checked where it turns: 21 is `one` and 11 is not;
    // 22 is `few` and 12 is not. A hand-written rule gets 1 and 2 right and
    // these wrong.
    const Catalog russian = pluralCatalog("ru");
    CHECK(categoryOf(russian, 1) == "one");
    CHECK(categoryOf(russian, 21) == "one");
    CHECK(categoryOf(russian, 11) == "many");
    CHECK(categoryOf(russian, 2) == "few");
    CHECK(categoryOf(russian, 24) == "few");
    CHECK(categoryOf(russian, 12) == "many");
    CHECK(categoryOf(russian, 5) == "many");
    CHECK(categoryOf(russian, 0) == "many");
}

TEST_CASE("Polish is the same shape with a different one")
{
    // 21 is `many` in Polish and `one` in Russian, which is exactly why it is
    // spelled out rather than folded in with the Slavic block.
    const Catalog polish = pluralCatalog("pl");
    CHECK(categoryOf(polish, 1) == "one");
    CHECK(categoryOf(polish, 21) == "many");
    CHECK(categoryOf(polish, 2) == "few");
    CHECK(categoryOf(polish, 22) == "few");
    CHECK(categoryOf(polish, 12) == "many");
    CHECK(categoryOf(polish, 5) == "many");
}

TEST_CASE("Czech has a few that is only two to four")
{
    const Catalog czech = pluralCatalog("cs");
    CHECK(categoryOf(czech, 1) == "one");
    CHECK(categoryOf(czech, 3) == "few");
    CHECK(categoryOf(czech, 5) == "other");
    // And 22 is NOT few, unlike every Slavic language beside it.
    CHECK(categoryOf(czech, 22) == "other");
}

TEST_CASE("Arabic is the six-category case, which is why zero and two exist")
{
    const Catalog arabic = pluralCatalog("ar");
    CHECK(categoryOf(arabic, 0) == "zero");
    CHECK(categoryOf(arabic, 1) == "one");
    CHECK(categoryOf(arabic, 2) == "two");
    CHECK(categoryOf(arabic, 3) == "few");
    CHECK(categoryOf(arabic, 10) == "few");
    CHECK(categoryOf(arabic, 11) == "many");
    CHECK(categoryOf(arabic, 99) == "many");
    CHECK(categoryOf(arabic, 100) == "other");
}

TEST_CASE("a negative count pluralises as its magnitude")
{
    // "-1 item" reading as "-1 items" is the kind of thing nobody notices until
    // a refund appears in a shop.
    const Catalog english = pluralCatalog("en");
    CHECK(categoryOf(english, -1) == "one");
    CHECK(categoryOf(english, -2) == "other");

    const Catalog russian = pluralCatalog("ru");
    CHECK(categoryOf(russian, -21) == "one");
}

TEST_CASE("a category the catalog does not carry falls back rather than showing nothing")
{
    // Which is what makes the subset safe: a rule that selects `few` against a
    // catalog written with only `one` and `other` must still produce text.
    Catalog polish;
    polish.setLocale("pl");
    const Catalog::LoadResult loaded =
        polish.loadFromJson(R"({ "count": { "one": "one", "other": "other" } })", "<partial>");
    REQUIRE_MESSAGE(loaded.ok, loaded.diagnostic);

    const I18nArg args[] = {{"count", i64{3}}};
    CHECK(polish.format(LUAUG_TR("count"), args) == "other");
}
