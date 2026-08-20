// `HotReloadService` and the state bag (ADR 0024, api-design.md §3.2, M3 brief
// Decision 4).
//
// The bag exists because at the moment it matters, the VM that held the value
// is being destroyed. Everything here is about that one sentence: what crosses,
// what refuses to, and what the world on the far side sees.

#include <doctest/doctest.h>

#include <memory>
#include <string>

#include "project_fixture.h"

#include "luaug/app/reload.h"
#include "luaug/core/i18n.h"
#include "luaug/script/reload_state.h"

using namespace luaug;
using luaug::app::testing::bootOptions;
using luaug::app::testing::Captured;
using luaug::app::testing::hasChildNamed;
using luaug::app::testing::Project;

namespace
{

// The bag is owned by the test, which is the whole point of it: it has to
// outlive the `WorldHost` a reload destroys.
struct Session
{
    script::ReloadState bag;
    app::WorldHostOptions options;
    std::unique_ptr<app::WorldHost> host;

    Session(const Project& project, Captured& capturedLog) : options(bootOptions(project.root)), log(capturedLog)
    {
        options.reloadState = &bag;
        host = std::make_unique<app::WorldHost>();
        const auto error = host->boot(options);
        if (error.has_value())
            FAIL(error->message);
    }

    Captured& log;

    void reload()
    {
        const app::ReloadReport report = app::reloadWorld(host, options);
        if (report.error.has_value())
            FAIL(report.error->message);
        REQUIRE(report.ok);
    }
};

} // namespace

TEST_CASE("what PreReload saved is what the next world reads back")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local hot = game:GetService("HotReloadService")

        local carried = hot:LoadState("counter")
        local marker = Instance.new("Folder")
        marker.Name = if carried == nil then "cold" else `carried-{carried}`
        marker.Parent = workspace

        hot.PreReload:Connect(function()
            hot:SaveState("counter", 42)
        end)
    )");

    Session session(project, log);
    CHECK(hasChildNamed(*session.host, "cold"));
    CHECK(session.bag.size() == 0);

    session.reload();

    // The handler ran on the outgoing world, and its value reached the new one.
    CHECK(session.bag.size() == 1);
    CHECK(hasChildNamed(*session.host, "carried-42"));
    CHECK_FALSE(hasChildNamed(*session.host, "cold"));
}

TEST_CASE("IsReload is false on a cold boot and true on the world a reload built")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local hot = game:GetService("HotReloadService")
        local marker = Instance.new("Folder")
        marker.Name = if hot:IsReload() then "reloaded" else "started"
        marker.Parent = workspace
    )");

    Session session(project, log);
    CHECK(hasChildNamed(*session.host, "started"));

    session.reload();
    CHECK(hasChildNamed(*session.host, "reloaded"));
}

TEST_CASE("PostReload fires on the world that was just built, inside the reload")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local hot = game:GetService("HotReloadService")
        hot.PostReload:Connect(function()
            local marker = Instance.new("Folder")
            marker.Name = "after"
            marker.Parent = workspace
        end)
    )");

    Session session(project, log);
    // A cold boot is not a reload, so nothing fires.
    CHECK_FALSE(hasChildNamed(*session.host, "after"));

    session.reload();

    // Fired and drained inside the reload rather than one tick later, so a
    // handler connected at file scope in the new world has already run by the
    // time the reload reports.
    CHECK(hasChildNamed(*session.host, "after"));
}

TEST_CASE("every value the bag accepts survives the round trip unchanged")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local hot = game:GetService("HotReloadService")

        if hot:IsReload() then
            local it = hot:LoadState("everything")
            local ok = it.flag == true
                and it.count == -17.5
                and it.text == "hello"
                and #it.list == 3
                and it.list[2] == "two"
                and it.nested.deep.value == 1
                and buffer.len(it.bytes) == 3
                and buffer.readu8(it.bytes, 1) == 200
                and it.empty ~= nil
                and next(it.empty) == nil
                and it.nothing == nil

            local marker = Instance.new("Folder")
            marker.Name = if ok then "intact" else "damaged"
            marker.Parent = workspace
            return
        end

        local bytes = buffer.create(3)
        buffer.writeu8(bytes, 0, 1)
        buffer.writeu8(bytes, 1, 200)
        buffer.writeu8(bytes, 2, 255)

        hot.PreReload:Connect(function()
            hot:SaveState("everything", {
                flag = true,
                count = -17.5,
                text = "hello",
                list = { "one", "two", "three" },
                nested = { deep = { value = 1 } },
                bytes = bytes,
                empty = {},
            })
        end)
    )");

    Session session(project, log);
    session.reload();

    CHECK(hasChildNamed(*session.host, "intact"));
    CHECK_FALSE(hasChildNamed(*session.host, "damaged"));
}

TEST_CASE("a value that cannot cross raises rather than being dropped")
{
    // Each of these is a handle into something a reload destroys, or a shape
    // with no finite copy. Raising is what keeps the loss from being discovered
    // one save later and blamed on the reload.
    const auto refuses = [](std::string_view expression)
    {
        Captured log;
        Project project;
        project.write(
            "src/scripts/main.luau",
            std::string(R"(
                local hot = game:GetService("HotReloadService")
                local ok = pcall(function()
                    hot:SaveState("bad", )")
                + std::string(expression) + R"()
                end)
                local marker = Instance.new("Folder")
                marker.Name = if ok then "accepted" else "refused"
                marker.Parent = workspace
            )");

        Session session(project, log);
        CHECK(hasChildNamed(*session.host, "refused"));
        CHECK_FALSE(hasChildNamed(*session.host, "accepted"));
        CHECK(session.bag.size() == 0);
    };

    SUBCASE("a function closes over a VM that is about to stop existing")
    {
        refuses("function() end");
    }
    SUBCASE("an Instance is a handle into a world that is about to be rebuilt")
    {
        refuses(R"(Instance.new("Folder"))");
    }
    SUBCASE("a cyclic table has no finite copy")
    {
        refuses("(function() local t = {} t.self = t return t end)()");
    }
    SUBCASE("a table that mixes array entries and named keys is neither shape")
    {
        refuses(R"({ "one", named = true })");
    }
    SUBCASE("a table keyed by something that is neither a string nor an index")
    {
        refuses("{ [true] = 1 }");
    }
}

TEST_CASE("saving the same key twice replaces the value and keeps its place")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local hot = game:GetService("HotReloadService")
        if hot:IsReload() then
            local marker = Instance.new("Folder")
            marker.Name = `a-{hot:LoadState("a")}-b-{hot:LoadState("b")}`
            marker.Parent = workspace
            return
        end
        hot.PreReload:Connect(function()
            hot:SaveState("a", 1)
            hot:SaveState("b", 2)
            hot:SaveState("a", 3)
        end)
    )");

    Session session(project, log);
    session.reload();

    CHECK(session.bag.size() == 2);
    CHECK(hasChildNamed(*session.host, "a-3-b-2"));
}

TEST_CASE("a world whose host kept no bag still saves and loads within itself")
{
    // `WorldHostOptions::reloadState` is null for every harness that has no
    // reload -- the replay gate, the bench, the render gates. `SaveState` there
    // must still work rather than being a documented no-op, which is the trap
    // `DebugService` taught us to design out (M2 Finding 15).
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local hot = game:GetService("HotReloadService")
        hot:SaveState("here", "and back")
        local marker = Instance.new("Folder")
        marker.Name = if hot:LoadState("here") == "and back" then "roundtripped" else "lost"
        marker.Parent = workspace
    )");

    auto host = std::make_unique<app::WorldHost>();
    const auto error = host->boot(bootOptions(project.root));
    if (error.has_value())
        FAIL(error->message);

    CHECK(hasChildNamed(*host, "roundtripped"));
}
