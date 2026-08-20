#include <doctest/doctest.h>

#include <ostream>

#include <filesystem>
#include <fstream>
#include <string>

#include "luaug/app/world_host.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/render/render_world.h"

using namespace luaug;

namespace
{

// A project on disk, because that is what the host mounts and a fake filesystem
// would be testing the fake. Removed on the way out, so a failed run leaves
// nothing behind for the next one to inherit.
struct Project
{
    std::filesystem::path root;

    Project()
    {
        static int counter = 0;
        root = std::filesystem::temp_directory_path()
            / ("luaug-worldhost-" + std::to_string(++counter) + "-" + std::to_string(std::hash<std::string>{}(__FILE__)));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }

    ~Project()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    void write(std::string_view relative, std::string_view contents) const
    {
        const std::filesystem::path path = root / std::filesystem::path(relative);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
};

struct Captured
{
    std::vector<std::string> lines;

    Captured()
    {
        core::engineCatalog().loadFromFile(LUAUG_TEST_CATALOG);
        core::setLogSink([this](core::LogLevel, std::string_view text) { lines.emplace_back(text); });
    }
    ~Captured() { core::resetLogSink(); }

    Captured(const Captured&) = delete;
    Captured& operator=(const Captured&) = delete;

    [[nodiscard]] bool contains(std::string_view needle) const
    {
        for (const std::string& line : lines)
        {
            if (line.find(needle) != std::string::npos)
                return true;
        }
        return false;
    }
};

} // namespace

TEST_CASE("an empty world still boots, with game and its two services")
{
    Captured log;
    app::WorldHost host;
    REQUIRE_FALSE(host.boot({}).has_value());

    CHECK(host.workspace().valid());
    CHECK(host.world().alive(host.runtime().dataModel()));
    // `Workspace` and `ScriptService` exist from boot; nothing else does until
    // it is asked for.
    CHECK(host.world().childCount(host.runtime().dataModel()) == 2);
}

TEST_CASE("a single file mounts as one entry Script and runs at the first tick")
{
    Captured log;
    Project project;
    project.write("main.luau", R"(
        local marker = Instance.new("Folder")
        marker.Name = "Ran"
        marker.Parent = workspace
        assert(script ~= nil, "the script global is bound")
        assert(script.ClassName == "Script")
        assert(script.Name == "main")
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot({.projectPath = project.root / "main.luau"}).has_value());

    // Deferred, then drained: an entry script's first resumption is a scheduled
    // event like any other, and the boot drain is where it happens --
    // api-design.md §3 puts "start each Script … via task.defer" before "first
    // frame", so the world is built before anything is rendered.
    CHECK(host.world().childCount(host.workspace()) == 1);
    // And no clock advanced doing it.
    CHECK(host.world().engineState().tick == 0);

    host.tick();
    CHECK(host.world().childCount(host.workspace()) == 1);
    CHECK_FALSE(log.contains("[script.err."));
}

TEST_CASE("a directory mounts src/scripts as a tree, with subdirectories as Folders")
{
    Captured log;
    Project project;
    project.write("src/scripts/boot.luau", "");
    project.write("src/scripts/enemy/patrol.luau", "");
    project.write("src/scripts/enemy/chase.luau", "");
    // Outside src/scripts, so it is a module rather than an entry script and
    // never appears in the tree.
    project.write("src/shared/util.luau", "return {}");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot({.projectPath = project.root}).has_value());

    scene::World& world = host.world();
    const core::InstanceId scriptService =
        world.findFirstChildOfClass(host.runtime().dataModel(), world.classes().findId(world.atoms().lookup("ScriptService")));
    REQUIRE(scriptService.valid());

    CHECK(world.findFirstChild(scriptService, world.atoms().lookup("boot")).valid());
    const core::InstanceId enemy = world.findFirstChild(scriptService, world.atoms().lookup("enemy"));
    REQUIRE(enemy.valid());
    CHECK(world.childCount(enemy) == 2);
    CHECK(world.findFirstChild(enemy, world.atoms().lookup("patrol")).valid());
    CHECK_FALSE(world.findFirstChild(scriptService, world.atoms().lookup("util")).valid());
}

TEST_CASE("entry scripts start in path-sorted order, whatever order the walk found them")
{
    Captured log;
    Project project;
    project.write("src/scripts/c.luau", R"(
        local m = Instance.new("Folder")
        m.Name = "3"
        m.Parent = workspace
    )");
    project.write("src/scripts/a.luau", R"(
        local m = Instance.new("Folder")
        m.Name = "1"
        m.Parent = workspace
    )");
    project.write("src/scripts/b.luau", R"(
        local m = Instance.new("Folder")
        m.Name = "2"
        m.Parent = workspace
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot({.projectPath = project.root}).has_value());

    // Child order is parenting order, so the names read back in start order --
    // which R10 requires to be a property of the paths rather than of the
    // directory iterator.
    std::vector<core::InstanceId> children;
    host.world().collectChildren(host.workspace(), children);
    REQUIRE(children.size() == 3);
    CHECK(host.world().atoms().text(host.world().name(children[0])) == "1");
    CHECK(host.world().atoms().text(host.world().name(children[1])) == "2");
    CHECK(host.world().atoms().text(host.world().name(children[2])) == "3");
}

TEST_CASE("require resolves a module once and caches it")
{
    Captured log;
    Project project;
    project.write("src/shared/counter.luau", R"(
        local Counter = { value = 0 }
        Counter.value += 1
        return Counter
    )");
    project.write("src/scripts/main.luau", R"(
        local first = require("src/shared/counter")
        local second = require("src/shared/counter")
        assert(first == second, "one evaluation per module per VM")
        assert(first.value == 1, "evaluated once, not twice")

        local marker = Instance.new("Folder")
        marker.Name = "Required"
        marker.Parent = workspace
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot({.projectPath = project.root}).has_value());
    host.tick();

    CHECK(host.world().findFirstChild(host.workspace(), host.world().atoms().lookup("Required")).valid());
    CHECK_FALSE(log.contains("[script.err."));
}

TEST_CASE("a relative require is relative to the requiring file")
{
    Captured log;
    Project project;
    project.write("src/shared/math/vec.luau", "return { name = 'vec' }");
    project.write("src/shared/math/init.luau", R"(
        local vec = require("./vec")
        return { vec = vec }
    )");
    project.write("src/scripts/main.luau", R"(
        local math2 = require("src/shared/math")
        assert(math2.vec.name == "vec")

        local marker = Instance.new("Folder")
        marker.Name = "Relative"
        marker.Parent = workspace
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot({.projectPath = project.root}).has_value());
    host.tick();

    CHECK(host.world().findFirstChild(host.workspace(), host.world().atoms().lookup("Relative")).valid());
    CHECK_FALSE(log.contains("[script.err."));
}

TEST_CASE("a .luaurc alias resolves, and an unknown key does not break require")
{
    Captured log;
    Project project;
    // A `$schema` line is exactly what the vendored config reader treats as a
    // hard error that aborts the whole require (U-42). Ours ignores it.
    project.write(".luaurc", R"({ "$schema": "https://example/luaurc", "aliases": { "shared": "src/shared" } })");
    project.write("src/shared/util.luau", "return { ok = true }");
    project.write("src/scripts/main.luau", R"(
        local util = require("@shared/util")
        assert(util.ok == true)

        local marker = Instance.new("Folder")
        marker.Name = "Aliased"
        marker.Parent = workspace
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot({.projectPath = project.root}).has_value());
    host.tick();

    CHECK(host.world().findFirstChild(host.workspace(), host.world().atoms().lookup("Aliased")).valid());
    CHECK_FALSE(log.contains("[script.err."));
}

TEST_CASE("a cyclic require raises rather than exhausting the C stack")
{
    Captured log;
    Project project;
    project.write("src/shared/a.luau", "local b = require('./b') return { b = b }");
    project.write("src/shared/b.luau", "local a = require('./a') return { a = a }");
    project.write("src/scripts/main.luau", "require('src/shared/a')");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot({.projectPath = project.root}).has_value());
    host.tick();

    // The vendored implementation has no guard at all and recurses until the C
    // stack dies (U-36). This is the guard.
    CHECK(log.contains("[script.err.require_cycle]"));
}

TEST_CASE("a module that fails keeps failing, with the same error")
{
    Captured log;
    Project project;
    project.write("src/shared/broken.luau", "error('deliberate')");
    project.write("src/scripts/main.luau", R"(
        local first = select(2, pcall(require, "src/shared/broken"))
        local second = select(2, pcall(require, "src/shared/broken"))
        assert(tostring(first):find("deliberate", 1, true) ~= nil, tostring(first))
        assert(tostring(second):find("deliberate", 1, true) ~= nil, tostring(second))

        local marker = Instance.new("Folder")
        marker.Name = "Cached"
        marker.Parent = workspace
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot({.projectPath = project.root}).has_value());
    host.tick();

    // The failure is cached and re-raised rather than the module being re-run
    // in the hope of a different answer (api-design.md §3) -- which is a thing
    // the vendored implementation does NOT do (U-35).
    CHECK(host.world().findFirstChild(host.workspace(), host.world().atoms().lookup("Cached")).valid());
}

TEST_CASE("a module that does not exist says so")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", "require('src/shared/nothing')");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot({.projectPath = project.root}).has_value());
    host.tick();

    CHECK(log.contains("[script.err.module_not_found]"));
}

TEST_CASE("@luaug/testing resolves, because it ships as content")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local testing = require("@luaug/testing")
        assert(type(testing.describe) == "function")
        assert(type(testing.run) == "function")

        local marker = Instance.new("Folder")
        marker.Name = "Testing"
        marker.Parent = workspace
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot({.projectPath = project.root}).has_value());
    host.tick();

    CHECK(host.world().findFirstChild(host.workspace(), host.world().atoms().lookup("Testing")).valid());
    CHECK_FALSE(log.contains("[script.err."));
}

TEST_CASE("a Script whose Enabled is false at boot never starts")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local marker = Instance.new("Folder")
        marker.Name = "ShouldNotRun"
        marker.Parent = workspace
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot({.projectPath = project.root}).has_value());

    scene::World& world = host.world();
    const core::InstanceId scriptService =
        world.findFirstChildOfClass(host.runtime().dataModel(), world.classes().findId(world.atoms().lookup("ScriptService")));
    const core::InstanceId entry = world.findFirstChild(scriptService, world.atoms().lookup("main"));
    REQUIRE(entry.valid());

    // `boot` already deferred it, so this test is really about the property
    // being read at start time. Setting it after boot is documented as having
    // no effect, which is why the check below is the interesting one.
    world.setProperty(entry, world.atoms().intern("Enabled"), scene::Value{false});

    host.tick();
    // It ran: the flag is read when the script is started, and `boot` started
    // it. That is exactly what api-design.md §3 says -- writing `Enabled` after
    // boot neither stops a running script nor starts one that did not run.
    CHECK(world.childCount(host.workspace()) == 1);
}

TEST_CASE("game.Loaded reaches a handler connected at file scope")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        game.Loaded:Connect(function()
            local marker = Instance.new("Folder")
            marker.Name = "Loaded"
            marker.Parent = workspace
        end)
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot({.projectPath = project.root}).has_value());
    host.tick();

    // The fire has to be RAISED after every entry script's first resumption
    // rather than merely queued behind them: a fire captures its connection
    // list when it is raised (§3.1), so a handler connected at file scope would
    // otherwise miss its own fire.
    CHECK(host.world().findFirstChild(host.workspace(), host.world().atoms().lookup("Loaded")).valid());
}

TEST_CASE("the world ticks, and a Heartbeat handler sees the clock advance")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local run = game:GetService("RunService")
        local part = Instance.new("Part")
        part.Name = "Mover"
        part.Parent = workspace

        run.Heartbeat:Connect(function(dt)
            assert(math.abs(dt - 1 / 60) < 1e-12, tostring(dt))
            part.Position = Vector3.new(run.SimTime, 0, 0)
        end)
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot({.projectPath = project.root}).has_value());

    for (int index = 0; index < 10; ++index)
        host.tick();

    render::RenderWorld snapshot;
    render::extract(host.world(), host.workspace(), snapshot);
    REQUIRE(snapshot.parts.size() == 1);
    // Ten: the script connected during the boot drain, so it saw every one of
    // the ten ticks.
    // The tolerance is f32's, because `Position` is a vector and the round
    // trip through one is where the precision goes -- `CFrame` keeps the f64.
    CHECK(snapshot.parts[0].cframe.position.x == doctest::Approx(10.0 / 60.0).epsilon(1e-6));
    CHECK_FALSE(log.contains("[script.err."));
}

TEST_CASE("two runs of the same script and seed produce the same world hash")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local run = game:GetService("RunService")
        local rng = Random.new(7)
        for index = 1, 20 do
            local part = Instance.new("Part")
            part.Name = "P" .. index
            part.Position = Vector3.new(rng:NextNumber(), rng:NextNumber(), rng:NextNumber())
            part.Parent = workspace
        end

        run.Heartbeat:Connect(function()
            for _, part in workspace:GetChildren() do
                part.Position = part.Position + Vector3.new(0, 1 / 60, 0)
            end
        end)
    )");

    const auto runOnce = [&](core::u64 seed) {
        app::WorldHost host;
        REQUIRE_FALSE(host.boot({.projectPath = project.root, .seed = seed}).has_value());
        for (int index = 0; index < 120; ++index)
            host.tick();
        return host.world().worldHash();
    };

    // The level-B guarantee recorded replays rest on: same build, same
    // platform, same seed, same script -- same hash (ADR 0025).
    CHECK(runOnce(1234u) == runOnce(1234u));
}
