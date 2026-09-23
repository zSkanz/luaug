// Terrain and block worlds streamed from disk (ADR 0075).
//
// End to end over real files and the real asynchronous reader: cells arrive
// around the camera, leave behind it, and a cell somebody changed stays -- the
// one property the whole design is for, since a streamer that dropped an edit
// would lose a player's work without saying so.
#include "luaug/app/field_streamer.h"
#include "luaug/asset/field_cells.h"
#include "luaug/platform/async_io.h"
#include "luaug/platform/file.h"
#include "luaug/scene/components.h"
#include "luaug/scene/world.h"

#include <doctest/doctest.h>
#include <filesystem>
#include <system_error>
#include <thread>

#include "inspector_fixture.h"

using namespace luaug;

namespace {

struct IoScope
{
    IoScope() { REQUIRE(platform::initIo(4)); }
    ~IoScope() { platform::shutdownIo(); }
};

struct StreamedWorld
{
    app::testing::Fixture fixture;
    scene::World world{fixture.classes, fixture.enums, fixture.atoms, 1234u};
    core::InstanceId workspace;
    core::InstanceId camera;
    core::InstanceId ground;
    std::filesystem::path directory;
    asset::ChunkIndex index;
    app::FieldStreamer streamer;

    // A kilometre square of two-metre ground -- one 64 m tile a cell -- and a
    // strip of blocks along the x axis, both written as cells the way a
    // partition writes them.
    StreamedWorld()
    {
        workspace = world.create(fixture.workspaceClass);
        world.workspaces().add(workspace, scene::WorkspaceComponent{});

        camera = world.create(fixture.cameraClass);
        world.cameras().add(camera, scene::CameraComponent{});
        (void)world.setParent(camera, workspace);
        world.workspaces().find(workspace)->currentCamera = camera;

        const asset::FieldSettings settings{.voxelSize = 2.0f, .minHeight = -32.0f, .maxHeight = 32.0f};
        asset::TerrainField source(settings);
        (void)asset::fillBlock(source, core::DVec3{0.0, -20.0, 0.0}, core::Vec3{1024.0f, 40.0f, 1024.0f}, 1);
        asset::VoxelGrid blocks;
        (void)blocks.fill(-300, 0, 0, 300, 2, 3, 1);

        ground = world.create(fixture.folderClass);
        scene::TerrainComponent terrain;
        terrain.field = asset::TerrainField(settings);
        world.terrains().add(ground, std::move(terrain));
        (void)world.setParent(ground, workspace);
        world.voxels().add(workspace, scene::VoxelComponent{});

        directory = std::filesystem::temp_directory_path() / "luaug-field-streamer-tests";
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        REQUIRE(platform::createDirectories(directory));
        const core::u32 tiles = asset::terrainCellTiles(settings.voxelSize);
        for (const asset::TerrainCell& cell : asset::splitTerrain(source))
            add(asset::ChunkId{cell.x, cell.z, asset::FieldLayerTerrain}, asset::encodeTerrainCell(cell),
                asset::terrainCellBounds(cell, tiles, core::DVec3{}));
        const core::u32 chunks = asset::voxelCellChunks(1.0f);
        for (const asset::VoxelCell& cell : asset::splitVoxels(blocks, 1.0f))
            add(asset::ChunkId{cell.x, cell.z, asset::FieldLayerVoxels}, asset::encodeVoxelCell(cell),
                asset::voxelCellBounds(cell, chunks));
        std::sort(index.chunks.begin(), index.chunks.end(),
                  [](const asset::ChunkIndexEntry& a, const asset::ChunkIndexEntry& b) { return a.id < b.id; });

        scene::EngineState& state = world.engineState();
        state.streamingLoadRadius = 160.0;
        state.streamingMinRadius = 96.0;

        streamer.setIndex(index, [this](const asset::ChunkIndexEntry& entry) {
            return std::optional<std::filesystem::path>(directory / entry.urn);
        });
        streamer.setWorld(&world, workspace);
    }

    ~StreamedWorld()
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    void add(asset::ChunkId id, const std::vector<std::byte>& bytes, core::DAABB bounds)
    {
        const std::string name = std::to_string(id.layer) + "_" + std::to_string(id.x) + "_" + std::to_string(id.z);
        REQUIRE(platform::writeFile(directory / name, bytes));
        asset::ChunkIndexEntry entry;
        entry.id = id;
        entry.bounds = bounds;
        entry.urn = name;
        entry.bytes = static_cast<core::u32>(bytes.size());
        index.chunks.push_back(entry);
    }

    void lookFrom(core::DVec3 position) { world.cameras().find(camera)->cframe.position = position; }

    [[nodiscard]] const asset::TerrainField& field() { return world.terrains().find(ground)->field; }
    [[nodiscard]] const asset::VoxelGrid& grid() { return world.voxels().find(workspace)->grid; }

    // Pumps until `done`, the way frames would, with the reader's threads
    // given time to finish.
    template <typename Done>
    [[nodiscard]] bool pumpUntil(Done&& done)
    {
        for (int frame = 0; frame < 20000; ++frame) {
            streamer.pump(50.0);
            if (done())
                return true;
            std::this_thread::yield();
        }
        return false;
    }
};

} // namespace

TEST_CASE("the ground streams in around the camera, and the simulation waits for it")
{
    IoScope io;
    StreamedWorld streamed;
    REQUIRE(streamed.streamer.active());
    CHECK_FALSE(streamed.streamer.primed());

    REQUIRE(streamed.pumpUntil([&] { return streamed.streamer.primed(); }));

    // The tile under the camera is here, and one four hundred metres off is not.
    CHECK(streamed.field().findTile(asset::TileKey{0, 0}) != nullptr);
    CHECK(streamed.field().findTile(asset::TileKey{-1, -1}) != nullptr);
    CHECK(streamed.field().findTile(asset::TileKey{6, 6}) == nullptr);
    // And so are the blocks under it, and not the ones at the strip's far end.
    CHECK(streamed.grid().get(2, 1, 1) == 1);
    CHECK(streamed.grid().get(290, 1, 1) == asset::AirBlock);
}

TEST_CASE("ground left behind is dropped, and ground somebody changed is kept")
{
    IoScope io;
    StreamedWorld streamed;
    REQUIRE(streamed.pumpUntil([&] { return streamed.streamer.primed(); }));

    // Dig into the cell under the camera -- which clones the tile, since the
    // streamer holds the one the cell brought -- and place a block.
    asset::TerrainField& field = streamed.world.terrains().find(streamed.ground)->field;
    field.setColumn(4, 4, 10.0f, 1);
    (void)streamed.world.voxels().find(streamed.workspace)->grid.set(5, 3, 1, 2);

    // Walk four hundred metres away.
    streamed.lookFrom(core::DVec3{420.0, 0.0, 420.0});
    REQUIRE(streamed.pumpUntil([&] {
        return streamed.field().findTile(asset::TileKey{6, 6}) != nullptr &&
               streamed.field().findTile(asset::TileKey{-1, -1}) == nullptr;
    }));

    // The untouched cell went; the dug one, and the one with the block, stayed.
    CHECK(streamed.field().findTile(asset::TileKey{-1, -1}) == nullptr);
    REQUIRE(streamed.field().findTile(asset::TileKey{0, 0}) != nullptr);
    CHECK(streamed.field().findTile(asset::TileKey{0, 0})->height[4 * asset::TileEdge + 4] == doctest::Approx(10.0));
    CHECK(streamed.grid().get(5, 3, 1) == 2);
    CHECK(streamed.grid().get(-40, 1, 1) == asset::AirBlock);
    CHECK(streamed.streamer.kept() >= 2);

    // And walking back brings the rest of the ground back without undoing the
    // dig: what the field holds wins over what a cell brings.
    streamed.lookFrom(core::DVec3{0.0, 0.0, 0.0});
    REQUIRE(streamed.pumpUntil([&] { return streamed.field().findTile(asset::TileKey{-1, -1}) != nullptr; }));
    CHECK(streamed.field().findTile(asset::TileKey{0, 0})->height[4 * asset::TileEdge + 4] == doctest::Approx(10.0));
}
