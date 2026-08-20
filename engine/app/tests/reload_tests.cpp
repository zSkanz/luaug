// The fast world restart (ADR 0024 + addendum, M3 brief Decisions 11 and 14).
//
// The claim under test is the one the whole model rests on: a reloaded world is
// indistinguishable from one that never reloaded. Anything weaker -- "the hash
// changed" -- passes against a reload that half-applied the edit.

#include <doctest/doctest.h>

#include <memory>
#include <string>

#include "project_fixture.h"

#include "luaug/app/reload.h"
#include "luaug/core/i18n.h"

using namespace luaug;
using luaug::app::testing::bootOptions;
using luaug::app::testing::Captured;
using luaug::app::testing::Project;

namespace
{

// A script whose observable result is entirely decided by `marker`, so two
// worlds built from two different values of it cannot hash the same.
std::string scriptNamed(std::string_view marker)
{
    return std::string(R"(
        local root = Instance.new("Folder")
        root.Name = ")")
        + std::string(marker) + R"("
        root.Parent = workspace

        local run = game:GetService("RunService")
        local ticks = 0
        run.Heartbeat:Connect(function()
            ticks += 1
            local part = Instance.new("Part")
            part.Name = ")" + std::string(marker) + R"(-" .. tostring(ticks)
            part.Position = vector.create(ticks, 0, 0)
            part.Parent = root
        end)
    )";
}

std::unique_ptr<app::WorldHost> bootHost(const Project& project)
{
    auto host = std::make_unique<app::WorldHost>();
    REQUIRE_FALSE(host->boot(bootOptions(project.root)).has_value());
    return host;
}

void tickTimes(app::WorldHost& host, int count)
{
    for (int i = 0; i < count; ++i)
        host.tick();
}

// The hash of a world built cold from `project` and ticked `count` times --
// the reference a reloaded world has to match.
core::u64 hashOfColdBoot(const Project& project, int count)
{
    auto host = std::make_unique<app::WorldHost>();
    REQUIRE_FALSE(host->boot(bootOptions(project.root)).has_value());
    tickTimes(*host, count);
    return host->world().worldHash();
}

} // namespace

TEST_CASE("a reloaded world is indistinguishable from one that never reloaded")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", scriptNamed("first"));

    constexpr int kTicks = 30;

    std::unique_ptr<app::WorldHost> host = bootHost(project);
    tickTimes(*host, kTicks);
    const core::u64 beforeEdit = host->world().worldHash();

    project.write("src/scripts/main.luau", scriptNamed("second"));

    const app::ReloadReport report = app::reloadWorld(host, bootOptions(project.root));
    if (report.error.has_value())
        FAIL(report.error->message);
    REQUIRE(report.ok);
    CHECK(report.mountedScripts == 1);

    tickTimes(*host, kTicks);
    const core::u64 afterReload = host->world().worldHash();

    // The whole claim, in one line: reload equals restart.
    CHECK(afterReload == hashOfColdBoot(project, kTicks));

    // And the edit actually took. Without this the assertion above would hold
    // just as well against a reload that did nothing at all.
    CHECK(afterReload != beforeEdit);
}

TEST_CASE("the tick counter and the instance ids restart with the world")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", scriptNamed("same"));

    std::unique_ptr<app::WorldHost> host = bootHost(project);
    tickTimes(*host, 12);
    const core::u64 firstRun = host->world().worldHash();

    // Nothing edited: reloading the same source must land on the same world as
    // the run that just happened, which is only true if a reload really is a
    // restart rather than a continuation.
    const app::ReloadReport report = app::reloadWorld(host, bootOptions(project.root));
    REQUIRE(report.ok);

    tickTimes(*host, 12);
    CHECK(host->world().worldHash() == firstRun);
}

TEST_CASE("a project that will not mount leaves the world that was running alone")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", scriptNamed("survivor"));

    std::unique_ptr<app::WorldHost> host = bootHost(project);
    tickTimes(*host, 5);
    const app::WorldHost* before = host.get();
    const core::u64 hashBefore = host->world().worldHash();

    // The common case in a loop whose point is that you save often.
    project.write("src/scripts/main.luau", "this is not valid Luau ((");

    const app::ReloadReport report = app::reloadWorld(host, bootOptions(project.root));
    CHECK_FALSE(report.ok);
    REQUIRE(report.error.has_value());
    CHECK(report.error->key.hash == LUAUG_TR("engine.reload.err.script_failed").hash);
    CHECK(report.loadFailures == 1);

    // Same object, same world, and it still runs.
    CHECK(host.get() == before);
    CHECK(host->world().worldHash() == hashBefore);
    tickTimes(*host, 5);
    CHECK(host->world().worldHash() != hashBefore);
}

TEST_CASE("one broken script among several still refuses the whole reload")
{
    // The single-script case is caught by the emptiness check as well; this is
    // the one that needs the compile count, because nine of the ten scripts
    // mounted and started perfectly well.
    Captured log;
    Project project;
    project.write("src/scripts/a.luau", scriptNamed("alpha"));
    project.write("src/scripts/b.luau", scriptNamed("beta"));

    std::unique_ptr<app::WorldHost> host = bootHost(project);
    tickTimes(*host, 4);
    const app::WorldHost* before = host.get();

    project.write("src/scripts/b.luau", "local x = ((");

    const app::ReloadReport report = app::reloadWorld(host, bootOptions(project.root));
    CHECK_FALSE(report.ok);
    CHECK(report.mountedScripts == 2);
    CHECK(report.loadFailures == 1);
    CHECK(host.get() == before);
}

TEST_CASE("a project that stopped containing scripts is refused, not swapped in")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", scriptNamed("present"));

    std::unique_ptr<app::WorldHost> host = bootHost(project);
    tickTimes(*host, 3);
    const app::WorldHost* before = host.get();

    std::error_code ec;
    std::filesystem::remove_all(project.root / "src" / "scripts", ec);

    const app::ReloadReport report = app::reloadWorld(host, bootOptions(project.root));
    CHECK_FALSE(report.ok);
    CHECK(report.mountedScripts == 0);
    REQUIRE(report.error.has_value());
    CHECK(report.error->key.hash == LUAUG_TR("engine.reload.err.no_scripts").hash);
    CHECK(host.get() == before);
}

TEST_CASE("a world with no project at all still reloads")
{
    // `--version` and the render gates boot one of these. Refusing it as empty
    // would make the emptiness check reject the one case that is meant to be.
    Captured log;
    auto host = std::make_unique<app::WorldHost>();
    REQUIRE_FALSE(host->boot({}).has_value());

    const app::ReloadReport report = app::reloadWorld(host, {});
    CHECK(report.ok);
    CHECK(report.mountedScripts == 0);
}

TEST_CASE("the reload reports the span its budget is measured against")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", scriptNamed("timed"));

    std::unique_ptr<app::WorldHost> host = bootHost(project);
    const app::ReloadReport report = app::reloadWorld(host, bootOptions(project.root));

    REQUIRE(report.ok);
    // Not a performance assertion -- ADR 0024's 500 ms budget is gated on a
    // real project by the M3 gate, not on a one-file fixture here. What this
    // pins is that the number is measured and reported at all, because a
    // budget nobody reports is a budget nobody checks.
    CHECK(report.spanMs > 0.0);
    CHECK(report.spanMs < 5000.0);
}
