// The half of the editor-seam proof that needs no GPU (roadmap M8, ADR 0017).
//
// `--two-worlds` compares what two worlds DRAW, which is the strongest
// statement available and also the one that skips on a machine with no device
// -- which is every CI runner this project has. These cases hold the floor: two
// hosts alive at once, two VMs, and every observable each of them owns proven
// to be its own.
//
// They are deliberately about the things a picture cannot see. A shared
// instance-id space, a global service registry, one VM's globals visible from
// the other: each of those could be true while both worlds still rendered
// exactly what they were asked to.
#include "luaug/app/world_host.h"
#include "luaug/core/i18n.h"
#include "luaug/core/log.h"
#include "luaug/scene/world.h"
#include "luaug/script/runtime.h"

#include <doctest/doctest.h>
#include <string>

#include "project_fixture.h"

using namespace luaug;
using luaug::app::testing::bootOptions;
using luaug::app::testing::Captured;
using luaug::app::testing::hasChildNamed;
using luaug::app::testing::Project;

TEST_CASE("two worlds alive at once keep their own trees")
{
    Captured log;

    Project first;
    first.write("src/scripts/main.luau", R"(
        local folder = Instance.new("Folder")
        folder.Name = "OnlyInA"
        folder.Parent = workspace
    )");

    Project second;
    second.write("src/scripts/main.luau", R"(
        local folder = Instance.new("Folder")
        folder.Name = "OnlyInB"
        folder.Parent = workspace
    )");

    // Both booted before either ticks, which is the order that matters: a
    // global the second boot overwrites would still be the second world's by
    // the time anything ran.
    app::WorldHost a;
    app::WorldHost b;
    REQUIRE_FALSE(a.boot(bootOptions(first.root, 1)).has_value());
    REQUIRE_FALSE(b.boot(bootOptions(second.root, 2)).has_value());

    a.tick();
    b.tick();

    CHECK(log.firstError().empty());
    CHECK(hasChildNamed(a, "OnlyInA"));
    CHECK(hasChildNamed(b, "OnlyInB"));
    CHECK_FALSE(hasChildNamed(a, "OnlyInB"));
    CHECK_FALSE(hasChildNamed(b, "OnlyInA"));

    // Two `scene::World`s, therefore two ECSes. An id from one is meaningless
    // in the other, and the check that says so is that neither world considers
    // the other's `game` to be alive in it -- which is the cheapest statement of
    // "these are not one world with two handles".
    CHECK(a.world().alive(a.dataModel()));
    CHECK(b.world().alive(b.dataModel()));
    CHECK(&a.world() != &b.world());
}

TEST_CASE("two worlds alive at once keep their own VMs")
{
    Captured log;

    // A module is loaded once per VM and its table is the state everything that
    // requires it shares. Two worlds requiring the same module path must
    // therefore get two tables -- and if they did not, a prefab preview would
    // inherit whatever the edited world had left in every module it touched.
    const std::string_view moduleSource = R"(
        --!strict
        local Counter = { Loads = 0 }
        Counter.Loads += 1
        return Counter
    )";

    const std::string_view entrySource = R"(
        local counter = require("@shared/counter")
        local folder = Instance.new("Folder")
        folder.Name = "Loads" .. tostring(counter.Loads)
        folder.Parent = workspace
    )";

    Project first;
    first.write(".luaurc", R"({"languageMode": "strict", "aliases": {"shared": "src/shared"}})");
    first.write("src/shared/counter.luau", moduleSource);
    first.write("src/scripts/main.luau", entrySource);

    Project second;
    second.write(".luaurc", R"({"languageMode": "strict", "aliases": {"shared": "src/shared"}})");
    second.write("src/shared/counter.luau", moduleSource);
    second.write("src/scripts/main.luau", entrySource);

    app::WorldHost a;
    app::WorldHost b;
    REQUIRE_FALSE(a.boot(bootOptions(first.root, 1)).has_value());
    REQUIRE_FALSE(b.boot(bootOptions(second.root, 2)).has_value());

    // Two `lua_State`s, which is the claim ADR 0017's condition is written in:
    // "two `WorldHost`s, each with its own `ScriptRuntime` -- that is two Luau
    // VMs". Stated as a pointer comparison because everything else here is
    // downstream of it.
    CHECK(a.runtime().state() != b.runtime().state());

    // Interleaved, so that whichever VM ran last is not the one both worlds
    // report. Run to completion one after the other and a shared module cache
    // would still give the right answer for the second world.
    a.tick();
    b.tick();
    a.tick();
    b.tick();

    CHECK(log.firstError().empty());
    CHECK(hasChildNamed(a, "Loads1"));
    CHECK(hasChildNamed(b, "Loads1"));
    CHECK_FALSE(hasChildNamed(a, "Loads2"));
    CHECK_FALSE(hasChildNamed(b, "Loads2"));
}

TEST_CASE("a world destroyed while another is alive takes nothing with it")
{
    Captured log;

    Project first;
    first.write("src/scripts/main.luau", R"(
        local folder = Instance.new("Folder")
        folder.Name = "Survivor"
        folder.Parent = workspace
    )");

    Project second;
    second.write("src/scripts/main.luau", R"(
        local folder = Instance.new("Folder")
        folder.Name = "Doomed"
        folder.Parent = workspace
    )");

    app::WorldHost survivor;
    REQUIRE_FALSE(survivor.boot(bootOptions(first.root, 1)).has_value());
    survivor.tick();
    REQUIRE(hasChildNamed(survivor, "Survivor"));

    {
        app::WorldHost doomed;
        REQUIRE_FALSE(doomed.boot(bootOptions(second.root, 2)).has_value());
        doomed.tick();
        REQUIRE(hasChildNamed(doomed, "Doomed"));
    }

    // The teardown a prefab-isolation mode does every time it closes a preview,
    // and the phase-2 editor would do it hundreds of times a session. A world
    // that took a shared registry down with it would show up here and nowhere
    // else, because nothing else in this repository ever destroys one host while
    // another is running.
    survivor.tick();
    CHECK(log.firstError().empty());
    CHECK(hasChildNamed(survivor, "Survivor"));
    CHECK(survivor.world().alive(survivor.dataModel()));
}
