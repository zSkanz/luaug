#include "luaug/imgcmp/command_line.h"

#include <doctest/doctest.h>
#include <initializer_list>
#include <string_view>
#include <vector>

using luaug::imgcmp::Command;
using luaug::imgcmp::CommandStatus;
using luaug::imgcmp::parseCommandLine;
using luaug::imgcmp::usageText;

namespace {

// argv[0] is already stripped, matching what main() passes.
Command parse(std::initializer_list<std::string_view> args)
{
    const std::vector<std::string_view> owned(args);
    return parseCommandLine(owned);
}

} // namespace

TEST_CASE("two paths and nothing else is the common case")
{
    const Command command = parse({"actual.png", "expected.png"});

    REQUIRE(command.status == CommandStatus::Compare);
    CHECK(command.actualPath == "actual.png");
    CHECK(command.expectedPath == "expected.png");
    CHECK(command.diffPath.empty());

    SUBCASE("the defaults absorb GPU rounding, not a changed image")
    {
        CHECK(command.options.tolerance == 2);
        CHECK(command.options.maxDifferentPixels == 0);
    }
}

TEST_CASE("options are accepted both separated and joined")
{
    SUBCASE("separated")
    {
        const Command command =
            parse({"a.png", "b.png", "--tolerance", "9", "--max-different-pixels", "40", "--diff", "out.png"});

        REQUIRE(command.status == CommandStatus::Compare);
        CHECK(command.options.tolerance == 9);
        CHECK(command.options.maxDifferentPixels == 40);
        CHECK(command.diffPath == "out.png");
    }

    SUBCASE("joined with '='")
    {
        const Command command = parse({"--tolerance=9", "a.png", "--max-different-pixels=40", "b.png", "--diff=o.png"});

        REQUIRE(command.status == CommandStatus::Compare);
        CHECK(command.options.tolerance == 9);
        CHECK(command.options.maxDifferentPixels == 40);
        CHECK(command.diffPath == "o.png");
        CHECK(command.actualPath == "a.png");
        CHECK(command.expectedPath == "b.png");
    }

    SUBCASE("the boundary values are inside the range")
    {
        CHECK(parse({"a.png", "b.png", "--tolerance", "0"}).options.tolerance == 0);
        CHECK(parse({"a.png", "b.png", "--tolerance", "255"}).options.tolerance == 255);
    }
}

TEST_CASE("help is a request, not a mistake")
{
    CHECK(parse({"-h"}).status == CommandStatus::HelpRequested);
    CHECK(parse({"--help"}).status == CommandStatus::HelpRequested);
    CHECK(parse({"a.png", "b.png", "--help"}).status == CommandStatus::HelpRequested);
    CHECK_FALSE(usageText().empty());
}

// Every case here would otherwise run the comparison with defaults nobody asked
// for, and report a pass that means nothing.
TEST_CASE("a malformed command line is refused rather than defaulted")
{
    const auto rejects = [](const Command& command) {
        CHECK(command.status == CommandStatus::UsageError);
        CHECK_FALSE(command.error.empty());
    };

    SUBCASE("missing operands")
    {
        rejects(parse({}));
        rejects(parse({"only-one.png"}));
    }

    SUBCASE("too many operands")
    {
        rejects(parse({"a.png", "b.png", "c.png"}));
    }

    SUBCASE("an unknown option")
    {
        rejects(parse({"a.png", "b.png", "--tolerence", "3"}));
        rejects(parse({"a.png", "b.png", "-t", "3"}));
    }

    SUBCASE("an option with no value")
    {
        rejects(parse({"a.png", "b.png", "--tolerance"}));
        rejects(parse({"a.png", "b.png", "--diff"}));
        rejects(parse({"a.png", "b.png", "--diff="}));
    }

    SUBCASE("a value that is not a whole number")
    {
        rejects(parse({"a.png", "b.png", "--tolerance", "high"}));
        rejects(parse({"a.png", "b.png", "--tolerance", "2.5"}));
        rejects(parse({"a.png", "b.png", "--max-different-pixels", "-1"}));
    }

    SUBCASE("a tolerance outside the channel range")
    {
        rejects(parse({"a.png", "b.png", "--tolerance", "256"}));
        rejects(parse({"a.png", "b.png", "--tolerance", "-1"}));
    }
}
