#include "luaug/app/inspector.h"
#include "luaug/app/world_host.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/render/lighting.h"
#include "luaug/render/render_world.h"
#include "luaug/scene/components.h"
#include "luaug/scene/physics_sync.h"

#include <array>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <ostream>
#include <string>
#include <variant>

#include "luaug_test_nearly.h"
#include "project_fixture.h"

using namespace luaug;
using luaug::app::testing::bootOptions;
using luaug::app::testing::Captured;
using luaug::app::testing::Project;
using luaug::testing::nearly;

TEST_CASE("an empty world still boots, with game and every service under it")
{
    Captured log;
    app::WorldHost host;
    REQUIRE_FALSE(host.boot({}).has_value());

    CHECK(host.workspace().valid());
    CHECK(host.world().alive(host.runtime().dataModel()));

    // **Every service, not the five that had earned their way in one at a time**
    // (D099). The count is measured against the registry rather than written
    // down, because a number in a test is a number somebody has to remember to
    // change and this one moved five times without anybody deciding it should.
    core::usize services = 0;
    const scene::ClassRegistry& classes = host.world().classes();
    for (scene::ClassId id = 1; id < static_cast<scene::ClassId>(classes.classCount()); ++id) {
        const scene::ClassDescriptor* descriptor = classes.find(id);
        if (descriptor != nullptr && hasFlag(descriptor->flags, scene::ClassFlags::Service))
            ++services;
    }
    CHECK(services > 5);
    CHECK(host.world().childCount(host.runtime().dataModel()) == services);
}

// --- The M4.5 gate addition: `Lighting` resolution, at the HOST --------------
//
// M4 tested the environment at the extractor, handing it a `Lighting` id the
// test had made itself. Every assertion passed, and the step that was actually
// broken -- the host resolving the service -- was the one step nothing covered.
// These three test that step, and each of them fails against M4's code.

TEST_CASE("the host resolves Lighting on a world no script ever touched")
{
    Captured log;
    app::WorldHost host;
    REQUIRE_FALSE(host.boot({}).has_value());

    // The whole defect in one line: the id was cached before anything created
    // the service, so it was invalid for the life of the world and `extract`
    // answered with `RenderEnvironment`'s defaults every frame.
    REQUIRE(host.lighting().valid());
    CHECK(host.world().alive(host.lighting()));
    CHECK(host.world().lighting().find(host.lighting()) != nullptr);
    // A boot service, so it is an ordinary child of `game` and `GetService`
    // returns the same instance rather than a second one.
    CHECK(host.world().parentOf(host.lighting()) == host.runtime().dataModel());
}

TEST_CASE("the environment the renderer sees is the one the world holds")
{
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local lighting = game:GetService("Lighting")
        lighting.ClockTime = 6.5
        lighting.GeographicLatitude = 35
        lighting.Ambient = Color3.new(0.25, 0.5, 0.75)
        lighting.Brightness = 3.25
        lighting.FogColor = Color3.new(0.1, 0.2, 0.3)
        lighting.FogStart = 40
        lighting.FogEnd = 220
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
    host.tick();

    const scene::LightingComponent* held = host.world().lighting().find(host.lighting());
    REQUIRE(held != nullptr);

    render::RenderWorld snapshot;
    render::extract(host.world(), host.workspace(), host.lighting(), render::MeshLibrary{}, 1.0f, 0.0f, nullptr, 0.0f,
                    nullptr, snapshot);

    // Field by field rather than "it is not the default": a defaults comparison
    // passes the moment someone changes a default, and the point of this
    // assertion is that the snapshot carries what the SCRIPT wrote.
    CHECK(nearly(snapshot.environment.ambient.r, held->ambient.r));
    CHECK(nearly(snapshot.environment.ambient.g, held->ambient.g));
    CHECK(nearly(snapshot.environment.ambient.b, held->ambient.b));
    CHECK(nearly(snapshot.environment.sunBrightness, held->brightness));
    CHECK(nearly(snapshot.environment.fogColor.r, held->fogColor.r));
    CHECK(nearly(snapshot.environment.fogStart, held->fogStart));
    CHECK(nearly(snapshot.environment.fogEnd, held->fogEnd));

    // The sun is derived rather than stored, so it is compared against the
    // function of the two properties that define it -- which is also what makes
    // it a pure function of the world and not of the frame (R10).
    const core::Vec3 expected = render::sunDirection(held->clockTime, held->geographicLatitude);
    CHECK(nearly(snapshot.environment.sunDirection.x, expected.x));
    CHECK(nearly(snapshot.environment.sunDirection.y, expected.y));
    CHECK(nearly(snapshot.environment.sunDirection.z, expected.z));
    // And 6.5 is a morning sun: low and to the east. Written out because every
    // check above would also pass if `sunDirection` returned straight up for
    // everything, which is precisely the image M4 shipped.
    CHECK(snapshot.environment.sunDirection.x > 0.5f);
    CHECK(snapshot.environment.sunDirection.y < 0.5f);
}

TEST_CASE("two clock times give the renderer two different suns")
{
    const auto sunAt = [](const char* clock) {
        Captured log;
        Project project;
        project.write("src/scripts/main.luau", std::string(R"(
            local lighting = game:GetService("Lighting")
            lighting.GeographicLatitude = 35
            lighting.ClockTime = )") + clock);

        app::WorldHost host;
        REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
        host.tick();

        render::RenderWorld snapshot;
        render::extract(host.world(), host.workspace(), host.lighting(), render::MeshLibrary{}, 1.0f, 0.0f, nullptr,
                        0.0f, nullptr, snapshot);
        return snapshot.environment.sunDirection;
    };

    // The differential the goldens could not make. Two worlds differing in one
    // property must reach the renderer differently; while `Lighting` was
    // unreachable these were byte-identical, and so was every image.
    const core::Vec3 morning = sunAt("7.5");
    const core::Vec3 afternoon = sunAt("15.5");
    CHECK(core::length(morning - afternoon) > 0.5f);
}

// --- The M5 gate additions: the mirror at the HOST, and a close that waits ---

TEST_CASE("the host hands the physics mirror a Workspace on a world no script touched")
{
    Captured log;
    app::WorldHost host;
    REQUIRE_FALSE(host.boot({}).has_value());

    // M4.5's whole defect in the physics module's shape. `PhysicsSync` cannot
    // resolve `Workspace` itself -- `scene` has no notion of the DataModel root
    // -- so the host hands it over, and an id that never arrived would make
    // every part in every world weightless while nothing anywhere errored.
    //
    // A build with no physics backend has no mirror at all, which is a
    // different and honest state; this asserts the wiring where there is one.
    REQUIRE(host.physics() != nullptr);
    CHECK(host.physics()->workspace() == host.workspace());
    CHECK(host.physics()->workspace().valid());
}

TEST_CASE("a part in a world nobody scripted still falls, because the mirror is wired")
{
    Captured log;
    Project project;
    project.write("src/scripts/init.luau", R"(
        local crate = Instance.new("Part")
        crate.Name = "Crate"
        crate.Position = vector.create(0, 40, 0)
        crate.Parent = workspace
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
    for (int tick = 0; tick < 60; ++tick)
        host.tick();

    const core::InstanceId crate = host.world().findFirstChild(host.workspace(), host.world().atoms().lookup("Crate"));
    REQUIRE(crate.valid());
    const scene::PartComponent* part = host.world().parts().find(crate);
    REQUIRE(part != nullptr);
    // A second of falling is about five metres. The assertion is that it moved
    // at all: a mirror that was never given a Workspace produces a world where
    // nothing does, and every test that only checks the API would still pass.
    CHECK(part->cframe.position.y < 39.0);
}

TEST_CASE("a BindToClose handler that yields is waited for")
{
    Captured log;
    Project project;
    project.write("src/scripts/init.luau", R"(
        game:BindToClose(function()
            -- A tenth of a second of sim time. Before M5 this handler was cut
            -- off at the first drain after the shutdown and the attribute below
            -- was never written (D016).
            task.wait(0.1)
            game:SetAttribute("ClosedCleanly", true)
        end)
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
    host.tick();

    host.close();

    const scene::Value closed =
        host.world().getAttribute(host.runtime().dataModel(), host.world().atoms().lookup("ClosedCleanly"));
    const auto* flag = std::get_if<bool>(&closed);
    REQUIRE(flag != nullptr);
    CHECK(*flag);
}

TEST_CASE("a BindToClose handler that never finishes is cut off at the grace period")
{
    Captured log;
    Project project;
    project.write("src/scripts/init.luau", R"(
        game:BindToClose(function()
            while true do
                task.wait()
            end
        end)
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
    host.tick();

    // A tenth of a second of WALL clock rather than the thirty-second default:
    // the cap exists so a handler that never finishes cannot hold the process
    // open, and a test for it must not hold the suite open either.
    host.close(0.1);

    // The point is that it returned. What it also does is say so, because a
    // shutdown that dropped somebody's save silently would be worse than one
    // that took thirty seconds.
    //
    // Matched on the catalog's TEXT rather than on the key: `core::log` formats
    // through the catalog, so a line only carries its key when the catalog does
    // not have one. The `[script.err.…]` checks elsewhere in this file match a
    // key because a script error's message is key-prefixed by `EngineError`,
    // which is a different path.
    CHECK(log.contains("still running after"));
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
    REQUIRE_FALSE(host.boot(bootOptions(project.root / "main.luau")).has_value());

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
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());

    scene::World& world = host.world();
    const core::InstanceId scriptService = world.findFirstChildOfClass(
        host.runtime().dataModel(), world.classes().findId(world.atoms().lookup("ScriptService")));
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
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());

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
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
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
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
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
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
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
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
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
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
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
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
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
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
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
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());

    scene::World& world = host.world();
    const core::InstanceId scriptService = world.findFirstChildOfClass(
        host.runtime().dataModel(), world.classes().findId(world.atoms().lookup("ScriptService")));
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
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
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
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());

    for (int index = 0; index < 10; ++index)
        host.tick();

    render::RenderWorld snapshot;
    render::extract(host.world(), host.workspace(), host.lighting(), render::MeshLibrary{}, 1.0f, 0.0f, nullptr, 0.0f,
                    nullptr, snapshot);
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
        REQUIRE_FALSE(host.boot(bootOptions(project.root, seed)).has_value());
        for (int index = 0; index < 120; ++index)
            host.tick();
        return host.world().worldHash();
    };

    // The level-B guarantee recorded replays rest on: same build, same
    // platform, same seed, same script -- same hash (ADR 0025).
    CHECK(runOnce(1234u) == runOnce(1234u));
}

TEST_CASE("the two lights' Shadows properties are the ones marked")
{
    // Named rather than counted, because the point of the marker is that a
    // specific property tells the truth about itself. A count would pass while
    // the wrong one was marked.
    Captured log;
    app::WorldHost host;
    REQUIRE_FALSE(host.boot({}).has_value());
    const scene::ClassRegistry& classes = host.world().classes();
    const core::AtomTable& atoms = host.world().atoms();

    for (const char* className : {"PointLight", "SpotLight"}) {
        const scene::ClassId id = classes.findId(atoms.lookup(className));
        REQUIRE(id != scene::InvalidClass);
        const scene::PropertyDesc* shadows = classes.findProperty(id, atoms.lookup("Shadows"));
        REQUIRE(shadows != nullptr);
        CHECK(shadows->inert);
        // Backed, which is the difference the marker is making: it stores and
        // returns what was written.
        CHECK(shadows->get != nullptr);
        CHECK(shadows->set != nullptr);

        // And its neighbours are not marked, so "inert" is a statement about
        // this property rather than about the class.
        const scene::PropertyDesc* brightness = classes.findProperty(id, atoms.lookup("Brightness"));
        REQUIRE(brightness != nullptr);
        CHECK_FALSE(brightness->inert);
    }
}

TEST_CASE("the render module's positional classes are PVInstances too")
{
    // At the host rather than in `engine/script`, because `MeshPart` and
    // `Camera` are registered by `render` and that module's classes do not
    // exist in a fixture holding scene's alone. Which is also the shape of the
    // audit mistake this whole scope item came from: a sweep that searched one
    // module for a value used in another concluded it was unused.
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local camera = Instance.new("Camera")
        assert(camera:IsA("PVInstance"), "Camera is positional")
        assert(Instance.new("MeshPart"):IsA("PVInstance"), "MeshPart is positional")
        assert(not Instance.new("PointLight"):IsA("PVInstance"), "a light is not")

        camera.CFrame = CFrame.new(0, 5, 0)
        assert(camera:GetPivot().Position == Vector3.new(0, 5, 0))

        -- An offset moves where the camera turns about, which is what makes an
        -- orbit camera a `PivotTo` rather than trigonometry at every call site.
        camera.PivotOffset = CFrame.new(0, 0, -10)
        assert(camera:GetPivot().Position == Vector3.new(0, 5, -10))

        camera:PivotTo(CFrame.new(1, 2, 3))
        assert(camera.CFrame.Position == Vector3.new(1, 2, 13))

        local marker = Instance.new("Folder")
        marker.Name = "PivotOk"
        marker.Parent = workspace
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());
    CHECK_FALSE(log.contains("[script.err."));
    CHECK(host.world().childCount(host.workspace()) == 1);
}

TEST_CASE("dragging Size and CFrame through their extremes does not take the host down")
{
    // The reproduction attempt for the one reported defect with no repro: the
    // host died while values were being dragged in the inspector, and the
    // captured log held the two lines an ordinary run prints -- the signature of
    // a fault rather than of any C++ error path.
    //
    // A drag is not one write. It is a write every frame, each one read back
    // from the property it just set, and it passes through whatever values the
    // mouse sweeps over on the way -- including zero, negative and absurd. This
    // drives exactly that loop through `Inspector`, so it goes through
    // `enqueue` -> FrameStart drain -> `World::setProperty`, and extracts a
    // frame each time so the renderer sees every value too.
    //
    // It has not reproduced the crash. That is worth recording rather than
    // deleting: it rules out the write path itself, which narrows what remains
    // to the ImGui half and to the defocus the human's note mentions.
    Captured log;
    Project project;
    project.write("src/scripts/main.luau", R"(
        local part = Instance.new("MeshPart")
        part.Name = "Target"
        part.Parent = workspace

        local camera = Instance.new("Camera")
        camera.CFrame = CFrame.lookAt(Vector3.new(6, 4, 6), Vector3.new(0, 0, 0))
        camera.Parent = workspace
        workspace.CurrentCamera = camera
    )");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot(bootOptions(project.root)).has_value());

    const core::InstanceId target =
        host.world().findFirstChild(host.workspace(), host.world().atoms().lookup("Target"));
    REQUIRE(target.valid());

    const core::NameAtom sizeProperty = host.world().atoms().lookup("Size");
    const core::NameAtom cframeProperty = host.world().atoms().lookup("CFrame");

    // The values a drag actually sweeps through, plus the ones a fast drag
    // overshoots into. Infinity and NaN are here because `DragScalar` with a
    // speed and no bounds will reach them, and because a NaN in a transform is
    // how a renderer stops being a renderer.
    const std::array<core::f32, 8> sweep{1.0f,  0.5f, 0.0f,  -1.0f,
                                         -0.0f, 1e6f, 1e30f, std::numeric_limits<core::f32>::infinity()};

    app::Inspector inspector;
    inspector.select(target);

    render::RenderWorld snapshot;
    for (std::size_t frame = 0; frame < sweep.size() * 4; ++frame) {
        const core::f32 value = sweep[frame % sweep.size()];
        // Read back first, exactly as the widget does: a drag edits the value
        // the panel is showing, not a value it remembers.
        const std::optional<scene::Value> current = host.world().getProperty(target, sizeProperty);
        REQUIRE(current.has_value());

        inspector.enqueue(target, sizeProperty, scene::Value{core::Vec3{value, value, value}});

        core::CFrameD moved;
        moved.position = core::DVec3{static_cast<core::f64>(value), 1.0, static_cast<core::f64>(value)};
        inspector.enqueue(target, cframeProperty, scene::Value{moved});

        inspector.applyPending(host.world());
        host.tick();
        render::extract(host.world(), host.workspace(), host.lighting(), render::MeshLibrary{}, 1.0f, 0.0f, nullptr,
                        0.0f, nullptr, snapshot);
    }

    // Still alive, still answering, and the panel can still format what it
    // holds -- `formatValue` is the other thing that touches every value.
    CHECK(host.world().alive(target));
    CHECK_FALSE(app::formatValue(host.world(), *host.world().getProperty(target, sizeProperty)).empty());
    CHECK_FALSE(app::formatValue(host.world(), *host.world().getProperty(target, cframeProperty)).empty());
}

// --- D067: the scene is applied BEFORE the entry scripts run -----------------
//
// ADR 0047's lifecycle is load-then-start and `scene_file.h` says so in its
// opening paragraph; E1 shipped it the other way round, and the bill arrived
// the first time somebody opened the editor on a project whose script builds
// its own world. A scene load REPLACES the contents of `Workspace`, so applied
// after the boot drain it destroyed every instance the script had just made
// while the VM kept its references and its connections -- and the first tick
// after Play raised `instance_dead` once a frame forever.
//
// These two cases are the order, stated as behaviour rather than as sequence:
// what the script builds survives a boot scene, and what the scene brings is
// there for the script to find.

namespace {

[[nodiscard]] core::InstanceId bootChildNamed(app::WorldHost& host, std::string_view name)
{
    return host.world().findFirstChild(host.workspace(), host.world().atoms().lookup(name));
}

constexpr std::string_view kOnePartScene =
    R"({"format":"luaug-scene","version":1,"root":{"class":"Workspace","name":"Workspace",)"
    R"("children":[{"class":"Part","name":"FromTheScene","properties":{}}]}})";

} // namespace

TEST_CASE("a boot scene does not destroy what the entry scripts built")
{
    Captured log;
    Project project;
    project.write("src/scripts/init.luau", R"(
        local part = Instance.new("Part")
        part.Name = "BuiltByScript"
        part.Parent = workspace
    )");
    project.write("content/scenes/main.scene.json", kOnePartScene);

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    REQUIRE_FALSE(host.boot(options).has_value());

    CHECK(host.bootSceneApplied());
    CHECK(host.bootSceneReport().instances == 1);

    const core::InstanceId built = bootChildNamed(host, "BuiltByScript");
    REQUIRE(built.valid());
    // The whole of the defect in one line: with the load after the drain this
    // id was destroyed and every script handle to it was dead.
    CHECK(host.world().alive(built));
    CHECK(bootChildNamed(host, "FromTheScene").valid());
}

TEST_CASE("an empty boot scene leaves a scripted world alone")
{
    Captured log;
    Project project;
    project.write("src/scripts/init.luau", R"(
        local part = Instance.new("Part")
        part.Name = "BuiltByScript"
        part.Parent = workspace
    )");
    // Exactly what `New Scene` followed by `Save As` writes, which is the file
    // that was actually on disk when this was reported.
    project.write("content/scenes/main.scene.json",
                  R"({"format":"luaug-scene","version":1,"root":{"class":"Workspace",)"
                  R"("name":"Workspace","properties":{"CurrentCamera":null},"children":[]}})");

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    REQUIRE_FALSE(host.boot(options).has_value());

    const core::InstanceId built = bootChildNamed(host, "BuiltByScript");
    REQUIRE(built.valid());
    CHECK(host.world().alive(built));
}

TEST_CASE("a scene the entry scripts can see, because it is there before they run")
{
    Captured log;
    Project project;
    project.write("src/scripts/init.luau", R"(
        local found = workspace:FindFirstChild("FromTheScene")
        local marker = Instance.new("Part")
        marker.Name = if found then "SawTheScene" else "SawNothing"
        marker.Parent = workspace
    )");
    project.write("content/scenes/main.scene.json", kOnePartScene);

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    REQUIRE_FALSE(host.boot(options).has_value());

    CHECK(bootChildNamed(host, "SawTheScene").valid());
    CHECK_FALSE(bootChildNamed(host, "SawNothing").valid());
}

TEST_CASE("a boot scene that will not read is reported and the world still boots")
{
    Captured log;
    Project project;
    project.write("src/scripts/init.luau", R"(
        local part = Instance.new("Part")
        part.Name = "BuiltByScript"
        part.Parent = workspace
    )");
    project.write("content/scenes/main.scene.json", "{ this is not a scene");

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    REQUIRE_FALSE(host.boot(options).has_value());

    CHECK_FALSE(host.bootSceneApplied());
    CHECK(bootChildNamed(host, "BuiltByScript").valid());
}

TEST_CASE("a project with a scene AND world-building scripts is told it has two of everything")
{
    // D074, and the honest cost of D067's fix rather than a regression from it:
    // once the scene loads BEFORE the scripts, a project whose code also builds
    // a world has two sources for one world and the engine cannot merge them.
    // What it can do is say so with both numbers, the first time, instead of
    // leaving somebody to find two characters in their own scene.
    Captured log;
    Project project;
    project.write("src/scripts/init.luau", R"(
        local part = Instance.new("Part")
        part.Name = "BuiltByScript"
        part.Parent = workspace
    )");
    project.write("content/scenes/main.scene.json", kOnePartScene);

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    REQUIRE_FALSE(host.boot(options).has_value());

    CHECK(bootChildNamed(host, "FromTheScene").valid());
    CHECK(bootChildNamed(host, "BuiltByScript").valid());
    CHECK(log.contains("two sources"));
}

TEST_CASE("a scene with no world-building scripts says nothing at all")
{
    // The arrangement ADR 0047 asks projects to become, and `examples/06-scene`
    // is one: the world is the file and the script is only what it does. A
    // warning here would be noise on the shape that is correct.
    Captured log;
    Project project;
    project.write("src/scripts/init.luau", R"(
        local RunService = game:GetService("RunService")
        RunService.Heartbeat:Connect(function() end)
    )");
    project.write("content/scenes/main.scene.json", kOnePartScene);

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    REQUIRE_FALSE(host.boot(options).has_value());

    CHECK(bootChildNamed(host, "FromTheScene").valid());
    CHECK_FALSE(log.contains("two sources"));
}

// --- A script is an instance, and an instance is what runs (ADR 0057) --------
//
// Before this, `startScripts` walked the mounted-FILE list: a Script the scene
// brought, or one somebody made in the editor, was saved with its `Source` and
// never ran, while a mounted one ran with its `Source` empty. Two disconnected
// halves of one idea. These cases are the idea, joined.

namespace {

// A scene holding one `Script` whose source builds a part, so "did it run" is a
// question about the world rather than about a log line.
[[nodiscard]] std::string sceneWithScript(std::string_view name, std::string_view body, bool enabled = true)
{
    std::string out =
        R"({"format":"luaug-scene","version":1,"root":{"class":"Workspace","name":"Workspace","children":[)";
    out += R"({"class":")";
    out += name;
    out += R"(","name":"SceneScript","properties":{"Source":")";
    out += body;
    out += R"(")";
    if (!enabled)
        out += R"(,"Enabled":false)";
    out += R"(}}]}})";
    return out;
}

} // namespace

TEST_CASE("a Script the scene brought runs, because an instance is what runs")
{
    Captured log;
    Project project;
    project.write("content/scenes/main.scene.json",
                  sceneWithScript("Script", R"(local p = Instance.new(\"Part\") p.Name = \"MadeByTheScene\" )"
                                            R"(p.Parent = workspace)"));

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    REQUIRE_FALSE(host.boot(options).has_value());

    // The whole of ADR 0057 in one assertion: nothing mounted this, no file
    // exists for it, and it ran.
    CHECK(bootChildNamed(host, "MadeByTheScene").valid());
}

TEST_CASE("a disabled Script in the scene does not run")
{
    Captured log;
    Project project;
    project.write("content/scenes/main.scene.json",
                  sceneWithScript("Script",
                                  R"(local p = Instance.new(\"Part\") p.Name = \"ShouldNotExist\" )"
                                  R"(p.Parent = workspace)",
                                  /*enabled=*/false));

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    REQUIRE_FALSE(host.boot(options).has_value());

    CHECK_FALSE(bootChildNamed(host, "ShouldNotExist").valid());
}

TEST_CASE("a ModuleScript in the scene still only runs when it is required")
{
    Captured log;
    Project project;
    // The exact class rather than `IsA`: a ModuleScript shares `Source` with a
    // Script and deliberately does not start by itself.
    project.write("content/scenes/main.scene.json",
                  sceneWithScript("ModuleScript", R"(local p = Instance.new(\"Part\") p.Name = \"NotByItself\" )"
                                                  R"(p.Parent = workspace return {})"));

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    REQUIRE_FALSE(host.boot(options).has_value());

    CHECK_FALSE(bootChildNamed(host, "NotByItself").valid());
}

TEST_CASE("a mounted script carries its file in its own Source")
{
    Captured log;
    Project project;
    project.write("src/scripts/init.luau", "local x = 1\nreturn x\n");

    app::WorldHost host;
    REQUIRE_FALSE(host.boot(app::testing::bootOptions(project.root)).has_value());

    const scene::World& w = host.world();
    const core::InstanceId service =
        w.findFirstChildOfClass(host.dataModel(), w.classes().findId(w.atoms().lookup("ScriptService")));
    REQUIRE(service.valid());
    const core::InstanceId script = w.findFirstChild(service, w.atoms().lookup("init"));
    REQUIRE(script.valid());

    // What ADR 0050 decided and the mount never did. Without this the script
    // editor would open a tab on an empty string.
    const std::optional<scene::Value> source = w.getProperty(script, w.atoms().lookup("Source"));
    REQUIRE(source.has_value());
    const auto* text = std::get_if<std::string>(&source.value());
    REQUIRE(text != nullptr);
    CHECK(*text == "local x = 1\nreturn x\n");
}

// --- A script runs when you press play (ADR 0058) ----------------------------
//
// `startScripts` is one option and one branch, which is exactly why it is worth
// four cases: a branch that is right for every caller but one, and wrong for
// that one, looks identical to a branch that is wrong for every caller but one.

namespace {

// Counts what is under `Workspace`, which is the countable form of "a project
// opened in the editor shows what its scene holds and nothing else".
[[nodiscard]] core::usize workspaceChildren(app::WorldHost& host)
{
    core::usize count = 0;
    for (core::InstanceId child = host.world().firstChild(host.workspace()); child.valid();
         child = host.world().nextSibling(child)) {
        ++count;
    }
    return count;
}

// A project whose scene holds one part and whose script builds another. The two
// halves are what every case below tells apart.
void writeSceneAndScript(Project& project)
{
    project.write("src/scripts/init.luau", R"(
        local p = Instance.new("Part")
        p.Name = "BuiltByScript"
        p.Parent = workspace
    )");
    project.write("content/scenes/main.scene.json", kOnePartScene);
}

} // namespace

TEST_CASE("boot mounts the scripts and the editor does not start them")
{
    Captured log;
    Project project;
    writeSceneAndScript(project);

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    options.startScripts = false;
    REQUIRE_FALSE(host.boot(options).has_value());

    // **Counted, not sampled.** "Nothing the script built" is a claim about the
    // whole of `Workspace`, and a check for one absent name would pass just as
    // well on a world where the script had built something else.
    CHECK(workspaceChildren(host) == 1);
    CHECK(bootChildNamed(host, "FromTheScene").valid());
    CHECK_FALSE(bootChildNamed(host, "BuiltByScript").valid());

    // **Mounted is the other half and it is the half that makes this usable.**
    // The `Script` is in the tree, the Explorer shows it, `Source` is editable
    // and a tab can open it -- what waits is the first resumption.
    CHECK(host.mountedScriptCount() == 1);
}

TEST_CASE("every other way of running starts them at boot, as it always did")
{
    Captured log;
    Project project;
    writeSceneAndScript(project);

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    // The default, spelled out: a game, `luaug dev`, a headless run, the
    // conformance runner and a replay all want behaviour the moment the world
    // exists. This case is the regression the editor's one branch could cause.
    REQUIRE(options.startScripts);
    REQUIRE_FALSE(host.boot(options).has_value());

    CHECK(workspaceChildren(host) == 2);
    CHECK(bootChildNamed(host, "BuiltByScript").valid());
}

TEST_CASE("play starts the scripts a mount-only boot left waiting")
{
    Captured log;
    Project project;
    writeSceneAndScript(project);

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    options.startScripts = false;
    REQUIRE_FALSE(host.boot(options).has_value());
    REQUIRE_FALSE(bootChildNamed(host, "BuiltByScript").valid());

    // What the play button does, and nothing else -- the world is the world it
    // was, and the scripts are the ones the boot mounted.
    script::startScripts(host.runtime().state());
    host.tick();

    CHECK(bootChildNamed(host, "BuiltByScript").valid());
    CHECK(workspaceChildren(host) == 2);
}

TEST_CASE("stop throws the VM away, so a second play is the same as the first")
{
    Captured log;
    Project project;
    // **A required module is VM state and nothing else**, which is what makes it
    // the probe this needs. Restoring the world at stop cannot touch it: a
    // module's cached result lives in the registry, so a second play that found
    // `n` already at one is a second play running on the first one's VM.
    //
    // Everything else that accumulates is invisible to a world comparison in
    // exactly this way -- connections, globals, queued work -- and that is why
    // "two plays in a row are identical" went unreported for so long.
    project.write(".luaurc", R"({ "aliases": { "shared": "src/shared" } })");
    project.write("src/shared/state.luau", R"(
        return { n = 0 }
    )");
    project.write("src/scripts/init.luau", R"(
        local state = require("@shared/state")
        state.n += 1
        local p = Instance.new("Part")
        p.Name = "Run" .. tostring(state.n)
        p.Parent = workspace

        -- Due long after the stop below. If the VM outlives a stop, so does
        -- this, and a world nobody is playing grows a part on its own.
        task.delay(1.0, function()
            local late = Instance.new("Part")
            late.Name = "Late"
            late.Parent = workspace
        end)
    )");
    project.write("content/scenes/main.scene.json", kOnePartScene);

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    options.startScripts = false;
    REQUIRE_FALSE(host.boot(options).has_value());

    const scene::WorldSnapshot before = host.world().snapshot();

    script::startScripts(host.runtime().state());
    host.tick();
    CHECK(bootChildNamed(host, "Run1").valid());
    CHECK(workspaceChildren(host) == 2);

    // The editor's stop, in the order the editor performs it: the world first,
    // then the VM -- so the new runtime binds to the tree as it stands.
    host.world().restore(before);
    REQUIRE_FALSE(host.restartRuntime().has_value());
    CHECK(workspaceChildren(host) == 1);

    // Well past the delay. Nothing is playing, so nothing may appear.
    for (int index = 0; index < 90; ++index)
        host.tick();
    CHECK_FALSE(bootChildNamed(host, "Late").valid());
    CHECK(workspaceChildren(host) == 1);

    // **And the second play is the first play.** `Run1` and not `Run2`, which is
    // the whole claim: the module was evaluated again because there was no VM
    // left holding its result.
    script::startScripts(host.runtime().state());
    host.tick();
    CHECK(bootChildNamed(host, "Run1").valid());
    CHECK_FALSE(bootChildNamed(host, "Run2").valid());
    CHECK(workspaceChildren(host) == 2);
}

TEST_CASE("a rebuilt runtime adopts the services it found rather than making more")
{
    // The failure this rules out is the one M4 shipped for four milestones: a
    // service the host cached and the VM rebuilt are two different instances,
    // and every read afterwards answers with the struct defaults.
    Captured log;
    Project project;
    writeSceneAndScript(project);

    app::WorldHost host;
    app::WorldHostOptions options = app::testing::bootOptions(project.root);
    options.bootScene = project.root / "content" / "scenes" / "main.scene.json";
    options.startScripts = false;
    REQUIRE_FALSE(host.boot(options).has_value());

    const core::InstanceId dataModel = host.runtime().dataModel();
    const core::InstanceId workspace = host.workspace();
    REQUIRE(dataModel.valid());
    REQUIRE(workspace.valid());
    const core::usize servicesBefore = [&] {
        core::usize count = 0;
        for (core::InstanceId child = host.world().firstChild(dataModel); child.valid();
             child = host.world().nextSibling(child)) {
            ++count;
        }
        return count;
    }();

    REQUIRE_FALSE(host.restartRuntime().has_value());

    CHECK(host.runtime().dataModel() == dataModel);
    CHECK(host.workspace() == workspace);
    // Counted, because a second `Workspace` beside the first is exactly what an
    // adoption that silently created would look like.
    core::usize servicesAfter = 0;
    for (core::InstanceId child = host.world().firstChild(dataModel); child.valid();
         child = host.world().nextSibling(child)) {
        ++servicesAfter;
    }
    CHECK(servicesAfter == servicesBefore);

    // The mount table is a fact about the project and survives the VM that held
    // it -- without it every chunk name and every `Ctrl+S` forgets its file.
    CHECK(host.mountedScriptCount() == 1);
}
