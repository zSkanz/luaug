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

TEST_CASE("looking at the sky, the wireframe is what is round the camera, as before")
{
    Ground ground;
    CHECK(ground.lines(core::DVec3{0.0, 40.0, 0.0}, core::Vec3{0.0f, 1.0f, 0.0f}) == 0);
    CHECK(ground.lines(core::DVec3{0.0, 2.0, 0.0}, core::Vec3{0.0f, 1.0f, 0.0f}) > 0);
}
