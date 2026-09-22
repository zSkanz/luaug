// GPU terrain (ADR 0071): which nodes are drawn, and what reaches the GPU.
//
// **Both halves are testable with no device worth the name.** The selection is
// a pure function of the terrain's tile grid and where the viewer is, so its
// guarantees -- the ground covered exactly once, neighbours never more than one
// level apart, nothing drawn over missing ground -- are asserted directly. The
// loader runs against the null device and is judged by what it uploaded.
#include "luaug/asset/terrain.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/render_world.h"
#include "luaug/render/terrain_loader.h"
#include "luaug/render/terrain_lod.h"
#include "luaug/rhi/backends.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <cstdlib>
#include <doctest/doctest.h>
#include <map>
#include <utility>
#include <vector>

using namespace luaug;
using namespace luaug::render;

namespace {

// A square of flat tiles, `tiles` a side, with the viewer at `viewer` in the
// field's own space.
struct Square
{
    std::vector<float> lo;
    std::vector<float> hi;
    TerrainLodSource source;

    Square(core::u32 tiles, core::DVec3 viewer)
        : lo(static_cast<core::usize>(tiles) * tiles, 0.0f), hi(static_cast<core::usize>(tiles) * tiles, 1.0f)
    {
        source.minTileX = 0;
        source.minTileZ = 0;
        source.tilesX = tiles;
        source.tilesZ = tiles;
        source.tileMin = lo;
        source.tileMax = hi;
        source.voxelSize = 0.5f;
        source.originFromViewer = core::DVec3{-viewer.x, -viewer.y, -viewer.z};
    }
};

// Which level covers each leaf tile; and whether any tile was covered twice.
struct Coverage
{
    std::map<std::pair<core::i32, core::i32>, core::i32> level;
    bool overlap = false;
};

Coverage coverageOf(const std::vector<TerrainNode>& nodes)
{
    Coverage coverage;
    for (const TerrainNode& node : nodes) {
        const auto tiles = static_cast<core::i32>(1u << node.level);
        const core::i32 firstX = node.latticeX / static_cast<core::i32>(TerrainGridQuads);
        const core::i32 firstZ = node.latticeZ / static_cast<core::i32>(TerrainGridQuads);
        for (core::i32 z = 0; z < tiles; ++z) {
            for (core::i32 x = 0; x < tiles; ++x) {
                const auto key = std::make_pair(firstX + x, firstZ + z);
                if (coverage.level.contains(key))
                    coverage.overlap = true;
                coverage.level[key] = static_cast<core::i32>(node.level);
            }
        }
    }
    return coverage;
}

} // namespace

TEST_CASE("the selection covers every tile exactly once")
{
    // A 512 m square with the viewer standing in one corner of it, so every
    // level from the finest to the coarsest is in play.
    const Square square(32, core::DVec3{20.0, 2.0, 20.0});
    std::vector<TerrainNode> nodes;
    selectTerrainNodes(square.source, TerrainLodSettings{}, nodes);
    REQUIRE_FALSE(nodes.empty());

    const Coverage coverage = coverageOf(nodes);
    CHECK_FALSE(coverage.overlap);
    CHECK(coverage.level.size() == 32u * 32u);
}

TEST_CASE("detail is finest under the viewer and coarsens with distance")
{
    const Square square(32, core::DVec3{20.0, 2.0, 20.0});
    std::vector<TerrainNode> nodes;
    selectTerrainNodes(square.source, TerrainLodSettings{}, nodes);
    const Coverage coverage = coverageOf(nodes);

    // The tile the viewer stands on is a leaf, drawn one vertex per lattice
    // step; the far corner, 700 m off, is not.
    CHECK(coverage.level.at({1, 1}) == 0);
    CHECK(coverage.level.at({31, 31}) > 0);
}

TEST_CASE("neighbouring tiles are never more than one level apart")
{
    // **The condition the morph needs to be crack-free.** A vertex on the edge
    // between a level-L node and a level-L+1 node is fully morphed onto the
    // coarse grid, which lines up with the coarse node's edge -- one level. Two
    // levels apart, the fine edge has vertices the coarse one does not, and
    // daylight shows through.
    for (const core::DVec3 viewer :
         {core::DVec3{20.0, 2.0, 20.0}, core::DVec3{256.0, 40.0, 256.0}, core::DVec3{-100.0, 5.0, 300.0}}) {
        CAPTURE(viewer.x);
        const Square square(32, viewer);
        std::vector<TerrainNode> nodes;
        selectTerrainNodes(square.source, TerrainLodSettings{}, nodes);
        const Coverage coverage = coverageOf(nodes);
        for (const auto& [key, level] : coverage.level) {
            for (const std::pair<core::i32, core::i32> step : {std::pair{1, 0}, std::pair{0, 1}}) {
                const auto neighbour = coverage.level.find({key.first + step.first, key.second + step.second});
                if (neighbour == coverage.level.end())
                    continue;
                CHECK(std::abs(neighbour->second - level) <= 1);
            }
        }
    }
}

TEST_CASE("nothing is drawn over tiles the field does not hold")
{
    Square square(8, core::DVec3{0.0, 2.0, 0.0});
    // A 2x2 block of tiles the field does not have.
    for (const core::usize index : {18u, 19u, 26u, 27u}) {
        square.lo[index] = 1.0f;
        square.hi[index] = -1.0f;
    }
    std::vector<TerrainNode> nodes;
    selectTerrainNodes(square.source, TerrainLodSettings{}, nodes);
    const Coverage coverage = coverageOf(nodes);
    CHECK_FALSE(coverage.level.contains({2, 2}));
    CHECK_FALSE(coverage.level.contains({3, 3}));
    CHECK(coverage.level.contains({0, 0}));
}

TEST_CASE("the selection is the same on every run")
{
    const Square square(16, core::DVec3{37.0, 3.0, 91.0});
    std::vector<TerrainNode> first;
    std::vector<TerrainNode> second;
    selectTerrainNodes(square.source, TerrainLodSettings{}, first);
    selectTerrainNodes(square.source, TerrainLodSettings{}, second);
    REQUIRE(first.size() == second.size());
    for (core::usize at = 0; at < first.size(); ++at) {
        CHECK(first[at].latticeX == second[at].latticeX);
        CHECK(first[at].latticeZ == second[at].latticeZ);
        CHECK(first[at].level == second[at].level);
    }
}

// --- The loader -------------------------------------------------------------

namespace {

struct LoaderFixture
{
    core::AtomTable atoms;
    scene::ClassRegistry classes;
    scene::EnumRegistry enums;
    scene::ClassId workspaceClass = scene::InvalidClass;
    scene::ClassId terrainClass = scene::InvalidClass;
    rhi::DeviceResult device = rhi::createNullDevice({.backend = rhi::BackendId::Null});
    rhi::ICmdList* cmd = nullptr;
    scene::World world;
    core::InstanceId root;
    core::InstanceId terrain;
    MeshCache cache;
    MeshLibrary library;
    TerrainLoader loader;

    LoaderFixture()
        : workspaceClass(
              classes.registerClass({.name = atoms.intern("Workspace"), .defaultName = atoms.intern("Workspace")})),
          terrainClass(
              classes.registerClass({.name = atoms.intern("Terrain"), .defaultName = atoms.intern("Terrain")})),
          world(classes, enums, atoms, 1234u)
    {
        REQUIRE(device != nullptr);
        cmd = device->beginFrame();
        REQUIRE(cmd != nullptr);
        root = world.create(workspaceClass);
        terrain = world.create(terrainClass);
        scene::TerrainComponent component;
        component.field =
            asset::TerrainField(asset::FieldSettings{.voxelSize = 0.5f, .minHeight = -32.0f, .maxHeight = 32.0f});
        asset::fillFlat(component.field, core::DVec3{0.0, 0.0, 0.0}, 64.0f, 0.0f, 1);
        world.terrains().add(terrain, std::move(component));
        REQUIRE(world.setParent(terrain, root) == std::nullopt);
    }

    ~LoaderFixture()
    {
        loader.destroy(*device, cache, library);
        cache.destroy(*device);
    }

    LoaderFixture(const LoaderFixture&) = delete;
    LoaderFixture& operator=(const LoaderFixture&) = delete;

    scene::TerrainComponent& component() { return *world.terrains().find(terrain); }
    core::u32 sync() { return loader.sync(*device, *cmd, world, atoms, cache, library); }
};

} // namespace

TEST_CASE("every tile is uploaded once, and a quiet frame uploads nothing")
{
    LoaderFixture fixture;
    (void)fixture.sync();
    const core::usize tiles = fixture.component().field.tileCount();
    REQUIRE(tiles > 0);
    CHECK(fixture.loader.lastTileUploads() == tiles);
    CHECK(fixture.loader.residentCount() == tiles);

    (void)fixture.sync();
    CHECK(fixture.loader.lastTileUploads() == 0);
}

TEST_CASE("a brush stroke uploads the tiles it touched and nothing else")
{
    // **This is the whole of what an edit costs now**: kilobytes to the GPU.
    // There is no mesh to rebuild, so a stroke that stays inside one tile is
    // one tile's upload however large the terrain is.
    LoaderFixture fixture;
    (void)fixture.sync();

    (void)asset::raiseBall(fixture.component().field, core::DVec3{4.0, 0.0, 4.0}, 2.0, 1.0f);
    fixture.component().fieldRevision += 1;
    (void)fixture.sync();
    CHECK(fixture.loader.lastTileUploads() == 1);
}

TEST_CASE("the render terrain names the atlas and bounds every tile")
{
    LoaderFixture fixture;
    (void)asset::raiseBall(fixture.component().field, core::DVec3{4.0, 0.0, 4.0}, 2.0, 3.0f);
    (void)fixture.sync();

    RenderWorld snapshot;
    fixture.loader.appendRenderTerrains(fixture.world, fixture.root, snapshot);
    REQUIRE(snapshot.terrains.size() == 1);
    const RenderTerrain& terrain = snapshot.terrains.front();
    CHECK(terrain.heights.valid());
    CHECK(terrain.materials.valid());
    CHECK(terrain.tileTable.valid());
    CHECK(terrain.tilesX * terrain.tilesZ == terrain.tileMin.size());
    // The raised tile's bounds include the bump; a flat one's are flat.
    float highest = 0.0f;
    for (const float value : terrain.tileMax)
        highest = std::max(highest, value);
    CHECK(highest > 2.5f);

    // A terrain outside the root is not in the world.
    RenderWorld elsewhere;
    fixture.loader.appendRenderTerrains(fixture.world, core::InstanceId{}, elsewhere);
    CHECK(elsewhere.terrains.empty());
}

TEST_CASE("a cave near the viewer is meshed and opens the ground; far away it closes")
{
    LoaderFixture fixture;
    (void)asset::fillBall(fixture.component().field, core::DVec3{4.0, -3.0, 4.0}, 1.5, 0);
    fixture.component().fieldRevision += 1;
    REQUIRE(fixture.component().field.brickCount() > 0);

    // No viewer, no cave: a headless run has nobody to stand in one.
    (void)fixture.sync();
    CHECK(fixture.loader.caveCount() == 0);

    fixture.loader.setFocus(core::DVec3{4.0, 2.0, 4.0});
    (void)fixture.sync();
    CHECK(fixture.loader.caveCount() == 1);
    // The tile under it went back up with the cave flag in its materials.
    CHECK(fixture.loader.lastTileUploads() == 1);
    CHECK(fixture.library.size() == 1);

    fixture.loader.setFocus(core::DVec3{400.0, 2.0, 400.0});
    (void)fixture.sync();
    CHECK(fixture.loader.caveCount() == 0);
    CHECK(fixture.loader.lastTileUploads() == 1);
    CHECK(fixture.library.size() == 0);
}
