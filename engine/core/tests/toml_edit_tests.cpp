// Changing one value in a project file without rewriting it.
//
// **What can be wrong here is everything the edit did NOT touch.** A writer that
// parsed the file and printed it back would pass any assertion about the value
// it changed while silently deleting the paragraph above it -- and every
// `luaug.toml` in this repository opens with one explaining why the settings in
// it are what they are. So most of what follows asserts absence: the comment is
// still there, the blank line is still there, the key nobody asked about is
// still spelled the way somebody spelled it.
#include "luaug/core/toml.h"
#include "luaug/core/toml_edit.h"

#include <array>
#include <doctest/doctest.h>
#include <string>

using namespace luaug;
using luaug::core::setTomlValue;

namespace {

// A project file with the shape the real ones have: a paragraph, headers,
// trailing comments, a blank line between sections.
constexpr std::string_view kProject = R"(# The flagship's project file, and the first one the ENGINE reads.
#
# Every other example is launched with a path and takes the defaults.

[project]
name = "LuauG Open World"
id = "dev.luaug.open-world"
version = "1.0.0"
scene = "scenes/main.scene.json"

[window]
title = "LuauG - Open World"
size = [1600, 900]

[graphics]
# What this world was AUTHORED against, which is what a graphics section means.
quality = "high"   # not a demand on the player's machine
)";

} // namespace

TEST_CASE("changing a value leaves every other byte where it was")
{
    const std::optional<std::string> out = setTomlValue(kProject, "graphics.quality", core::tomlString("low"));
    REQUIRE(out.has_value());

    // The value changed...
    core::TomlDocument document;
    REQUIRE(document.parse(*out).ok);
    CHECK(std::string(document.string("graphics.quality").value_or("")) == "low");

    // ...and the paragraph, the other sections and the trailing comment did not.
    CHECK(out->find("# The flagship's project file") != std::string::npos);
    CHECK(out->find("# Every other example is launched with a path") != std::string::npos);
    CHECK(out->find("# What this world was AUTHORED against") != std::string::npos);
    CHECK(out->find("# not a demand on the player's machine") != std::string::npos);
    CHECK(out->find("id = \"dev.luaug.open-world\"") != std::string::npos);
    CHECK(out->find("size = [1600, 900]") != std::string::npos);

    // And nothing of the old value survives.
    CHECK(out->find("\"high\"") == std::string::npos);
}

TEST_CASE("the file grows by nothing when the key was already there")
{
    // The property that says this is an edit and not a rewrite: the same number
    // of lines, in the same order.
    const std::optional<std::string> out = setTomlValue(kProject, "project.version", core::tomlString("2.0.0"));
    REQUIRE(out.has_value());

    const auto lines = [](std::string_view text) {
        std::size_t count = 0;
        for (const char c : text)
            count += c == '\n' ? 1u : 0u;
        return count;
    };
    CHECK(lines(*out) == lines(kProject));
}

TEST_CASE("two keys of the same name under different tables are two different settings")
{
    // The failure a flat search would produce, and it would produce it on the
    // first project file that had a `name` in two sections.
    constexpr std::string_view text = R"([project]
name = "the project"

[window]
name = "the window"
)";

    const std::optional<std::string> out = setTomlValue(text, "window.name", core::tomlString("changed"));
    REQUIRE(out.has_value());

    core::TomlDocument document;
    REQUIRE(document.parse(*out).ok);
    CHECK(std::string(document.string("project.name").value_or("")) == "the project");
    CHECK(std::string(document.string("window.name").value_or("")) == "changed");
}

TEST_CASE("a key the file does not have is added under its own table, not at the end")
{
    // A key under the wrong header is a key the reader never finds -- which
    // looks exactly like the setting not applying.
    const std::optional<std::string> out = setTomlValue(kProject, "project.icon", core::tomlString("icon.ico"));
    REQUIRE(out.has_value());

    core::TomlDocument document;
    REQUIRE(document.parse(*out).ok);
    CHECK(std::string(document.string("project.icon").value_or("")) == "icon.ico");
    // Still under `[project]`, so it is before `[window]`.
    CHECK(out->find("icon = ") < out->find("[window]"));
}

TEST_CASE("a table the file does not have is created at the end")
{
    const std::optional<std::string> out = setTomlValue(kProject, "dev.port", core::tomlNumber(4560));
    REQUIRE(out.has_value());

    core::TomlDocument document;
    REQUIRE(document.parse(*out).ok);
    CHECK(document.number("dev.port") == doctest::Approx(4560.0));
    CHECK(out->find("[dev]") != std::string::npos);
    // And nothing that was there moved.
    CHECK(std::string(document.string("graphics.quality").value_or("")) == "high");
}

TEST_CASE("an array is written as one line and reads back as the numbers it was given")
{
    const std::array<core::f64, 2> size{1920.0, 1080.0};
    const std::optional<std::string> out = setTomlValue(kProject, "window.size", core::tomlNumberArray(size));
    REQUIRE(out.has_value());

    core::TomlDocument document;
    REQUIRE(document.parse(*out).ok);
    const std::span<const core::f64> read = document.numbers("window.size");
    REQUIRE(read.size() == 2);
    CHECK(read[0] == doctest::Approx(1920.0));
    CHECK(read[1] == doctest::Approx(1080.0));
}

TEST_CASE("an integral number is written as an integer, so a save is not a diff")
{
    // `size = [1600, 900]` round-tripped as `1600.0` would show up in a diff for
    // a value nobody changed. The reader coerces neither way, so nothing
    // downstream can tell them apart -- which makes this purely about the file
    // staying readable to a person.
    CHECK(core::tomlNumber(1600.0) == "1600");
    CHECK(core::tomlNumber(-7.0) == "-7");
    CHECK(core::tomlNumber(0.5) != "0");
}

TEST_CASE("a string with a quote in it comes back as the string it was")
{
    const std::optional<std::string> out =
        setTomlValue(kProject, "project.name", core::tomlString("a \"quoted\" name\\with a slash"));
    REQUIRE(out.has_value());

    core::TomlDocument document;
    REQUIRE(document.parse(*out).ok);
    CHECK(std::string(document.string("project.name").value_or("")) == "a \"quoted\" name\\with a slash");
}

TEST_CASE("a hash inside a value is not a comment")
{
    // The scan that finds a trailing comment has to know it is inside a string,
    // or a colour written as `#ff0000` loses everything after the hash -- and
    // the file stops parsing.
    constexpr std::string_view text = "[theme]\naccent = \"#204080\"\n";
    const std::optional<std::string> out = setTomlValue(text, "theme.accent", core::tomlString("#801020"));
    REQUIRE(out.has_value());

    core::TomlDocument document;
    REQUIRE(document.parse(*out).ok);
    CHECK(std::string(document.string("theme.accent").value_or("")) == "#801020");
}

TEST_CASE("a commented-out key is not the key")
{
    // `# quality = "low"` is somebody's note about a setting, not the setting.
    // Editing it would change a comment and leave the real value alone, which
    // is the worst of both.
    constexpr std::string_view text = "[graphics]\n# quality = \"low\"\nquality = \"high\"\n";
    const std::optional<std::string> out = setTomlValue(text, "graphics.quality", core::tomlString("medium"));
    REQUIRE(out.has_value());

    CHECK(out->find("# quality = \"low\"") != std::string::npos);
    core::TomlDocument document;
    REQUIRE(document.parse(*out).ok);
    CHECK(std::string(document.string("graphics.quality").value_or("")) == "medium");
}

TEST_CASE("a top-level key lives above every header")
{
    constexpr std::string_view text = "schema = 1\n\n[project]\nname = \"x\"\n";
    const std::optional<std::string> out = setTomlValue(text, "schema", core::tomlNumber(2));
    REQUIRE(out.has_value());

    core::TomlDocument document;
    REQUIRE(document.parse(*out).ok);
    CHECK(document.number("schema") == doctest::Approx(2.0));
    CHECK(std::string(document.string("project.name").value_or("")) == "x");
}

TEST_CASE("the result of an edit still parses, whatever was edited")
{
    // The claim the whole thing rests on: this produces TOML. Asserted over
    // every key in the fixture rather than over one, because the interesting
    // cases are the last line of a table and the last line of the file.
    for (const std::string_view key : {"project.name", "project.scene", "window.title", "window.size",
                                       "graphics.quality", "project.brand.new", "nowhere.at.all"}) {
        const std::optional<std::string> out = setTomlValue(kProject, key, core::tomlString("value"));
        REQUIRE_MESSAGE(out.has_value(), std::string(key));
        core::TomlDocument document;
        const core::TomlDocument::ParseResult result = document.parse(*out);
        CHECK_MESSAGE(result.ok, std::string(key), ": ", result.diagnostic);
    }
}

TEST_CASE("an empty key is refused rather than corrupting the file")
{
    CHECK_FALSE(setTomlValue(kProject, "", "1").has_value());
    CHECK_FALSE(setTomlValue(kProject, "project.", "1").has_value());
}
