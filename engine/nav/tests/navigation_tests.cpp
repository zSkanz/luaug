// Navigation over a real world (ADR 0089): Recast builds from the scene's parts
// and terrain, and Detour answers. Every case is a small level somebody could
// have built -- a floor, a wall with a door in it, an island -- and what a
// path through it has to do.
#include "luaug/asset/terrain.h"
#include "luaug/nav/nav.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <cmath>
#include <doctest/doctest.h>
#include <memory>

#include "class_descriptors.gen.h"

using namespace luaug;

namespace {

struct Level
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    scene::World world{classes, enums, atoms, 1234u};
    core::InstanceId workspace;
    std::unique_ptr<nav::INavigation> navigation;
    core::u64 tick = 0;

    Level()
    {
        scene::generated::registerClasses(classes, atoms);
        scene::generated::registerEnums(enums, atoms);
        workspace = world.create(classes.findId(atoms.intern("Workspace")));
        REQUIRE(workspace.valid());
        navigation = nav::createNavigation(world);
        REQUIRE(navigation != nullptr);
        navigation->setWorkspace(workspace);
        navigation->setTick(tick);
    }

    // An anchored box, as a level is built from.
    core::InstanceId block(core::DVec3 centre, core::Vec3 size, bool anchored = true)
    {
        const core::InstanceId id = world.create(classes.findId(atoms.intern("Part")));
        REQUIRE(id.valid());
        REQUIRE_FALSE(world.setParent(id, workspace).has_value());
        world.parts().find(id)->cframe.position = centre;
        world.parts().find(id)->size = size;
        world.rigidBodies().find(id)->anchored = anchored;
        return id;
    }

    // Forty metres of floor whose top is y = 0.
    core::InstanceId floor() { return block({0.0, -0.5, 0.0}, {40.0f, 1.0f, 40.0f}); }

    void nextTick() { navigation->setTick(++tick); }
};

[[nodiscard]] double horizontal(core::DVec3 a, core::DVec3 b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
}

} // namespace

TEST_CASE("across open floor the path is a straight line to the goal")
{
    Level level;
    level.floor();
    const std::optional<nav::NavPath> path = level.navigation->findPath({-15.0, 0.0, -15.0}, {15.0, 0.0, 12.0});
    REQUIRE(path.has_value());
    CHECK(path->complete);
    REQUIRE(path->points.size() == 2);
    CHECK(horizontal(path->points.back(), {15.0, 0.0, 12.0}) < 0.1);
    CHECK(std::abs(path->points.back().y) < 0.3);
    CHECK(level.navigation->tileCount() > 0);
}

TEST_CASE("a wall with a door in it sends the path through the door")
{
    Level level;
    level.floor();
    // A wall along x = 0, open between z = -2 and z = 2.
    level.block({0.0, 1.5, -11.0}, {1.0f, 3.0f, 18.0f});
    level.block({0.0, 1.5, 11.0}, {1.0f, 3.0f, 18.0f});

    const std::optional<nav::NavPath> path = level.navigation->findPath({-10.0, 0.0, -12.0}, {10.0, 0.0, -12.0});
    REQUIRE(path.has_value());
    CHECK(path->complete);
    REQUIRE(path->points.size() >= 3);
    bool throughDoor = false;
    for (const core::DVec3& point : path->points) {
        if (std::abs(point.x) < 1.5)
            throughDoor = throughDoor || std::abs(point.z) < 2.0;
        // Never inside the wall.
        CHECK_FALSE((std::abs(point.x) < 0.5 && std::abs(point.z) > 2.0));
    }
    CHECK(throughDoor);
}

TEST_CASE("a goal on an island nobody can reach is a partial path, and no floor is no path")
{
    Level level;
    level.floor();
    level.block({60.0, -0.5, 0.0}, {10.0f, 1.0f, 10.0f});
    const std::optional<nav::NavPath> path = level.navigation->findPath({0.0, 0.0, 0.0}, {60.0, 0.0, 0.0});
    REQUIRE(path.has_value());
    CHECK_FALSE(path->complete);
    // It ends at the edge of the floor it is on, towards the island.
    CHECK(path->points.back().x > 15.0);
    CHECK(path->points.back().x < 21.0);

    // Standing on nothing: there is no path from here at all.
    CHECK_FALSE(level.navigation->findPath({0.0, 50.0, 200.0}, {0.0, 0.0, 0.0}).has_value());
}

TEST_CASE("only what is anchored and solid is ground")
{
    Level level;
    const core::InstanceId loose = level.block({0.0, -0.5, 0.0}, {40.0f, 1.0f, 40.0f}, false);
    CHECK_FALSE(level.navigation->findPath({-5.0, 0.0, 0.0}, {5.0, 0.0, 0.0}).has_value());

    level.world.rigidBodies().find(loose)->anchored = true;
    level.world.rigidBodies().find(loose)->canCollide = false;
    level.nextTick();
    CHECK_FALSE(level.navigation->findPath({-5.0, 0.0, 0.0}, {5.0, 0.0, 0.0}).has_value());

    level.world.rigidBodies().find(loose)->canCollide = true;
    level.nextTick();
    CHECK(level.navigation->findPath({-5.0, 0.0, 0.0}, {5.0, 0.0, 0.0}).has_value());
}

TEST_CASE("a wall that moves is seen by the next tick's query")
{
    Level level;
    level.floor();
    // Wall to wall across the floor.
    const core::InstanceId wall = level.block({0.0, 1.5, 0.0}, {1.0f, 3.0f, 40.0f});
    const std::optional<nav::NavPath> blocked = level.navigation->findPath({-10.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    REQUIRE(blocked.has_value());
    CHECK_FALSE(blocked->complete);

    // The same tick sees the same world: nothing is gathered twice.
    level.world.parts().find(wall)->cframe.position = {0.0, 1.5, 100.0};
    CHECK_FALSE(level.navigation->findPath({-10.0, 0.0, 0.0}, {10.0, 0.0, 0.0})->complete);

    level.nextTick();
    const std::optional<nav::NavPath> open = level.navigation->findPath({-10.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    REQUIRE(open.has_value());
    CHECK(open->complete);
}

TEST_CASE("a wall a script builds is in the next query of the same tick")
{
    // Found by the conformance spec: a gather cached for the tick answered from
    // before the wall existed. `World::mutations` is what makes a write
    // through the world's verbs visible at once.
    Level level;
    level.floor();
    CHECK(level.navigation->findPath({-10.0, 0.0, 0.0}, {10.0, 0.0, 0.0})->complete);
    (void)level.block({0.0, 1.5, 0.0}, {1.0f, 3.0f, 40.0f});
    CHECK_FALSE(level.navigation->findPath({-10.0, 0.0, 0.0}, {10.0, 0.0, 0.0})->complete);
}

TEST_CASE("the nearest point on the mesh, within a reach")
{
    Level level;
    level.floor();
    const std::optional<core::DVec3> below = level.navigation->nearestPoint({3.0, 1.5, -4.0}, 3.0f);
    REQUIRE(below.has_value());
    CHECK(horizontal(*below, {3.0, 0.0, -4.0}) < 0.1);
    CHECK(std::abs(below->y) < 0.3);
    CHECK_FALSE(level.navigation->nearestPoint({3.0, 30.0, -4.0}, 3.0f).has_value());
}

TEST_CASE("a straight walk stops at the wall")
{
    Level level;
    level.floor();
    level.block({0.0, 1.5, 0.0}, {1.0f, 3.0f, 40.0f});
    const std::optional<core::DVec3> stop = level.navigation->raycast({-10.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    REQUIRE(stop.has_value());
    // Short of the wall's face by about the agent's radius.
    CHECK(stop->x < -0.5);
    CHECK(stop->x > -1.5);

    const std::optional<core::DVec3> clear = level.navigation->raycast({-10.0, 0.0, 5.0}, {-2.0, 0.0, 5.0});
    REQUIRE(clear.has_value());
    CHECK(clear->x == doctest::Approx(-2.0).epsilon(0.01));
}

TEST_CASE("a region built ahead of time needs no build on the first path")
{
    Level level;
    level.floor();
    const core::usize built = level.navigation->buildRegion({-20.0, -1.0, -20.0}, {20.0, 1.0, 20.0});
    CHECK(built > 0);
    CHECK(level.navigation->buildRegion({-20.0, -1.0, -20.0}, {20.0, 1.0, 20.0}) == 0);
}

TEST_CASE("two navigations over one world find the same path, point for point")
{
    Level level;
    level.floor();
    level.block({0.0, 1.5, -11.0}, {1.0f, 3.0f, 18.0f});
    level.block({5.0, 1.5, 8.0}, {6.0f, 3.0f, 1.0f});
    const std::unique_ptr<nav::INavigation> second = nav::createNavigation(level.world);
    second->setWorkspace(level.workspace);
    const auto a = level.navigation->findPath({-10.0, 0.0, -12.0}, {12.0, 0.0, 15.0});
    const auto b = second->findPath({-10.0, 0.0, -12.0}, {12.0, 0.0, 15.0});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->points.size() == b->points.size());
    for (core::usize at = 0; at < a->points.size(); ++at) {
        CHECK(a->points[at].x == b->points[at].x);
        CHECK(a->points[at].y == b->points[at].y);
        CHECK(a->points[at].z == b->points[at].z);
    }
}

TEST_CASE("terrain is ground: a path across a flat field")
{
    Level level;
    const core::InstanceId terrain = level.world.create(level.classes.findId(level.atoms.intern("Terrain")));
    REQUIRE(terrain.valid());
    REQUIRE_FALSE(level.world.setParent(terrain, level.workspace).has_value());
    scene::TerrainComponent* ground = level.world.terrains().find(terrain);
    REQUIRE(ground != nullptr);
    ground->field.setHeightRange(-32.0f, 32.0f);
    (void)asset::fillFlat(ground->field, core::DVec3{0.0, 0.0, 0.0}, 64.0f, 0.0f, 1);
    ground->fieldRevision += 1;

    const std::optional<nav::NavPath> path = level.navigation->findPath({-10.0, 0.0, -10.0}, {10.0, 0.0, 10.0});
    REQUIRE(path.has_value());
    CHECK(path->complete);
    CHECK(std::abs(path->points.back().y) < 1.0);
}
