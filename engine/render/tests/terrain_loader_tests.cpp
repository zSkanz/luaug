// Terrain on the GPU (ADR 0082): which nodes are drawn, and what is meshed.
//
// **Testable with no device worth the name.** The loader runs against the null
// device and is judged by what it built and what it chose to draw: every piece
// of ground drawn once, finest under the viewer, nothing drawn where a node is
// not ready yet, and an edit rebuilding the node it touched and not the rest.
#include "luaug/asset/terrain.h"
#include "luaug/render/mesh_cache.h"
#include "luaug/render/render_world.h"
#include "luaug/render/terrain_loader.h"
#include "luaug/rhi/backends.h"
#include "luaug/scene/class_registry.h"
#include "luaug/scene/components.h"
#include "luaug/scene/enum_registry.h"
#include "luaug/scene/world.h"

#include <chrono>
#include <doctest/doctest.h>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace luaug;
using namespace luaug::render;

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

    // Flat ground `size` metres across, at a metre voxel.
    explicit LoaderFixture(float size = 256.0f)
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
            asset::TerrainField(asset::FieldSettings{.voxelSize = 1.0f, .minHeight = -32.0f, .maxHeight = 64.0f});
        (void)asset::fillFlat(component.field, core::DVec3{0.0, 0.0, 0.0}, size, 0.0f, 1);
        world.terrains().add(terrain, std::move(component));
        REQUIRE(world.setParent(terrain, root) == std::nullopt);
        loader.setBuildsPerSync(1000);
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

    // Syncs until nothing is left to build, and answers the draws.
    std::vector<TerrainNodeDraw> settle()
    {
        for (int frame = 0; frame < 64; ++frame) {
            (void)sync();
            if (!loader.pending() && loader.lastBuilds() == 0)
                break;
        }
        return loader.draws(world);
    }
};

// The node a URN names: `terrain://<id>/<level>/<x>,<z>`.
[[nodiscard]] TerrainNodeKey keyOf(const core::AtomTable& atoms, core::NameAtom urn)
{
    const std::string text(atoms.text(urn));
    TerrainNodeKey key;
    const auto first = text.find('/', 10);
    const auto second = text.find('/', first + 1);
    const auto comma = text.find(',', second + 1);
    key.level = static_cast<core::u32>(std::stoul(text.substr(first + 1, second - first - 1)));
    key.x = std::stoi(text.substr(second + 1, comma - second - 1));
    key.z = std::stoi(text.substr(comma + 1));
    return key;
}

// Every chunk column the draws cover, and how many times.
[[nodiscard]] std::map<std::pair<core::i32, core::i32>, int> coverage(const core::AtomTable& atoms,
                                                                      const std::vector<TerrainNodeDraw>& draws)
{
    std::map<std::pair<core::i32, core::i32>, int> covered;
    for (const TerrainNodeDraw& draw : draws) {
        const TerrainNodeKey key = keyOf(atoms, draw.urn);
        const auto across = static_cast<core::i32>(1u << key.level);
        for (core::i32 z = 0; z < across; ++z) {
            for (core::i32 x = 0; x < across; ++x)
                covered[{key.x * across + x, key.z * across + z}] += 1;
        }
    }
    return covered;
}

} // namespace

TEST_CASE("with no viewer nothing is meshed or drawn")
{
    LoaderFixture fixture;
    (void)fixture.sync();
    CHECK(fixture.loader.residentCount() == 0);
    CHECK(fixture.loader.draws(fixture.world).empty());
}

TEST_CASE("the ground is drawn once everywhere, finest under the viewer")
{
    LoaderFixture fixture;
    fixture.loader.setFocus(core::DVec3{8.0, 4.0, 8.0});
    const std::vector<TerrainNodeDraw> draws = fixture.settle();
    REQUIRE_FALSE(draws.empty());

    // 256 m of 32 m chunk columns is eight a side, each drawn exactly once.
    const auto covered = coverage(fixture.atoms, draws);
    for (core::i32 z = -4; z < 4; ++z) {
        for (core::i32 x = -4; x < 4; ++x) {
            CAPTURE(x);
            CAPTURE(z);
            REQUIRE(covered.contains({x, z}));
            CHECK(covered.at({x, z}) == 1);
        }
    }
    // The column under the viewer at full detail; one far away coarser.
    core::u32 under = 99;
    core::u32 far = 0;
    for (const TerrainNodeDraw& draw : draws) {
        const TerrainNodeKey key = keyOf(fixture.atoms, draw.urn);
        const auto across = static_cast<core::i32>(1u << key.level);
        if (key.x * across <= 0 && 0 < (key.x + 1) * across && key.z * across <= 0 && 0 < (key.z + 1) * across)
            under = key.level;
        far = std::max(far, key.level);
    }
    CHECK(under == 0);
    CHECK(far > 0);
}

TEST_CASE("a quiet frame builds nothing, and an edit rebuilds every node it touched in one frame")
{
    LoaderFixture fixture;
    fixture.loader.setFocus(core::DVec3{8.0, 4.0, 8.0});
    (void)fixture.settle();
    (void)fixture.sync();
    CHECK(fixture.loader.lastBuilds() == 0);

    // A dig in the middle of one column: that column's node and the eight
    // round it, whose openness reads 12 m into it -- **all in the frame the dig
    // lands**, whatever the loading budget. Spread over frames, neighbours
    // showed two versions of one edit: the owner's flicker while editing.
    fixture.loader.setBuildsPerSync(1);
    (void)asset::fillBall(fixture.component().field, core::DVec3{16.0, 0.0, 16.0}, 3.0, 0);
    fixture.component().fieldRevision += 1;
    (void)fixture.sync();
    CHECK(fixture.loader.lastBuilds() == 9);
    (void)fixture.sync();
    CHECK(fixture.loader.lastBuilds() == 0);
}

TEST_CASE("a node is drawn until its children are ready, so a change of level shows no hole")
{
    LoaderFixture fixture;
    // Far away first: coarse nodes only.
    fixture.loader.setFocus(core::DVec3{2000.0, 4.0, 2000.0});
    const std::vector<TerrainNodeDraw> far = fixture.settle();
    REQUIRE_FALSE(far.empty());

    // Then close, one build a frame: every frame still covers every column.
    fixture.loader.setBuildsPerSync(1);
    fixture.loader.setFocus(core::DVec3{8.0, 4.0, 8.0});
    for (int frame = 0; frame < 40; ++frame) {
        (void)fixture.sync();
        const auto covered = coverage(fixture.atoms, fixture.loader.draws(fixture.world));
        for (core::i32 z = -4; z < 4; ++z) {
            for (core::i32 x = -4; x < 4; ++x) {
                CAPTURE(frame);
                CAPTURE(x);
                CAPTURE(z);
                CHECK(covered.contains({x, z}));
            }
        }
    }
}

TEST_CASE("the ground under the viewer stays at full detail for as long as the viewer stays")
{
    // **The owner's flicker in the cave example.** The ancestors of the nodes
    // drawn close up were let go after a few seconds undrawn; one rebuilt
    // counted as ready and was drawn over the whole close-up, coarse, for the
    // frames its children took to come back -- every few seconds, a pop from
    // the finest ground to the coarsest and back.
    LoaderFixture fixture;
    fixture.loader.setFocus(core::DVec3{8.0, 4.0, 8.0});
    (void)fixture.settle();
    fixture.loader.setBuildsPerSync(1);
    for (int frame = 0; frame < 600; ++frame) {
        // Drifting slowly, as a walking camera does.
        fixture.loader.setFocus(core::DVec3{8.0 + frame * 0.01, 4.0, 8.0});
        (void)fixture.sync();
        core::u32 under = 99;
        for (const TerrainNodeDraw& draw : fixture.loader.draws(fixture.world)) {
            const TerrainNodeKey key = keyOf(fixture.atoms, draw.urn);
            const auto across = static_cast<core::i32>(1u << key.level);
            if (key.x * across <= 0 && 0 < (key.x + 1) * across && key.z * across <= 0 && 0 < (key.z + 1) * across)
                under = key.level;
        }
        CAPTURE(frame);
        REQUIRE(under == 0);
    }
}

TEST_CASE("the mesh a node is drawn with is its ground, with skirts under its sides")
{
    LoaderFixture fixture;
    const asset::TerrainMesh leaf = meshTerrainNode(fixture.component().field, TerrainNodeKey{0, 0, 0});
    REQUIRE_FALSE(leaf.mesh.indices.empty());
    // Skirts are in the drawn mesh, not the collider's surface.
    CHECK(leaf.mesh.indices.size() > leaf.colliderIndices.size());
    const asset::TerrainMesh nothing = meshTerrainNode(fixture.component().field, TerrainNodeKey{0, 40, 40});
    CHECK(nothing.mesh.indices.empty());
}

TEST_CASE("the render terrain carries the palette, and only for terrain in the world")
{
    LoaderFixture fixture;
    RenderWorld snapshot;
    fixture.loader.appendRenderTerrains(fixture.world, fixture.root, snapshot);
    REQUIRE(snapshot.terrains.size() == 1);
    CHECK(snapshot.terrains.front().id == fixture.terrain);
    CHECK(snapshot.terrains.front().palette[1][3] == 1.0f);

    RenderWorld elsewhere;
    fixture.loader.appendRenderTerrains(fixture.world, core::InstanceId{}, elsewhere);
    CHECK(elsewhere.terrains.empty());
}

TEST_CASE("a terrain that goes away takes its meshes with it")
{
    LoaderFixture fixture;
    fixture.loader.setFocus(core::DVec3{8.0, 4.0, 8.0});
    (void)fixture.settle();
    REQUIRE(fixture.loader.residentCount() > 0);
    fixture.world.terrains().remove(fixture.terrain);
    (void)fixture.sync();
    CHECK(fixture.loader.residentCount() == 0);
    CHECK(fixture.library.size() == 0);
}

// --- What a dig costs to draw, measured (D164) ---------------------------------
//
// The owner's report was a dig that stalled the editor. A dig now rebuilds the
// mesh of the node it touched, so the number that decides whether it stalls is
// what meshing one node costs. Skipped by default and run on demand, the shape
// every cost measurement in this repository takes: a number that varies with
// the machine must not gate anything.
//
//     luaug_render_tests --test-case="what meshing a terrain node costs" --no-skip
TEST_CASE("what meshing a terrain node costs" * doctest::skip())
{
    for (const float voxel : {1.0f, 0.5f}) {
        asset::TerrainField field(asset::FieldSettings{.voxelSize = voxel, .minHeight = -64.0f, .maxHeight = 64.0f});
        const float size = 256.0f * voxel;
        // In doubles for the brushes, which take world positions (R9).
        const auto v = static_cast<double>(voxel);
        (void)asset::fillFlat(field, core::DVec3{0.0, 0.0, 0.0}, size, 0.0f, 1);
        (void)asset::raiseBall(field, core::DVec3{16.0 * v, 0.0, 16.0 * v}, 12.0 * v, 6.0f * voxel);
        (void)asset::fillBall(field, core::DVec3{16.0 * v, -2.0 * v, 16.0 * v}, 5.0 * v, 0);

        const auto start = std::chrono::steady_clock::now();
        constexpr int Runs = 20;
        std::size_t triangles = 0;
        for (int run = 0; run < Runs; ++run)
            triangles = meshTerrainNode(field, TerrainNodeKey{0, 0, 0}).mesh.indices.size() / 3;
        const double leaf =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / Runs;

        const auto brushStart = std::chrono::steady_clock::now();
        for (int run = 0; run < Runs; ++run)
            (void)asset::fillBall(field, core::DVec3{8.0 * v, 0.0, 8.0 * v}, 6.0 * v,
                                  static_cast<core::u8>(run % 2 == 0 ? 0 : 1));
        const double brush =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - brushStart).count() / Runs;

        MESSAGE("voxel " << voxel << " m: a full-detail node meshes in " << leaf << " ms (" << triangles
                         << " triangles); a ball of radius 6 voxels writes in " << brush << " ms");
    }
}
