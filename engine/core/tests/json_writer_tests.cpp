// The JSON writer (ADR 0033 addendum). What it produces has to be readable by
// the reader beside it and by whatever the CLI uses, so the round trip through
// `JsonDocument` is the assertion that matters.

#include <doctest/doctest.h>

// doctest stringifies a failed comparison through `operator<<`, and a
// `string_view` has no declaration for one without this. Without it the failure
// is a page of template errors inside <string_view> rather than a test.
#include <ostream>

#include <limits>
#include <string>

#include "luaug/core/json.h"
#include "luaug/core/json_writer.h"

using luaug::core::JsonDocument;
using luaug::core::jsonQuote;
using luaug::core::JsonWriter;

TEST_CASE("an object writes its commas itself")
{
    JsonWriter writer;
    writer.beginObject();
    writer.field("type", "reloaded");
    writer.field("id", static_cast<luaug::core::u64>(7));
    writer.field("ok", true);
    writer.endObject();

    CHECK(writer.text() == R"({"type":"reloaded","id":7,"ok":true})");
}

TEST_CASE("an empty object and an empty array carry no comma at all")
{
    JsonWriter writer;
    writer.beginObject();
    writer.key("empty");
    writer.beginObject();
    writer.endObject();
    writer.key("none");
    writer.beginArray();
    writer.endArray();
    writer.endObject();

    CHECK(writer.text() == R"({"empty":{},"none":[]})");
}

TEST_CASE("nesting keeps each container's own comma state")
{
    // The bug this pins is one shared "have I written something" flag: the
    // inner container closing would leave the outer one thinking it had.
    JsonWriter writer;
    writer.beginObject();
    writer.key("paths");
    writer.beginArray();
    writer.value("a.luau");
    writer.value("b.luau");
    writer.endArray();
    writer.field("count", static_cast<luaug::core::i64>(2));
    writer.endObject();

    CHECK(writer.text() == R"({"paths":["a.luau","b.luau"],"count":2})");
}

TEST_CASE("the escapes JSON requires, and no others")
{
    CHECK(jsonQuote("plain") == R"("plain")");
    CHECK(jsonQuote("say \"hi\"") == R"("say \"hi\"")");
    CHECK(jsonQuote("back\\slash") == R"("back\\slash")");
    CHECK(jsonQuote("line\nbreak") == R"("line\nbreak")");
    // A control character is the one thing JSON forbids raw, and the
    // six-byte escape is the only spelling the format has for one. Written
    // without a raw string on purpose: the expectation is a BACKSLASH
    // followed by u0007, and a raw string cannot tell that from the byte.
    CHECK(jsonQuote(std::string("bell\x07")) == "\"bell\\u0007\"");

    // UTF-8 passes through byte for byte. Re-encoding it into \u escapes would
    // be legal JSON and would make every catalog message unreadable in a diff.
    CHECK(jsonQuote("olá") == "\"olá\"");
}

TEST_CASE("a number that JSON cannot spell becomes null rather than garbage")
{
    // `inf` and `nan` are what the C library prints and neither is JSON. Null
    // is the one answer every parser accepts, and it is honest.
    JsonWriter writer;
    writer.beginObject();
    writer.field("finite", 0.5);
    writer.field("infinite", std::numeric_limits<luaug::core::f64>::infinity());
    writer.endObject();

    JsonDocument document;
    const auto parsed = document.parse(writer.text());
    REQUIRE_MESSAGE(parsed.ok, parsed.diagnostic);
}

TEST_CASE("what the writer writes, the reader reads")
{
    JsonWriter writer;
    writer.beginObject();
    writer.field("type", "sample");
    writer.field("tick", static_cast<luaug::core::u64>(600));
    writer.field("hash", "0123456789abcdef");
    writer.field("ms", 12.25);
    writer.key("paths");
    writer.beginArray();
    writer.value("src/scripts/main.luau");
    writer.value("src/shared/util.luau");
    writer.endArray();
    writer.endObject();

    JsonDocument document;
    const auto parsed = document.parse(writer.text());
    REQUIRE_MESSAGE(parsed.ok, parsed.diagnostic);

    const auto root = document.root();
    CHECK(root["type"].asString() == "sample");
    CHECK(root["tick"].asNumber() == 600.0);
    CHECK(root["ms"].asNumber() == 12.25);
    CHECK(root["paths"].size() == 2);
    CHECK(root["paths"].at(1).asString() == "src/shared/util.luau");
}
