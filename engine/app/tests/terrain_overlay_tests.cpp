// The terrain's debug overlay (`View > Terrain Wireframe` / `Terrain Normals`).
//
// **The owner's report, reproduced**: every View switch but the grid seemed to
// do nothing while editing. For the terrain's two, the cause was where the
// overlay looked -- a cube of thirty-two voxels round the camera, which holds
// no ground once the camera is more than sixteen voxels up, which is where an
// editor's camera spends its time. It now meshes round the ground the camera
// looks at.
#include "luaug/app/terrain_overlay.h"
#include "luaug/asset/terrain.h"
#include "luaug/render/debug_draw.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <doctest/doctest.h>

#include "class_descriptors.gen.h"

using namespace luaug;

namespace {

struct Ground
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    scene::World world{classes, enums, atoms, 1234u};

    Ground()
    {
        scene::generated::registerClasses(classes, atoms);
        scene::generated::registerEnums(enums, atoms);
        const core::InstanceId terrain = world.create(classes.findId(atoms.intern("Terrain")));
        REQUIRE(terrain.valid());
        scene::TerrainComponent* component = world.terrains().find(terrain);
        REQUIRE(component != nullptr);
        component->field.setHeightRange(-32.0f, 32.0f);
        // A flat field whose surface is y = 0.
        (void)asset::fillFlat(component->field, core::DVec3{0.0, 0.0, 0.0}, 64.0f, 0.0f, 1);
        component->fieldRevision += 1;
    }

    [[nodiscard]] core::usize lines(core::DVec3 eye, core::Vec3 forward)
    {
        render::DebugDraw draw;
        app::drawTerrainDebug(world, eye, forward, true, false, draw);
        return draw.lineCount();
    }

    // How wide, on x, the drawn wireframe is.
    [[nodiscard]] double span(core::DVec3 eye, core::Vec3 forward)
    {
        render::DebugDraw draw;
        app::drawTerrainDebug(world, eye, forward, true, false, draw);
        if (draw.empty())
            return 0.0;
        float low = draw.vertices()[0].position.x;
        float high = low;
        for (const render::DebugVertex& vertex : draw.vertices()) {
            low = std::min(low, vertex.position.x);
            high = std::max(high, vertex.position.x);
        }
        return static_cast<double>(high - low);
    }
};

} // namespace

TEST_CASE("the wireframe shows the ground an editor's camera looks down at from high up")
{
    Ground ground;
    // Forty metres up, looking down: the report's case. Nothing, before.
    CHECK(ground.lines(core::DVec3{0.0, 40.0, 0.0}, core::Vec3{0.0f, -1.0f, 0.0f}) > 0);
    // Looking at it at an angle from further away still.
    CHECK(ground.lines(core::DVec3{-60.0, 50.0, 0.0}, core::normalize(core::Vec3{1.0f, -0.8f, 0.0f})) > 0);
}

TEST_CASE("from far away the wireframe covers the ground in view, not a patch of it")
{
    // The owner's second report: sixty metres off, the first fix drew a
    // thirty-two metre square so dense it read as a solid block.
    Ground ground;
    const core::Vec3 down = core::normalize(core::Vec3{1.0f, -1.0f, 0.0f});
    CHECK(ground.span(core::DVec3{-45.0, 45.0, 0.0}, down) > 60.0);
    // Far off it is a coarser level -- fewer, longer triangles over the same
    // ground -- and up close the voxels' own.
    const core::usize near = ground.lines(core::DVec3{0.0, 3.0, 0.0}, core::Vec3{0.0f, -1.0f, 0.0f});
    const core::usize far = ground.lines(core::DVec3{-200.0, 200.0, 0.0}, down);
    CHECK(far > 0);
    CHECK(far < near);
}

TEST_CASE("looking at the sky, the wireframe is what is round the camera, as before")
{
    Ground ground;
    CHECK(ground.lines(core::DVec3{0.0, 80.0, 0.0}, core::Vec3{0.0f, 1.0f, 0.0f}) == 0);
    CHECK(ground.lines(core::DVec3{0.0, 2.0, 0.0}, core::Vec3{0.0f, 1.0f, 0.0f}) > 0);
}
