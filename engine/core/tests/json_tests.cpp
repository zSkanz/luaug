#include <cmath>
#include <doctest/doctest.h>
// doctest prints a failing comparison through operator<<, and the accessors
// here hand back std::string_view -- which json.h has no reason to make
// streamable on its own.
#include "luaug/core/json.h"

#include <ostream>
#include <string>

using luaug::core::JsonDocument;
using luaug::core::JsonType;
using luaug::core::JsonValue;
using luaug::core::usize;

namespace {

JsonDocument parseOrFail(std::string_view json)
{
    JsonDocument document;
    const JsonDocument::ParseResult result = document.parse(json, "test");
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
    return document;
}

std::string diagnosticOf(std::string_view json)
{
    JsonDocument document;
    const JsonDocument::ParseResult result = document.parse(json, "test");
    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(result.diagnostic.empty());
    // The source name is half of what makes a diagnostic actionable when three
    // manifests fail on the same byte.
    REQUIRE(result.diagnostic.find("test:") == 0);
    return result.diagnostic;
}

// The offset a diagnostic names, so a test can assert *where* the parser
// stopped rather than only that it did.
usize offsetIn(const std::string& diagnostic)
{
    const std::string marker = "at byte ";
    const usize at = diagnostic.rfind(marker);
    REQUIRE_MESSAGE(at != std::string::npos, diagnostic);
    return static_cast<usize>(std::stoull(diagnostic.substr(at + marker.size())));
}

std::string repeated(usize count, std::string_view open, std::string_view close, std::string_view middle = "")
{
    std::string text;
    for (usize i = 0; i < count; ++i)
        text.append(open);
    text.append(middle);
    for (usize i = 0; i < count; ++i)
        text.append(close);
    return text;
}

} // namespace

TEST_CASE("every value type round-trips")
{
    const JsonDocument document = parseOrFail(R"({
        "string": "text",
        "number": -12.5,
        "integer": 42,
        "yes": true,
        "no": false,
        "nothing": null,
        "array": [1, "two", false, null, {"deep": true}],
        "object": {"nested": {"more": [[]]}}
    })");
    const JsonValue root = document.root();

    REQUIRE(root.type() == JsonType::Object);
    CHECK(root.size() == 8);

    CHECK(root["string"].asString() == "text");
    CHECK(root["number"].asNumber() == doctest::Approx(-12.5));
    CHECK(root["integer"].asInteger() == 42);
    CHECK(root["yes"].asBool());
    // The fallback is the opposite of the stored value, so a `false` that came
    // from the document is distinguishable from one that came from the default.
    CHECK_FALSE(root["no"].asBool(true));

    // A null member is present but null; a missing one is neither.
    CHECK(root["nothing"].type() == JsonType::Null);
    CHECK(static_cast<bool>(root["nothing"]));
    CHECK_FALSE(static_cast<bool>(root["absent"]));
    CHECK(root["absent"].isNull());

    const JsonValue array = root["array"];
    REQUIRE(array.type() == JsonType::Array);
    REQUIRE(array.size() == 5);
    CHECK(array.at(0).asInteger() == 1);
    CHECK(array.at(1).asString() == "two");
    CHECK_FALSE(array.at(2).asBool(true));
    CHECK(array.at(3).type() == JsonType::Null);
    CHECK(array.at(4)["deep"].asBool());

    CHECK(root["object"]["nested"]["more"].at(0).type() == JsonType::Array);
    CHECK(root["object"]["nested"]["more"].at(0).size() == 0);
}

TEST_CASE("object key order is preserved as written")
{
    // The property a golden-file comparison depends on: iteration order is the
    // file's order, not a hash order.
    const JsonDocument document = parseOrFail(R"({"zulu": 1, "alpha": 2, "mike": 3})");
    const JsonValue root = document.root();

    REQUIRE(root.size() == 3);
    CHECK(root.keyAt(0) == "zulu");
    CHECK(root.keyAt(1) == "alpha");
    CHECK(root.keyAt(2) == "mike");

    // keyAt(i) and at(i) name the same member.
    CHECK(root.at(0).asInteger() == 1);
    CHECK(root.at(1).asInteger() == 2);
    CHECK(root.at(2).asInteger() == 3);
}

TEST_CASE("duplicate keys: the last one wins, and both stay visible")
{
    const JsonDocument document = parseOrFail(R"({"k": "first", "k": "second"})");
    const JsonValue root = document.root();

    CHECK(root["k"].asString() == "second");
    // Nothing is dropped, so a caller for whom a duplicate is an error -- the
    // i18n catalog is one -- can still find it by iterating.
    REQUIRE(root.size() == 2);
    CHECK(root.keyAt(0) == "k");
    CHECK(root.keyAt(1) == "k");
    CHECK(root.at(0).asString() == "first");
}

TEST_CASE("numbers")
{
    SUBCASE("exponent forms")
    {
        const JsonDocument document = parseOrFail(R"([1e3, 1E+3, 1e-3, -2.5e2, 0.5])");
        const JsonValue root = document.root();

        CHECK(root.at(0).asNumber() == doctest::Approx(1000.0));
        CHECK(root.at(1).asNumber() == doctest::Approx(1000.0));
        CHECK(root.at(2).asNumber() == doctest::Approx(0.001));
        CHECK(root.at(3).asNumber() == doctest::Approx(-250.0));
        CHECK(root.at(4).asNumber() == doctest::Approx(0.5));
    }

    SUBCASE("negative zero keeps its sign")
    {
        const JsonDocument document = parseOrFail("-0");
        CHECK(document.root().asNumber() == 0.0);
        CHECK(std::signbit(document.root().asNumber()));
        CHECK(document.root().asInteger() == 0);
    }

    SUBCASE("asInteger accepts a whole value however it was written")
    {
        const JsonDocument document = parseOrFail(R"([2, 2.0, 2e3, -7])");
        const JsonValue root = document.root();

        CHECK(root.at(0).asInteger() == 2);
        CHECK(root.at(1).asInteger() == 2);
        CHECK(root.at(2).asInteger() == 2000);
        CHECK(root.at(3).asInteger() == -7);
    }

    SUBCASE("asInteger refuses a fractional value rather than truncating")
    {
        const JsonDocument document = parseOrFail("[1.5, -0.5, 1e-3]");
        const JsonValue root = document.root();

        CHECK(root.at(0).asInteger(-1) == -1);
        CHECK(root.at(1).asInteger(-1) == -1);
        CHECK(root.at(2).asInteger(-1) == -1);
        // The value itself is still there; only the integer reading is refused.
        CHECK(root.at(0).asNumber() == doctest::Approx(1.5));
    }

    SUBCASE("the i64 boundary")
    {
        const JsonDocument document = parseOrFail(R"([9223372036854775807, -9223372036854775808,
                                                     9223372036854775808, 123456789012345678901234567890])");
        const JsonValue root = document.root();

        // Written whole and in range: every digit survives, because the token is
        // converted to an i64 directly rather than through a double.
        CHECK(root.at(0).asInteger() == 9223372036854775807LL);
        CHECK(root.at(1).asInteger() == -9223372036854775807LL - 1);

        // One past the top, and far past it: still valid JSON numbers, but not
        // integers any i64 can hold, so the fallback rather than a wrapped value.
        CHECK(root.at(2).type() == JsonType::Number);
        CHECK(root.at(2).asInteger(-1) == -1);
        CHECK(root.at(3).asInteger(-1) == -1);
        CHECK(root.at(3).asNumber() > 1.0e29);
    }
}

TEST_CASE("nothing is coerced")
{
    const JsonDocument document = parseOrFail(R"({"text": "42", "count": 42, "flag": true})");
    const JsonValue root = document.root();

    // A digit string is a string. Reading it as a number is the typo-shaped
    // failure this reader exists to make visible.
    CHECK(root["text"].asNumber(-1.0) == doctest::Approx(-1.0));
    CHECK(root["text"].asInteger(-1) == -1);
    CHECK(root["text"].asBool(true));

    CHECK(root["count"].asString("fallback") == "fallback");
    CHECK(root["count"].asBool(true));

    CHECK(root["flag"].asNumber(-1.0) == doctest::Approx(-1.0));
    CHECK(root["flag"].asInteger(-1) == -1);
    CHECK(root["flag"].asString("fallback") == "fallback");
}

TEST_CASE("accessors are total")
{
    const JsonDocument document = parseOrFail(R"({"array": [1], "scalar": 7})");
    const JsonValue root = document.root();

    SUBCASE("a missing key yields a null value, not a crash")
    {
        const JsonValue missing = root["nope"];
        CHECK(missing.isNull());
        CHECK_FALSE(static_cast<bool>(missing));
        CHECK(missing.size() == 0);
        CHECK(missing.asString("fallback") == "fallback");
        CHECK(missing.at(0).isNull());
        CHECK(missing["deeper"].isNull());
        CHECK(missing.keyAt(0).empty());
    }

    SUBCASE("an out-of-range index yields a null value")
    {
        CHECK(root["array"].at(0).asInteger() == 1);
        CHECK(root["array"].at(1).isNull());
        CHECK(root["array"].at(9999).isNull());
    }

    SUBCASE("container accessors on a scalar are empty rather than undefined")
    {
        const JsonValue scalar = root["scalar"];
        CHECK(scalar.size() == 0);
        CHECK(scalar.at(0).isNull());
        CHECK_FALSE(scalar.has("anything"));
        CHECK(scalar["anything"].isNull());
        CHECK(scalar.keyAt(0).empty());
    }

    SUBCASE("has() answers only for objects")
    {
        CHECK(root.has("array"));
        CHECK_FALSE(root.has("nope"));
        CHECK_FALSE(root["array"].has("0"));
    }

    SUBCASE("a default-constructed value and an unparsed document are null")
    {
        CHECK(JsonValue{}.isNull());
        CHECK(JsonValue{}.asNumber(3.0) == doctest::Approx(3.0));

        const JsonDocument fresh;
        CHECK(fresh.root().isNull());
        CHECK_FALSE(static_cast<bool>(fresh.root()));
    }
}

TEST_CASE("strings decode escapes")
{
    SUBCASE("the two-character escapes")
    {
        const JsonDocument document = parseOrFail(R"({"k": "a\"b\\c\nd\te\/f\r\b\f"})");
        CHECK(document.root()["k"].asString() == "a\"b\\c\nd\te/f\r\b\f");
    }

    SUBCASE("\\u escapes, including a surrogate pair")
    {
        // Written as escapes rather than literal UTF-8: a literal character
        // would pass straight through and prove nothing about the \u path.
        const JsonDocument document = parseOrFail("{\"bmp\": \"\\u00e9\", \"astral\": \"\\ud83d\\ude00\"}");
        CHECK(document.root()["bmp"].asString() == "\xC3\xA9");            // U+00E9
        CHECK(document.root()["astral"].asString() == "\xF0\x9F\x98\x80"); // U+1F600
    }

    SUBCASE("an embedded NUL is a byte, not a terminator")
    {
        const JsonDocument document = parseOrFail("{\"k\": \"a\\u0000b\"}");
        const std::string_view text = document.root()["k"].asString();
        REQUIRE(text.size() == 3);
        CHECK(text[1] == '\0');
    }
}

TEST_CASE("malformed documents are rejected, and the diagnostic names the offset")
{
    struct Case
    {
        const char* json;
        usize offset;
        const char* what;
    };

    // Each `offset` is the byte the parser stopped on -- the whole point of
    // owning this reader rather than reporting "invalid JSON".
    const Case cases[] = {
        {"", 0, "an empty document"},
        {"   \n\t ", 6, "whitespace only"},
        {"[1, 2] extra", 7, "trailing content after a complete value"},
        {"{\"a\": 1,}", 8, "trailing comma in an object"},
        {"[1,]", 3, "trailing comma in an array"},
        {"[1,,2]", 3, "an elided array element"},
        {"{'a': 1}", 1, "a single-quoted key"},
        {"[/*comment*/1]", 1, "a comment where a value belongs"},
        {"[1] // comment", 4, "a comment after the document"},
        {"NaN", 0, "NaN"},
        {"Infinity", 0, "Infinity"},
        {"-Infinity", 1, "negative Infinity"},
        {"tru", 0, "a truncated literal"},
        {"{\"a\" 1}", 5, "a missing colon"},
        {"{\"a\": 1", 7, "an unterminated object"},
        {"[1", 2, "an unterminated array"},
        {"{1: 2}", 1, "an unquoted key"},
        {"01", 1, "a leading zero"},
        {"1.5.2", 3, "a second decimal point"},
        {".5", 0, "a missing integer part"},
        {"5.", 2, "a missing fraction"},
        {"1e", 2, "a missing exponent"},
        {"1e999", 0, "an exponent that overflows a double"},
        {"\"abc", 4, "an unterminated string"},
        {"\"a\nb\"", 2, "a raw control character in a string"},
        {"\"\\x\"", 3, "an unsupported escape"},
        {"\"\\u00g0\"", 5, "a bad hex digit"},
        {"\"\\ud83d\"", 7, "a lone high surrogate"},
        {"\"\\udc00\"", 7, "a lone low surrogate"},
        {"\"\\ud83d\\u0041\"", 13, "a high surrogate followed by a non-surrogate"},
    };

    for (const Case& testCase : cases) {
        const std::string diagnostic = diagnosticOf(testCase.json);
        CHECK_MESSAGE(offsetIn(diagnostic) == testCase.offset, testCase.what << " -> " << diagnostic);
    }
}

TEST_CASE("a failed parse leaves the document empty rather than half-built")
{
    JsonDocument document;
    REQUIRE(document.parse(R"({"k": 1})", "test").ok);
    CHECK(document.root()["k"].asInteger() == 1);

    CHECK_FALSE(document.parse("{ bad", "test").ok);
    CHECK(document.root().isNull());

    // And the same document parses again afterwards.
    REQUIRE(document.parse(R"({"k": 2})", "test").ok);
    CHECK(document.root()["k"].asInteger() == 2);
}

TEST_CASE("nesting is bounded")
{
    SUBCASE("reasonable nesting parses")
    {
        const JsonDocument arrays = parseOrFail(repeated(64, "[", "]"));
        const JsonValue root = arrays.root();
        CHECK(root.type() == JsonType::Array);

        JsonValue level = root;
        for (usize depth = 1; depth < 64; ++depth) {
            REQUIRE(level.size() == 1);
            level = level.at(0);
        }
        CHECK(level.size() == 0);

        const JsonDocument objects = parseOrFail(repeated(64, R"({"a":)", "}", "1"));
        CHECK(objects.root()["a"].type() == JsonType::Object);
    }

    SUBCASE("a pathological document is refused, not survived by luck")
    {
        // Built here rather than written out: the interesting depth is far
        // beyond anything a literal could carry, and this is the input a corrupt
        // or hostile file would supply.
        const std::string diagnostic = diagnosticOf(repeated(5000, "[", "]"));
        CHECK(diagnostic.find("nesting") != std::string::npos);

        const std::string objectDiagnostic = diagnosticOf(repeated(5000, R"({"a":)", "}", "1"));
        CHECK(objectDiagnostic.find("nesting") != std::string::npos);
    }
}

TEST_CASE("a document owns its strings")
{
    // The contract stated in json.h: values are views into the document, and
    // the decoded text must survive every string in it, escapes included.
    JsonDocument document;
    {
        const std::string json = R"({"a": "\u00e9one", "b": "two"})";
        REQUIRE(document.parse(json, "test").ok);
    }

    CHECK(document.root()["a"].asString() == "\xC3\xA9one");
    CHECK(document.root()["b"].asString() == "two");
}

TEST_CASE("a UTF-8 byte-order mark is skipped rather than refused")
{
    // **Notepad writes one by default**, and so does Windows PowerShell's
    // `Out-File` for UTF-8, and so does Visual Studio. Every engine JSON a
    // person edits by hand can therefore arrive with three bytes in front of
    // it, and a strict reader -- which is not WRONG, RFC 8259 leaves this to the
    // implementation -- turns that into a file that parses as nothing and is
    // ignored in silence.
    JsonDocument document;
    const std::string withBom = std::string("\xEF\xBB\xBF") + R"({"scale":1.25,"theme":"dark"})";

    const JsonDocument::ParseResult parsed = document.parse(withBom, "appearance.json");
    REQUIRE_MESSAGE(parsed.ok, parsed.diagnostic);
    CHECK(document.root()["scale"].asNumber() == doctest::Approx(1.25));
    CHECK(document.root()["theme"].asString() == "dark");
}

TEST_CASE("a byte-order mark anywhere but the start is still what it was")
{
    // Only the very beginning, and only the UTF-8 spelling. A BOM in the middle
    // of a document is a zero-width no-break space in somebody's data, and
    // skipping one there would be editing what they wrote.
    JsonDocument document;
    const std::string inside = std::string(R"({"name":")") + "\xEF\xBB\xBF" + R"(a"})";
    REQUIRE(document.parse(inside, "inside.json").ok);
    CHECK(document.root()["name"].asString().size() == 4);

    // And a document that is nothing but a mark is still not a document: what
    // was skipped is a prefix, not a value.
    CHECK_FALSE(document.parse("\xEF\xBB\xBF", "empty.json").ok);
}
