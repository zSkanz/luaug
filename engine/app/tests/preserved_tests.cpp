// `PreserveOnReload` (ADR 0024, api-design.md §3.2, M3 brief Decision 5).
//
// The tagged instance has the same problem as the state bag: the world it lives
// in is what a reload destroys. What is asserted here is that it comes back
// where it was, with what it had, and early enough for a script to find it.

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

struct Session
{
    script::ReloadState bag;
    app::WorldHostOptions options;
    std::unique_ptr<app::WorldHost> host;
    Captured& log;

    Session(const Project& project, Captured& capturedLog)
        : options(bootOptions(project.root))
        , log(capturedLog)
    {
        options.reloadState = &bag;
        host = std::make_unique<app::WorldHost>();
        const auto error = host->boot(options);
        if (error.has_value())
            FAIL(error->message);
        // A script error is contained by design, so a fixture that does not
        // look for one lets a broken setup script masquerade as a broken
        // feature. This test file was written with `Anchored` on a `Part`,
        // which no class declares -- and without this the failure showed up
        // three assertions later as a missing child.
        REQUIRE_MESSAGE(log.errors.empty(), log.firstError());
    }

    app::ReloadReport reload()
    {
        const app::ReloadReport report = app::reloadWorld(host, options);
        if (report.error.has_value())
            FAIL(report.error->message);
        REQUIRE(report.ok);
        REQUIRE_MESSAGE(log.errors.empty(), log.firstError());
        return report;
    }
};

[[nodiscard]] core::InstanceId childNamed(app::WorldHost& host, core::InstanceId parent, std::string_view name)
{
    return host.world().findFirstChild(parent, host.world().atoms().lookup(name));
}

} // namespace

TEST_CASE("a tagged instance comes back where it was, with what it had")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local hot = game:GetService("HotReloadService")

        if hot:IsReload() then
            return
        end

        local model = Instance.new("Model")
        model.Name = "Character"
        model:SetAttribute("Score", 7)
        model.Parent = workspace
        model:AddTag("PreserveOnReload")

        local part = Instance.new("Part")
        part.Name = "Torso"
        part.Position = vector.create(1, 2, 3)
        part.Transparency = 0.5
        part.Parent = model
    )");

    Session session(project, log);
    REQUIRE(hasChildNamed(*session.host, "Character"));

    const app::ReloadReport report = session.reload();
    CHECK(report.preserve.captured == 1);
    CHECK(report.preserve.restored == 1);
    CHECK(report.preserve.skipped == 0);
    // `Parent` is the only Instance-valued property in the v1 surface, and it
    // is excluded by name. The first class to declare another makes this move.
    CHECK(report.preserve.droppedReferences == 0);

    // The reloaded script returns early, so anything under `workspace` now is
    // there because the restore put it there.
    const core::InstanceId model = childNamed(*session.host, session.host->workspace(), "Character");
    REQUIRE(model.valid());

    scene::World& world = session.host->world();
    CHECK(world.getAttribute(model, world.atoms().lookup("Score")) == scene::Value{7.0});
    CHECK(world.hasTag(model, world.atoms().lookup("PreserveOnReload")));

    const core::InstanceId torso = childNamed(*session.host, model, "Torso");
    REQUIRE(torso.valid());
    CHECK(world.getProperty(torso, world.atoms().lookup("Transparency")) == scene::Value{0.5});
    CHECK(world.getProperty(torso, world.atoms().lookup("Position")) == scene::Value{core::Vec3{1.0f, 2.0f, 3.0f}});
}

TEST_CASE("the preserved instance is in the tree before any entry script runs")
{
    // The whole reason the restore happens where it does. A script that looks
    // for what it left behind has to find it on its first line, not one tick
    // later -- otherwise `IsReload` tells it something it cannot act on.
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local hot = game:GetService("HotReloadService")

        if hot:IsReload() then
            local found = workspace:FindFirstChild("Character")
            local marker = Instance.new("Folder")
            marker.Name = if found ~= nil then "found-it" else "missing"
            marker.Parent = workspace
            return
        end

        local model = Instance.new("Model")
        model.Name = "Character"
        model.Parent = workspace
        model:AddTag("PreserveOnReload")
    )");

    Session session(project, log);
    session.reload();

    CHECK(hasChildNamed(*session.host, "found-it"));
    CHECK_FALSE(hasChildNamed(*session.host, "missing"));
}

TEST_CASE("a tag on a descendant of a tagged instance does not restore it twice")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local hot = game:GetService("HotReloadService")
        if hot:IsReload() then
            return
        end

        local outer = Instance.new("Model")
        outer.Name = "Outer"
        outer.Parent = workspace
        outer:AddTag("PreserveOnReload")

        local inner = Instance.new("Model")
        inner.Name = "Inner"
        inner.Parent = outer
        inner:AddTag("PreserveOnReload")
    )");

    Session session(project, log);
    const app::ReloadReport report = session.reload();

    // One capture, not two: the outer one already carries the inner.
    CHECK(report.preserve.captured == 1);
    CHECK(report.preserve.restored == 1);

    const core::InstanceId outer = childNamed(*session.host, session.host->workspace(), "Outer");
    REQUIRE(outer.valid());
    CHECK(session.host->world().childCount(outer) == 1);
    CHECK(session.host->world().childCount(session.host->workspace()) == 1);
}

TEST_CASE("an ancestor the new world does not have yet is replayed, not invented")
{
    // The instance was three levels down under folders a script built, and the
    // scripts have not run when the restore happens. The chain comes back with
    // the classes it had rather than with a guess.
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local hot = game:GetService("HotReloadService")
        if hot:IsReload() then
            return
        end

        local level = Instance.new("Folder")
        level.Name = "Level"
        level.Parent = workspace

        local props = Instance.new("Model")
        props.Name = "Props"
        props.Parent = level

        local crate = Instance.new("Part")
        crate.Name = "Crate"
        crate.Parent = props
        crate:AddTag("PreserveOnReload")
    )");

    Session session(project, log);
    const app::ReloadReport report = session.reload();
    CHECK(report.preserve.restored == 1);

    scene::World& world = session.host->world();
    const core::InstanceId level = childNamed(*session.host, session.host->workspace(), "Level");
    REQUIRE(level.valid());
    CHECK(world.atoms().text(world.classes().find(world.classOf(level))->name) == "Folder");

    const core::InstanceId props = childNamed(*session.host, level, "Props");
    REQUIRE(props.valid());
    CHECK(world.atoms().text(world.classes().find(world.classOf(props))->name) == "Model");
    CHECK(childNamed(*session.host, props, "Crate").valid());
}

TEST_CASE("an unparented tagged instance is skipped and counted, not silently lost")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local hot = game:GetService("HotReloadService")
        if hot:IsReload() then
            return
        end

        -- Never parented, so there is nowhere to put it back. What is being
        -- preserved is where it was as much as what it was.
        local orphan = Instance.new("Model")
        orphan.Name = "Orphan"
        orphan:AddTag("PreserveOnReload")
    )");

    Session session(project, log);
    const app::ReloadReport report = session.reload();

    CHECK(report.preserve.captured == 0);
    CHECK(report.preserve.restored == 0);
    CHECK(report.preserve.skipped == 1);
}

TEST_CASE("nothing tagged means nothing carried, and no cost for asking")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local marker = Instance.new("Folder")
        marker.Name = "plain"
        marker.Parent = workspace
    )");

    Session session(project, log);
    const app::ReloadReport report = session.reload();

    CHECK(report.preserve.captured == 0);
    CHECK(report.preserve.restored == 0);
    CHECK(report.preserve.skipped == 0);
    CHECK(hasChildNamed(*session.host, "plain"));
}

TEST_CASE("a preserved subtree keeps its child order")
{
    // Child order is observable -- it is what `GetChildren` returns and what the
    // world hash covers -- so a restore that rebuilt it in tag order or in hash
    // order would be a determinism bug that looked like nothing at all.
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local hot = game:GetService("HotReloadService")
        if hot:IsReload() then
            return
        end

        local model = Instance.new("Model")
        model.Name = "Ordered"
        model.Parent = workspace
        model:AddTag("PreserveOnReload")

        for _, name in { "zeta", "alpha", "mid" } do
            local part = Instance.new("Part")
            part.Name = name
            part.Parent = model
        end
    )");

    Session session(project, log);
    session.reload();

    scene::World& world = session.host->world();
    const core::InstanceId model = childNamed(*session.host, session.host->workspace(), "Ordered");
    REQUIRE(model.valid());

    std::vector<core::InstanceId> children;
    world.collectChildren(model, children);
    REQUIRE(children.size() == 3);
    CHECK(world.atoms().text(world.name(children[0])) == "zeta");
    CHECK(world.atoms().text(world.name(children[1])) == "alpha");
    CHECK(world.atoms().text(world.name(children[2])) == "mid");
}
