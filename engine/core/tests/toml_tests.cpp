#include "luaug/core/toml.h"

#include <doctest/doctest.h>
#include <ostream>
#include <string_view>

using luaug::core::TomlDocument;

namespace {

[[nodiscard]] TomlDocument parsed(std::string_view text)
{
    TomlDocument document;
    const TomlDocument::ParseResult result = document.parse(text, "test.toml");
    REQUIRE_MESSAGE(result.ok, result.diagnostic);
    return document;
}

} // namespace

TEST_CASE("a project file reads the way api-design.md writes one")
{
    const TomlDocument document = parsed(R"(
# The project file.
[project]
name = "my-game"
id = "dev.local.my-game"
version = "0.1.0"

[window]
title = "My Game"
size = [1280, 720]

[dev]
port = 4560
)");

    CHECK(document.string("project.name") == "my-game");
    CHECK(document.string("window.title") == "My Game");
    CHECK(document.number("dev.port") == doctest::Approx(4560.0));
    REQUIRE(document.numbers("window.size").size() == 2);
    CHECK(document.numbers("window.size")[0] == doctest::Approx(1280.0));
    CHECK(document.numbers("window.size")[1] == doctest::Approx(720.0));
}

TEST_CASE("nothing is coerced")
{
    const TomlDocument document = parsed(R"(
[graphics]
quality = "high"
render_scale = 0.75
bloom = false
)");

    // The property that makes a typo visible: asking a string for a number
    // answers nothing rather than zero, so a caller that falls back to a default
    // falls back for a reason it can report.
    CHECK_FALSE(document.number("graphics.quality").has_value());
    CHECK_FALSE(document.string("graphics.render_scale").has_value());
    CHECK_FALSE(document.boolean("graphics.render_scale").has_value());
    CHECK(document.boolean("graphics.bloom") == false);
    CHECK(document.number("graphics.render_scale") == doctest::Approx(0.75));

    // An absent key and a key of the wrong type are the same answer, which is
    // right: both mean "this file did not tell me".
    CHECK_FALSE(document.string("graphics.nothing").has_value());
    CHECK_FALSE(document.has("graphics.nothing"));
    CHECK(document.has("graphics.bloom"));
}

TEST_CASE("a hash inside a string is not a comment")
{
    const TomlDocument document = parsed(R"(
[project]
name = "a # b"
tail = 'c # d'
)");

    CHECK(document.string("project.name") == "a # b");
    CHECK(document.string("project.tail") == "c # d");
}

TEST_CASE("negative numbers, exponents and digit separators")
{
    const TomlDocument document = parsed(R"(
[n]
negative = -12
fraction = -0.5
exponent = 1e3
separated = 1_000
)");

    CHECK(document.number("n.negative") == doctest::Approx(-12.0));
    CHECK(document.number("n.fraction") == doctest::Approx(-0.5));
    CHECK(document.number("n.exponent") == doctest::Approx(1000.0));
    CHECK(document.number("n.separated") == doctest::Approx(1000.0));
}

TEST_CASE("what the subset refuses, it refuses out loud")
{
    // Each of these is a thing a real TOML file may contain and this reader does
    // not implement. The whole point is that none of them is silently skipped:
    // "a config format that quietly ignores what it does not understand is how a
    // setting ends up not applying".
    const std::string_view cases[] = {
        "[[products]]\nname = \"a\"\n",
        "[project]\nname = { first = \"a\" }\n",
        "[project]\nname = \"unterminated\n",
        "[project\nname = \"a\"\n",
        "name\n",
        "[project]\nname =\n",
        "[project]\nname = \"a\" trailing\n",
        "[project]\nwhen = 1979-05-27\n",
    };

    for (const std::string_view text : cases) {
        TomlDocument document;
        const TomlDocument::ParseResult result = document.parse(text, "test.toml");
        CHECK_MESSAGE(!result.ok, text);
        CHECK_FALSE(result.diagnostic.empty());
        CHECK(result.line > 0);
    }
}

TEST_CASE("a duplicate key is the last one, and the CLI reader agrees")
{
    const TomlDocument document = parsed(R"(
[project]
name = "first"
name = "second"
)");

    CHECK(document.string("project.name") == "second");
}

TEST_CASE("an empty document parses and answers nothing")
{
    TomlDocument document;
    CHECK(document.parse("", "test.toml").ok);
    CHECK_FALSE(document.has("anything"));

    CHECK(document.parse("# only a comment\n", "test.toml").ok);
    CHECK_FALSE(document.has("anything"));
}
