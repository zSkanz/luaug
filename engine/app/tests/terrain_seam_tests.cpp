// F1's seam-continuity gate (phase-2-4-plan, Part H, H1), kept for the voxel
// grid (ADR 0082).
//
// **Three things are built from one terrain** -- the field a brush and a
// raycast read, the collider a character stands on, and the mesh a player sees
// -- and each is built in pieces: colliders a chunk at a time, meshes a node at
// a time, and the field itself streamed in cells. Every place two pieces meet is
// a seam where they could disagree. This gate measures it.
//
// The fixture puts a tunnel under rolling ground, opened to the sky by a pit,
// crossing a 64 m streaming-cell boundary and several chunk boundaries. The
// field is cut into streaming cells, encoded, decoded and stitched back the way
// the field streamer does it. Then one ray per column goes down through the
// collider, the field and the drawn surface, and all three must agree.

#include "luaug/app/world_host.h"
#include "luaug/asset/field_cells.h"
#include "luaug/asset/terrain.h"
#include "luaug/asset/terrain_cell.h"
#include "luaug/render/terrain_loader.h"
#include "luaug/scene/components.h"
#include "luaug/scene/physics_sync.h"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <optional>
#include <vector>

#include "project_fixture.h"

using namespace luaug;
using luaug::app::testing::bootOptions;
using luaug::app::testing::Project;

namespace {

constexpr double Voxel = 0.5;
// The fixture's footprint, in metres: two streaming cells wide in x, so the
// tunnel crosses the cell boundary at x = 64, and two chunks deep in z, so it
// runs along the chunk boundary at z = 16.
constexpr double MinX = 36.0;
constexpr double MaxX = 92.0;
constexpr double MinZ = 8.0;
constexpr double MaxZ = 24.0;
constexpr double TunnelY = -5.0;
constexpr double TunnelZ = 16.0;

[[nodiscard]] asset::TerrainField authoredField()
{
    asset::TerrainField field(asset::FieldSettings{.voxelSize = static_cast<float>(Voxel)});
    field.setHeightRange(-32.0f, 32.0f);
    // Ground from the world's floor to y = 2 over x 32..96, z 0..32, then
    // rolled by a few raised balls.
    asset::fillBlock(field, core::DVec3{64.0, -15.0, 16.0}, core::Vec3{64.0f, 34.0f, 32.0f}, 1);
    (void)asset::raiseBall(field, core::DVec3{48.0, 2.0, 10.0}, 7.0, 1.5f, 1);
    (void)asset::raiseBall(field, core::DVec3{70.0, 2.0, 22.0}, 9.0, 2.0f, 1);
    (void)asset::raiseBall(field, core::DVec3{84.0, 2.0, 14.0}, 6.0, 1.0f, 1);
    // The tunnel: a line of balls from x 50 to 80 at y -5, along z = 16.
    for (double x = 50.0; x <= 80.0; x += 1.0)
        (void)asset::fillBall(field, core::DVec3{x, TunnelY, TunnelZ}, 2.5, 0);
    // And a pit down into it, so the sky reaches the tunnel through a hole.
    for (double y = TunnelY; y <= 4.0; y += 1.0)
        (void)asset::fillBall(field, core::DVec3{56.0, y, TunnelZ}, 2.0, 0);
    return field;
}

// What the field streamer does with a saved field: cut it into cells, write
// each, read each back, and stitch them into one.
[[nodiscard]] asset::TerrainField streamedField(const asset::TerrainField& authored)
{
    asset::TerrainField stitched(authored.settings());
    for (const asset::TerrainCell& cell : asset::splitTerrain(authored)) {
        const std::vector<std::byte> bytes = asset::encodeTerrainCell(cell);
        asset::TerrainCell read;
        REQUIRE_FALSE(asset::decodeTerrainCell(bytes, read).has_value());
        stitched.shareFrom(read.field);
    }
    return stitched;
}

// The highest point of the drawn surface below `from` on the vertical at
// (x, z), by testing every triangle -- which a fixture this size can afford.
// Skirts are left out: they hang under the surface and a ray from above meets
// the surface first, but one from inside a tunnel could meet a roof's skirt.
[[nodiscard]] std::optional<double> drawnSurface(const std::vector<asset::TerrainMesh>& nodes, double x, double from,
                                                 double z)
{
    std::optional<double> best;
    for (const asset::TerrainMesh& node : nodes) {
        const std::vector<core::Vec3>& points = node.colliderPoints;
        const std::vector<core::u32>& indices = node.colliderIndices;
        const auto point = [&](core::usize at) {
            const core::Vec3 p = points[indices[at]];
            return core::DVec3{static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z)};
        };
        for (core::usize at = 0; at + 2 < indices.size(); at += 3) {
            const core::DVec3 a = point(at);
            const core::DVec3 b = point(at + 1);
            const core::DVec3 c = point(at + 2);
            // A vertical ray reduces to a 2D point-in-triangle test in xz and
            // a plane height.
            const double d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
            if (std::abs(d) < 1e-12)
                continue;
            const double u = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / d;
            const double v = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / d;
            const double w = 1.0 - u - v;
            if (u < -1e-9 || v < -1e-9 || w < -1e-9)
                continue;
            const double y = u * a.y + v * b.y + w * c.y;
            if (y <= from && (!best || y > *best))
                best = y;
        }
    }
    return best;
}

} // namespace

TEST_CASE("the field, the collider and the drawn surface agree across every seam a tunnel crosses")
{
    const asset::TerrainField authored = authoredField();
    REQUIRE(authored.chunkCount() > 0);
    const asset::TerrainField field = streamedField(authored);
    // **Stitched cells are the field that was saved**, key for key and byte
    // for byte, or everything below is measuring the wrong ground.
    REQUIRE(field.digest() == authored.digest());

    Project project;
    project.write("main.luau", "");
    app::WorldHost host;
    REQUIRE_FALSE(host.boot(bootOptions(project.root / "main.luau")).has_value());
    scene::World& world = host.world();
    const core::InstanceId terrain = world.create(world.classes().findId(world.atoms().intern("Terrain")));
    REQUIRE(terrain.valid());
    REQUIRE_FALSE(world.setParent(terrain, host.workspace()).has_value());
    scene::TerrainComponent* component = world.terrains().find(terrain);
    REQUIRE(component != nullptr);
    component->field = field;
    component->fieldRevision += 1;

    // Things that move, spread along the tunnel just above it, so every chunk
    // the rays cross is within the collider's reach; enough ticks for every one
    // of them to have been built at its budget.
    const scene::ClassId partClass = world.classes().findId(world.atoms().intern("Part"));
    std::vector<core::InstanceId> movers;
    for (double x = MinX; x <= MaxX; x += 16.0) {
        const core::InstanceId mover = world.create(partClass);
        movers.push_back(mover);
        world.parts().find(mover)->cframe.position = core::DVec3{x, 10.0, TunnelZ};
        REQUIRE_FALSE(world.setParent(mover, host.workspace()).has_value());
    }
    for (int tick = 0; tick < 80; ++tick)
        host.tick();

    scene::PhysicsSync* physics = host.physics();
    REQUIRE(physics != nullptr);
    // The rays measure the ground, so the parts that fell onto it are left out.
    std::vector<core::u64> excluded;
    for (const core::InstanceId mover : movers)
        excluded.push_back(physics->userDataOf(mover));
    physics::QueryFilter notMovers;
    notMovers.mode = physics::QueryFilter::Mode::Exclude;
    notMovers.userData = excluded;

    // Every full-detail node over the footprint, as the renderer builds it: a
    // column of 16 m chunks at half a metre.
    std::vector<asset::TerrainMesh> nodes;
    for (core::i32 z = -1; z <= 2; ++z) {
        for (core::i32 x = 1; x <= 6; ++x)
            nodes.push_back(render::meshTerrainNode(field, render::TerrainNodeKey{0, x, z}));
    }

    // **Measured as the field's distance at each hit, not as a difference in
    // height.** A vertical ray grazing a vertical wall -- the rim of the pit --
    // has a hit height that moves by the whole wall for a millimetre sideways,
    // so a height difference there measures the ray, not the surfaces. How far
    // a hit point is from the field's surface is the same number on a wall as
    // on a floor.
    double worstCollider = 0.0;
    double worstDrawn = 0.0;
    core::usize rays = 0;
    core::usize caveRays = 0;
    core::usize misses = 0;
    const auto offSurface = [&field](double x, double y, double z) {
        return std::abs(static_cast<double>(asset::sampleField(field, core::DVec3{x, y, z}).distance));
    };
    const auto measure = [&](double x, double fromY, double z) {
        const std::optional<asset::TerrainHit> truth =
            asset::raycastField(field, core::DVec3{x, fromY, z}, core::Vec3{0.0f, -1.0f, 0.0f}, 80.0);
        physics::RayHit hit;
        const bool collided = physics->backend().raycast(
            physics->worldHandle(), physics::RayD{core::DVec3{x, fromY, z}, core::Vec3{0.0f, -80.0f, 0.0f}}, notMovers,
            hit);
        const std::optional<double> drawn = drawnSurface(nodes, x, fromY, z);
        rays += 1;
        if (!truth || !collided || !drawn) {
            misses += 1;
            INFO("x=" << x << " z=" << z << " from=" << fromY << " field=" << truth.has_value()
                      << " collider=" << collided << " drawn=" << drawn.has_value());
            CHECK(false);
            return;
        }
        worstCollider = std::max(worstCollider, offSurface(x, hit.position.y, z));
        worstDrawn = std::max(worstDrawn, offSurface(x, *drawn, z));
    };

    // One ray per column from the sky, over the whole footprint, at the
    // column's centre.
    for (double z = MinZ + 0.25; z < MaxZ; z += Voxel) {
        for (double x = MinX + 0.25; x < MaxX; x += Voxel)
            measure(x, 40.0, z);
    }
    // And one per column from inside the tunnel, down onto its floor.
    for (double x = 51.25; x < 79.0; x += Voxel) {
        for (double z = TunnelZ - 1.0 + 0.25; z < TunnelZ + 1.0; z += Voxel) {
            measure(x, TunnelY + 0.5, z);
            caveRays += 1;
        }
    }

    // **The bound is a quarter of a voxel.** The collider and the drawn surface
    // are the same surface net over the same voxels, so what separates either
    // from the field is how a surface net places a vertex: at the mean of its
    // cell's edge crossings, which leaves the flat faces exact and cuts a chord
    // across a curve. A seam that opened by even half a voxel would not pass.
    INFO(rays << " rays (" << caveRays << " inside the tunnel), worst collider " << worstCollider << " m, worst drawn "
              << worstDrawn << " m");
    CHECK(misses == 0);
    CHECK(worstCollider < 0.25 * Voxel);
    CHECK(worstDrawn < 0.25 * Voxel);
}
